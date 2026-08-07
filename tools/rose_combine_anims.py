#!/usr/bin/env python3
"""
rose_combine_anims.py  —  Build ONE GLB containing a skeleton + reference mesh +
                          many ZMO animations as separate glTF animation tracks.

Why combined?  UE5 Interchange refuses to import an animation-only glTF with the
default pipeline ("nothing to import").  By bundling a reference body mesh, the
file imports as a normal SkeletalMesh — and every animation track in the file
becomes an AnimSequence on that mesh's single shared Skeleton.  One import call
produces the shared skeleton + all AnimSequences, correctly linked.

Usage (programmatic):
    from rose_combine_anims import convert
    convert(zmd_path, ref_zms_path, [(name, zmo_path), ...], "FEMALE_anims.glb")
"""
import io
import os
import struct
import sys
from typing import List, Tuple

_TOOLS = os.path.dirname(os.path.abspath(__file__))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)


def _dds_to_png(dds_path: str) -> bytes:
    """Decode a ROSE DDS (DXT-compressed) and re-encode as PNG bytes."""
    from PIL import Image
    im = Image.open(dds_path).convert("RGBA")
    buf = io.BytesIO()
    im.save(buf, format="PNG")
    return buf.getvalue()

from rose_parser.formats.zmd import parse as parse_zmd
from rose_parser.formats.zms import parse as parse_zms
from rose_parser.formats.zmo import parse as parse_zmo, CTYPE_POSITION, CTYPE_ROTATION

from rose_to_gltf import (
    _GLBBuilder, _bone_world_mats, _mat4_rigid_inv,
    _pos_r2g, _pos_r2g_cm, _quat_r2g, _build_primitive,
    _pack_f32, COMP_FLOAT,
)


def _add_animation(g: _GLBBuilder, zmd_bone_count: int, name: str, zmo_path: str) -> bool:
    """Append one ZMO as a glTF animation entry.  Returns False if it has no usable channels."""
    zmo = parse_zmo(zmo_path)
    fps = max(zmo.fps, 1)
    n_frames = zmo.num_frames
    if n_frames <= 0:
        return False

    # Shared time input for this animation
    times = [i / fps for i in range(n_frames)]
    times_data = struct.pack(f'<{len(times)}f', *times)
    times_bv   = g._add_bview(times_data)
    times_acc  = g._add_acc(times_bv, COMP_FLOAT, len(times), "SCALAR",
                            [times[0]], [times[-1]])

    samplers = []
    channels = []

    for ch in zmo.channels:
        bone_idx = ch.refer_id
        if bone_idx >= zmd_bone_count:
            continue

        if ch.channel_type == CTYPE_ROTATION:
            frames = [_quat_r2g(*f) for f in ch.frames]
            out_bv  = g._add_bview(_pack_f32(frames))
            out_acc = g._add_acc(out_bv, COMP_FLOAT, len(frames), "VEC4")
            path = "rotation"

        elif ch.channel_type == CTYPE_POSITION:
            frames = [_pos_r2g_cm(*f) for f in ch.frames]
            xs = [f[0] for f in frames]; ys = [f[1] for f in frames]; zs = [f[2] for f in frames]
            out_bv  = g._add_bview(_pack_f32(frames))
            out_acc = g._add_acc(out_bv, COMP_FLOAT, len(frames), "VEC3",
                                 [min(xs), min(ys), min(zs)], [max(xs), max(ys), max(zs)])
            path = "translation"
        else:
            continue

        sidx = len(samplers)
        samplers.append({"input": times_acc, "output": out_acc, "interpolation": "LINEAR"})
        channels.append({"sampler": sidx, "target": {"node": bone_idx, "path": path}})

    if not samplers:
        return False

    g.animations.append({"name": name, "samplers": samplers, "channels": channels})
    return True


def convert(zmd_path: str, ref_zms_paths, anims: List[Tuple[str, str]],
            output_path: str) -> None:
    """
    zmd_path      : skeleton
    ref_zms_paths : list of body-part ZMS (body/face/hair/foot) combined into the
                    reference mesh.  Each becomes a primitive (section) of one
                    skeletal mesh, so the imported asset is a full animated body
                    on the shared skeleton.  A single str is also accepted.
    anims         : list of (anim_name, zmo_path)
    output_path   : .glb to write
    """
    if isinstance(ref_zms_paths, str):
        ref_zms_paths = [ref_zms_paths]
    zmd = parse_zmd(zmd_path)
    g = _GLBBuilder()
    n_bones = len(zmd.bones)

    # ── bone nodes ────────────────────────────────────────────────────────────
    world_mats = _bone_world_mats(zmd)
    children_of = {i: [] for i in range(n_bones)}
    roots = []
    for bone in zmd.bones:
        if bone.parent_id == bone.index:
            roots.append(bone.index)
        else:
            children_of[bone.parent_id].append(bone.index)

    for bone in zmd.bones:
        tx, ty, tz = _pos_r2g(*bone.translation)
        gx, gy, gz, gw = _quat_r2g(*bone.rotation)
        node = {"name": bone.name, "translation": [tx, ty, tz], "rotation": [gx, gy, gz, gw]}
        kids = children_of[bone.index]
        if kids:
            node["children"] = list(kids)
        g.nodes.append(node)

    # ── skin ──────────────────────────────────────────────────────────────────
    ibm_acc = g.add_f32_mat4([_mat4_rigid_inv(m) for m in world_mats])
    g.skins.append({
        "name": os.path.splitext(os.path.basename(zmd_path))[0],
        "skeleton": roots[0],
        "joints": list(range(n_bones)),
        "inverseBindMatrices": ibm_acc,
    })
    g.scene_nodes = list(roots)

    # ── reference mesh (one primitive/section per part) ───────────────────────
    # Each entry is a dict: {path, pin (bone or None), texture (abs dds path or
    # None), alpha (bool), two_side (bool)}.  Legacy str / (path,pin) also accepted.
    primitives = []
    ref_verts = 0
    for entry in ref_zms_paths:
        pin_dummy = None
        pin_xform = None
        if isinstance(entry, dict):
            rp = entry["path"]; pin_bone = entry.get("pin") or 0
            pin_dummy = entry.get("pin_dummy")
            pin_xform = entry.get("pin_xform")
            tex = entry.get("texture"); alpha = entry.get("alpha", False)
            two_side = entry.get("two_side", False)
            blink = entry.get("blink", False)
        elif isinstance(entry, tuple):
            rp, pin_bone = entry; tex = None; alpha = False; two_side = False; blink = False
        else:
            rp = entry; pin_bone = 0; tex = None; alpha = False; two_side = False; blink = False

        try:
            zms = parse_zms(rp)
        except Exception as e:
            print(f"    [skip ref] {rp}: {e}")
            continue

        # ROSE blinks by clipping the face's eye material groups: the FIRST group
        # is the closed-eye overlay, the LAST is the open-eye overlay, the middle
        # is the always-drawn face (see zz_model.cpp / zz_renderer_d3d.cpp).  Split
        # the face into those groups as separate, distinctly-named UE sections so
        # the runtime can toggle them.  Needs exactly the >=3-group layout.
        base = os.path.splitext(os.path.basename(rp))[0]
        groups = None
        if blink and tex and os.path.isfile(tex) and zms.mat_ids and len(zms.mat_ids) >= 3:
            off = 0
            spans = []
            for m in zms.mat_ids:
                spans.append((off, m.num_faces)); off += m.num_faces
            groups = [
                (spans[0],  f"{base}_eyeclosed"),   # first group = closed-eye
                ((spans[1][0], off - spans[1][0] - spans[-1][1]), f"{base}_main"),
                (spans[-1], f"{base}_eyeopen"),     # last group = open-eye
            ]

        # Build material(s) from the DDS texture (if available)
        mat_idx = None
        eye_mats = None
        if tex and os.path.isfile(tex):
            try:
                png = _dds_to_png(tex)
                if groups:
                    # same texture, 3 distinct materials → 3 toggleable sections
                    eye_mats = [g.add_png_material(png, name=nm, alpha_mask=alpha,
                                                   double_sided=True) for _, nm in groups]
                else:
                    mat_idx = g.add_png_material(png, name=base,
                                                 alpha_mask=alpha, double_sided=two_side)
            except Exception as e:
                print(f"    [tex fail] {tex}: {e}")
        elif tex:
            print(f"    [tex missing] {tex}")

        pin_matrix = world_mats[pin_bone] if 0 <= pin_bone < len(world_mats) else None
        # Cap-style parts: position at a head DUMMY (top of head), still skinned
        # to the head bone.  pin_matrix = parent_bone_world * dummy_local.
        if pin_dummy is not None and pin_dummy < len(zmd.dummies):
            dm = zmd.dummies[pin_dummy]
            dq = _quat_r2g(*dm.rotation) if dm.rotation else (0.0, 0.0, 0.0, 1.0)
            dt = _pos_r2g(*dm.translation)
            from rose_to_gltf import _mat4_from_tq, _mat4_mul
            dummy_local = _mat4_from_tq(dt[0], dt[1], dt[2], dq[0], dq[1], dq[2], dq[3])
            parent = dm.parent_id if 0 <= dm.parent_id < len(world_mats) else pin_bone
            pin_bone = parent
            pin_matrix = _mat4_mul(world_mats[parent], dummy_local)
        # Weapons: the ZSC part carries a local grip transform (orients the mesh
        # in the hand) applied AFTER the dummy: vertex_world = dummy_world · part.
        if pin_xform is not None:
            from rose_to_gltf import _mat4_from_tq, _mat4_mul
            pt = _pos_r2g(*pin_xform["pos"])
            grq = _quat_r2g(*pin_xform["rot"])          # ZSC quat (w,x,y,z) → glTF (x,y,z,w)
            part_local = _mat4_from_tq(pt[0], pt[1], pt[2], grq[0], grq[1], grq[2], grq[3])
            sx, sy, sz = pin_xform.get("scale", (1.0, 1.0, 1.0))
            if (sx, sy, sz) != (1.0, 1.0, 1.0):
                for ci, sf in zip((0, 1, 2), (sx, sy, sz)):
                    part_local[ci*4+0] *= sf; part_local[ci*4+1] *= sf; part_local[ci*4+2] *= sf
            # Grip orientation correction (weapons): in our converted frame the ZSC
            # grip leaves the blade ~90° off the hand axis, so the held weapon points
            # sideways instead of along the hand.  ROSE_GRIP_FIX = "x,y,z,w" quat,
            # pre-multiplied (rotates the weapon in the HAND-local frame) to bring the
            # blade in line.  Tunable so we can dial it in.
            _corr = os.environ.get("ROSE_GRIP_FIX", "").strip()
            if _corr:
                cq = [float(t) for t in _corr.split(",")]
                Rc = _mat4_from_tq(0.0, 0.0, 0.0, cq[0], cq[1], cq[2], cq[3])
                part_local = _mat4_mul(Rc, part_local)
            pin_matrix = part_local if pin_matrix is None else _mat4_mul(pin_matrix, part_local)
        if groups and eye_mats:
            # 3 sections: closed-eye, main, open-eye (order matters for blink).
            for (span, _nm), m_idx in zip(groups, eye_mats):
                prim = _build_primitive(g, zms, pin_bone=pin_bone, pin_matrix=pin_matrix,
                                        material_index=m_idx, face_range=span)
                if prim is not None:
                    primitives.append(prim)
            ref_verts += len(zms.vertices)
        else:
            prim = _build_primitive(g, zms, pin_bone=pin_bone, pin_matrix=pin_matrix,
                                    material_index=mat_idx)
            if prim is not None:
                primitives.append(prim)
                ref_verts += len(zms.vertices)
    if primitives:
        mesh_idx  = len(g.meshes)
        mesh_node = len(g.nodes)
        g.meshes.append({"name": "RefBody", "primitives": primitives})
        g.nodes.append({"name": "RefBody", "mesh": mesh_idx, "skin": 0})
        g.scene_nodes.append(mesh_node)

    # ── animations ────────────────────────────────────────────────────────────
    ok = 0
    for name, zmo_path in anims:
        try:
            if _add_animation(g, n_bones, name, zmo_path):
                ok += 1
        except Exception as e:
            print(f"    [skip anim] {name}: {e}")

    with open(output_path, 'wb') as f:
        f.write(g.build_glb())
    print(f"  → {output_path}  ({n_bones} bones, {len(primitives)} parts, "
          f"{ok}/{len(anims)} anims, {ref_verts} ref verts)")
