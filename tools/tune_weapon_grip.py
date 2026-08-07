#!/usr/bin/env python3
"""
tune_weapon_grip.py — Build candidate weapon GLBs for ONE base weapon, each baked
with a different ROSE_GRIP_FIX correction quaternion, under throwaway ids 9000+.

Import them (ue5_import_weapons.py ROSE_ONLY=weapon_9000,...weapon_9006) and browse
weapon 9000..9006 in PIE to see which grip orientation is correct, then bake the
whole catalog with that quaternion via:
    ROSE_GRIP_FIX="x,y,z,w" py -3.9 build_weapons.py --gender F

The grip correction is pre-multiplied in HAND-LOCAL space (rose_combine_anims.py),
so it rotates every weapon uniformly — good, since they share one grip.

Usage:  py -3.9 tune_weapon_grip.py [base_weapon_id]   (default 1 = a 1H sword)
"""
import os
import sys

_T = os.path.dirname(os.path.abspath(__file__))
if _T not in sys.path:
    sys.path.insert(0, _T)
from rose_avatar import resolve_item
from rose_combine_anims import convert

BASE = int(sys.argv[1]) if len(sys.argv) > 1 else 1
GENDER = "F"
S = 0.70710678  # sin/cos 45° → a 90° rotation quaternion component

# fake browse id -> (human label, ROSE_GRIP_FIX "x,y,z,w").  The 6 cardinal 90°
# rotations bracket a "~90° off" grip; pick the one that points the blade out of
# the fist along the hand.  9000 = no correction (current catalog look).
CANDIDATES = {
    9000: ("none / current", "0,0,0,1"),
    9001: ("+90 about X", f"{S},0,0,{S}"),
    9002: ("-90 about X", f"-{S},0,0,{S}"),
    9003: ("+90 about Y", f"0,{S},0,{S}"),
    9004: ("-90 about Y", f"0,-{S},0,{S}"),
    9005: ("+90 about Z", f"0,0,{S},{S}"),
    9006: ("-90 about Z", f"0,0,-{S},{S}"),
}


def main():
    root = os.path.normpath(os.path.join(_T, ".."))
    src = os.path.join(root, "unreal-engine rose", "RoseUE", "SourceAssets")
    A = os.path.join(src, "3DDATA", "AVATAR")
    zmd = os.path.join(A, "FEMALE.ZMD")
    out = os.path.join(src, "GLTF", "AVATAR", "MODULAR", "FEMALE")
    os.makedirs(out, exist_ok=True)

    parts = resolve_item(A, src, "WEAPON", BASE, GENDER)
    if not parts:
        print(f"[tune] weapon {BASE} has no parts"); return
    print(f"[tune] base weapon_{BASE} ({len(parts)} part(s)) -> "
          f"candidates {min(CANDIDATES)}..{max(CANDIDATES)}")
    for fid, (label, quat) in CANDIDATES.items():
        os.environ["ROSE_GRIP_FIX"] = quat
        convert(zmd, parts, [], os.path.join(out, f"weapon_{fid}.glb"))
        print(f"   weapon_{fid}: {label:16}  ROSE_GRIP_FIX={quat}")
    print("[tune] done. Import weapon_9000..9006 and browse them in PIE.")


if __name__ == "__main__":
    main()
