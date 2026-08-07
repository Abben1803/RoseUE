#!/usr/bin/env python3
"""
gen_map_texture_manifest.py — Texture manifest for a zone's map scene
(atlas + master-material pass over /Game/Maps/<ZONE>/Scene).

Sources:
  mapforge/exports/<ZONE>.glb.materials.json — material NAME -> texture_src +
    mode/twosided/kind (the imported material assets carry these names).
  Zone ZSC packs (LIST_ZONE cols 11/12) + LIST_MORPH_OBJECT.STB — which ZMS
    meshes use each texture, for UV-bounds tiling detection.
Terrain tile textures are always atlasable (mapforge emits per-patch UVs in
[0,1] — export_map.py build_terrain_tile).  Water (kind='water') and untextured
color materials are skipped (special-purpose master per policy).

Output: tools/_tmp/map_texture_manifest_<ZONE>.json
  { "textures": {src: {"tiled": bool, "category": "map_<zone>"}},
    "materials": [ {name, texture_src, mode, twosided} ] }

Usage:  py -3.9 gen_map_texture_manifest.py [ZONE=JPT01]
Then:   py -3.9 build_texture_atlases.py _tmp/map_texture_manifest_<ZONE>.json \
                                          _tmp/map_atlas_manifest_<ZONE>.json
"""
import json
import os
import sys

_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.stb import parse as parse_stb
from rose_parser.formats.zsc import parse as parse_zsc
from rose_parser.formats.zms import parse as parse_zms

# Asset source: Arua (CLAUDE.md "Asset source: Arua, always").  Was the
# deprecated SourceAssets/3DDATA classic tree; ZSC object ids and STB row
# ids do not line up across eras, so a row resolved to different content
# than the Arua-era DataTables this feeds.  Output roots are unchanged.
SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
# Mesh/texture resolution root.  MUST match mapforge's ASSET_ROOT, which is now
# Arua (mapforge/config.py).  This was hardcoded to the classic client and is
# how JPT01 got imported from the classic tree.  No model asset resolves from
# classic any more -- see CLAUDE.md "Asset source: Arua, always".
CLIENT_3D = os.environ.get("MAPFORGE_ASSET_ROOT", SRC)   # mapforge ASSET_ROOT


def norm(p):
    return os.path.normcase(os.path.normpath(p))


def resolve(rel):
    """'3DDATA\\x' relative -> abs under the mapforge asset root."""
    parts = [q for q in rel.replace("\\", "/").split("/") if q]
    if parts and parts[0].upper() == "3DDATA":
        parts = parts[1:]
    return os.path.join(CLIENT_3D, *parts)


_zms_cache = {}
def uv_ok(zms_abs):
    if zms_abs not in _zms_cache:
        try:
            z = parse_zms(zms_abs)
            ok = True
            for v in z.vertices:
                if v.uvs:
                    u, w = v.uvs[0]
                    if not (-0.01 <= u <= 1.01 and -0.01 <= w <= 1.01):
                        ok = False
                        break
            _zms_cache[zms_abs] = ok
        except Exception:
            _zms_cache[zms_abs] = True
    return _zms_cache[zms_abs]


def main():
    zone = (sys.argv[1] if len(sys.argv) > 1 else "JPT01").upper()
    mats_json = os.path.join(os.path.normpath(os.path.join(_T, "..")),
                             "mapforge", "exports", f"{zone}.glb.materials.json")
    with open(mats_json, encoding="utf-8") as f:
        materials = json.load(f)["materials"]

    # texture_src -> ZMS users, from the zone's deco/cnst packs + morph STB.
    zones = parse_stb(os.path.join(SRC, "STB", "LIST_ZONE.STB"))
    row = next(i for i in range(zones.num_rows())
               if zone in (zones.get(i, 1) + zones.get(i, 2)).upper())
    tex_users = {}
    for col in (11, 12):
        rel = zones.get(row, col)
        if not rel.strip():
            continue
        try:
            zsc = parse_zsc(resolve(rel))
        except Exception as e:
            print(f"[mapman] pack {rel}: {e}")
            continue
        for m in zsc.models:
            for p in m.parts:
                if p.mesh_id >= len(zsc.mesh_files) or p.material_id >= len(zsc.materials):
                    continue
                tex = zsc.materials[p.material_id].texture_path
                if tex:
                    tex_users.setdefault(norm(resolve(tex)), []).append(
                        resolve(zsc.mesh_files[p.mesh_id]))
    mstb_path = os.path.join(SRC, "STB", "LIST_MORPH_OBJECT.STB")
    if os.path.isfile(mstb_path):
        mstb = parse_stb(mstb_path)
        for r in range(mstb.num_rows()):
            mesh, tex = mstb.get(r, 1), mstb.get(r, 3)
            if mesh and tex:
                tex_users.setdefault(norm(resolve(tex)), []).append(resolve(mesh))

    textures = {}
    out_mats = []
    skipped = {"water": 0, "untextured": 0}
    for e in materials:
        src = e.get("texture_src")
        if e.get("kind") == "water":
            skipped["water"] += 1
            continue
        if not src:
            skipped["untextured"] += 1
            continue
        # Terrain materials carry BOTH tile layers (kind='terrain'): the bottom
        # map in texture_src and the top map — whose ALPHA is the blend weight —
        # in texture_src2. Both must land in the atlas.
        src2 = e.get("texture_src2")
        for s in (src, src2):
            if not s:
                continue
            users = tex_users.get(norm(s), [])
            # No ZSC users -> terrain tile (mapforge terrain UVs are per-patch
            # [0,1]) -> atlasable.
            tiled = any(not uv_ok(u) for u in users)
            t = textures.setdefault(s, {"tiled": False, "category": f"map_{zone.lower()}"})
            if tiled:
                t["tiled"] = True
        out_mats.append({"name": e["name"], "texture_src": src,
                         "texture_src2": src2,
                         "kind": e.get("kind"),
                         "mode": e.get("mode", "OPAQUE"),
                         "twosided": bool(e.get("twosided")),
                         "alpha_ref": e.get("alpha_ref", 128),
                         "blend_type": e.get("blend_type", 0),
                         "alpha_value": e.get("alpha_value", 1.0)})

    out = os.path.join(_T, "_tmp", f"map_texture_manifest_{zone}.json")
    with open(out, "w", encoding="utf-8") as f:
        json.dump({"textures": textures, "materials": out_mats}, f, indent=1)
    tiled = sum(1 for t in textures.values() if t["tiled"])
    print(f"[mapman] {zone}: {len(out_mats)} materials, {len(textures)} textures "
          f"({tiled} tiled), skipped {skipped} -> {out}")


if __name__ == "__main__":
    main()
