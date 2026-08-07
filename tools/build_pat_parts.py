#!/usr/bin/env python3
"""
build_pat_parts.py — Build the PAT (vehicle) part GLBs.

LIST_PAT.STB row id == LIST_PAT.ZSC object id (the avatar master key).

The family comes from LIST_PAT.STB col 72 "PAT Class (1:Cart, 2:CG)", which is
the table's own answer to which of ROSE's three PAT features an item belongs to:

    1 cart         assembled from 5 parts, burns fuel  -> PAT/CART/CART01.ZMD
    2 castle gear  assembled from 5 parts, burns fuel  -> CASTLEGEAR02.ZMD
    3 mount        standalone single model, no parts, no fuel

Mounts ride the CART skeleton: their models are cart-derived (cart bodies,
boats, carpets) and the 57 mounts that have motion data point at the cart
motion rows (281/261).  The rest have no motion data at all — TYPE_MOTION ends
at row 625 — so they fall back to the same cart clips.  Sharing base_21 keeps
one skeleton and one animation set for both.

The base GLBs carry the family's animation set as glTF tracks, so importing
base_<pet>.glb yields the skeleton AND every AnimSequence in one pass (the
Interchange trick rose_combine_anims relies on).  Track name = ZMO stem, which
is what pat_motion.csv records — keep the two in step.

Outputs under SourceAssets/GLTF/PAT/:
    base_21.glb    cart skeleton + default body mesh + cart/mount animations
    base_31.glb    castle-gear skeleton + default body + castle-gear animations
    pat_<id>.glb   one per LIST_PAT item (full skeleton, compatible with base)

Usage:  py -3.9 tools/build_pat_parts.py [--only 1,2,121]
"""
import argparse
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _TOOLS)
sys.path.insert(0, os.path.join(os.path.dirname(_TOOLS), "mapforge"))

from rose_zsc import read_zsc                      # mapforge parser (flags + tex)
from rose_combine_anims import convert
from rose_parser.formats.stb import parse as parse_stb
from rose_parser.formats.zms import parse as parse_zms

ARUA = r"C:\QQ-iROSE Online\extracted\3DDATA"
OUT = os.path.join(os.path.dirname(_TOOLS), "unreal-engine rose", "RoseUE",
                   "SourceAssets", "GLTF", "PAT")

SKEL = {
    21: os.path.join(ARUA, "PAT", "CART", "CART01.ZMD"),
    31: os.path.join(ARUA, "PAT", "CASTLEGEAR", "CASTLEGEAR02", "CASTLEGEAR02.ZMD"),
}

# LIST_PAT data columns (see gen_pat_tables.py for the full map)
C_ITEMTYPE, C_PART_TYPE = 4, 16
C_PAT_MOTION, C_AVATAR_MOTION, C_PAT_CLASS = 40, 41, 72

PAT_CART, PAT_CG, PAT_MOUNT = 1, 2, 3
# PAT Class -> merge-target skeleton. Mounts share the cart skeleton.
PET_FOR_CLASS = {PAT_CART: 21, PAT_CG: 31, PAT_MOUNT: 21}

# enumCART_ANI order (src/client/cobjcart.h)
N_ACTIONS = 8


def pet_for_row(stb, i):
    """Merge-target skeleton for a LIST_PAT row, or None if not a PAT item."""
    return PET_FOR_CLASS.get(stb.get_int(i, C_PAT_CLASS))


def is_body_row(stb, i):
    """BODY rows (t_eRidePART 0) own the motion ROW base; mounts are body-only."""
    itemtype = stb.get_int(i, C_ITEMTYPE)
    if stb.get_int(i, C_PAT_CLASS) == PAT_MOUNT:
        return True
    return itemtype > 0 and (itemtype // 10) % 10 == 1


def family_anims(stb, tm, fm):
    """{pet: [(track_name, zmo_abs)]} — every clip a family's BODY rows reach.

    Cells are TYPE_MOTION[base + action][col] -> FILE_MOTION index -> ZMO. Rows
    past the end of TYPE_MOTION (the 625/626 mount bases) simply yield nothing,
    which is why those mounts fall back to the cart clips at runtime.
    """
    bases = {}
    for i in range(1, stb.num_rows()):
        pet = pet_for_row(stb, i)
        if pet is None or not is_body_row(stb, i):
            continue
        for col in (C_PAT_MOTION, C_AVATAR_MOTION):
            b = stb.get_int(i, col)
            if b > 0:
                bases.setdefault(pet, set()).add(b)

    out = {}
    for pet, base_set in bases.items():
        seen, anims = set(), []
        for base in sorted(base_set):
            for act in range(N_ACTIONS):
                row = base + act
                if row >= tm.num_rows():
                    continue
                for c in range(tm.num_cols()):
                    raw = tm.get(row, c).strip()
                    if not raw.isdigit():
                        continue
                    fidx = int(raw)
                    if fidx <= 0 or fidx >= fm.num_rows():
                        continue
                    rel = fm.get(fidx, 0).strip()
                    if not rel:
                        continue
                    stem = os.path.splitext(os.path.basename(rel.replace("\\", "/")))[0]
                    if stem.lower() in seen:
                        continue
                    ab = resolve(rel)
                    if not ab:
                        continue
                    seen.add(stem.lower())
                    anims.append((stem, ab))
        out[pet] = anims
    return out


def resolve(rel):
    parts = [p for p in rel.replace("\\", "/").split("/") if p]
    if parts and parts[0].lower() == "3ddata":
        parts = parts[1:]
    cur = ARUA
    for p in parts:
        if not os.path.isdir(cur):
            return None
        m = [e for e in os.listdir(cur) if e.lower() == p.lower()]
        if not m:
            return None
        cur = os.path.join(cur, m[0])
    return cur if os.path.exists(cur) else None


def is_skinned(zms_abs):
    try:
        return bool(parse_zms(zms_abs).has_skin)
    except Exception:
        return False


def item_parts(zsc, oid):
    """List of convert() part dicts for ZSC object oid, or None."""
    if not (0 <= oid < len(zsc.models)):
        return None
    out = []
    for p in zsc.models[oid].parts:
        mesh_rel = zsc.meshes[p.mesh_idx] if 0 <= p.mesh_idx < len(zsc.meshes) else None
        if not mesh_rel:
            continue
        ab = resolve(mesh_rel)
        if not ab:
            continue
        mat = zsc.materials[p.mat_idx] if 0 <= p.mat_idx < len(zsc.materials) else None
        tex = resolve(mat.path) if mat and mat.path else None
        out.append({
            "path": ab,
            # skinned parts follow the skeleton; rigid ones pin to the part's
            # bone (or root) exactly like monster parts
            "pin": None if is_skinned(ab) else max(0, p.bone_idx),
            "texture": tex,
            "alpha": bool(mat.is_alpha) if mat else False,
        })
    return out or None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--only", default="")
    args = ap.parse_args()
    only = {int(x) for x in args.only.split(",") if x.strip()}

    os.makedirs(OUT, exist_ok=True)
    stb = parse_stb(os.path.join(ARUA, "STB", "LIST_PAT.STB"))
    tm = parse_stb(os.path.join(ARUA, "STB", "TYPE_MOTION.STB"))
    fm = parse_stb(os.path.join(ARUA, "STB", "FILE_MOTION.STB"))
    zsc = read_zsc(os.path.join(ARUA, "PAT", "LIST_PAT.ZSC"))
    print(f"[pat] stb rows {stb.num_rows()}  zsc objects {len(zsc.models)}")

    anims_for = family_anims(stb, tm, fm)
    for pet, a in sorted(anims_for.items()):
        print(f"[pat] pet {pet}: {len(a)} animation clips")

    # bases: the first cart / castle-gear BODY item supplies the reference mesh
    base_ref = {}
    for i in range(1, stb.num_rows()):
        pet = pet_for_row(stb, i)
        itemtype = stb.get_int(i, C_ITEMTYPE)
        if pet in SKEL and pet not in base_ref and itemtype in (511, 512):
            parts = item_parts(zsc, i)
            if parts:
                base_ref[pet] = parts
    for pet, zmd in SKEL.items():
        if pet not in base_ref:
            print(f"[pat] no base body for pet {pet}??")
            continue
        convert(zmd, base_ref[pet], anims_for.get(pet, []),
                os.path.join(OUT, f"base_{pet}.glb"))
        print(f"[pat] base_{pet}.glb ({len(anims_for.get(pet, []))} anims)")

    built = skipped = 0
    by_class = {PAT_CART: 0, PAT_CG: 0, PAT_MOUNT: 0}
    for i in range(1, stb.num_rows()):
        if only and i not in only:
            continue
        pat_class = stb.get_int(i, C_PAT_CLASS)
        pet = PET_FOR_CLASS.get(pat_class)
        if pet not in SKEL:
            if stb.get(i, 0).strip():
                skipped += 1
            continue
        parts = item_parts(zsc, i)
        if not parts:
            continue
        try:
            convert(SKEL[pet], parts, [], os.path.join(OUT, f"pat_{i}.glb"))
            built += 1
            by_class[pat_class] += 1
        except Exception as e:
            print(f"[pat] {i}: FAILED {e}")
    print(f"[pat] done: {built} part GLBs "
          f"(cart {by_class[PAT_CART]}, castle gear {by_class[PAT_CG]}, "
          f"mount {by_class[PAT_MOUNT]}; skipped {skipped} non-PAT rows) -> {OUT}")


if __name__ == "__main__":
    main()
