#!/usr/bin/env python3
"""
export_zone_starts.py — extract each zone's classic spawn point from its ZON.

Every ROSE zone's ZON carries named event points; 'start' is the zone-in spawn
('restore' points are revive pads).  File order is (x, height, y); the proven
UE mapping (same as portals/export_portals.py): UE = (x+520000, -(y+520000)).

Writes tools/_tmp/zone_starts.json: { "JPT01": [ue_x, ue_y], ... }
Zones whose ZON lacks a 'start' fall back to the first 'restore'.
Arua ZON first (matches the imported map geometry), classic fallback.
"""
import glob
import json
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _TOOLS)
from rose_parser.formats.stb import parse as parse_stb
from rose_parser.formats.zon import parse as parse_zon

CENTER = 520000.0
ROOTS = [r"C:\QQ-iROSE Online\extracted\3DDATA",
         r"C:\rose-next-classic\unreal-engine rose\RoseUE\SourceAssets\3DDATA"]

# zone key -> ZON path, via LIST_ZONE.STB col 2 (the ZON file) per root
out = {}
for root in ROOTS:
    stb_path = os.path.join(root, "STB", "LIST_ZONE.STB")
    if not os.path.exists(stb_path):
        continue
    stb = parse_stb(stb_path)
    for row in range(stb.num_rows()):
        zon_rel = (stb.get(row, 1) or "").strip()   # col 1 = ZON path (col 0 = name)
        if not zon_rel or zon_rel == ".":
            continue
        parts = [p for p in zon_rel.replace("\\", "/").split("/") if p]
        if parts and parts[0].upper() == "3DDATA":
            parts = parts[1:]
        zon_abs = os.path.join(root, *parts)
        if not os.path.isfile(zon_abs):
            continue
        key = os.path.basename(os.path.dirname(zon_abs)).upper()  # zone dir = zone key
        if key in out:
            continue                     # Arua (first root) wins
        try:
            z = parse_zon(zon_abs)
        except Exception:
            continue
        start = None
        restore = None
        for e in z.event_objects:
            n = e.name.upper()
            if n == "START" and start is None:
                start = e.position
            elif n == "RESTORE" and restore is None:
                restore = e.position
        p = start or restore
        if p is None:
            continue
        x, h, y = p                       # file order (x, height, y)
        out[key] = [x + CENTER, -(y + CENTER)]

path = os.path.join(_TOOLS, "_tmp", "zone_starts.json")
json.dump(out, open(path, "w"), indent=0, sort_keys=True)
print(f"[starts] {len(out)} zones -> {path}")
for k in ("JG01ARUA"):
    print(f"[starts]   {k}: {out.get(k)}")
