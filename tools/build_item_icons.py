#!/usr/bin/env python3
"""
build_item_icons.py — Slice item icons by PURE GRID ARITHMETIC (the icon rule).

IconIdx (STB col 9, stored AS-IS in the CSVs) maps directly onto the icon
sheets:  sheet = ICON<idx//169 + 1>.dds,  cell = idx%169  in a 13x13 grid of
40px cells on a 512x512 sheet (geometry verified against the well-formed
classic ITEM1.TSI: rects are exactly col*40,row*40,+40, 169 per sheet).

NO TSI enumeration (Arua's TSI is ragged — flat ordering drifts), and NEVER
any STL-key remapping (STLs are name/description links only).

Reads  RoseUE/DataTables/*.csv (IconIdx column)
Writes RoseUE/SourceAssets/UI/Icons/icon_<idx:05d>.png (referenced icons only;
       5 digits to dodge UE's UDIM name pattern)

Usage:  py -3.9 tools/build_item_icons.py
"""
import csv
import os

from PIL import Image

RES_DIR = r"C:\QQ-iROSE Online\extracted\3DDATA\CONTROL\RES"
DT_DIR = r"C:\rose-next-classic\unreal-engine rose\RoseUE\DataTables"
OUT_DIR = r"C:\rose-next-classic\unreal-engine rose\RoseUE\SourceAssets\UI\Icons"

TABLES = ("weapons", "body", "arms", "foot", "cap", "back", "subwpn", "faceitem",
          "jewel", "consumable", "gem", "material", "pat")

PER_SHEET = 169     # 13 x 13
COLS = 13
CELL = 40           # px


def main():
    wanted = set()
    for t in TABLES:
        p = os.path.join(DT_DIR, t + ".csv")
        if not os.path.exists(p):
            continue
        with open(p, encoding="utf-8") as f:
            for row in csv.DictReader(f):
                try:
                    idx = int(row.get("IconIdx") or 0)
                except ValueError:
                    continue
                if idx > 0:
                    wanted.add(idx)
    print(f"[icons] {len(wanted)} unique icons referenced by the item tables")

    os.makedirs(OUT_DIR, exist_ok=True)
    sheets = {}
    made = set()
    done = missing_sheet = 0
    for idx in sorted(wanted):
        sheet_no = idx // PER_SHEET + 1          # icon01.dds holds 0..168
        cell = idx % PER_SHEET
        name = f"ICON{sheet_no:02d}.DDS"
        if name not in sheets:
            p = os.path.join(RES_DIR, name)
            sheets[name] = Image.open(p).convert("RGBA") if os.path.exists(p) else None
        sheet = sheets[name]
        if sheet is None:
            missing_sheet += 1
            continue
        r, c = cell // COLS, cell % COLS
        x0, y0 = c * CELL, r * CELL
        spr = sheet.crop((x0, y0, min(x0 + CELL, sheet.width), min(y0 + CELL, sheet.height)))
        spr.save(os.path.join(OUT_DIR, f"icon_{idx:05d}.png"))
        made.add(idx)
        done += 1
    print(f"[icons] sliced {done} -> {OUT_DIR}  ({missing_sheet} past the last sheet)")

    # ── PRUNE slices no current table references ─────────────────────────────
    #
    # Slicing only WRITES.  A previous era's icons therefore survive on disk,
    # and because ue5_import_ui_sprites.py builds its orphan list from the PNG
    # folder, those survivors look "expected" and its sweep never fires — so
    # UE keeps a texture per stale index and the importer overwrites nothing.
    #
    # That shipped the era-mixing bug: after the Arua->classic switch, 4735
    # July slices stayed behind and every item icon in the game was Arua art
    # addressed by a classic IconIdx (Arua tops out at 7612, classic at 8450 —
    # the indices do not correspond).  Deleting here is what lets the sweep
    # downstream see the truth.
    # Keyed on what was PRODUCED, not what was wanted: an index whose sheet is
    # missing would otherwise keep its stale slice and count as legitimate.
    keep = {f"icon_{i:05d}.png" for i in made}
    stale = [f for f in os.listdir(OUT_DIR)
             if f.lower().endswith(".png") and f not in keep]
    for f in stale:
        os.remove(os.path.join(OUT_DIR, f))
    print(f"[icons] pruned {len(stale)} slice(s) no table references")


if __name__ == "__main__":
    main()
