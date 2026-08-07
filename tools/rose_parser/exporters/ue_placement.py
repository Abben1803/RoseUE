"""Export IFO placement data to JSON actor-placement manifests for UE5.

Each object in an IFO lump becomes a JSON record with:
  - type: lump name (object/mob/construction/effect/regen/warp)
  - obj_id: row index into relevant data table (ZSC model list or STB)
  - position: [x, y, z] in UE coordinate space (cm, Z-up, left-hand)
  - rotation: [w, x, y, z] quaternion
  - scale: [x, y, z]
  - chunk: [cx, cy] chunk coordinates
  - event_id, warp_id for connectivity

ROSE → UE coordinate transform:
  ROSE: right-hand, Y-forward, Z-up, centimeters
  UE5:  left-hand, X-forward, Z-up, centimeters
  Transform: UE_x = ROSE_y, UE_y = ROSE_x, UE_z = ROSE_z
  Rotation: negate quaternion y-component to flip handedness
"""
import json
import os
from typing import Dict, List, Tuple

from ..formats.ifo import IFO, IFOObject, LUMP_NAMES


def _rose_to_ue_pos(pos: Tuple[float, float, float]) -> List[float]:
    x, y, z = pos
    return [y, x, z]


def _rose_to_ue_quat(q: Tuple[float, float, float, float]) -> List[float]:
    w, x, y, z = q
    # Flip handedness: negate y
    return [w, -y, x, z]  # (w, qx, qy, qz) in UE convention


def object_to_dict(obj: IFOObject, lump_type: int, chunk_cx: int, chunk_cy: int) -> Dict:
    return {
        "type": LUMP_NAMES.get(lump_type, f"lump_{lump_type}"),
        "name": obj.name,
        "obj_type": obj.obj_type,
        "obj_id": obj.obj_id,
        "warp_id": obj.warp_id,
        "event_id": obj.event_id,
        "map_pos": [obj.map_x, obj.map_y],
        "chunk": [chunk_cx, chunk_cy],
        "position": _rose_to_ue_pos(obj.position),
        "rotation": _rose_to_ue_quat(obj.rotation),
        "scale": list(obj.scale),
        "extra": obj.extra,
    }


def export_ifo(ifo: IFO, out_path: str,
               chunk_cx: int = 0, chunk_cy: int = 0) -> None:
    """Export a single IFO file's objects to JSON."""
    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    records: List[Dict] = []

    for ltype, obj_list in ifo.objects.items():
        for obj in obj_list:
            records.append(object_to_dict(obj, ltype, chunk_cx, chunk_cy))

    output = {
        "chunk": [chunk_cx, chunk_cy],
        "map_info": None,
        "objects": records,
    }
    if ifo.map_info:
        mi = ifo.map_info
        output["map_info"] = {
            "bg_music_id": mi.bg_music_id,
            "sky_id": mi.sky_id,
            "respawn_pos": _rose_to_ue_pos(mi.respawn_pos),
        }

    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(output, f, indent=2)


def export_zone_placements(chunks: List[Dict], out_path: str) -> None:
    """Merge all IFO objects from a zone's chunks into a single JSON manifest.

    Args:
        chunks: list from AssetResolver.chunk_files()
        out_path: output .json path
    """
    from ..formats import ifo as ifo_mod

    all_objects: List[Dict] = []
    meta: List[Dict] = []

    for chunk in chunks:
        cx, cy = chunk["cx"], chunk["cy"]
        ifo_path = chunk.get("ifo")
        if not ifo_path:
            continue
        try:
            ifo_data = ifo_mod.parse(ifo_path)
        except Exception as e:
            print(f"  [warn] IFO {ifo_path}: {e}")
            continue

        chunk_meta: Dict = {"chunk": [cx, cy]}
        if ifo_data.map_info:
            mi = ifo_data.map_info
            chunk_meta["map_info"] = {
                "bg_music_id": mi.bg_music_id,
                "sky_id": mi.sky_id,
                "respawn_pos": _rose_to_ue_pos(mi.respawn_pos),
            }
        meta.append(chunk_meta)

        for ltype, obj_list in ifo_data.objects.items():
            for obj in obj_list:
                all_objects.append(object_to_dict(obj, ltype, cx, cy))

    os.makedirs(os.path.dirname(out_path) or ".", exist_ok=True)
    output = {
        "chunks": meta,
        "total_objects": len(all_objects),
        "objects": all_objects,
    }
    with open(out_path, "w", encoding="utf-8") as f:
        json.dump(output, f, indent=2)
