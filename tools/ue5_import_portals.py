"""
ue5_import_portals.py — Place ARoseWarpPortal actors in a zone level from
tools/_tmp/portals.json (export_portals.py).

Also removes any hand-placed ARoseCharacter from the level — ARoseGameMode
spawns the pawn at the PlayerStart now, and a placed one would double-possess.

Env: ROSE_ZONE=JPT01.  Run headless, editor CLOSED.  Re-runnable.
"""
import json
import os
import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
ZONE = os.environ.get("ROSE_ZONE", "JDT01").upper()
LEVEL = f"/Game/Maps/{ZONE}/L_{ZONE}"

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

with open(os.path.join(TOOLS, "_tmp", "portals.json"), encoding="utf-8") as f:
    portals = json.load(f).get(ZONE, [])

if not unreal.EditorAssetLibrary.does_asset_exist(LEVEL):
    print(f"[portal] {ZONE}: level missing, skipped")
    raise SystemExit(0)
if not LES.load_level(LEVEL):
    raise RuntimeError(f"could not load {LEVEL}")

removed = chars = 0
for a in list(EAS.get_all_level_actors()):
    if isinstance(a, unreal.RoseWarpPortal):
        EAS.destroy_actor(a); removed += 1
    elif isinstance(a, unreal.RoseCharacter):
        EAS.destroy_actor(a); chars += 1

placed = 0
for i, p in enumerate(portals):
    x, y, z = p["pos"]
    a = EAS.spawn_actor_from_class(unreal.RoseWarpPortal,
                                   unreal.Vector(x, y, z + 100.0),
                                   unreal.Rotator(0.0, 0.0, p.get("yaw", 0.0)))
    if not a:
        continue
    a.set_actor_label(f"Warp_{ZONE}_{i}_{p['dest_zone']}")
    a.set_editor_property("dest_level", p["dest_level"])
    a.set_editor_property("dest_x", float(p["dest_pos"][0]))
    a.set_editor_property("dest_y", float(p["dest_pos"][1]))
    a.set_editor_property("dest_zone_name", p["dest_name"])
    e = p["extent"]
    a.set_editor_property("trigger_extent", unreal.Vector(e[0], e[1], e[2]))
    placed += 1

LES.save_current_level()
print(f"[portal] {ZONE}: {placed}/{len(portals)} gates placed "
      f"(removed {removed} old, {chars} placed characters) -> saved")
