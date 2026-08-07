"""
ue5_import_map_scene.py — Import a mapforge zone GLB as a placed SCENE, not
loose assets: creates a level, runs Interchange ImportScene (synchronous) so
every glTF node becomes an actor with its transform, then fixes collision,
hides the debug collision geometry, and adds lighting + a PlayerStart.

The GLB comes from  mapforge/export_map.py <ZONE>  and contains named group
nodes: Objects, Terrain, Collision (IFO boxes + WalkBlocked MOV grid), Water.

Env:
  ROSE_MAP_GLB   absolute path to the zone .glb            (required)
  ROSE_MAP_ZONE  zone key, e.g. JPT01                      (default: glb stem)
  ROSE_MAP_WIPE  "0" to keep any existing /Game/Maps/<zone> (default: wipe)

Run headless WITH RHI (shader compile), editor CLOSED:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=tools/ue5_import_map_scene.py ...
"""
import os
import unreal

GLB = os.environ.get("ROSE_MAP_GLB", "")
ZONE = os.environ.get("ROSE_MAP_ZONE", "") or os.path.splitext(os.path.basename(GLB))[0]
WIPE = os.environ.get("ROSE_MAP_WIPE", "1").strip() != "0"

ROOT = f"/Game/Maps/{ZONE}"
LEVEL = f"{ROOT}/L_{ZONE}"
SCENE_PKG = f"{ROOT}/Scene"

EAL = unreal.EditorAssetLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(m):
    unreal.log(f"[mapscene] {m}")


if not GLB or not os.path.exists(GLB):
    raise RuntimeError(f"ROSE_MAP_GLB not found: {GLB!r}")

# ---- 1. clean slate ---------------------------------------------------------
# The editor may have opened L_<ZONE> as its start-up map, which keeps the level
# package LOADED.  delete_directory() then silently fails to remove that one
# .umap (it does delete everything else), and the new_level() below fails with
# "could not create level" because an asset still exists at the path.  Net
# effect: the Scene assets are destroyed and the level is left dangling.
# Switching to a scratch level first unloads L_<ZONE> so the wipe is complete.
if WIPE:
    try:
        LES.new_level(f"/Temp/__mapscene_scratch_{ZONE}")
        log("switched to scratch level (unloads any open L_<ZONE>)")
    except Exception as e:
        log(f"scratch-level switch failed, continuing ({e})")

    if EAL.does_directory_exist(ROOT):
        EAL.delete_directory(ROOT)
        log(f"wiped {ROOT}")

    # Verify: a surviving level asset means the wipe did NOT take and the
    # import would fail below -- fail loudly here instead of half-destroying.
    if EAL.does_asset_exist(LEVEL):
        EAL.delete_asset(LEVEL)
        log(f"force-deleted surviving {LEVEL}")

if not LES.new_level(LEVEL):
    raise RuntimeError(f"could not create level {LEVEL}")
log(f"created level {LEVEL}")

# ---- 2. synchronous Interchange scene import --------------------------------
mgr = unreal.InterchangeManager.get_interchange_manager_scripted()
sd = unreal.InterchangeManager.create_source_data(GLB)
params = unreal.ImportAssetParameters()
params.set_editor_property("is_automated", True)
ok = mgr.import_scene(SCENE_PKG, sd, params)
log(f"import_scene returned {ok}")
try:
    mgr.wait_until_all_tasks_done(False)
except Exception as e:
    log(f"wait_until_all_tasks_done unavailable ({e})")
if not ok:
    raise RuntimeError("Interchange ImportScene failed")

actors = list(EAS.get_all_level_actors())
sm_actors = [a for a in actors if isinstance(a, unreal.StaticMeshActor)]
log(f"level has {len(actors)} actors ({len(sm_actors)} static-mesh)")

# ---- 3. classify by the named glTF group nodes ------------------------------
def descendants(actor):
    out = []
    stack = list(actor.get_attached_actors())
    while stack:
        a = stack.pop()
        out.append(a)
        stack.extend(a.get_attached_actors())
    return out


groups = {}
for a in actors:
    lbl = a.get_actor_label()
    if lbl in ("Objects", "Terrain", "Collision", "Water") and lbl not in groups:
        groups[lbl] = set(descendants(a))
for k, v in groups.items():
    log(f"group {k}: {len(v)} descendants")

col_set = groups.get("Collision", set())
water_set = groups.get("Water", set())

# ---- 4. collision + visibility ----------------------------------------------
# Complex-as-simple on every imported mesh asset so traces/characters hit the
# real triangles (glTF imports have no simple hulls). Also disable Nanite:
# Interchange builds every mesh with it, but the terrain patch meshes have
# world-baked vertices ~5e7 units from origin — Nanite quantizes positions
# against those huge bounds and simplifies each patch independently, causing
# visible cracks between terrain patches (ue5_fix_map_nanite.py rationale).
n_flag = n_nanite = 0
for path in EAL.list_assets(SCENE_PKG, recursive=True):
    obj = EAL.load_asset(path)
    if not isinstance(obj, unreal.StaticMesh):
        continue
    bs = obj.get_editor_property("body_setup")
    if bs:
        bs.set_editor_property("collision_trace_flag",
                               unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
        n_flag += 1
    ns = obj.get_editor_property("nanite_settings")
    if ns.get_editor_property("enabled"):
        ns.set_editor_property("enabled", False)
        obj.set_editor_property("nanite_settings", ns)
        n_nanite += 1
log(f"complex-as-simple on {n_flag} static meshes, nanite off on {n_nanite}")

n_col = n_water = 0
for a in sm_actors:
    comp = a.static_mesh_component
    if a in water_set or a.get_actor_label().startswith("Water"):
        # visible, never blocks
        comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
        n_water += 1
        continue
    comp.set_collision_profile_name("BlockAll")
    comp.set_collision_enabled(unreal.CollisionEnabled.QUERY_AND_PHYSICS)
    if a in col_set or a.get_actor_label() == "WalkBlocked":
        # ROSE gameplay blockers: keep the collision, hide the red debug mesh
        a.set_actor_hidden_in_game(True)
        n_col += 1
log(f"{n_col} hidden blockers, {n_water} water surfaces (no collision)")

# ---- 5. lighting + PlayerStart ----------------------------------------------
def spawn(cls, loc=(0, 0, 0), rot=(0, 0, 0), label=None):
    a = EAS.spawn_actor_from_class(cls, unreal.Vector(*loc),
                                   unreal.Rotator(roll=rot[0], pitch=rot[1], yaw=rot[2]))
    if a and label:
        a.set_actor_label(label)
    return a


sun = spawn(unreal.DirectionalLight, rot=(0, -45, 30), label="Sun")
try:
    sun.light_component.set_editor_property("atmosphere_sun_light", True)
except Exception:
    pass
spawn(unreal.SkyAtmosphere, label="SkyAtmosphere")
sky = spawn(unreal.SkyLight, label="SkyLight")
try:
    sky.light_component.set_editor_property("real_time_capture", True)
except Exception:
    pass

# bounds from the placed map actors, then trace for the ground at the centre
mn = [1e18, 1e18, 1e18]
mx = [-1e18, -1e18, -1e18]
for a in sm_actors:
    if a in col_set:
        continue
    o, e = a.get_actor_bounds(False)
    for i, ax in enumerate(("x", "y", "z")):
        mn[i] = min(mn[i], getattr(o, ax) - getattr(e, ax))
        mx[i] = max(mx[i], getattr(o, ax) + getattr(e, ax))
log("map bounds min=(%.0f,%.0f,%.0f) max=(%.0f,%.0f,%.0f)" % (*mn, *mx))

cx, cy = (mn[0] + mx[0]) / 2.0, (mn[1] + mx[1]) / 2.0
gz = None
world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
if world:
    hit = unreal.SystemLibrary.line_trace_single(
        world, unreal.Vector(cx, cy, mx[2] + 5000.0), unreal.Vector(cx, cy, mn[2] - 5000.0),
        unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [], unreal.DrawDebugTrace.NONE, True)
    if hit:
        gz = hit.to_tuple()[4].z if isinstance(hit, unreal.HitResult) else None
        try:
            gz = hit.get_editor_property("impact_point").z
        except Exception:
            pass
spawn_z = (gz if gz is not None else (mn[2] + mx[2]) / 2.0) + 120.0
spawn(unreal.PlayerStart, loc=(cx, cy, spawn_z), label=f"{ZONE}_PlayerStart")
log("PlayerStart at (%.0f, %.0f, %.0f) ground=%s" % (cx, cy, spawn_z, gz))

# ---- 6. save everything ------------------------------------------------------
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)
LES.save_current_level()
log(f"DONE — level {LEVEL} saved with {len(EAS.get_all_level_actors())} actors")
