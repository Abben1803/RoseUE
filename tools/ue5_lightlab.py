"""
ue5_lightlab.py — headless visual A/B lab for the "object faces render black"
bug.  Loads a zone, frames the camera at PlayerStart, and takes screenshots
under controlled variants:

  00_lit_yawN      — baseline lit view, 4 compass yaws
  10_unlit_yaw0    — unlit (textures only; proves base color is fine)
  20_sky10_yawN    — skylight intensity x10 (does sky fill appear at all?)
  30_swap_yawN     — every /Game/Atlas/MI material on the nearest few object
                     meshes swapped to plain WorldGridMaterial (default lit) —
                     if faces light up now, M_RoseMaster is the culprit;
                     still black = geometry/normals/lighting.

Nothing is saved: the level is NOT saved, MIs are not saved, the material swap
is done on the actor components in memory only.

Run (editor CLOSED, needs RHI — no -nullrhi):
  UnrealEditor-Cmd.exe <proj> -ExecCmds="py C:/rose-next-classic/tools/ue5_lightlab.py" ^
      -unattendedscriptexit -nosplash -stdout -FullStdOutLogOutput -abslog=<log>
Env: ROSE_ZONE (default SUM_EVENT), ROSE_SHOTDIR (default tools/_tmp/lightlab)
"""
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "SUM_EVENT").upper()
SHOTS = os.environ.get("ROSE_SHOTDIR",
                       os.path.join(os.path.dirname(os.path.abspath(__file__)), "_tmp", "lightlab"))
os.makedirs(SHOTS, exist_ok=True)

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

print(f"[lab] zone={ZONE} shots -> {SHOTS}")
if not LES.load_level(f"/Game/Maps/{ZONE}/L_{ZONE}"):
    raise RuntimeError("level load failed")

world = UES.get_editor_world()

# ── camera: PlayerStart, eye height, slight down-pitch ────────────────────────
start = None
sky = None
for a in EAS.get_all_level_actors():
    if isinstance(a, unreal.PlayerStart) and start is None:
        start = a.get_actor_location()
    elif isinstance(a, unreal.SkyLight):
        sky = a
if start is None:
    start = unreal.Vector(0, 0, 200)
eye = unreal.Vector(start.x, start.y, start.z + 250)
print(f"[lab] camera at {eye}")

# nearest object-group static meshes (for the material swap variant)
def nearest_object_actors(n=12):
    out = []
    for a in EAS.get_all_level_actors():
        if not isinstance(a, unreal.StaticMeshActor):
            continue
        lbl = a.get_actor_label()
        if "node" not in lbl:        # importer names placed objects <zone>_node_N
            continue
        d = (a.get_actor_location() - eye).length()
        out.append((d, a))
    out.sort(key=lambda p: p[0])
    return [a for _, a in out[:n]]

GRID = unreal.load_asset("/Engine/EngineMaterials/WorldGridMaterial")

# ── async choreography: one action every few ticks, then quit ────────────────
STEPS = []   # (name, callable)

def shot(tag, yaw):
    def do():
        UES.set_level_viewport_camera_info(eye, unreal.Rotator(0, -12, yaw))
    return do

def snap(tag):
    def do():
        p = os.path.join(SHOTS, f"{ZONE}_{tag}.png")
        unreal.AutomationLibrary.take_high_res_screenshot(1600, 900, p)
        print(f"[lab] shot {p}")
    return do

def cmd(c):
    def do():
        unreal.SystemLibrary.execute_console_command(world, c)
        print(f"[lab] cmd: {c}")
    return do

def set_sky(v):
    def do():
        if sky:
            sky.light_component.set_editor_property("intensity", v)
            sky.light_component.recapture_sky()
            print(f"[lab] skylight intensity={v}")
    return do

def swap_mats():
    def do():
        n = 0
        for a in nearest_object_actors():
            c = a.static_mesh_component
            for i in range(c.get_num_materials()):
                m = c.get_material(i)
                if m and "/Game/Atlas/MI/" in m.get_path_name():
                    c.set_material(i, GRID)
                    n += 1
        print(f"[lab] swapped {n} slots to WorldGridMaterial")
    return do

# baseline lit, 4 yaws
for yaw in (0, 90, 180, 270):
    STEPS.append((f"cam{yaw}", shot("lit", yaw)))
    STEPS.append((f"snap", snap(f"00_lit_yaw{yaw}")))
# unlit proof
STEPS.append(("unlit", cmd("viewmode unlit")))
STEPS.append(("cam0", shot("unlit", 0)))
STEPS.append(("snap", snap("10_unlit_yaw0")))
STEPS.append(("lit", cmd("viewmode lit")))
# skylight x10
STEPS.append(("sky10", set_sky(10.0)))
for yaw in (0, 180):
    STEPS.append((f"cam{yaw}", shot("sky", yaw)))
    STEPS.append((f"snap", snap(f"20_sky10_yaw{yaw}")))
STEPS.append(("sky3", set_sky(3.0)))
# material swap
STEPS.append(("swap", swap_mats()))
for yaw in (0, 180):
    STEPS.append((f"cam{yaw}", shot("swap", yaw)))
    STEPS.append((f"snap", snap(f"30_swap_yaw{yaw}")))

state = {"i": -1, "tick": 0, "handle": None}
TICKS_PER_STEP = 20      # let shaders/screenshots flush between actions

def on_tick(dt):
    state["tick"] += 1
    if state["tick"] % TICKS_PER_STEP != 0:
        return
    state["i"] += 1
    if state["i"] >= len(STEPS):
        print("[lab] done, quitting")
        unreal.unregister_slate_post_tick_callback(state["handle"])
        unreal.SystemLibrary.quit_editor()
        return
    name, fn = STEPS[state["i"]]
    try:
        fn()
    except Exception as e:
        print(f"[lab] step {name} failed: {e}")

state["handle"] = unreal.register_slate_post_tick_callback(on_tick)
print(f"[lab] scheduled {len(STEPS)} steps")
