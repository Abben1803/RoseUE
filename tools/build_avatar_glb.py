#!/usr/bin/env python3
"""
build_avatar_glb.py — Build a custom ROSE avatar GLB from item IDs.

Pick any body/face/hair/cap/arms/foot/back by STB ID and gender; this resolves
each via the STB→ZSC binding, bakes the parts + textures into one skeletal mesh,
and includes the gender's animations (same as the default AnimRig).

Usage:
    python build_avatar_glb.py --gender F --body 1 --hair 110 --face 1 \
        --arms 1 --foot 1 --cap 0 --name cheria_test

Outputs SourceAssets/GLTF/AVATAR/CUSTOM/<name>.glb
"""
import argparse
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from rose_avatar import resolve_avatar
from rose_combine_anims import convert
from build_combined_anims import _max_bone, AVATAR_BONE_COUNT


def _gather_anims(motion_dir, gender):
    """Female/Male gendered anims + bone-compatible neutral anims (skills/emotes)."""
    suffix = "_F1" if gender == "F" else "_M1"
    gendered, neutral = [], []
    for zmo in sorted(os.listdir(motion_dir)):
        if not zmo.upper().endswith(".ZMO"):
            continue
        path = os.path.join(motion_dir, zmo)
        if _max_bone(path) >= AVATAR_BONE_COUNT:
            continue
        stem = os.path.splitext(zmo)[0]
        up = stem.upper()
        if up.endswith(suffix):
            gendered.append((stem, path))
        elif not (up.endswith("_F1") or up.endswith("_M1")):
            neutral.append((stem, path))
    return gendered + neutral


def main():
    ap = argparse.ArgumentParser(description="Build a custom ROSE avatar GLB")
    ap.add_argument("--gender", choices=["F", "M"], default="F")
    ap.add_argument("--body", type=int, default=1)
    ap.add_argument("--face", type=int, default=1)
    ap.add_argument("--hair", type=int, default=120)
    ap.add_argument("--arms", type=int, default=1)
    ap.add_argument("--foot", type=int, default=1)
    ap.add_argument("--cap",  type=int, default=None, help="optional headgear id")
    ap.add_argument("--back", type=int, default=None, help="optional back/cape id")
    ap.add_argument("--name", default="custom_avatar")
    ap.add_argument("--no-anims", action="store_true", help="skeleton+mesh only, no animations")
    args = ap.parse_args()

    project_root = os.path.normpath(os.path.join(_TOOLS, ".."))
    ue_project   = os.path.join(project_root, "unreal-engine rose", "RoseUE")
    src_assets   = os.path.join(ue_project, "SourceAssets")
    # Asset SOURCE is Arua (CLAUDE.md); only the GLB OUTPUT stays in SourceAssets.
    # src_assets used to serve as both, pinning reads to the deprecated classic
    # tree whose ZSC object ids do not match the Arua-era DataTables.
    rose_3ddata  = os.environ.get("ROSE_ASSET_ROOT",
                                  r"C:\QQ-iROSE Online\extracted\3DDATA")
    # ZSC part paths embed a leading '3DDATA\', so the resolve base is its parent.
    asset_base   = os.path.dirname(rose_3ddata)
    avatar_dir   = os.path.join(rose_3ddata, "AVATAR")
    motion_dir   = os.path.join(rose_3ddata, "MOTION", "AVATAR")
    out_dir      = os.path.join(src_assets, "GLTF", "AVATAR", "CUSTOM")
    os.makedirs(out_dir, exist_ok=True)

    zmd = os.path.join(avatar_dir, "FEMALE.ZMD" if args.gender == "F" else "MALE.ZMD")

    config = {"BODY": args.body, "ARMS": args.arms, "FOOT": args.foot,
              "FACE": args.face, "HAIR": args.hair, "CAP": args.cap, "BACK": args.back}
    parts = resolve_avatar(avatar_dir, asset_base, config, args.gender)
    if not parts:
        print("ERROR: no parts resolved")
        sys.exit(1)

    anims = [] if args.no_anims else _gather_anims(motion_dir, args.gender)
    out = os.path.join(out_dir, f"{args.name}.glb")

    print(f"[{args.name}] gender={args.gender} parts={len(parts)} anims={len(anims)}")
    for p in parts:
        print(f"    {os.path.basename(p['path']):28s} pin={p['pin']} "
              f"tex={os.path.basename(p['texture']) if p['texture'] else None}")
    convert(zmd, parts, anims, out)


if __name__ == "__main__":
    main()
