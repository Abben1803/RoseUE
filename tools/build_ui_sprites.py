#!/usr/bin/env python3
"""
build_ui_sprites.py — Slice the named classic-client UI sprites out of their
DDS/TGA sheets into per-sprite PNGs for UE import.

Reads Content/UI/atlas.json (from gen_ui_json.py) + the sheets in
client/out/3DDATA/CONTROL/RES, writes:
  SourceAssets/UI/Sprites/<SPRITE_NAME>.png
  Content/UI/sprites.json   (name -> size, for the C++ renderer)

TSI rects: end point is EXCLUSIVE — verified against the dialog layout sizes.

Usage:  py tools/build_ui_sprites.py
"""
import json
import os
import re

from PIL import Image

# ROSE_UI_ROOT picks the skin - see gen_ui_json.py.  Both tools MUST be run
# with the SAME root, or the sprites and the atlas describe different art.
_UI_ROOT = os.environ.get("ROSE_UI_ROOT", "")
_DEFAULT_RES = r"C:\rose-next-classic\client\out\3DDATA\CONTROL\RES"
RES_DIR = os.path.join(_UI_ROOT, "RES") if _UI_ROOT else _DEFAULT_RES
UI_DIR = r"C:\rose-next-classic\unreal-engine rose\RoseUE\Content\UI"
OUT_DIR = r"C:\rose-next-classic\unreal-engine rose\RoseUE\SourceAssets\UI\Sprites"


def safe(name):
    return re.sub(r"[^A-Za-z0-9_]", "_", name)


def main():
    atlas = json.load(open(os.path.join(UI_DIR, "atlas.json")))["sprites"]
    os.makedirs(OUT_DIR, exist_ok=True)

    sheets = {}   # lowercase sheet name -> PIL image
    def sheet(name):
        key = name.lower()
        if key not in sheets:
            path = os.path.join(RES_DIR, name)
            if not os.path.exists(path):
                # sheet names are case-insensitive on the VFS
                for f in os.listdir(RES_DIR):
                    if f.lower() == key:
                        path = os.path.join(RES_DIR, f)
                        break
            im = Image.open(path)
            im.load()
            sheets[key] = im.convert("RGBA")
        return sheets[key]

    manifest, bad = {}, []
    for name, rec in atlas.items():
        x0, y0, x1, y1 = rec["rect"]
        if x1 <= x0 or y1 <= y0:
            bad.append((name, "empty rect"))
            continue
        try:
            im = sheet(rec["sheet"])
        except Exception as e:
            bad.append((name, f"sheet {rec['sheet']}: {e}"))
            continue
        # clamp: a few sprites overhang their sheet by a pixel
        cx1, cy1 = min(x1, im.width), min(y1, im.height)
        crop = im.crop((x0, y0, cx1, cy1))
        fn = safe(name)
        crop.save(os.path.join(OUT_DIR, fn + ".png"))
        manifest[name] = {"file": fn, "size": [crop.width, crop.height]}

    # ── 9-slice window frames ───────────────────────────────────────────────
    #
    # The glass UI draws every window as a NINE-SLICE: GEN_WND01_TL/TC/TR,
    # _CL/_CC/_CR, _BL/_BC/_BR — 23px corners with 1px stretchable edges and
    # centre.  Those nine rects are CONTIGUOUS in the sheet, so instead of
    # compositing nine images per window we emit ONE image covering the union
    # and record the corner size.  Slate then 9-slices it natively with a Box
    # brush + margin, which is both cheaper and pixel-exact.
    #
    # Emitted as <PREFIX>_BOX, with the margin in sprites.json so the C++ side
    # does not have to hardcode 23.
    CORNERS = ("TL", "TC", "TR", "CL", "CC", "CR", "BL", "BC", "BR")
    fams = {}
    for name in atlas:
        for suf in CORNERS:
            if name.endswith("_" + suf):
                fams.setdefault(name[: -(len(suf) + 1)], set()).add(suf)

    boxes = 0
    for prefix, have in sorted(fams.items()):
        if not set(CORNERS).issubset(have):
            continue   # not a full nine-slice family
        recs = [atlas[f"{prefix}_{s}"] for s in CORNERS]
        sheets_used = {r["sheet"].lower() for r in recs}
        if len(sheets_used) != 1:
            continue   # split across sheets - cannot be one image

        xs = [r["rect"][0] for r in recs] + [r["rect"][2] for r in recs]
        ys = [r["rect"][1] for r in recs] + [r["rect"][3] for r in recs]
        x0, y0, x1, y1 = min(xs), min(ys), max(xs), max(ys)

        tl = atlas[prefix + "_TL"]["rect"]
        mx = tl[2] - tl[0]          # corner width  (23)
        my = tl[3] - tl[1]          # corner height (23)

        try:
            im = sheet(recs[0]["sheet"])
        except Exception as e:
            bad.append((prefix + "_BOX", f"sheet: {e}"))
            continue

        crop = im.crop((x0, y0, min(x1, im.width), min(y1, im.height)))
        fn = safe(prefix + "_BOX")
        crop.save(os.path.join(OUT_DIR, fn + ".png"))
        manifest[prefix + "_BOX"] = {
            "file": fn,
            "size": [crop.width, crop.height],
            "margin": [mx, my],      # corner size, for a Slate Box brush
        }
        boxes += 1

    with open(os.path.join(UI_DIR, "sprites.json"), "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1)
    print(f"[ui] {boxes} nine-slice window frames emitted as <PREFIX>_BOX")
    print(f"[ui] sliced {len(manifest)} sprites -> {OUT_DIR}  ({len(bad)} skipped)")
    for n, e in bad[:10]:
        print(f"  skip {n}: {e}")


if __name__ == "__main__":
    main()
