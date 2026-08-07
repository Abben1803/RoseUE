#!/usr/bin/env python3
"""
gen_status_effects.py — buff/debuff visual data from LIST_STATUS.STB.

Emits Content/DataTables/status_effects.json:
  { "<status_id>": { "fx": <FILE_EFFECT row>, "bad": 0|1|2, "symbol": n } }

Columns (src/common/include/rose/io/stb.h): 3 = STATE_PRIFITS_LOSSES
(0 good / 1 bad / 2 other), 9 = STATE_SYMBOL (HUD icon), 10 = STATE_STEP_EFFECT
(the looping on-character effect while the status is active).

Offline: py -3.9 tools/gen_status_effects.py
"""
import json
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _TOOLS)
from rose_parser.formats.stb import parse as parse_stb

UE = os.path.normpath(os.path.join(_TOOLS, "..", "unreal-engine rose", "RoseUE"))
OUT = os.path.join(UE, "Content", "DataTables", "status_effects.json")

# Asset source: Arua (CLAUDE.md "Asset source: Arua, always").  Was the
# deprecated SourceAssets/3DDATA classic tree; ZSC object ids and STB row
# ids do not line up across eras, so a row resolved to different content
# than the Arua-era DataTables this feeds.  Output roots are unchanged.
SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
stb = parse_stb(os.path.join(SRC, "STB", "LIST_STATUS.STB"))
out = {}
for i in range(stb.num_rows()):
    fx = stb.get_int(i, 10)
    bad = stb.get_int(i, 3)
    sym = stb.get_int(i, 9)
    if fx <= 0 and sym <= 0:
        continue
    out[str(i)] = {"fx": fx, "bad": bad, "symbol": sym}

os.makedirs(os.path.dirname(OUT), exist_ok=True)
json.dump(out, open(OUT, "w"), separators=(",", ":"))
print(f"[statusfx] {len(out)} statuses with fx/symbol -> {OUT}")
