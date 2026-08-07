#!/usr/bin/env py -3.9
"""
gen_refine_table.py — LIST_GRADE.STB -> Content/DataTables/refine.json

The classic refine ("grade") table: one row per grade 1..9 with the stat
bonuses an item gains at that grade and the AUTHENTIC glow colour the client
tints the refined item with (RGB GLOW column, packed "RRRGGGBBB" — e.g. grade 1
"030050030" = (30,50,30) faint green, grade 9 "255255140" bright white-gold).

Consumed by ARoseCharacter::LoadRefineData (same FFileHelper+Json pattern as
gear_equip.json).  Stats semantics (cuserdata.cpp): weapon grade adds ATK+HIT;
armor/shield grade adds DEF+RES+AVOID.

Usage: py -3.9 tools/gen_refine_table.py
"""
import json
import os
import sys

_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.stb import parse as parse_stb

SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
OUT = os.path.join(_T, "..", "unreal-engine rose", "RoseUE", "Content",
                   "DataTables", "refine.json")

stb = parse_stb(os.path.join(SRC, "STB", "LIST_GRADE.STB"))
grades = []
for r in range(1, stb.num_rows()):
    atk = stb.get(r, 0)
    if not (atk or "").strip():
        break                              # grades end at the first empty row
    glow = (stb.get(r, 5) or "").strip()   # "RRRGGGBBB"
    rgb = [int(glow[i:i + 3]) for i in (0, 3, 6)] if len(glow) == 9 else [0, 0, 0]
    grades.append({
        "grade": r,
        "atk":   int(stb.get(r, 0) or 0),
        "hit":   int(stb.get(r, 1) or 0),
        "def":   int(stb.get(r, 2) or 0),
        "res":   int(stb.get(r, 3) or 0),
        "avoid": int(stb.get(r, 4) or 0),
        "r": rgb[0], "g": rgb[1], "b": rgb[2],
    })

os.makedirs(os.path.dirname(OUT), exist_ok=True)
json.dump({"grades": grades}, open(OUT, "w"), indent=1)
print(f"[refine] {len(grades)} grades -> {os.path.normpath(OUT)}")
for g in grades:
    print(f"  +{g['grade']}: atk {g['atk']:3d}  hit {g['hit']:3d}  def {g['def']:3d}  "
          f"res {g['res']:3d}  avoid {g['avoid']:2d}  glow ({g['r']},{g['g']},{g['b']})")
