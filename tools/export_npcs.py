#!/usr/bin/env python3
"""
export_npcs.py — Export a zone's town-NPC placements (IFO NPC lump) to JSON
for ue5_import_npcs.py.

Faithful to io_terrain.cpp LUMP_TERRAIN_MOB: each entry = LIST_NPC id + AI id
+ dialog CON filename (matched case-insensitively against LIST_EVENT.STB
col 3 basenames → event row, like the client does; we also keep the stem so
the runtime can load Content/Dialogs/<STEM>.json directly).

Position converts like every other placement:
    UE = (x + 520000, -(y + 520000), z)
Facing: IFO quaternions are (x,y,z,w) file order; ROSE NPC rotation is about
+Z, and the Y-mirror into UE flips the angle sign:
    ue_yaw_deg = -degrees(2 * atan2(z, w))

Usage:  py -3.9 export_npcs.py [ZONE=JPT01 | ALL] [OUT_ZONE]
Env:    ROSE_ASSET_ROOT — read IFOs from another client's 3DDATA (e.g. the
        modern client at RoseUE/extracted/3DDATA for JPT01V2); ids are still
        FILTERED against the CLASSIC LIST_NPC (mixing STBs across clients is
        forbidden), so new-client-only NPCs are dropped.
        OUT_ZONE renames the output/level key (JPT01 → JPT01V2).
Output: tools/_tmp/npcs_<OUT_ZONE|ZONE>.json
"""
import json
import math
import os
import sys

_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.ifo import parse as parse_ifo, BLOCK_NPC
from rose_parser.formats.stb import parse as parse_stb

# Default asset root is Arua (CLAUDE.md); the classic tree is deprecated.
CLASSIC = r"C:\QQ-iROSE Online\extracted\3DDATA"
SRC = os.environ.get("ROSE_ASSET_ROOT", CLASSIC)
CENTER_WORLD = 32 * 16000 + 8000   # 520000


def all_zones():
    maps = os.path.join(SRC, "MAPS")
    for planet in sorted(os.listdir(maps)):
        pd = os.path.join(maps, planet)
        if not os.path.isdir(pd):
            continue
        for zone in sorted(os.listdir(pd)):
            if os.path.isdir(os.path.join(pd, zone)):
                yield zone, os.path.join(pd, zone)


def zone_dir(zone):
    for z, d in all_zones():
        if z.upper() == zone.upper():
            return d
    raise SystemExit(f"zone not found: {zone}")


def load_event_index():
    """LIST_EVENT.STB col 3 basename (lower, no ext) -> row id (CLASSIC)."""
    stb = parse_stb(os.path.join(CLASSIC, "STB", "LIST_EVENT.STB"))
    idx = {}
    for i in range(stb.num_rows()):
        p = stb.get(i, 3)
        if p:
            stem = os.path.splitext(os.path.basename(p.replace("\\", "/")))[0]
            idx[stem.lower()] = i
    return idx


def export_zone(zone, d, ev_index, npc_stb):
    npcs = []
    ids = set()
    dropped = 0
    for f in sorted(os.listdir(d)):
        if not f.upper().endswith(".IFO"):
            continue
        ifo = parse_ifo(os.path.join(d, f))
        for o in ifo.get_lump(BLOCK_NPC):
            # Filter against the CLASSIC LIST_NPC: a placement from another
            # client whose id has no classic row (no model/dialog) is dropped.
            if o.obj_id >= npc_stb.num_rows() or not (
                    npc_stb.get(o.obj_id, 0).strip() or npc_stb.get(o.obj_id, 40).strip()):
                dropped += 1
                continue
            x, y, z = o.position
            qx, qy, qz, qw = o.rotation   # reader labels are wxyz but file is xyzw
            yaw = -math.degrees(2.0 * math.atan2(qz, qw))
            con = os.path.splitext(os.path.basename(
                o.npc_con.replace("\\", "/")))[0] if o.npc_con else ""
            # dialogs must exist in the CLASSIC set (Content/Dialogs)
            if con and ev_index.get(con.lower()) is None:
                con = ""
            npcs.append({
                "npc_id": o.obj_id,
                "chunk": f,
                "ue_pos": [x + CENTER_WORLD, -(y + CENTER_WORLD), z],
                "ue_yaw": yaw,
                "ai": o.npc_ai,
                "con": con.upper(),
                "event_id": ev_index.get(con.lower(), 0) if con else 0,
                "npc_type": npc_stb.get_int(o.obj_id, 27),
            })
            ids.add(o.obj_id)
    if dropped:
        print(f"[npcs] {zone}: dropped {dropped} placements with no classic LIST_NPC row")
    return npcs, ids


def main():
    arg = (sys.argv[1] if len(sys.argv) > 1 else "JPT01").upper()
    out_override = sys.argv[2].upper() if len(sys.argv) > 2 else ""
    ev_index = load_event_index()
    npc_stb = parse_stb(os.path.join(CLASSIC, "STB", "LIST_NPC.STB"))

    zones = list(all_zones()) if arg == "ALL" else [(arg, zone_dir(arg))]
    out_dir = os.path.join(_T, "_tmp")
    os.makedirs(out_dir, exist_ok=True)

    all_ids = set()
    for zone, d in zones:
        out_zone = out_override or zone.upper()
        npcs, ids = export_zone(zone.upper(), d, ev_index, npc_stb)
        if not npcs and arg == "ALL":
            continue
        out = os.path.join(out_dir, f"npcs_{out_zone}.json")
        with open(out, "w", encoding="utf-8") as fp:
            json.dump({"zone": out_zone, "npcs": npcs}, fp, indent=1)
        with_dlg = sum(1 for n in npcs if n["con"])
        print(f"[npcs] {out_zone}: {len(npcs)} NPCs "
              f"({with_dlg} with dialogs, {len(ids)} unique models) -> {out}")
        all_ids |= ids

    if all_ids:
        print("[npcs] ROSE_ONLY=\"" + ",".join(str(i) for i in sorted(all_ids)) + "\"")


if __name__ == "__main__":
    main()
