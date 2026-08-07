#!/usr/bin/env py -3.9
"""
build_subwpn_static.py — Build SUB-WEAPONS (shields/off-hands) as RAW
STATIC-mesh GLBs for socket attachment to the character's left hand, exactly
like build_weapons_static.py builds held weapons.

LIST_SUBWPN parts attach to a hand dummy named by the ZSC part itself
(dummy 2 = L_SHIELD) with a real grip rotation — same family as weapons, so the
same static pipeline applies: raw mesh + own material, orientation is a
live-tuned relative transform in C++ (ARoseCharacter::SubGrip*).

Cutout shields use the TRUE mask signal (is_alpha && alpha_test) like backs.

Output: SourceAssets/GLTF/AVATAR/MODULAR/<gender>/SUBWPNSTATIC/subwpn_<id>.glb
Usage:  py -3.9 build_subwpn_static.py --gender F [--ids 1,2,3]
"""
import argparse, os, sys
_T = os.path.dirname(os.path.abspath(__file__))
if _T not in sys.path:
    sys.path.insert(0, _T)

from rose_avatar import resolve_item
from rose_parser.formats.zsc import parse as parse_zsc
from build_weapons import ROSE_3DDATA
from build_back_static import _emit_back_glb   # mask-aware static emitter


def subwpn_ids():
    zsc = parse_zsc(os.path.join(ROSE_3DDATA, "WEAPON", "LIST_SUBWPN.ZSC"))
    return [i for i in range(len(zsc.models)) if zsc.models[i].parts]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gender", choices=["F", "M"], default="F")
    ap.add_argument("--ids", default="")
    a = ap.parse_args()
    root = os.path.normpath(os.path.join(_T, ".."))
    src = os.path.dirname(ROSE_3DDATA)
    A = os.path.join(ROSE_3DDATA, "AVATAR")
    ue_assets = os.path.join(root, "unreal-engine rose", "RoseUE", "SourceAssets")
    out = os.path.join(ue_assets, "GLTF", "AVATAR", "MODULAR",
                       "FEMALE" if a.gender == "F" else "MALE", "SUBWPNSTATIC")
    os.makedirs(out, exist_ok=True)
    ids = [int(x) for x in a.ids.split(",") if x.strip()] or subwpn_ids()
    n = 0
    for i in ids:
        parts = resolve_item(A, src, "SUBWPN", i, a.gender)
        if not parts:
            continue
        if _emit_back_glb(os.path.join(out, f"subwpn_{i}.glb"), f"subwpn_{i}", parts):
            n += 1
    print(f"[subwpn] built {n} static sub-weapon GLBs -> {out}")


if __name__ == "__main__":
    main()
