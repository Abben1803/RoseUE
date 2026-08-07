#!/usr/bin/env python3
"""
build_monsters.py — Build one GLB per monster/NPC from LIST_NPC.CHR + PART_NPC.ZSC.

The binding (verified, faithful to CCharDatLIST::Load_MOBorNPC +
CObjNPC::Create): LIST_NPC.STB row id == LIST_NPC.CHR model index.  Each CHR
model names its skeleton (ZMD), its body parts (indices into PART_NPC.ZSC
objects — mesh + material/texture per part) and its animations (MOB_ANI slot →
ZMO path from the CHR's own motion-file list).

Output: SourceAssets/GLTF/NPC/npc_<id>.glb — skeleton + all textured parts +
every animation as a track named "npc_<id>_<slot>" (stop/move/attack/hit/die/
run/cast01/...), ready for ue5_import_monsters.py.

Usage:  py -3.9 build_monsters.py            # all valid models
        ROSE_ONLY="1,2,5" py -3.9 build_monsters.py   # specific ids
"""
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from rose_parser.formats.chr_ import parse as parse_chr
from rose_parser.formats.zsc import parse as parse_zsc
from rose_parser.formats.zms import parse as parse_zms
from rose_combine_anims import convert

# Asset SOURCE and GLB OUTPUT roots are deliberately separate.  SRC used to be
# one path serving both, which pinned reads to the deprecated classic tree:
# LIST_NPC.STB is 4206 rows in Arua vs 3033 in classic and the ids do not line
# up, so npc_<id>.glb was built from a different monster than the DataTables
# name (e.g. row 1000 = 'Love Balloons' in Arua, 'Goddess Arua' in classic).
ROSE_3DDATA = os.environ.get("ROSE_ASSET_ROOT",
                             r"C:\QQ-iROSE Online\extracted\3DDATA")
# CHR/ZSC store VFS paths that already include the leading '3DDATA\', so
# _resolve() joins against the PARENT of the 3DDATA directory.
SRC = os.path.dirname(ROSE_3DDATA)
NPC_DIR = os.path.join(ROSE_3DDATA, "NPC")
OUT_DIR = os.path.join(r"C:/rose-next-classic/unreal-engine rose/RoseUE/SourceAssets",
                       "GLTF", "NPC")

# MOB_ANI_* (src/common/shared/datatype.h) — glTF track name per CHR anim slot.
MOB_ANI_NAMES = {
    0: "stop", 1: "move", 2: "attack", 3: "hit", 4: "die", 5: "run",
    6: "cast01", 7: "skill01", 8: "cast02", 9: "skill02", 10: "etc",
}

_zms_skin_cache = {}


def _resolve(rel):
    """CHR/ZSC paths ('3Ddata\\...') are VFS-relative → under SourceAssets/."""
    return os.path.normpath(os.path.join(SRC, rel.replace("\\", os.sep)))


def _is_skinned(mesh_abs):
    if mesh_abs not in _zms_skin_cache:
        try:
            _zms_skin_cache[mesh_abs] = parse_zms(mesh_abs).has_skin()
        except Exception:
            _zms_skin_cache[mesh_abs] = False
    return _zms_skin_cache[mesh_abs]


def resolve_model(chr_, zsc, idx):
    """(zmd_path, parts, anims) for CHR model idx, or None if unusable."""
    m = chr_.models[idx]
    if not m.is_valid:
        return None
    if not (0 <= m.skel_index < len(chr_.skeleton_files)):
        return None
    zmd = _resolve(chr_.skeleton_files[m.skel_index])
    if not os.path.isfile(zmd):
        print(f"  [npc {idx}] missing skeleton {zmd}")
        return None

    parts = []
    for bp in m.body_part_indices:
        if bp >= len(zsc.models):
            continue
        for zp in zsc.models[bp].parts:
            if zp.mesh_id >= len(zsc.mesh_files):
                continue
            mesh_abs = _resolve(zsc.mesh_files[zp.mesh_id])
            if not os.path.isfile(mesh_abs):
                print(f"  [npc {idx}] missing mesh {mesh_abs}")
                continue
            mat = zsc.materials[zp.material_id] if zp.material_id < len(zsc.materials) else None
            tex = _resolve(mat.texture_path) if mat and mat.texture_path else None
            parts.append({
                "path": mesh_abs,
                # Mob parts are normally skinned; a rigid part pins to root.
                "pin": None if _is_skinned(mesh_abs) else 0,
                "texture": tex,
                # NPC materials say alpha_test=1 but their DDS alpha holds
                # specular/glow, not coverage — masked import clips body pixels
                # (same as weapons).  Import OPAQUE; ue5_fix_monster_materials.py
                # repairs already-imported ones.
                "alpha": False,
                "two_side": True,
            })
    if not parts:
        return None

    anims = []
    for ani_type, midx in sorted(m.animations.items()):
        if not (0 <= midx < len(chr_.motion_files)):
            continue
        zmo = _resolve(chr_.motion_files[midx])
        if not os.path.isfile(zmo):
            print(f"  [npc {idx}] missing anim {zmo}")
            continue
        slot = MOB_ANI_NAMES.get(ani_type, f"ani{ani_type}")
        # Interchange names the AnimSequence asset <glb-filename><track-name>,
        # so a track named "_stop" imports as "npc_<id>_stop".
        anims.append((f"_{slot}", zmo))
    return zmd, parts, anims


def main():
    chr_ = parse_chr(os.path.join(NPC_DIR, "LIST_NPC.CHR"))
    zsc = parse_zsc(os.path.join(NPC_DIR, "PART_NPC.ZSC"))
    os.makedirs(OUT_DIR, exist_ok=True)

    only = [int(x) for x in os.environ.get("ROSE_ONLY", "").split(",") if x.strip()]
    ids = only or [i for i, m in enumerate(chr_.models) if m.is_valid]

    built = skipped = failed = 0
    for idx in ids:
        if idx < 0 or idx >= len(chr_.models):
            print(f"[npc {idx}] out of range"); continue
        r = resolve_model(chr_, zsc, idx)
        if r is None:
            skipped += 1
            continue
        zmd, parts, anims = r
        out = os.path.join(OUT_DIR, f"npc_{idx}.glb")
        # Resumable batches: keep existing GLBs (ROSE_FORCE=1 rebuilds).
        if os.path.isfile(out) and not os.environ.get("ROSE_FORCE", "").strip():
            skipped += 1
            continue
        print(f"[npc {idx}] {chr_.models[idx].name}  parts={len(parts)} anims={len(anims)}")
        try:
            convert(zmd, parts, anims, out)
            built += 1
        except Exception as e:
            print(f"  [npc {idx}] FAILED: {e}")
            failed += 1
    print(f"[monsters] built {built}, skipped {skipped}, failed {failed} -> {OUT_DIR}")


if __name__ == "__main__":
    main()
