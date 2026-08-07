#!/usr/bin/env python3
"""
gen_zone_bgm.py — zone -> background music, from LIST_ZONE.STB.

Writes RoseUE/Content/Sounds/zone_bgm.json:
    { "JPT01": { "day": "/Game/Sounds/bgm/town02_junon",
                 "night": "/Game/Sounds/bgm/town02_junon" }, ... }

SOUND IS NOT IN THE ASSET CLIENT.  QQ-iROSE ships only 3DDATA and CAMERAS — no
audio at all — so the OGG/WAV files come from the classic client's `client/sound`
tree, the same way the glass UI comes from its own root.  Sound is era-
independent: a BGM track is not indexed by any STB row the way a mesh is, so
mixing it across clients cannot silently resolve to the wrong asset.

Only the TABLE is read from the current asset root; the files are matched by
basename against what the sound tree actually has.

Usage:  py -3.9 tools/gen_zone_bgm.py
"""
import json
import os
import sys

_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.stb import parse as parse_stb

SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
SOUND_ROOT = r"C:\QQ-iROSE Online\Sound"
OUT = (r"C:\rose-next-classic\unreal-engine rose\RoseUE\Content\Sounds"
       r"\zone_bgm.json")
GAME_BGM = "/Game/Sounds/bgm"


def col_by_name(stb, *names):
    """Bind by HEADER NAME — indices drift between clients (RoseStbSchema)."""
    wanted = {n.lower().replace(" ", "") for n in names}
    for c in range(stb.num_cols()):
        if (stb.col_name(c) or "").lower().replace(" ", "") in wanted:
            return c
    return -1


def main():
    stb = parse_stb(os.path.join(SRC, "STB", "LIST_ZONE.STB"))
    c_zon = col_by_name(stb, "ZON", "Zone Path", ".zon Path")
    c_day = col_by_name(stb, "BGM Day")
    c_night = col_by_name(stb, "BGM Night")
    if min(c_zon, c_day) < 0:
        print(f"[bgm] cannot bind columns (zon={c_zon} day={c_day}) — aborting")
        return

    have = {}
    # Folder case differs between clients (QQ-iROSE "BGM", classic "bgm") and
    # this runs on a case-sensitive-ish path join, so resolve it rather than
    # assuming either spelling.
    bgm_dir = os.path.join(SOUND_ROOT, "BGM")
    if not os.path.isdir(bgm_dir):
        for d in os.listdir(SOUND_ROOT) if os.path.isdir(SOUND_ROOT) else []:
            if d.lower() == "bgm":
                bgm_dir = os.path.join(SOUND_ROOT, d)
                break
    if os.path.isdir(bgm_dir):
        for f in os.listdir(bgm_dir):
            stem, ext = os.path.splitext(f)
            if ext.lower() in (".ogg", ".wav"):
                have[stem.lower()] = stem
    print(f"[bgm] {len(have)} track(s) on disk in {bgm_dir}")

    out, missing = {}, set()
    for r in range(1, stb.num_rows()):
        zon = (stb.get(r, c_zon) or "").strip()
        if not zon:
            continue
        zone = os.path.splitext(os.path.basename(zon.replace("\\", "/")))[0].upper()

        def resolve(col):
            if col < 0:
                return ""
            raw = (stb.get(r, col) or "").strip()
            if not raw:
                return ""
            stem = os.path.splitext(os.path.basename(raw.replace("\\", "/")))[0]
            real = have.get(stem.lower())
            if not real:
                missing.add(stem)
                return ""
            return f"{GAME_BGM}/{real}"

        day = resolve(c_day)
        night = resolve(c_night) or day
        if day or night:
            out[zone] = {"day": day, "night": night}

    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=1, sort_keys=True)
    print(f"[bgm] {len(out)} zone(s) -> {OUT}")
    if missing:
        print(f"[bgm] {len(missing)} track(s) named by the table but absent from "
              f"the sound tree: {sorted(missing)[:8]}")


if __name__ == "__main__":
    main()
