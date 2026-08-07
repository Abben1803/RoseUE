#!/usr/bin/env python3
"""
export_mob_spawns.py — Export a zone's monster-spawn (REGEN) points from its
chunk IFOs to JSON for ue5_import_mob_spawns.py.

Positions convert to UE world space with the SAME mapping the imported map
scene uses (verified against tools/map_scene_probe.json):
    UE = (x + 520000, -(y + 520000), z)      [ROSE cm, zone-centred in file]
Interval stays in SECONDS and range in METERS (raw file values — the runtime
scales like CRegenPOINT::Load does: ×1000 ms, ×100 cm).

Usage:  py -3.9 export_mob_spawns.py [ZONE=JPT01] [OUT_ZONE]
Env:    ROSE_ASSET_ROOT — read IFOs from another client's 3DDATA (JPT01V2);
        OUT_ZONE renames the output/level key.
Output: tools/_tmp/mob_spawns_<OUT_ZONE|ZONE>.json
"""
import json
import os
import sys

_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.ifo import parse as parse_ifo, BLOCK_MONSTER

# Default asset root is Arua (CLAUDE.md); the classic tree is deprecated.
SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
CENTER_WORLD = 32 * 16000 + 8000   # 520000 (mapforge/config.py)


def zone_dir(zone: str) -> str:
    # Zones live at 3DDATA/MAPS/<PLANET>/<ZONE>/ — search the planets.
    maps = os.path.join(SRC, "MAPS")
    for planet in os.listdir(maps):
        d = os.path.join(maps, planet, zone)
        if os.path.isdir(d):
            return d
    raise SystemExit(f"zone folder not found under {maps}: {zone}")


def main():
    zone = (sys.argv[1] if len(sys.argv) > 1 else "JPT01").upper()
    out_zone = sys.argv[2].upper() if len(sys.argv) > 2 else zone
    d = zone_dir(zone)

    points = []
    ids = {}
    for f in sorted(os.listdir(d)):
        if not f.upper().endswith(".IFO"):
            continue
        ifo = parse_ifo(os.path.join(d, f))
        for o in ifo.get_lump(BLOCK_MONSTER):
            x, y, z = o.position
            entry = {
                "name": o.extra or o.name,
                "chunk": f,
                "ue_pos": [x + CENTER_WORLD, -(y + CENTER_WORLD), z],
                "basic": [{"npc_id": e.npc_id, "count": e.count, "name": e.name}
                          for e in o.spawn_basic],
                "tactic": [{"npc_id": e.npc_id, "count": e.count, "name": e.name}
                           for e in o.spawn_tactic],
                "interval": o.spawn_interval,        # seconds
                "limit": o.spawn_limit,
                "range": o.spawn_range,              # meters
                "tactic_points": o.spawn_tactic_points,
            }
            points.append(entry)
            for e in o.spawn_basic + o.spawn_tactic:
                ids[e.npc_id] = e.name

    out_dir = os.path.join(_T, "_tmp")
    os.makedirs(out_dir, exist_ok=True)
    out = os.path.join(out_dir, f"mob_spawns_{out_zone}.json")
    with open(out, "w", encoding="utf-8") as fp:
        json.dump({"zone": out_zone, "points": points}, fp, indent=1)

    print(f"[spawns] {out_zone}: {len(points)} spawn points -> {out}")
    print(f"[spawns] npc ids used: "
          + ", ".join(f"{i}({n})" for i, n in sorted(ids.items())))
    print("[spawns] ROSE_ONLY=\"" + ",".join(str(i) for i in sorted(ids)) + "\"")


if __name__ == "__main__":
    main()
