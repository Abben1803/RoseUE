#!/usr/bin/env python3
"""
gen_store_tables.py — Generate the NPC store DATA TABLE (stores.csv) from
LIST_SELL.STB + LIST_SELL_S.STL.

Column map (rose/io/stb.h): 0 = tab name, 1 = STL key, 2..31 = STORE_ITEM
slots (item SN = type*1000 + no; 0 = empty).  NPC rows reference these via
NPC_SELL_TAB0-3 (LIST_NPC cols 21-24 → npcs.csv SellTab0-3).

Usage:  py -3.9 gen_store_tables.py
"""
import csv, os, sys
_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.stb import parse as parse_stb
from rose_parser.formats.stl import parse as parse_stl

# Asset source: Arua (CLAUDE.md "Asset source: Arua, always").  Was the
# deprecated SourceAssets/3DDATA classic tree; ZSC object ids and STB row
# ids do not line up across eras, so a row resolved to different content
# than the Arua-era DataTables this feeds.  Output roots are unchanged.
SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
OUT_DIR = r"C:/rose-next-classic/unreal-engine rose/RoseUE/DataTables"
STORE_SLOTS = 30


def main():
    stb = parse_stb(os.path.join(SRC, "STB", "LIST_SELL.STB"))
    stl = parse_stl(os.path.join(SRC, "STB", "LIST_SELL_S.STL"))

    # Items = semicolon-joined item SNs (slot order kept, zeros dropped) — one
    # string column parses cleaner than 30 int columns in the UE row struct.
    header = ["Name", "Id", "DisplayName", "Items"]
    rows = []
    for i in range(stb.num_rows()):
        items = [stb.get_int(i, 2 + n) for n in range(STORE_SLOTS)]
        if not any(items):
            continue
        key = stb.get(i, 1)
        name = (stl.get(key) if key else "") or stb.get(i, 0)
        rows.append([f"sell_{i}", i, name,
                     ";".join(str(x) for x in items if x)])

    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, "stores.csv")
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f); w.writerow(header)
        for r in rows:
            w.writerow(r)
    print(f"[stores] stores.csv: {len(rows)} tabs -> {path}")


if __name__ == "__main__":
    main()
