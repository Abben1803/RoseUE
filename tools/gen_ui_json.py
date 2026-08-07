#!/usr/bin/env python3
"""
gen_ui_json.py — Convert the classic-client ROSE UI to modern JSON (no XML at
runtime).  Source of truth: the rose-next-classic client (src/client/interface/
io_imageres.cpp — TSIs per module id, GID strings resolved by name).

  client/3ddata/control/xml/*.xml          ->  RoseUE/Content/UI/Layouts/<dlg>.json
  client/out/3DDATA/CONTROL/RES/*.TSI      ->  RoseUE/Content/UI/atlas.json

The dialog JSON is the XML tree, normalized: tag -> "type", attributes lowercased
with ints/floats coerced, children nested.  atlas.json maps sprite name ->
{sheet, rect:[x0,y0,x1,y1]} (TSI layout per rose-lib tsi.rs).  Also reports every
GID the dialogs reference that no TSI defines.

Usage:  py tools/gen_ui_json.py
"""
import json
import os
import re
import struct
import sys
import xml.etree.ElementTree as ET

# Source UI SKIN, overridable so more than one can be built.
#
#   default        the classic client UI
#   ROSE_UI_ROOT   a CONTROL folder holding XML/ and RES/ (e.g. the GLASS
#                  UI at C:\3DDATA\CONTROL) - same XML names and the same
#                  TSI format, so it is a drop-in source for this tool.
#
# Layout names match 1:1 between skins (dlgavata, dlgbank, dlgchat, ...),
# which is what makes swapping skins a RE-RUN rather than a port.
_UI_ROOT = os.environ.get("ROSE_UI_ROOT", "")
if _UI_ROOT:
    XML_DIR = os.path.join(_UI_ROOT, "XML")
    RES_DIR = os.path.join(_UI_ROOT, "RES")
else:
    XML_DIR = r"C:\rose-next-classic\client\3ddata\control\xml"
    RES_DIR = r"C:\rose-next-classic\client\out\3DDATA\CONTROL\RES"
OUT_DIR = r"C:\rose-next-classic\unreal-engine rose\RoseUE\Content\UI"
LAYOUT_DIR = os.path.join(OUT_DIR, "Layouts")

# Every attribute that names a sprite (classic buttons use NORMALGID/OVERGID/
# DOWNGID; plain images use GID; Arua-era files used GID_UP/...).
GID_KEYS = ("GID", "NORMALGID", "OVERGID", "DOWNGID", "DISABLEGID", "BLINKGID",
            "GID_UP", "GID_DOWN", "GID_OVER", "GID_DISABLE")


def read_tsi(path):
    """TSI per rose-tools/rose-lib/src/files/tsi.rs: sheets, then per-sheet
    sprites of (sheet_id u16, start u32x2, end u32x2, color u32, name char[32])."""
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
    sheets = []
    for _ in range(ns):
        ln = u16()
        name = d[o:o + ln].decode("latin-1")
        o += ln
        u32()  # color key
        sheets.append((name, []))
    u16()  # total sprite count
    for i in range(ns):
        for _ in range(u16()):
            u16()  # sheet id (== i)
            x0, y0, x1, y1 = u32(), u32(), u32(), u32()
            u32()  # color
            nm = d[o:o + 32].split(b"\x00")[0].decode("latin-1")
            o += 32
            sheets[i][1].append((nm, x0, y0, x1, y1))
    return sheets


def coerce(v):
    try:
        return int(v)
    except ValueError:
        try:
            return float(v)
        except ValueError:
            return v


def node_json(el):
    out = {"type": el.tag.upper()}
    for k, v in el.attrib.items():
        key = k.lower()
        # The TAG is the control type.  A few controls ALSO carry their own
        # TYPE= attribute (SCROLLBAR TYPE="0", dlgoption's sliders TYPE="1"),
        # which used to overwrite the tag name and leave the node untyped —
        # 28 controls across 11 dialogs silently vanished at render time.
        if key == "type":
            key = "subtype"
        out[key] = coerce(v)
    # element text = the control's meaning (e.g. <BUTTON ...>STR</BUTTON>)
    label = (el.text or "").strip()
    if label:
        out["label"] = label
    kids = [node_json(c) for c in el]
    if kids:
        out["children"] = kids
    return out


def parse_dialog(path):
    try:
        return ET.parse(path).getroot()
    except ET.ParseError:
        # A few files have stray '&' / unescaped chars — repair and retry.
        txt = open(path, encoding="utf-8", errors="replace").read()
        txt = re.sub(r"&(?!(amp|lt|gt|quot|apos);)", "&amp;", txt)
        return ET.fromstring(txt)


def main():
    os.makedirs(LAYOUT_DIR, exist_ok=True)

    # ---- atlas from every TSI present ----
    atlas, sheets_meta = {}, []
    for f in sorted(os.listdir(RES_DIR)):
        if not f.lower().endswith(".tsi"):
            continue
        for sheet, sprites in read_tsi(os.path.join(RES_DIR, f)):
            sheets_meta.append({"tsi": f, "sheet": sheet, "sprites": len(sprites),
                                "present": os.path.exists(os.path.join(RES_DIR, sheet))})
            for nm, x0, y0, x1, y1 in sprites:
                atlas[nm] = {"sheet": sheet, "rect": [x0, y0, x1, y1]}

    # ---- dialogs ----
    used_gids, ok, failed = set(), 0, []
    for f in sorted(os.listdir(XML_DIR)):
        if not f.lower().endswith(".xml"):
            continue
        try:
            root = parse_dialog(os.path.join(XML_DIR, f))
        except Exception as e:
            failed.append((f, str(e)))
            continue
        doc = node_json(root)
        name = os.path.splitext(f)[0]
        doc["name"] = name
        for el in root.iter():
            for key in GID_KEYS:
                g = el.attrib.get(key)
                if g:
                    used_gids.add(g)
        with open(os.path.join(LAYOUT_DIR, name + ".json"), "w", encoding="utf-8") as fh:
            json.dump(doc, fh, indent=1)
        ok += 1

    missing = sorted(g for g in used_gids if g not in atlas)
    with open(os.path.join(OUT_DIR, "atlas.json"), "w", encoding="utf-8") as fh:
        json.dump({"sprites": atlas, "sheets": sheets_meta,
                   "unresolved_gids": missing}, fh, indent=1)

    print(f"[ui] dialogs: {ok} converted, {len(failed)} failed")
    for f, e in failed:
        print(f"  FAIL {f}: {e[:100]}")
    print(f"[ui] atlas: {len(atlas)} sprites from TSIs; "
          f"{sum(1 for s in sheets_meta if not s['present'])}/{len(sheets_meta)} sheets MISSING on disk")
    print(f"[ui] GIDs used by dialogs: {len(used_gids)}; unresolved (no TSI defines them): {len(missing)}")
    pref = {}
    for g in missing:
        pref[g.split("_")[0]] = pref.get(g.split("_")[0], 0) + 1
    print("[ui] unresolved by prefix:", dict(sorted(pref.items(), key=lambda kv: -kv[1])[:15]))


if __name__ == "__main__":
    main()
