#!/usr/bin/env python3
"""
gen_drop_table.py — export ITEM_DROP.STB into a runtime JSON the UE port loads.

The server's drop roll (CCal::Get_DropITEM, src/common/calculation.cpp:117) reads
DROPITEM_ITEMNO(row, NO) = g_TblDropITEM.get_int32(row, 1 + NO): each drop-table
row holds up to 50 encoded item codes (type*1000 + itemno); the mob's NPC
DROP_TYPE column picks the row.  We only need the rows referenced by a spawnable
NPC (npcs.csv DropType), so the JSON stays small.

Output: RoseUE/Content/DataTables/item_drops.json
    { "cols": 50, "rows": { "<rowIdx>": [c0, c1, ... c49], ... } }
where c<NO> = DROPITEM_ITEMNO(row, NO) (col 1+NO).

Usage:  py -3.9 gen_drop_table.py
"""
import csv, json, os, sys
_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.stb import parse as parse_stb

# Asset source: Arua (CLAUDE.md "Asset source: Arua, always").  Was the
# deprecated SourceAssets/3DDATA classic tree; ZSC object ids and STB row
# ids do not line up across eras, so a row resolved to different content
# than the Arua-era DataTables this feeds.  Output roots are unchanged.
SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
OUT_DIR = r"C:/rose-next-classic/unreal-engine rose/RoseUE/Content/DataTables"
NPCS_CSV = r"C:/rose-next-classic/unreal-engine rose/RoseUE/DataTables/npcs.csv"
NUM_NO = 50   # NO = 0..49 → cols 1..50 (item_drop.stb col_count is 51)


def referenced_rows():
    """Drop-table rows referenced by any spawnable NPC (npcs.csv DropType)."""
    rows = set()
    if not os.path.isfile(NPCS_CSV):
        print(f"[drops] {NPCS_CSV} not found — emitting all non-empty rows")
        return None
    with open(NPCS_CSV, newline="", encoding="utf-8") as f:
        for r in csv.DictReader(f):
            try:
                dt = int(r.get("DropType", 0))
            except ValueError:
                dt = 0
            if dt > 0:
                rows.add(dt)
    return rows


def main():
    stb = parse_stb(os.path.join(SRC, "STB", "ITEM_DROP.STB"))
    n_rows = stb.num_rows()
    want = referenced_rows()

    out_rows = {}
    for row in range(1, n_rows):
        if want is not None and row not in want:
            continue
        cols = [stb.get_int(row, 1 + no) for no in range(NUM_NO)]
        if any(c != 0 for c in cols):
            out_rows[str(row)] = cols

    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, "item_drops.json")
    with open(path, "w", encoding="utf-8") as f:
        json.dump({"cols": NUM_NO, "rows": out_rows}, f, separators=(",", ":"))
    print(f"[drops] item_drops.json: {len(out_rows)} drop rows "
          f"({'referenced' if want is not None else 'all non-empty'}) -> {path}")


if __name__ == "__main__":
    main()
