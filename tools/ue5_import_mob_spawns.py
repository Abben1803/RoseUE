"""
ue5_import_mob_spawns.py — Place ARoseMonsterSpawner actors in zone levels
from the IFO REGEN data exported by tools/export_mob_spawns.py.

Loads /Game/Maps/<ZONE>/L_<ZONE>, wipes previous RoseMonsterSpawner actors
(re-runnable), spawns one per IFO MonsterSpawn point with the basic/tactic
slots, interval (s), limit, range (IFO meters -> cm) and saves the level.

Run headless, editor CLOSED (RoseUEEditor must be compiled first):
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=tools/ue5_import_mob_spawns.py ...
Env:
  ROSE_ZONE   zone key (default JPT01), or ALL = every _tmp/mob_spawns_*.json
              whose level exists
  ROSE_SKIP   comma-separated zones to skip in ALL mode
"""
import glob
import json
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "JPT01").upper()
SKIP = {z.strip().upper() for z in os.environ.get("ROSE_SKIP", "").split(",") if z.strip()}
TOOLS = os.path.dirname(os.path.abspath(__file__))

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log(f"[mobspawn] {m}")


def entries(lst):
    out = []
    for e in lst:
        s = unreal.RoseSpawnEntry()
        s.set_editor_property("npc_id", int(e["npc_id"]))
        s.set_editor_property("count", int(e["count"]))
        out.append(s)
    return out


def place_zone(zone, json_path):
    level = f"/Game/Maps/{zone}/L_{zone}"
    if not EAL.does_asset_exist(level):
        log(f"{zone}: no level {level} — skipped")
        return

    with open(json_path, "r", encoding="utf-8") as f:
        points = json.load(f)["points"]

    if not LES.load_level(level):
        log(f"{zone}: could not load {level} — skipped")
        return

    removed = 0
    for a in list(EAS.get_all_level_actors()):
        if isinstance(a, unreal.RoseMonsterSpawner):
            EAS.destroy_actor(a)
            removed += 1

    placed = 0
    for i, p in enumerate(points):
        x, y, z = p["ue_pos"]
        a = EAS.spawn_actor_from_class(unreal.RoseMonsterSpawner,
                                       unreal.Vector(x, y, z), unreal.Rotator())
        if not a:
            log(f"{zone}: FAILED spawn point {i} at ({x:.0f},{y:.0f},{z:.0f})")
            continue
        a.set_actor_label(f"Regen_{zone}_{i}_{p['chunk'].split('.')[0]}")
        a.set_editor_property("basic_spawns", entries(p["basic"]))
        a.set_editor_property("tactic_spawns", entries(p["tactic"]))
        a.set_editor_property("interval", float(max(1, p["interval"])))
        a.set_editor_property("limit_count", int(p["limit"]))
        a.set_editor_property("range", float(p["range"] * 100))   # m -> cm
        a.set_editor_property("tactic_points", int(p["tactic_points"]) or 100)
        placed += 1

    LES.save_current_level()
    log(f"{zone}: {placed}/{len(points)} spawners placed (removed {removed} old) — saved")


if ZONE == "ALL":
    for path in sorted(glob.glob(os.path.join(TOOLS, "_tmp", "mob_spawns_*.json"))):
        zone = os.path.basename(path)[len("mob_spawns_"):-len(".json")].upper()
        if zone in SKIP:
            log(f"{zone}: in ROSE_SKIP — skipped")
            continue
        place_zone(zone, path)
else:
    place_zone(ZONE, os.path.join(TOOLS, "_tmp", f"mob_spawns_{ZONE}.json"))
log("DONE")
