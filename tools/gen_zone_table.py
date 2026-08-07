#!/usr/bin/env python3
"""
gen_zone_table.py — zone id -> level key table for runtime warps.

QSD teleport rewards (and WARP.STB) reference zones by LIST_ZONE.STB row id;
our levels are named L_<map-folder-key>.  Emit the id->key map so
RoseQuest.cpp can resolve quest teleports:

  Content/Quests/zones.json   { "2": "JPT01", "6": "JG01", ... }

Usage: py -3.9 gen_zone_table.py
"""
import json
import os
import sys

_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.stb import parse as parse_stb

# Asset source: Arua (CLAUDE.md "Asset source: Arua, always").  Was the
# deprecated SourceAssets/3DDATA classic tree; ZSC object ids and STB row
# ids do not line up across eras, so a row resolved to different content
# than the Arua-era DataTables this feeds.  Output roots are unchanged.
SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
OUT = r"C:/rose-next-classic/unreal-engine rose/RoseUE/Content/Quests/zones.json"

zones = parse_stb(os.path.join(SRC, "STB", "LIST_ZONE.STB"))
table = {}
for i in range(zones.num_rows()):
    zon = zones.get(i, 1)
    if not zon.strip():
        continue
    parts = [p for p in zon.replace("\\", "/").split("/") if p]
    if len(parts) < 2:
        continue
    table[str(i)] = parts[-2].upper()

with open(OUT, "w", encoding="utf-8") as f:
    json.dump(table, f, indent=1, sort_keys=True)
print(f"[zones] {len(table)} zones -> {OUT}")
