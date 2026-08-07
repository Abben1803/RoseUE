#!/usr/bin/env python3
"""
build_minimaps.py — Convert ROSE per-zone MINIMAP.DDS + the shared arrow cursor
into UE-importable PNGs, and emit the runtime manifest that the in-game minimap
window (Source/RoseUE/RoseUIMinimap.cpp) uses to place the player marker.

Run with **py -3.9** (VS Python 3.9 has Pillow; bare `py` = 3.14 without it).
This script writes PNGs + JSON only — it never touches UE, so it is safe to run.

  Inputs
    client/3ddata/MAPS/<PLANET>/<ZONE>/MINIMAP.DDS   (58 on disk; DXT-compressed)
    client/3ddata/control/RES/MINIMAP_ARROW.TGA      (the player heading cursor)
    <SRC>/STB/LIST_ZONE.STB                           (start-block columns 8/9/10)
  Outputs
    unreal-engine rose/RoseUE/SourceAssets/UI/MINIMAPS/<ZONE>.png
    unreal-engine rose/RoseUE/SourceAssets/UI/MINIMAPS/MINIMAP_ARROW.png
    unreal-engine rose/RoseUE/Content/UI/Minimaps/minimaps.json

  minimaps.json schema (one entry per zone that has a PNG):
    "<ZONE>": {
      "texture":  "/Game/UI/Minimaps/<ZONE>",   # UE asset path (import target)
      "start_x":  <int block>,                   # LIST_ZONE.STB col 9  (Start X)
      "start_y":  <int block>,                   # LIST_ZONE.STB col 10 (Start Y)
      "px_w":     <int>,                         # PNG width  in pixels
      "px_h":     <int>,                         # PNG height in pixels
      "zone_name":"<designation>"                # LIST_ZONE.STB col 1, if known
    }

────────────────────────────────────────────────────────────────────────────────
  WORLD → MINIMAP-PIXEL FORMULA  (derived from
  src/client/interface/dlgs/cminimapdlg.cpp — cited by line)
────────────────────────────────────────────────────────────────────────────────
  Constants (all confirmed in local source):
    MINIMAP_RESOLUTION_PER_MAP = 64   px per zone block   (cminimapdlg.cpp:45)
    PATCH_COUNT_PER_MAP_AXIS   = 16                       (terraindef.h:16)
    GRID_COUNT_PER_PATCH_AXIS  = 4                        (terrain grid, runtime)
    nGRID_SIZE                 = 250  cm                  (terrain grid, runtime)
    MAP_COUNT_PER_ZONE_AXIS    = 64                       (terraindef.h:8)

  Block world size = 16 * 4 * 250 = 16000 cm = 160 m  (one MINIMAP.DDS 64px tile).
  fGetWorldDistancePerPixel() = 16*4*250 / 64 = 250 cm/px   (cminimapdlg.cpp:690-693)

  CalculateDisplayPos() sets the world coord of the texture's top-left region:
    m_fMinMinimapWorldPosX = 16000 * start_x                 (cminimapdlg.cpp:158-159)
    m_fMaxMinimapWorldPosY = 16000 * (64 - start_y + 1)      (cminimapdlg.cpp:160-162)

  Draw() sprite-center math maps a ROSE world point (wx, wy) [cm] to a texture
  pixel (u, v), where the "+ MINIMAP_RESOLUTION_PER_MAP" (=64) is the 1-block
  border that MINIMAP.DDS carries on every side (cminimapdlg.cpp:210-215, and the
  matching "-2" texture-block trim at cminimapdlg.cpp:171-172):
    u = (wx - m_fMinMinimapWorldPosX) / 250 + 64
    v = (m_fMaxMinimapWorldPosY - wy) / 250 + 64

  ── This port's ROSE→UE transform (tools/export_mob_spawns.py:8) ──
    UE_X =  rose_x_cm + 520000        rose_x_cm = UE_X - 520000
    UE_Y = -(rose_y_cm + 520000)      rose_y_cm = -UE_Y - 520000
  So, straight from the pawn's UE (X, Y):
    u = ((UE_X - 520000) - 16000*start_x) / 250 + 64
    v = (16000*(64 - start_y + 1) - (-UE_Y - 520000)) / 250 + 64
      = (16000*(65 - start_y) + UE_Y + 520000) / 250 + 64

  The runtime (RoseUIMinimap.cpp) uses exactly this — see the same derivation
  transcribed there.  The pawn heading (arrow rotation) is -yaw about the pixel;
  ROSE rotates the cursor by -D3DXToRadian(direction) (cminimapdlg.cpp:250,271).
"""
import json
import os
import sys

try:
    from PIL import Image
except ImportError:
    sys.exit("[minimaps] Pillow not found — run with  py -3.9  (VS Python 3.9)")

_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.stb import parse as parse_stb

REPO = os.path.dirname(_T)
# Asset SOURCE roots all move to Arua (CLAUDE.md); OUT_* below stay put.
# Zone ids do not line up across eras — LIST_ZONE.STB is 235 rows in Arua vs 66
# in classic (row 10 = 'Adventurers Plains (EVO)' vs 'Character Select') — and
# the maps themselves were reimported from Arua, so a classic-sourced minimap
# was being keyed to an Arua zone.  Arua also ships 115 MINIMAP.DDS vs 38.
ROSE_3DDATA = os.environ.get("ROSE_ASSET_ROOT",
                             r"C:\QQ-iROSE Online\extracted\3DDATA")
SRC_3DDATA = ROSE_3DDATA
CLIENT_MAPS = os.path.join(ROSE_3DDATA, "MAPS")
ARROW_TGA = os.path.join(ROSE_3DDATA, "CONTROL", "RES", "MINIMAP_ARROW.TGA")

OUT_PNG_DIR = os.path.join(REPO, "unreal-engine rose", "RoseUE", "SourceAssets", "UI", "MINIMAPS")
OUT_JSON = os.path.join(REPO, "unreal-engine rose", "RoseUE", "Content", "UI", "Minimaps", "minimaps.json")

# LIST_ZONE.STB columns (src/common/include/rose/io/stb.h:487-489).
ZONE_MINIMAP_NAME = 8   # e.g. "3Ddata\Maps\Junon\JPT01\minimap.DDS"
ZONE_MINIMAP_STARTX = 9
ZONE_MINIMAP_STARTY = 10
# Data col 0 = "Name" (the human zone name, e.g. "City of Junon Polis").
# (Headers are ['', 'Name', 'Designation', ...]; get(row, 0) is the first DATA
# column, which the header labels "Name".)
ZONE_NAME_COL = 0


def find_minimap_dds():
    """Return {ZONE_UPPER: abs_path_to_MINIMAP.DDS} by walking client MAPS."""
    out = {}
    if not os.path.isdir(CLIENT_MAPS):
        sys.exit(f"[minimaps] client maps dir missing: {CLIENT_MAPS}")
    for planet in sorted(os.listdir(CLIENT_MAPS)):
        pdir = os.path.join(CLIENT_MAPS, planet)
        if not os.path.isdir(pdir):
            continue
        for zone in sorted(os.listdir(pdir)):
            zdir = os.path.join(pdir, zone)
            if not os.path.isdir(zdir):
                continue
            for f in os.listdir(zdir):
                if f.lower() == "minimap.dds":
                    out[zone.upper()] = os.path.join(zdir, f)
                    break
    return out


def zone_starts_from_stb():
    """Return {ZONE_UPPER: (start_x, start_y, designation)} keyed by the ZONE dir
    parsed out of LIST_ZONE.STB's minimap-path column (col 8)."""
    stb_path = os.path.join(SRC_3DDATA, "STB", "LIST_ZONE.STB")
    if not os.path.isfile(stb_path):
        print(f"[minimaps] WARNING LIST_ZONE.STB missing ({stb_path}); starts default to 31/31")
        return {}
    stb = parse_stb(stb_path)
    out = {}
    for i in range(stb.num_rows()):
        mm = stb.get(i, ZONE_MINIMAP_NAME).strip()
        if not mm:
            continue
        # ".../<ZONE>/minimap.DDS" → <ZONE>
        norm = mm.replace("\\", "/").rstrip("/")
        parts = [p for p in norm.split("/") if p]
        if len(parts) < 2:
            continue
        zone = parts[-2].upper()   # dir containing minimap.DDS
        sx = stb.get_int(i, ZONE_MINIMAP_STARTX, 31)
        sy = stb.get_int(i, ZONE_MINIMAP_STARTY, 31)
        name = stb.get(i, ZONE_NAME_COL).strip()
        # First row wins (duplicate zone dirs across rows share one texture).
        out.setdefault(zone, (sx, sy, name))
    return out


def dds_to_png(dds_path, png_path):
    """Pillow reads DXT DDS natively; write straight-alpha RGBA PNG."""
    img = Image.open(dds_path)
    img = img.convert("RGBA")
    img.save(png_path, "PNG")
    return img.size  # (w, h)


def main():
    os.makedirs(OUT_PNG_DIR, exist_ok=True)
    os.makedirs(os.path.dirname(OUT_JSON), exist_ok=True)

    dds = find_minimap_dds()
    starts = zone_starts_from_stb()
    print(f"[minimaps] {len(dds)} MINIMAP.DDS on disk; {len(starts)} zones in LIST_ZONE.STB")

    manifest = {}
    converted, no_start = 0, []
    for zone in sorted(dds):
        png_path = os.path.join(OUT_PNG_DIR, f"{zone}.png")
        try:
            w, h = dds_to_png(dds[zone], png_path)
        except Exception as e:  # noqa: BLE001 — report + continue, don't abort the batch
            print(f"[minimaps] FAILED {zone}: {e}")
            continue
        sx, sy, name = starts.get(zone, (31, 31, ""))
        if zone not in starts:
            no_start.append(zone)
        manifest[zone] = {
            "texture": f"/Game/UI/Minimaps/{zone}",
            "start_x": sx,
            "start_y": sy,
            "px_w": w,
            "px_h": h,
            "zone_name": name,
        }
        converted += 1

    # Shared player-heading cursor.
    arrow_ok = False
    if os.path.isfile(ARROW_TGA):
        try:
            aw, ah = dds_to_png(ARROW_TGA, os.path.join(OUT_PNG_DIR, "MINIMAP_ARROW.png"))
            arrow_ok = True
            print(f"[minimaps] arrow → MINIMAP_ARROW.png ({aw}x{ah})")
        except Exception as e:  # noqa: BLE001
            print(f"[minimaps] arrow FAILED: {e}")
    else:
        print(f"[minimaps] WARNING arrow TGA missing: {ARROW_TGA}")

    with open(OUT_JSON, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=1, sort_keys=True)

    # ── PRUNE zones this client does not have ───────────────────────────────
    #
    # Converting only WRITES, so a previous client's minimaps survive here and
    # get imported alongside the current ones — the switch to QQ-iROSE left 78
    # stale PNGs behind and the importer happily took 143 files for a 65-entry
    # manifest.  They are unreachable while no manifest key matches them, which
    # is exactly how the icon era-mixing stayed hidden for a whole session: an
    # asset that is wrong but unreferenced today is wrong and REFERENCED the
    # moment a zone name reappears.  See build_item_icons.py for the same block.
    keep = {f"{z}.png" for z in manifest}
    keep.add("MINIMAP_ARROW.png")
    stale = [f for f in os.listdir(OUT_PNG_DIR)
             if f.lower().endswith(".png") and f not in keep]
    for f in stale:
        os.remove(os.path.join(OUT_PNG_DIR, f))
    print(f"[minimaps] pruned {len(stale)} minimap(s) this client does not ship")

    print(f"[minimaps] converted {converted}/{len(dds)} zones → {OUT_PNG_DIR}")
    if no_start:
        print(f"[minimaps] {len(no_start)} zones had no LIST_ZONE.STB row "
              f"(start defaulted 31/31): {', '.join(no_start)}")
    print(f"[minimaps] manifest → {OUT_JSON}  ({len(manifest)} entries)")
    if not arrow_ok:
        print("[minimaps] NOTE arrow PNG not produced — runtime falls back to no cursor")


if __name__ == "__main__":
    main()
