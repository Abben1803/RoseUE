#!/usr/bin/env python3
"""
build_back_static.py — Build BACK items (wings/capes) as RAW STATIC-mesh GLBs,
each keeping its OWN material/texture, for socket attachment to the character's
back dummy bone.

This deliberately mirrors build_weapons_static.py.  The atlas + skeletal-merge
path (build_modular / gear) shares ONE material per mesh, so items that share a
mesh (every Astarot wing = mesh_5175) could not carry distinct textures.  Here
each back item is its own static mesh with its own material straight from the
ZSC — no atlas, no merge, no CellOffset — so shared-mesh items simply can't
collide.  Faithful to ROSE, where back items LINK to the back dummy (like
weapons link to the hand), not skinned into the body.

Output: SourceAssets/GLTF/AVATAR/MODULAR/<gender>/BACKSTATIC/back_<id>.glb
Assets are read from Arua (build_weapons.ROSE_3DDATA).

Usage:  py -3.9 build_back_static.py --gender F --ids 5,25,26
        py -3.9 build_back_static.py --gender F            (whole set)
"""
import argparse, os, sys
_T = os.path.dirname(os.path.abspath(__file__))
if _T not in sys.path:
    sys.path.insert(0, _T)

from rose_avatar import resolve_item
from rose_parser.formats.zms import parse as parse_zms
from rose_parser.formats.zsc import parse as parse_zsc
from rose_to_gltf import _GLBBuilder
from build_weapons import ROSE_3DDATA
# Geometry + DDS→PNG helpers are shared with weapons; only the material's alpha
# mode differs (weapons are OPAQUE, wings need MASK), so we emit our own GLB.
from build_weapons_static import _dds_png, _static_primitive


def back_ids():
    """Every BACK slot id whose ZSC object actually has parts (mirrors weapon_ids)."""
    zsc = parse_zsc(os.path.join(ROSE_3DDATA, "AVATAR", "LIST_BACK.ZSC"))
    return [i for i in range(len(zsc.models)) if zsc.models[i].parts]


def _emit_back_glb(out_path, name, parts):
    """Like the weapon emitter, but the material uses the ZSC alpha flag so wings
    cut out their transparent (white) background instead of rendering opaque."""
    g = _GLBBuilder()
    prims = []
    for p in parts:
        try:
            zms = parse_zms(p["path"])
        except Exception as e:
            print(f"  [skip] {p['path']}: {e}"); continue
        mat_idx = None
        tex = p.get("texture")
        if tex and os.path.isfile(tex):
            try:
                # ROSE back materials are alpha-tested cutouts (wings) — MASK, not
                # OPAQUE; and two-sided so the far wing face shows.  Both come from
                # the ZSC part flags (resolve_item: alpha, two_side).
                mat_idx = g.add_png_material(
                    _dds_png(tex),
                    name=os.path.splitext(os.path.basename(p["path"]))[0],
                    alpha_mask=bool(p.get("alpha", True)),
                    double_sided=bool(p.get("two_side", True)))
            except Exception as e:
                print(f"  [tex] {tex}: {e}")
        prim = _static_primitive(g, zms, mat_idx)
        if prim:
            prims.append(prim)
    if not prims:
        return False
    mesh_idx = len(g.meshes)
    g.meshes.append({"name": name, "primitives": prims})
    node_idx = len(g.nodes)
    g.nodes.append({"name": name, "mesh": mesh_idx})
    g.scene_nodes.append(node_idx)
    with open(out_path, "wb") as f:
        f.write(g.build_glb())
    return True


def build_one(avatar_dir, src, bid, gender, out_dir):
    parts = resolve_item(avatar_dir, src, "BACK", bid, gender)
    if not parts:
        return False
    # Back items are single-mesh in practice; if a ZSC lists several parts they
    # all belong to the one back item, so emit them together in one GLB.
    return _emit_back_glb(os.path.join(out_dir, f"back_{bid}.glb"), f"back_{bid}", parts)


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
                       "FEMALE" if a.gender == "F" else "MALE", "BACKSTATIC")
    os.makedirs(out, exist_ok=True)
    ids = [int(x) for x in a.ids.split(",") if x.strip()] or back_ids()
    n = 0
    for i in ids:
        if build_one(A, src, i, a.gender, out):
            n += 1
        if n % 50 == 0 and n:
            print(f"[backstatic] {n}/{len(ids)} ...")
    print(f"[backstatic] built {n} static back GLBs -> {out}")


if __name__ == "__main__":
    main()
