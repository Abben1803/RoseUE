#!/usr/bin/env python3
"""
build_gear_manifest.py — Phase 1 of the all-gear-from-Arua import.

Enumerates every real (non-empty) item in the Arua gear + back ZSCs, resolves
each to its part list (mesh file + texture + material flags), and DEDUPES:
  - unique skeletal meshes  (what actually gets imported to UE)
  - unique textures         (packed into 2048 atlases, 256 x 128px per atlas)

Writes tools/_tmp/gear_manifest.json:
  { "meshes":   [ "3ddata/.../x.zms", ... ],              # index = mesh asset id
    "textures": [ "3ddata/.../x.dds", ... ],              # index = tex id
    "atlas":    { tex_id: [atlas_page, u0,v0,u1,v1] },    # atlas cell per texture
    "items":    { "F/BODY/123": [ {mesh, tex, two_side, alpha, ...}, ... ] } }

Offline (no UE), fast.  Env: ROSE_ASSET_ROOT (default client/3ddata).
"""
import json
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _TOOLS)
from rose_parser.formats import zsc as zscmod

ROOT = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
OUT = os.path.join(_TOOLS, "_tmp", "gear_manifest.json")
ATLAS_PX, CELL_PX = 2048, 128
PER_AXIS = ATLAS_PX // CELL_PX          # 16
PER_ATLAS = PER_AXIS * PER_AXIS          # 256

# (gender, slot) -> ZSC relative path.  BACK is gender-neutral (one ZSC).
SLOTS = [
    ("F", "BODY", "AVATAR/LIST_WBODY.ZSC"), ("M", "BODY", "AVATAR/LIST_MBODY.ZSC"),
    ("F", "ARMS", "AVATAR/LIST_WARMS.ZSC"), ("M", "ARMS", "AVATAR/LIST_MARMS.ZSC"),
    ("F", "FOOT", "AVATAR/LIST_WFOOT.ZSC"), ("M", "FOOT", "AVATAR/LIST_MFOOT.ZSC"),
    ("F", "CAP",  "AVATAR/LIST_WCAP.ZSC"),  ("M", "CAP",  "AVATAR/LIST_MCAP.ZSC"),
    ("N", "BACK", "AVATAR/LIST_BACK.ZSC"),  ("N", "SUBWPN", "WEAPON/LIST_SUBWPN.ZSC"),
    ("N", "WEAPON", "WEAPON/LIST_WEAPON.ZSC")
]

mesh_ids, tex_ids = {}, {}
meshes, textures = [], []
items = {}


def mesh_id(path):
    k = path.lower().replace("\\", "/")
    if k not in mesh_ids:
        mesh_ids[k] = len(meshes); meshes.append(k)
    return mesh_ids[k]


def tex_id(path):
    k = path.lower().replace("\\", "/")
    if k not in tex_ids:
        tex_ids[k] = len(textures); textures.append(k)
    return tex_ids[k]


for gender, slot, rel in SLOTS:
    p = os.path.join(ROOT, *rel.split("/"))
    if not os.path.exists(p):
        print(f"[manifest] MISSING {rel} — skipped"); continue
    z = zscmod.parse(p)
    for iid, model in enumerate(z.models):
        if not model.parts:
            continue
        parts = []
        for part in model.parts:
            if not (0 <= part.mesh_id < len(z.mesh_files)):
                continue
            mat = z.materials[part.material_id] if 0 <= part.material_id < len(z.materials) else None
            parts.append({
                "mesh": mesh_id(z.mesh_files[part.mesh_id]),
                "tex": tex_id(mat.texture_path) if mat else -1,
                "two_side": bool(mat.is_2side) if mat else False,
                "alpha": bool(mat.is_alpha) if mat else False,
                "alpha_test": int(mat.alpha_test) if mat else 0,
            })
        if parts:
            items[f"{gender}/{slot}/{iid}"] = parts

# assign each texture to an atlas cell (row-major)
atlas = {}
for tid in range(len(textures)):
    page, cell = divmod(tid, PER_ATLAS)
    cy, cx = divmod(cell, PER_AXIS)
    u0, v0 = cx / PER_AXIS, cy / PER_AXIS
    atlas[tid] = [page, u0, v0, u0 + 1.0 / PER_AXIS, v0 + 1.0 / PER_AXIS]

os.makedirs(os.path.dirname(OUT), exist_ok=True)
json.dump({"meshes": meshes, "textures": textures, "atlas": atlas, "items": items},
          open(OUT, "w"), separators=(",", ":"))

n_atlas = (len(textures) + PER_ATLAS - 1) // PER_ATLAS
print(f"[manifest] items={len(items)}  unique_meshes={len(meshes)}  "
      f"unique_textures={len(textures)}  atlases={n_atlas}")
print(f"[manifest] wrote {OUT} ({os.path.getsize(OUT)//1024} KB)")
