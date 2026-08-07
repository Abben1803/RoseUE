#!/usr/bin/env python3
"""
gen_dialog_data.py — Convert every 3DDATA/EVENT/*.CON conversation script to
JSON for the UE dialog runtime, with text resolved from ULNGTB_CON.LTB.

Output: Content/Dialogs/<stem>.json   (stem = CON filename without extension,
uppercased — the IFO NPC 'file' string matches case-insensitively)

JSON shape (mirrors cevent.cpp structures):
  {
    "eventFuncs": ["", ... 16],        # Lua funcs per event index (on-dead etc.)
    "gateCheck":  "func",              # messages[0].check — gates the whole conv
    "gateClick":  "func",              # messages[0].click — End() restart hook
    "menus": [ [ {"type":int, "child":int, "check":str, "click":str,
                  "text":str, "textKo":str}, ... ], ... ],
    "lua": "<base64 Lua 4.0 bytecode chunk>"
  }

Usage:  py -3.9 gen_dialog_data.py
"""
import base64
import json
import os
import sys

_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.con import parse as parse_con
from rose_parser.formats.ltb import parse as parse_ltb

# Asset source: Arua (CLAUDE.md "Asset source: Arua, always").  Was the
# deprecated SourceAssets/3DDATA classic tree; row ids do not line up across
# eras, so rows resolved to different content than the Arua-era DataTables.
# Arua ships 525 EVENT/*.CON vs classic's 125.  Output root is unchanged.
SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
OUT_DIR = r"C:/rose-next-classic/unreal-engine rose/RoseUE/Content/Dialogs"

LANG_EN = 2   # ULNGTB_CON.LTB columns: 0 key, 1 Korean, 2 English, 3 JP, 4/5 CN
LANG_KO = 1


def main():
    event_dir = os.path.join(SRC, "EVENT")
    ltb = parse_ltb(os.path.join(event_dir, "ULNGTB_CON.LTB"))
    print(f"[dialogs] ULNGTB_CON.LTB: {ltb.row_count} rows x {ltb.col_count} langs")

    os.makedirs(OUT_DIR, exist_ok=True)
    n = 0
    for f in sorted(os.listdir(event_dir)):
        if not f.upper().endswith(".CON"):
            continue
        path = os.path.join(event_dir, f)
        try:
            con = parse_con(path)
        except Exception as e:
            print(f"[dialogs] FAIL {f}: {e}")
            continue

        def text(sid, col):
            t = ltb.get(sid, col)
            return t if t else ltb.get(sid, LANG_KO)

        menus = []
        for menu in con.menus:
            menus.append([{
                "type": it.type,
                "child": it.child,
                "check": it.check_func,
                "click": it.click_func,
                "text": text(it.str_id, LANG_EN),
                "textKo": ltb.get(it.str_id, LANG_KO),
            } for it in menu])

        gate = con.messages[0] if con.messages else None
        doc = {
            "eventFuncs": con.event_funcs,
            "gateCheck": gate.check_func if gate else "",
            "gateClick": gate.click_func if gate else "",
            "menus": menus,
            "lua": base64.b64encode(con.lua_chunk).decode("ascii"),
        }

        stem = os.path.splitext(f)[0].upper()
        with open(os.path.join(OUT_DIR, stem + ".json"), "w",
                  encoding="utf-8") as fp:
            json.dump(doc, fp, ensure_ascii=False, indent=1)
        n += 1

    print(f"[dialogs] {n} CON files -> {OUT_DIR}")


if __name__ == "__main__":
    main()
