"""
ue5_import_npcs.py — Place ARoseNpc actors (town NPCs) in a zone level from
the IFO NPC data exported by tools/export_npcs.py.

Loads /Game/Maps/<ZONE>/L_<ZONE>, wipes previous RoseNpc actors (re-runnable),
spawns one per IFO NPC block (npc id + facing yaw + dialog CON stem), ground-
snaps them, and saves the level.  NPC models must be imported first
(build_monsters.py + ue5_import_monsters.py with the export's ROSE_ONLY list).

Run headless, editor CLOSED (RoseUEEditor must be compiled first):
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=tools/ue5_import_npcs.py ...
Env:
  ROSE_ZONE   zone key (default JPT01), or ALL = every _tmp/npcs_*.json whose
              level exists
"""
import glob
import json
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "JD01").upper()
SKIP = {z.strip().upper() for z in os.environ.get("ROSE_SKIP", "").split(",") if z.strip()}
# The exported ue_yaw misses the NPC model-forward offset — placed NPCs faced 90°
# to their left.  +90 turns them right onto their authored facing (tunable).
YAW_OFF = float(os.environ.get("ROSE_YAW_OFF", "90"))
TOOLS = os.path.dirname(os.path.abspath(__file__))

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log(f"[npcs] {m}")


def place_zone(zone, json_path):
    level = f"/Game/Maps/{zone}/L_{zone}"
    if not EAL.does_asset_exist(level):
        log(f"{zone}: no level {level} — skipped")
        return

    with open(json_path, "r", encoding="utf-8") as f:
        npcs = json.load(f)["npcs"]

    if not LES.load_level(level):
        log(f"{zone}: could not load {level} — skipped")
        return

    removed = 0
    for a in list(EAS.get_all_level_actors()):
        if isinstance(a, unreal.RoseNpc):
            EAS.destroy_actor(a)
            removed += 1

    placed = 0
    for i, p in enumerate(npcs):
        x, y, z = p["ue_pos"]
        # Spawn slightly above the authored point; ARoseMonster's capsule
        # settles onto the ground (feet-at-capsule-bottom mesh offset).
        a = EAS.spawn_actor_from_class(
            unreal.RoseNpc, unreal.Vector(x, y, z + 100.0),
            unreal.Rotator(0.0, 0.0, float(p["ue_yaw"]) + YAW_OFF))
        if not a:
            log(f"{zone}: FAILED npc {p['npc_id']} at ({x:.0f},{y:.0f},{z:.0f})")
            continue
        a.set_actor_label(f"Npc_{zone}_{p['npc_id']}_{p['con'] or 'nodlg'}_{i}")
        a.set_editor_property("npc_id", int(p["npc_id"]))
        a.set_editor_property("con_stem", p["con"])
        a.set_editor_property("event_id", int(p["event_id"]))
        a.set_editor_property("ai_id", int(p["ai"]))
        placed += 1

    LES.save_current_level()
    log(f"{zone}: {placed}/{len(npcs)} NPCs placed (removed {removed} old) — saved")


if ZONE == "ALL":
    for path in sorted(glob.glob(os.path.join(TOOLS, "_tmp", "npcs_*.json"))):
        zone = os.path.basename(path)[5:-5].upper()
        if zone in SKIP:
            log(f"{zone}: in ROSE_SKIP — skipped")
            continue
        place_zone(zone, path)
else:
    place_zone(ZONE, os.path.join(TOOLS, "_tmp", f"npcs_{ZONE}.json"))
log("DONE")
