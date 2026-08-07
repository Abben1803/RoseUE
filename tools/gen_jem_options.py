#!/usr/bin/env py -3.9
"""
gen_jem_options.py — LIST_JEMITEM.STB -> Content/DataTables/jem_options.json

The random "bonus option" a dropped item can carry (FRoseItemStack.Bonus /
classic m_nGEM_OP) is a ROW ID into LIST_JEMITEM.STB.  Each option row grants up
to two named stats: TYPE at col 16/18, VALUE at col 17/19 (verified against the
classic macros GEMITEM_ADD_DATA_TYPE/VALUE, stb.h:242 — the Arua header row is
mislabeled by one, so read by INDEX here, not by header name).

TYPE is a t_AbilityINDEX (datatype.h:527): 10..15 = STR..SENSE, 16 HP, 17 MP,
18 ATK, 19 DEF, 20 HIT, 21 RES, 22 AVOID, 23 SPEED, 24 ATK_SPD, 25 WEIGHT,
26 CRIT, 27/28 HP/MP recovery, 29 MP save, 38/39 MAX_HP/MP, 40 MONEY.

Output: { "options": { "<row>": [[type, value], ...] } }
Usage:  py -3.9 tools/gen_jem_options.py
"""
import json
import os
import sys

_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.stb import parse as parse_stb

SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
OUT = os.path.join(_T, "..", "unreal-engine rose", "RoseUE", "Content",
                   "DataTables", "jem_options.json")

stb = parse_stb(os.path.join(SRC, "STB", "LIST_JEMITEM.STB"))
opts = {}
for r in range(1, stb.num_rows()):
    pairs = []
    for base in (16, 18):
        t = (stb.get(r, base) or "").strip()
        v = (stb.get(r, base + 1) or "").strip()
        if t and v:
            try:
                pairs.append([int(t), int(v)])
            except ValueError:
                pass
    if pairs:
        opts[str(r)] = pairs

json.dump({"options": opts}, open(OUT, "w"), indent=0)
print(f"[jemop] {len(opts)} option rows -> {os.path.normpath(OUT)}")
for r in ("1", "3", "11", "12", "100", "150"):
    if r in opts:
        print(f"  option {r}: {opts[r]}")
