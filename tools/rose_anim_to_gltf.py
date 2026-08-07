#!/usr/bin/env python3
"""
rose_anim_to_gltf.py  —  Convert ROSE ZMO animation + ZMD skeleton to glTF 2.0 GLB.

The output GLB contains the full bone hierarchy (matching the skeletal mesh GLBs
produced by rose_to_gltf.py) plus a glTF animation track.  When importing into
UE5 via Interchange, choose "Import Animation Only" and point it at the same
Skeleton asset created from the skeletal-mesh import.

Usage:
    python rose_anim_to_gltf.py FEMALE.ZMD AVATAR_IDLE_F1.ZMO -o idle_f.glb
"""
import argparse
import json
import os
import struct
import sys
from typing import List

_TOOLS = os.path.dirname(os.path.abspath(__file__))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from rose_parser.formats.zmd import parse as parse_zmd, ZMD
from rose_parser.formats.zmo import parse as parse_zmo, ZMO, CTYPE_POSITION, CTYPE_ROTATION

# Re-use helpers from rose_to_gltf
from rose_to_gltf import (
    _GLBBuilder, _bone_world_mats, _mat4_rigid_inv,
    _pos_r2g, _pos_r2g_cm, _quat_r2g,
    _pack_f32, _pad4, SCALE,
    COMP_FLOAT,
)


def convert(zmd_path: str, zmo_path: str, output_path: str) -> None:
    """Convert one ZMO animation into a GLB that UE5 can import as AnimSequence."""
    zmd = parse_zmd(zmd_path)
    zmo = parse_zmo(zmo_path)
    g   = _GLBBuilder()

    # ── 1. Bone nodes (same structure as rose_to_gltf) ───────────────────────
    world_mats = _bone_world_mats(zmd)
    children_of = {i: [] for i in range(len(zmd.bones))}
    roots = []
    for bone in zmd.bones:
        if bone.parent_id == bone.index:
            roots.append(bone.index)
        else:
            children_of[bone.parent_id].append(bone.index)

    for bone in zmd.bones:
        tx, ty, tz = _pos_r2g(*bone.translation)
        gx, gy, gz, gw = _quat_r2g(*bone.rotation)
        node = {
            "name": bone.name,
            "translation": [tx, ty, tz],
            "rotation":    [gx, gy, gz, gw],
        }
        kids = children_of[bone.index]
        if kids:
            node["children"] = [k for k in kids]
        g.nodes.append(node)

    # ── 2. Skin (required so UE5 links animation to skeleton) ────────────────
    ibm_mats = [_mat4_rigid_inv(m) for m in world_mats]
    ibm_acc  = g.add_f32_mat4(ibm_mats)
    g.skins.append({
        "name": os.path.basename(zmd_path),
        "skeleton": roots[0],
        "joints":   list(range(len(zmd.bones))),
        "inverseBindMatrices": ibm_acc,
    })
    g.scene_nodes = [r for r in roots]

    # ── 3. Build animation ────────────────────────────────────────────────────
    fps       = max(zmo.fps, 1)
    n_frames  = zmo.num_frames
    duration  = (n_frames - 1) / fps if n_frames > 1 else 0.0

    # Time input accessor (shared by all channels)
    times = [i / fps for i in range(n_frames)]
    times_data = struct.pack(f'<{len(times)}f', *times)
    times_bv   = g._add_bview(times_data)
    times_acc  = g._add_acc(times_bv, COMP_FLOAT, len(times), "SCALAR",
                             [times[0]], [times[-1]])

    samplers = []
    channels = []

    for ch in zmo.channels:
        bone_idx = ch.refer_id
        if bone_idx >= len(zmd.bones):
            continue

        if ch.channel_type == CTYPE_ROTATION:
            # rotation frames: ROSE (w,x,y,z) → glTF (x,z,-y,w)
            frames_gltf = [_quat_r2g(*f) for f in ch.frames]
            out_data    = _pack_f32(frames_gltf)
            out_bv      = g._add_bview(out_data)
            out_acc     = g._add_acc(out_bv, COMP_FLOAT, len(frames_gltf), "VEC4")
            path        = "rotation"

        elif ch.channel_type == CTYPE_POSITION:
            # position frames: ROSE cm → glTF metres, axis-swapped
            frames_gltf = [_pos_r2g_cm(*f) for f in ch.frames]
            xs = [f[0] for f in frames_gltf]
            ys = [f[1] for f in frames_gltf]
            zs = [f[2] for f in frames_gltf]
            out_data = _pack_f32(frames_gltf)
            out_bv   = g._add_bview(out_data)
            out_acc  = g._add_acc(out_bv, COMP_FLOAT, len(frames_gltf), "VEC3",
                                   [min(xs), min(ys), min(zs)],
                                   [max(xs), max(ys), max(zs)])
            path = "translation"

        else:
            continue  # skip UV/alpha/scale channels for now

        sampler_idx = len(samplers)
        samplers.append({"input": times_acc, "output": out_acc, "interpolation": "LINEAR"})
        channels.append({
            "sampler": sampler_idx,
            "target": {"node": bone_idx, "path": path},
        })

    if samplers:
        anim_name = os.path.splitext(os.path.basename(zmo_path))[0]
        # Patch the raw JSON dict since _GLBBuilder doesn't have an animations list yet
        g._animations = [{"name": anim_name, "samplers": samplers, "channels": channels}]
    else:
        g._animations = []

    # ── 4. Write GLB ──────────────────────────────────────────────────────────
    # Monkey-patch build_glb to include animations
    orig_build = g.build_glb.__func__

    def _build_with_anim(self) -> bytes:
        gltf = {
            "asset": {"version": "2.0", "generator": "rose_anim_to_gltf"},
            "scene": 0,
            "scenes": [{"name": "Scene", "nodes": self.scene_nodes}],
            "nodes": self.nodes,
            "accessors": self._accs,
            "bufferViews": self._bviews,
            "buffers": [{"byteLength": len(self._buf)}],
        }
        if self.skins:
            gltf["skins"] = self.skins
        if self._animations:
            gltf["animations"] = self._animations

        json_bytes  = json.dumps(gltf, separators=(',', ':')).encode('utf-8')
        json_padded = _pad4(json_bytes, 0x20)
        bin_padded  = _pad4(bytes(self._buf), 0)
        chunks = (
            struct.pack('<II', len(json_padded), 0x4E4F534A) + json_padded +
            struct.pack('<II', len(bin_padded),  0x004E4942) + bin_padded
        )
        total  = 12 + len(chunks)
        header = struct.pack('<III', 0x46546C67, 2, total)
        return header + chunks

    import types
    g.build_glb = types.MethodType(_build_with_anim, g)

    glb = g.build_glb()
    with open(output_path, 'wb') as f:
        f.write(glb)
    print(f"  → {output_path}  ({len(zmd.bones)} bones, {n_frames} frames @ {fps} fps, "
          f"{len(samplers)} channels, {duration:.2f}s)")


def main():
    ap = argparse.ArgumentParser(description="Convert ROSE ZMO animation to glTF 2.0 GLB")
    ap.add_argument("zmd", help="Path to .ZMD skeleton file")
    ap.add_argument("zmo", help="Path to .ZMO animation file")
    ap.add_argument("-o", "--output", required=True, help="Output .glb path")
    args = ap.parse_args()
    convert(args.zmd, args.zmo, args.output)


if __name__ == "__main__":
    main()
