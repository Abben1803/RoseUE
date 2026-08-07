#!/usr/bin/env python3
"""
build_combined_anims.py  —  Produce FEMALE_anims.glb and MALE_anims.glb.

Each file bundles the gender skeleton + a reference body mesh + every avatar
animation for that gender (split by _F1 / _M1 suffix).  Import the two files in
UE5 to get one shared Skeleton per gender plus all AnimSequences linked to it.

Output: SourceAssets/GLTF/AVATAR/ANIM_COMBINED/{FEMALE,MALE}_anims.glb
"""
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from rose_parser.formats.zsc import parse as parse_zsc
from rose_parser.formats.zmo import parse as parse_zmo
from rose_combine_anims import convert

AVATAR_BONE_COUNT = 21   # FEMALE/MALE.ZMD both have 21 bones (indices 0..20)

_zsc_cache = {}


def _material_for(avatar_dir, src_assets, zsc_name, zms_stem):
    """Look up (texture_abs_path, alpha_mask, two_side) for a ZMS via its ZSC.

    The ZSC is authoritative for textures + material flags — texture names don't
    always match the ZMS name (e.g. hair).  Returns (None, False, False) if not found.
    """
    zsc_path = os.path.join(avatar_dir, zsc_name)
    if zsc_path not in _zsc_cache:
        try:
            _zsc_cache[zsc_path] = parse_zsc(zsc_path)
        except Exception:
            _zsc_cache[zsc_path] = None
    z = _zsc_cache[zsc_path]
    if z is None:
        return (None, False, False)

    mi = None
    for i, m in enumerate(z.mesh_files):
        if m and os.path.splitext(os.path.basename(m))[0].upper() == zms_stem.upper():
            mi = i
            break
    if mi is None:
        return (None, False, False)

    for model in z.models:
        for part in model.parts:
            if part.mesh_id == mi:
                mat = z.materials[part.material_id]
                rel = mat.texture_path.replace('\\', os.sep).replace('/', os.sep)
                tex_abs = os.path.normpath(os.path.join(src_assets, rel))
                alpha = bool(mat.is_alpha or mat.alpha_test)
                return (tex_abs, alpha, bool(mat.is_2side))
    return (None, False, False)


def _max_bone(zmo_path: str) -> int:
    """Highest bone index referenced by any channel (−1 if none)."""
    try:
        zmo = parse_zmo(zmo_path)
        return max((c.refer_id for c in zmo.channels), default=-1)
    except Exception:
        return 999  # unreadable → treat as incompatible


def _ref_mesh_from_zsc(zsc_path: str, src_assets: str) -> str:
    """Return absolute path to the first ZMS referenced by a body ZSC."""
    zsc = parse_zsc(zsc_path)
    for rel in zsc.mesh_files:
        if not rel:
            continue
        rel = rel.replace('\\', os.sep).replace('/', os.sep)
        abs_path = os.path.normpath(os.path.join(src_assets, rel))
        if os.path.isfile(abs_path):
            return abs_path
    raise FileNotFoundError(f"No usable ZMS found in {zsc_path}")


def run():
    project_root = os.path.normpath(os.path.join(_TOOLS, ".."))
    ue_project   = os.path.join(project_root, "unreal-engine rose", "RoseUE")
    src_assets   = os.path.join(ue_project, "SourceAssets")
    # Asset SOURCE is Arua (CLAUDE.md); only the GLB OUTPUT stays in SourceAssets.
    # Arua ships 562 MOTION/AVATAR files vs classic's 462, so the classic tree is
    # missing skill motions this combined GLB is meant to carry.
    rose_3ddata  = os.environ.get("ROSE_ASSET_ROOT",
                                  r"C:\QQ-iROSE Online\extracted\3DDATA")
    avatar_dir   = os.path.join(rose_3ddata, "AVATAR")
    motion_dir   = os.path.join(rose_3ddata, "MOTION", "AVATAR")
    out_dir      = os.path.join(src_assets, "GLTF", "AVATAR", "ANIM_COMBINED")
    os.makedirs(out_dir, exist_ok=True)

    zmo_files = sorted(f for f in os.listdir(motion_dir) if f.upper().endswith(".ZMO"))

    # Classify every ZMO once: gendered (_F1/_M1) vs neutral, and bone-compatible.
    female_only, male_only, neutral = [], [], []
    excluded = []
    for zmo in zmo_files:
        stem = os.path.splitext(zmo)[0]
        path = os.path.join(motion_dir, zmo)
        if _max_bone(path) >= AVATAR_BONE_COUNT:
            excluded.append(stem)            # mech / vehicle skeleton, not the avatar
            continue
        up = stem.upper()
        if up.endswith("_F1"):
            female_only.append((stem, path))
        elif up.endswith("_M1"):
            male_only.append((stem, path))
        else:
            neutral.append((stem, path))     # skills/emotes → both genders

    print(f"Classified: {len(female_only)} female, {len(male_only)} male, "
          f"{len(neutral)} neutral (both), {len(excluded)} excluded (non-avatar skeleton)")

    # Default base-avatar parts baked into the reference mesh so the imported
    # AnimRig asset is a full animated character on the shared skeleton.
    # ROSE bodies are split top (00100, torso) + bottom (00110, legs).
    # Non-skinned parts (face/hair) are pinned to the HEAD bone (index 4).
    # (folder, zms filename, pin bone, ZSC for texture lookup)
    HEAD_BONE = 4
    DEFAULTS = {
        "FEMALE": [("BODY", "BODY2_00100.ZMS", None, "LIST_WBODY.ZSC"),
                   ("BODY", "BODY2_00110.ZMS", None, "LIST_WBODY.ZSC"),
                   ("ARMS", "ARM2_00100.ZMS", None, "LIST_WARMS.ZSC"),
                   ("FACE", "FACE2_00100.ZMS", HEAD_BONE, "LIST_WFACE.ZSC"),
                   ("HAIR", "HAIR02_00100.ZMS", HEAD_BONE, "LIST_WHAIR.ZSC"),
                   ("FOOT", "FOOT2_00100.ZMS", None, "LIST_WFOOT.ZSC")],
        "MALE":   [("BODY", "BODY1_00100.ZMS", None, "LIST_MBODY.ZSC"),
                   ("BODY", "BODY1_00110.ZMS", None, "LIST_MBODY.ZSC"),
                   ("ARMS", "ARM1_00100.ZMS", None, "LIST_MARMS.ZSC"),
                   ("FACE", "FACE1_00100.ZMS", HEAD_BONE, "LIST_MFACE.ZSC"),
                   ("HAIR", "HAIR01_00100.ZMS", HEAD_BONE, "LIST_MHAIR.ZSC"),
                   ("FOOT", "FOOT1_00100.ZMS", None, "LIST_MFOOT.ZSC")],
    }

    for gender, zmd_name, gendered in [
        ("FEMALE", "FEMALE.ZMD", female_only),
        ("MALE",   "MALE.ZMD",   male_only),
    ]:
        zmd_path = os.path.join(avatar_dir, zmd_name)
        ref_zms = []
        n_tex = 0
        for folder, fname, pin, zsc in DEFAULTS[gender]:
            p = os.path.join(avatar_dir, folder, fname)
            if not os.path.isfile(p):
                print(f"  [warn] default part missing: {p}")
                continue
            stem = os.path.splitext(fname)[0]
            tex, alpha, two_side = _material_for(avatar_dir, src_assets, zsc, stem)
            if tex and os.path.isfile(tex):
                n_tex += 1
            # Now that pinned parts have correct winding, render them single-sided
            # so the hair's inner faces don't drape over and occlude the face.
            # Keep the FACE double-sided as a safety net (it's the frontmost part).
            if folder == "FACE":
                two_side = True
            elif folder == "HAIR":
                two_side = False
            ref_zms.append({"path": p, "pin": pin, "texture": tex,
                            "alpha": alpha, "two_side": two_side})
        anims = gendered + neutral

        print(f"[{gender}] skeleton={zmd_name} ref_parts={len(ref_zms)} "
              f"textured={n_tex} anims={len(anims)}")
        convert(zmd_path, ref_zms, anims, os.path.join(out_dir, f"{gender}_anims.glb"))


if __name__ == "__main__":
    run()
