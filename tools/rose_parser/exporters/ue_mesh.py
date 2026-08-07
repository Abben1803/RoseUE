"""Export ROSE mesh/skeleton/animation data to UE5-importable JSON format.

Outputs an intermediate JSON that describes:
  - Static meshes (ZMS without skin)
  - Skeletal meshes (ZMS + ZMD skeleton)
  - Animations (ZMO motion data)

The JSON can then be imported into UE5 via:
  - Python scripting in the UE editor (unreal.EditorAssetLibrary)
  - Or used to generate FBX via fbx-python / pyfbx

Coordinate transform (ROSE → UE5):
  ROSE: right-hand, Y-forward (north), Z-up, centimeters
  UE5:  left-hand, X-forward, Z-up, centimeters
  pos: (rx, ry, rz) → (ry, rx, rz)
  quat: (rw, rx, ry, rz) → (rw, -ry, rx, rz)
  For animations, scale position by ZZ_SCALE_IN (1/100) already done in zmd.py
"""
import json
import os
from typing import Dict, List, Optional, Tuple

from ..formats.zms import ZMS, VF_NORMAL, VF_BLEND_WEIGHT
from ..formats.zmd import ZMD
from ..formats.zmo import ZMO, CTYPE_POSITION, CTYPE_ROTATION, CTYPE_SCALE


def _r2u_pos(p: Tuple[float, float, float]) -> List[float]:
    x, y, z = p
    return [y, x, z]


def _r2u_quat(q: Tuple[float, float, float, float]) -> List[float]:
    w, x, y, z = q
    return [w, -y, x, z]


def export_static_mesh(zms: ZMS, out_path: str,
                       material_paths: Optional[List[str]] = None) -> None:
    """Export a ZMS mesh without skeleton to a JSON descriptor."""
    verts = []
    for v in zms.vertices:
        entry: Dict = {
            "pos": _r2u_pos(v.position),
        }
        if v.normal:
            nx, ny, nz = v.normal
            entry["normal"] = [ny, nx, nz]
        if v.uvs:
            entry["uvs"] = [list(uv) for uv in v.uvs]
        if v.color:
            entry["color"] = list(v.color)
        verts.append(entry)

    faces = [list(f) for f in zms.faces]

    mat_sections = [
        {"mat_index": m.mat_index, "num_faces": m.num_faces}
        for m in zms.mat_ids
    ]

    out: Dict = {
        "type": "static_mesh",
        "version": zms.version,
        "bounds_min": _r2u_pos(zms.bounds_min),
        "bounds_max": _r2u_pos(zms.bounds_max),
        "vertices": verts,
        "faces": faces,
        "mat_sections": mat_sections,
        "material_paths": material_paths or [],
    }

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)


def export_skeletal_mesh(zms: ZMS, zmd: ZMD, out_path: str,
                         material_paths: Optional[List[str]] = None) -> None:
    """Export a skinned ZMS + ZMD skeleton to JSON."""
    bones = []
    for b in zmd.bones:
        bones.append({
            "index": b.index,
            "parent": b.parent_id,
            "name": b.name,
            "pos": _r2u_pos(b.translation),
            "rot": _r2u_quat(b.rotation),
        })
    for d in zmd.dummies:
        rot = _r2u_quat(d.rotation) if d.rotation else [1, 0, 0, 0]
        bones.append({
            "index": len(zmd.bones) + d.index,
            "parent": d.parent_id,
            "name": d.name,
            "pos": _r2u_pos(d.translation),
            "rot": rot,
            "is_dummy": True,
        })

    verts = []
    for v in zms.vertices:
        entry: Dict = {
            "pos": _r2u_pos(v.position),
        }
        if v.normal:
            nx, ny, nz = v.normal
            entry["normal"] = [ny, nx, nz]
        if v.uvs:
            entry["uvs"] = [list(uv) for uv in v.uvs]
        if v.blend_weights and v.blend_indices:
            # Remap local mesh bone slot → skeleton global bone index
            remap = zms.bone_indices
            gi = [remap[i] if i < len(remap) else 0 for i in v.blend_indices]
            entry["blend_weights"] = list(v.blend_weights)
            entry["blend_bone_indices"] = gi
        verts.append(entry)

    out: Dict = {
        "type": "skeletal_mesh",
        "version": zms.version,
        "skeleton": {"bones": bones},
        "bounds_min": _r2u_pos(zms.bounds_min),
        "bounds_max": _r2u_pos(zms.bounds_max),
        "vertices": verts,
        "faces": [list(f) for f in zms.faces],
        "mat_sections": [
            {"mat_index": m.mat_index, "num_faces": m.num_faces}
            for m in zms.mat_ids
        ],
        "material_paths": material_paths or [],
    }

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)


def export_animation(zmo: ZMO, zmd: ZMD, out_path: str,
                     anim_name: str = "anim") -> None:
    """Export a ZMO animation to a UE AnimSequence-compatible JSON.

    Each channel becomes a bone track. The output lists per-bone
    position/rotation/scale arrays indexed by frame number.
    """
    SCALE_IN = 1.0 / 100.0  # ZMO positions are raw engine units (cm)

    bone_tracks: Dict[int, Dict] = {}

    for ch in zmo.channels:
        bid = ch.refer_id
        if bid not in bone_tracks:
            bone_tracks[bid] = {"bone_id": bid, "pos": [], "rot": [], "scale": []}

        if ch.channel_type == CTYPE_POSITION:
            bone_tracks[bid]["pos"] = [_r2u_pos((x * SCALE_IN, y * SCALE_IN, z * SCALE_IN))
                                        for (x, y, z) in ch.frames]
        elif ch.channel_type == CTYPE_ROTATION:
            bone_tracks[bid]["rot"] = [_r2u_quat(q) for q in ch.frames]
        elif ch.channel_type == CTYPE_SCALE:
            bone_tracks[bid]["scale"] = [s for s in ch.frames]

    # Fill bone names from skeleton if available
    tracks_list = []
    for bid, track in sorted(bone_tracks.items()):
        bone_name = ""
        if bid < len(zmd.bones):
            bone_name = zmd.bones[bid].name
        track["bone_name"] = bone_name
        tracks_list.append(track)

    out: Dict = {
        "type": "animation",
        "name": anim_name,
        "fps": zmo.fps,
        "num_frames": zmo.num_frames,
        "duration": zmo.duration_seconds(),
        "bone_tracks": tracks_list,
    }

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(out, f, indent=2)
