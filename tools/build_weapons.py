#!/usr/bin/env python3
"""
build_weapons.py — Build GLBs for the FULL player-weapon catalog.

Every non-empty LIST_WEAPON.ZSC object whose STB `Type` (col 4) is a real player
weapon class (211..271) becomes `weapon_<id>.glb` under
SourceAssets/GLTF/AVATAR/MODULAR/<gender>/.  This includes:
  * single right-hand weapons (1H/2H sword, staff, bow, cannon)   -> dummy 0
  * single left-hand weapons (guns)                               -> dummy 1
  * dual-wield (katar / dual sword / dual gun)  -> 2 parts, dummy 0 AND dummy 1
The resolver/converter place each ZSC part at its own hand dummy with the part's
grip rotation, so dual-wield meshes land one-per-hand automatically.

Type 0 = monster natural weapons (mobwpn/*, attach to back/mouse dummies) — NOT
player-equippable; excluded by default (pass --include-mob to build them too).

Usage:
    py -3.9 build_weapons.py --gender F
    py -3.9 build_weapons.py --gender F --ids 400,401,402   # subset
"""
import argparse
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from rose_avatar import resolve_item
from rose_combine_anims import convert
from rose_parser.formats.stb import parse as parse_stb
from rose_parser.formats.zsc import parse as parse_zsc

# Asset source.  Arua is THE source (CLAUDE.md "Asset source: Arua, always").
# This USED to read SourceAssets/3DDATA (the deprecated classic-era tree), whose
# LIST_WEAPON.ZSC has only 671 non-empty objects vs Arua's 2,794 — so 787 of the
# 1,344 weapons in weapons.csv silently built as "empty" (e.g. id 28 Requiem of
# Souls Sword), and the ones that DID build came from a tree whose ZSC object ids
# do not line up with the Arua-era DataTables = wrong mesh per row id.
# Same env var + default as build_modular.py; points AT the 3DDATA directory.
ROSE_3DDATA = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")

# Real player-weapon Type values (STB col 4); everything else (notably 0 = mob) skipped.
PLAYER_TYPES = {211, 212, 221, 222, 223, 231, 232, 233, 241, 242, 251, 252, 253, 271}
STB_TYPE_COL = 4


def weapon_ids(include_mob=False):
    stb = parse_stb(os.path.join(ROSE_3DDATA, "STB", "LIST_WEAPON.STB"))
    zsc = parse_zsc(os.path.join(ROSE_3DDATA, "WEAPON", "LIST_WEAPON.ZSC"))
    ids = []
    for i in range(len(zsc.models)):
        if not zsc.models[i].parts:
            continue
        if not include_mob and stb.get_int(i, STB_TYPE_COL) not in PLAYER_TYPES:
            continue
        ids.append(i)
    return ids


def run(gender, ids=None, include_mob=False):
    project_root = os.path.normpath(os.path.join(_TOOLS, ".."))
    # Read assets from Arua; write GLBs into the UE SourceAssets tree.  These are
    # two different roots — do NOT collapse them back into one `src`.
    src = os.path.dirname(ROSE_3DDATA)
    A = os.path.join(ROSE_3DDATA, "AVATAR")
    zmd = os.path.join(A, "FEMALE.ZMD" if gender == "F" else "MALE.ZMD")
    ue_assets = os.path.join(project_root, "unreal-engine rose", "RoseUE", "SourceAssets")
    out = os.path.join(ue_assets, "GLTF", "AVATAR", "MODULAR",
                       "FEMALE" if gender == "F" else "MALE")
    os.makedirs(out, exist_ok=True)

    if ids is None:
        ids = weapon_ids(include_mob)
    print(f"[weapons] building {len(ids)} weapon GLBs ({'F' if gender=='F' else 'M'}) -> {out}")

    built = empty = 0
    for n, wid in enumerate(ids, 1):
        parts = resolve_item(A, src, "WEAPON", wid, gender)
        if not parts:
            empty += 1
            continue
        convert(zmd, parts, [], os.path.join(out, f"weapon_{wid}.glb"))
        built += 1
        if n % 50 == 0:
            print(f"[weapons] {n}/{len(ids)} ... last weapon_{wid} ({len(parts)} parts)")
    print(f"[weapons] done: {built} built, {empty} empty/skipped")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gender", choices=["F", "M"], default="F")
    ap.add_argument("--ids", default="", help="comma id list (default: whole catalog)")
    ap.add_argument("--include-mob", action="store_true", help="also build Type-0 mob weapons")
    a = ap.parse_args()
    ids = [int(x) for x in a.ids.split(",") if x.strip()] or None
    run(a.gender, ids, a.include_mob)


if __name__ == "__main__":
    main()
