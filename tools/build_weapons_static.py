#!/usr/bin/env python3
"""
build_weapons_static.py — Build weapons as RAW STATIC-mesh GLBs (no skeleton, no
skin, no baked grip) for socket attachment to the character's hand bone.

The held-weapon orientation is then a single relative transform on the attach
socket (tuned live in the editor, then baked), instead of being skinned into the
merge — faithful to ROSE (weapons are linked to the hand dummy, not skinned) and
fully controllable.

Output: SourceAssets/GLTF/AVATAR/MODULAR/<gender>/WPNSTATIC/weapon_<id>.glb
Each part keeps its own material/texture (dual-wield → 2 parts in one file; the
C++ can split per hand later).  Right-hand single weapons are the common case.

Usage:  py -3.9 build_weapons_static.py --gender F --ids 2,101,31
        py -3.9 build_weapons_static.py --gender F            (whole player set)
"""
import argparse, os, sys
_T = os.path.dirname(os.path.abspath(__file__))
if _T not in sys.path:
    sys.path.insert(0, _T)

from rose_avatar import resolve_item
from rose_parser.formats.zms import parse as parse_zms
from rose_to_gltf import (_GLBBuilder, _pos_r2g, _normal_r2g, BASIS_FLIPS_WINDING,
                          COMP_FLOAT, TARGET_ARRAY)
from build_weapons import ROSE_3DDATA, weapon_ids


def _dds_png(dds):
    from PIL import Image
    import io
    im = Image.open(dds).convert("RGBA")
    buf = io.BytesIO(); im.save(buf, format="PNG"); return buf.getvalue()


def _static_primitive(g, zms, material_index):
    verts, faces = zms.vertices, zms.faces
    if not verts or not faces:
        return None
    positions = [_pos_r2g(*v.position) for v in verts]
    attrs = {"POSITION": g.add_f32_vec3(positions, TARGET_ARRAY)}
    if zms.has_normals() and verts[0].normal is not None:
        attrs["NORMAL"] = g.add_f32_vec3([_normal_r2g(*v.normal) for v in verts], TARGET_ARRAY)
    if verts[0].uvs:
        attrs["TEXCOORD_0"] = g.add_f32_vec2([(v.uvs[0][0], v.uvs[0][1]) for v in verts], TARGET_ARRAY)
    if BASIS_FLIPS_WINDING:
        faces = [(a, c, b) for (a, b, c) in faces]
    prim = {"attributes": attrs, "indices": g.add_u16_indices(faces)}
    if material_index is not None:
        prim["material"] = material_index
    return prim


def _emit_glb(out_path, name, parts):
    """Write one static-mesh GLB from a list of parts (each = one prim)."""
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
                # Respect the ZSC alpha flag: cutout weapons (e.g. Long Sword
                # osw3, openwork guards/banners) have real transparency, so
                # building them OPAQUE renders the transparent areas as a
                # black/white background.  MASK when the ZSC part is alpha-tested.
                mat_idx = g.add_png_material(_dds_png(tex), name=os.path.splitext(os.path.basename(p["path"]))[0],
                                             alpha_mask=bool(p.get("mask", False)), double_sided=True)
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


def build_one(avatar_dir, src, wid, gender, out_dir):
    parts = resolve_item(avatar_dir, src, "WEAPON", wid, gender)
    if not parts:
        return False
    # Single-part weapon → one GLB.  Dual-wield (katar/dual-sword/dual-gun: 2 parts,
    # dummy 0=right + dummy 1=left) → ONE GLB PER HAND so each attaches to its own
    # hand: weapon_<id>.glb (right, dummy 0) + weapon_<id>_off.glb (left, dummy 1).
    if len(parts) <= 1:
        return _emit_glb(os.path.join(out_dir, f"weapon_{wid}.glb"), f"weapon_{wid}", parts)
    parts = sorted(parts, key=lambda p: p.get("pin_dummy") or 0)   # dummy 0 first
    ok = _emit_glb(os.path.join(out_dir, f"weapon_{wid}.glb"), f"weapon_{wid}", [parts[0]])
    ok2 = _emit_glb(os.path.join(out_dir, f"weapon_{wid}_off.glb"), f"weapon_{wid}_off", parts[1:])
    return ok or ok2


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gender", choices=["F", "M"], default="F")
    ap.add_argument("--ids", default="")
    a = ap.parse_args()
    root = os.path.normpath(os.path.join(_T, ".."))
    # Assets come from Arua (see build_weapons.ROSE_3DDATA); GLBs go to SourceAssets.
    src = os.path.dirname(ROSE_3DDATA)
    A = os.path.join(ROSE_3DDATA, "AVATAR")
    ue_assets = os.path.join(root, "unreal-engine rose", "RoseUE", "SourceAssets")
    out = os.path.join(ue_assets, "GLTF", "AVATAR", "MODULAR",
                       "FEMALE" if a.gender == "F" else "MALE", "WPNSTATIC")
    os.makedirs(out, exist_ok=True)
    ids = [int(x) for x in a.ids.split(",") if x.strip()] or weapon_ids()
    n = 0
    for i in ids:
        if build_one(A, src, i, a.gender, out):
            n += 1
        if n % 50 == 0 and n:
            print(f"[wpnstatic] {n}/{len(ids)} ...")
    print(f"[wpnstatic] built {n} static weapon GLBs -> {out}")


if __name__ == "__main__":
    main()
