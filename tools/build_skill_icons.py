#!/usr/bin/env python3
"""
build_skill_icons.py — Slice SKILL icons from the client's dedicated skill icon
sheet.  Skill icons are NOT in the item icon space: the client draws them from
IMAGE_RES_SKILL_ICON = 3DData\\Control\\Res\\SKILLICON.TSI, indexed by
SKILL_ICON_NO (io_imageres.cpp:211; skill.cpp:139 CSkill::DrawIcon ->
Draw(..., IMAGE_RES_SKILL_ICON, SKILL_ICON_NO)).  Using ITEM1.TSI here (the old
mistake) yields armor icons.

Reads  RoseUE/DataTables/skills.csv (IconIdx = SKILL_ICON_NO)
Writes RoseUE/SourceAssets/UI/SkillIcons/skillicon_<idx:05d>.png  (5-digit pad
       dodges UE's UDIM collapse, same as item icons).
"""
import csv
import os
import struct

from PIL import Image

RES_DIR = r"C:\QQ-iROSE Online\extracted\3DDATA\CONTROL\RES"
DT_DIR = r"C:\rose-next-classic\unreal-engine rose\RoseUE\DataTables"
OUT_DIR = r"C:\rose-next-classic\unreal-engine rose\RoseUE\SourceAssets\UI\SkillIcons"


def read_tsi_flat(path):
    """Flattened (sheet, x0, y0, x1, y1) sprite list in TSI order (= icon index)."""
    d = open(path, "rb").read()
    o = 0

    def u16():
        nonlocal o
        v = struct.unpack_from("<H", d, o)[0]
        o += 2
        return v

    def u32():
        nonlocal o
        v = struct.unpack_from("<I", d, o)[0]
        o += 4
        return v

    ns = u16()
    sheet_names = []
    for _ in range(ns):
        ln = u16()
        sheet_names.append(d[o:o + ln].decode("latin-1"))
        o += ln
        u32()
    u16()
    flat = []
    for i in range(ns):
        for _ in range(u16()):
            u16()
            x0, y0, x1, y1 = u32(), u32(), u32(), u32()
            u32()
            o += 32
            flat.append((sheet_names[i], x0, y0, x1, y1))
    return flat


def main():
    flat = read_tsi_flat(os.path.join(RES_DIR, "SKILLICON.TSI"))
    print(f"[skillicons] SKILLICON.TSI: {len(flat)} sprites")

    wanted = set()
    with open(os.path.join(DT_DIR, "skills.csv"), encoding="utf-8-sig") as f:
        for row in csv.DictReader(f):
            idx = int(row.get("IconIdx") or 0)
            if idx > 0:
                wanted.add(idx)
    print(f"[skillicons] {len(wanted)} unique skill icons referenced")

    os.makedirs(OUT_DIR, exist_ok=True)
    sheets, done, bad = {}, 0, 0
    made = set()
    missing_sheets = set()
    for idx in sorted(wanted):
        if not (0 <= idx < len(flat)):
            bad += 1
            continue
        sheet, x0, y0, x1, y1 = flat[idx]
        key = sheet.lower()
        if key not in sheets:
            path = os.path.join(RES_DIR, sheet)
            if not os.path.exists(path):
                for f in os.listdir(RES_DIR):
                    if f.lower() == key:
                        path = os.path.join(RES_DIR, f)
                        break
            # A TSI can name sheets the client does not ship: this one lists
            # skill05.dds while only SKILL01-03 exist.  Record the gap and carry
            # on — crashing here loses the 200+ icons that ARE available, and a
            # blank icon is recoverable where a wrong one is not.
            if not os.path.exists(path):
                sheets[key] = None
                missing_sheets.add(sheet)
            else:
                im = Image.open(path)
                im.load()
                sheets[key] = im.convert("RGBA")
        im = sheets[key]
        if im is None:
            bad += 1
            continue
        crop = im.crop((x0, y0, min(x1, im.width), min(y1, im.height)))
        crop.save(os.path.join(OUT_DIR, f"skillicon_{idx:05d}.png"))
        made.add(idx)
        done += 1
    print(f"[skillicons] sliced {done} -> {OUT_DIR}  ({bad} unavailable, left blank)")
    if missing_sheets:
        print(f"[skillicons] sheets the TSI names but the client does not ship: "
              f"{sorted(missing_sheets)}")

    # Keep ONLY what this run produced — see the same block in
    # build_item_icons.py for why leaving slices behind silently serves the
    # previous era's art.
    #
    # Keyed on `made`, not `wanted`: an index the tables reference but this
    # sheet cannot supply (out-of-range above) would otherwise keep its stale
    # slice and count as legitimate.  A blank icon is recoverable; the wrong
    # item's picture looks correct and hides the gap.
    keep = {f"skillicon_{i:05d}.png" for i in made}
    stale = [f for f in os.listdir(OUT_DIR)
             if f.lower().endswith(".png") and f not in keep]
    for f in stale:
        os.remove(os.path.join(OUT_DIR, f))
    print(f"[skillicons] pruned {len(stale)} slice(s) no table references")


if __name__ == "__main__":
    main()
