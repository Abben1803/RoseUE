"""
ue5_apply_zsc_part_flags.py — Apply ZSC per-part data to imported map levels:

  * collision mode 0 (zz_collision_level & 7 == 0) -> NoCollision profile
    (faithful to the client: setCollisionLevel(0) = never collides; the old
    UE4 plugin did the same via SetCollisionResponseToAllChannels(ECR_Ignore))
  * ZZ_CL_NOTCAMERACOLLISION (1<<6) -> ignore ECC_Camera
  * animated parts (SWITCH_CNST_ANI) -> tag "RoseAnim_<node>", mobility
    Movable, and one ARoseMapAnimManager spawned per level (ZoneKey set)

Reads tools/_tmp/zsc_parts_<ZONE>.json (gen_zsc_part_manifest.py). Every actor
is validated against the manifest's expected UE location before it is touched;
mismatches are skipped and counted.

Env:  ROSE_ZONES  comma list or ALL (default JPT01)
Run headless, editor CLOSED, -nullrhi is fine.
"""
import os
import json
import re
import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
ZONES = os.environ.get("ROSE_ZONES", "JPT01").strip()

EAL = unreal.EditorAssetLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

NODE_RE = re.compile(r"_node_(\d+)$")


def log(m):
    unreal.log(f"[zscflags] {m}")


if ZONES.upper() == "ALL":
    zones = sorted(
        f[len("zsc_parts_"):-len(".json")]
        for f in os.listdir(os.path.join(TOOLS, "_tmp"))
        if f.startswith("zsc_parts_") and f.endswith(".json"))
else:
    zones = [z.strip() for z in ZONES.split(",") if z.strip()]

for zone in zones:
    mp = os.path.join(TOOLS, "_tmp", f"zsc_parts_{zone}.json")
    if not os.path.exists(mp):
        log(f"skip {zone}: no manifest")
        continue
    with open(mp) as f:
        man = json.load(f)
    actors_by_node = man["actors"]
    if not actors_by_node:
        log(f"{zone}: nothing to do")
        continue
    level = f"/Game/Maps/{zone}/L_{zone}"
    if not EAL.does_asset_exist(level):
        log(f"skip {zone}: no level {level}")
        continue
    if not LES.load_level(level):
        log(f"skip {zone}: level load failed")
        continue

    n_nocol = n_nocam = n_anim = n_mismatch = 0
    seen_nodes = set()
    for a in EAS.get_all_level_actors():
        if not isinstance(a, unreal.StaticMeshActor):
            continue
        m = NODE_RE.search(a.get_actor_label())
        if not m:
            continue
        entry = actors_by_node.get(m.group(1))
        if entry is None:
            continue
        # validate placement against the manifest before touching anything
        loc = a.get_actor_location()
        exp = entry["ue_loc"]
        if max(abs(loc.x - exp[0]), abs(loc.y - exp[1]), abs(loc.z - exp[2])) > 2.0:
            n_mismatch += 1
            continue
        seen_nodes.add(m.group(1))
        comp = a.static_mesh_component
        if entry.get("no_collision"):
            comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
            comp.set_collision_profile_name("NoCollision")
            n_nocol += 1
        elif entry.get("no_camera"):
            comp.set_collision_response_to_channel(
                unreal.CollisionChannel.ECC_CAMERA, unreal.CollisionResponse.ECR_IGNORE)
            n_nocam += 1
        if entry.get("anim"):
            tag = f"RoseAnim_{m.group(1)}"
            tags = list(a.tags)
            if tag not in [str(t) for t in tags]:
                tags.append(tag)
                a.tags = tags
            comp.set_mobility(unreal.ComponentMobility.MOVABLE)
            n_anim += 1

    # one manager per level when the zone has animated parts
    has_anim_json = os.path.exists(os.path.join(
        TOOLS, "..", "unreal-engine rose", "RoseUE", "Content", "MapAnims", f"{zone}.json"))
    if n_anim and has_anim_json:
        existing = [a for a in EAS.get_all_level_actors()
                    if a.get_class().get_name() == "RoseMapAnimManager"]
        if not existing:
            mgr = EAS.spawn_actor_from_class(
                unreal.RoseMapAnimManager, unreal.Vector(0, 0, 0), unreal.Rotator(0, 0, 0))
            mgr.set_editor_property("zone_key", zone)
            mgr.set_actor_label("RoseMapAnimManager")
            log(f"{zone}: spawned RoseMapAnimManager")
        else:
            existing[0].set_editor_property("zone_key", zone)

    missing = len(actors_by_node) - len(seen_nodes)
    LES.save_current_level()
    log(f"{zone}: no_collision={n_nocol} no_camera={n_nocam} anim={n_anim} "
        f"mismatch={n_mismatch} unmatched_manifest_nodes={missing} SAVED")

log("DONE")
