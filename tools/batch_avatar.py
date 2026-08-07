#!/usr/bin/env python3
"""
batch_avatar.py  —  Batch-export all ROSE avatar parts + animations to GLB.

Reads every W* (female) and M* (male) ZSC file in 3DDATA/AVATAR/, extracts the
referenced ZMS mesh paths, and converts each unique mesh to a GLB alongside its
skeleton.  Then converts all avatar ZMO animations.

Output layout (under OUT_DIR, default SourceAssets/GLTF/AVATAR/):
    FEMALE/
        skeleton.glb          ← skeleton only (import first to create Skeleton asset)
        WBODY_ep6-f-body-naked.glb
        ...
    MALE/
        skeleton.glb
        MBODY_ep6-m-body-naked.glb
        ...
    ANIM/
        AVATAR_IDLE_F1.glb
        ...

Usage:
    python tools/batch_avatar.py [--src <SourceAssets dir>] [--out <output dir>]
"""
import argparse
import os
import sys
import traceback
from typing import Dict, List, Set

_TOOLS = os.path.dirname(os.path.abspath(__file__))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from rose_parser.formats.zsc import parse as parse_zsc
from rose_to_gltf    import convert as mesh_to_glb
from rose_anim_to_gltf import convert as anim_to_glb

# ── helpers ───────────────────────────────────────────────────────────────────

def _src_path(src_root: str, relative_path: str) -> str:
    """Resolve a ROSE relative path (e.g. '3DDATA/AVATAR/BODY/foo.ZMS') to absolute.

    ZSC mesh paths are stored relative to the game root (the parent of 3DDATA/).
    src_root is .../SourceAssets/3DDATA/AVATAR/, so we go up two directories to
    reach .../SourceAssets/, then append the 3DDATA-prefixed path.
    """
    rel = relative_path.replace('\\', os.sep).replace('/', os.sep)
    if rel.upper().startswith('3DDATA'):
        candidate = os.path.join(src_root, '..', '..', rel)
    else:
        candidate = os.path.join(src_root, rel)
    return os.path.normpath(candidate)


def _collect_zms_from_zsc(zsc_path: str, src_root: str) -> List[str]:
    """Parse a ZSC file and return absolute paths to all referenced ZMS files."""
    try:
        zsc = parse_zsc(zsc_path)
    except Exception as e:
        print(f"  [SKIP ZSC] {zsc_path}: {e}")
        return []

    out = []
    for rel in zsc.mesh_files:
        if not rel:
            continue
        abs_path = _src_path(src_root, rel)
        if os.path.isfile(abs_path):
            out.append(abs_path)
        else:
            # Try case-insensitive on Windows - path already works on Windows
            # Try uppercasing the filename component
            head, tail = os.path.split(abs_path)
            upper = os.path.join(head, tail.upper())
            if os.path.isfile(upper):
                out.append(upper)
    return out


def _safe_stem(path: str) -> str:
    return os.path.splitext(os.path.basename(path))[0]


# ── main logic ────────────────────────────────────────────────────────────────

def run(src_dir: str, out_dir: str) -> None:
    """
    src_dir : path to SourceAssets/3DDATA/AVATAR/
    out_dir : output root (SourceAssets/GLTF/AVATAR/ by default)
    """
    avatar_dir = src_dir
    zmd_female = os.path.join(avatar_dir, "FEMALE.ZMD")
    zmd_male   = os.path.join(avatar_dir, "MALE.ZMD")
    motion_dir = os.path.normpath(os.path.join(avatar_dir, "..", "MOTION", "AVATAR"))

    for p in (zmd_female, zmd_male):
        if not os.path.isfile(p):
            print(f"ERROR: Missing skeleton: {p}")
            sys.exit(1)

    os.makedirs(os.path.join(out_dir, "FEMALE"), exist_ok=True)
    os.makedirs(os.path.join(out_dir, "MALE"),   exist_ok=True)
    os.makedirs(os.path.join(out_dir, "ANIM"),   exist_ok=True)

    # ── mesh parts from ZSC files ─────────────────────────────────────────────
    # Note: skeleton-only GLBs (no mesh) crash UE5's GLTFCore importer.
    # The first body-part GLB imported will establish the Skeleton asset instead.
    zsc_files = sorted(f for f in os.listdir(avatar_dir) if f.upper().endswith(".ZSC"))

    # Determine skeleton for each ZSC (W prefix = female, M prefix = male)
    def _zmd_for(zsc_name: str) -> str:
        up = zsc_name.upper()
        if "LIST_W" in up:
            return zmd_female
        if "LIST_M" in up:
            return zmd_male
        # LIST_BACK etc. — export twice
        return None  # handled separately below

    seen_female: Set[str] = set()
    seen_male:   Set[str] = set()

    print("\n[Mesh parts]")
    for zsc_name in zsc_files:
        zsc_path = os.path.join(avatar_dir, zsc_name)
        zsc_stem = _safe_stem(zsc_name)

        zmd = _zmd_for(zsc_name)
        groups = [(zmd, "FEMALE" if "LIST_W" in zsc_name.upper() else "MALE")] if zmd else \
                 [(zmd_female, "FEMALE"), (zmd_male, "MALE")]

        zms_paths = _collect_zms_from_zsc(zsc_path, src_dir)
        if not zms_paths:
            continue

        for zmd_path, gender in groups:
            seen = seen_female if gender == "FEMALE" else seen_male
            gender_dir = os.path.join(out_dir, gender)

            for zms_path in zms_paths:
                key = zms_path.upper()
                if key in seen:
                    continue
                seen.add(key)

                stem    = _safe_stem(zms_path)
                out_glb = os.path.join(gender_dir, f"{zsc_stem}_{stem}.glb")
                try:
                    mesh_to_glb(zmd_path, [zms_path], out_glb)
                except Exception:
                    print(f"  [FAIL] {out_glb}")
                    traceback.print_exc()

    # ── animations ────────────────────────────────────────────────────────────
    print("\n[Animations]")
    if os.path.isdir(motion_dir):
        zmo_files = sorted(f for f in os.listdir(motion_dir) if f.upper().endswith(".ZMO"))
        for zmo_name in zmo_files:
            zmo_path = os.path.join(motion_dir, zmo_name)
            stem     = _safe_stem(zmo_name)
            out_glb  = os.path.join(out_dir, "ANIM", f"{stem}.glb")

            # Female anims use _F suffix, male use _M; default to female skeleton
            zmd = zmd_male if stem.upper().endswith("_M1") or "_M_" in stem.upper() \
                  else zmd_female
            try:
                anim_to_glb(zmd, zmo_path, out_glb)
            except Exception:
                print(f"  [FAIL] {out_glb}")
                traceback.print_exc()
    else:
        print(f"  [SKIP] motion dir not found: {motion_dir}")

    print("\nDone.")


def main():
    ap = argparse.ArgumentParser(description="Batch-export ROSE avatar to GLB files")
    ap.add_argument("--src", default=None,
                    help="Path to SourceAssets/3DDATA/AVATAR (auto-detected if omitted)")
    ap.add_argument("--out", default=None,
                    help="Output directory (default: SourceAssets/GLTF/AVATAR)")
    args = ap.parse_args()

    # Auto-detect paths relative to this script's location
    project_root = os.path.normpath(os.path.join(_TOOLS, ".."))
    ue_project   = os.path.join(project_root, "unreal-engine rose", "RoseUE")

    # Asset SOURCE is Arua (CLAUDE.md); only the GLB OUTPUT stays in SourceAssets.
    # The equip ZSCs differ hugely across eras, and object ids do not line up.
    src = args.src or os.path.join(
        os.environ.get("ROSE_ASSET_ROOT",
                       r"C:\QQ-iROSE Online\extracted\3DDATA"), "AVATAR")
    out = args.out or os.path.join(ue_project, "SourceAssets", "GLTF", "AVATAR")

    print(f"Source : {src}")
    print(f"Output : {out}")

    if not os.path.isdir(src):
        print(f"ERROR: Source directory not found: {src}")
        sys.exit(1)

    run(src, out)


if __name__ == "__main__":
    main()
