"""
ue5_probe_mesh_build.py — Inspect build state of imported map-scene static
meshes: Nanite on/off, LOD count, vertex/tri counts, bounds size and origin
magnitude. Answers "why are there spaces between terrain patches".

Env:
  ROSE_MAP_ZONE   zone key (default JPT01)
  ROSE_PROBE_OUT  output json (default tools/mesh_build_probe.json)
"""
import os
import json
import unreal

ZONE = os.environ.get("ROSE_MAP_ZONE", "JPT01")
OUT = os.environ.get("ROSE_PROBE_OUT",
                     os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                  "mesh_build_probe.json"))
SCENE = f"/Game/Maps/{ZONE}/Scene"

EAL = unreal.EditorAssetLibrary

rows = []
paths = [p for p in EAL.list_assets(SCENE, recursive=True)]
unreal.log(f"[buildprobe] {len(paths)} assets under {SCENE}")

n = 0
for p in paths:
    asset = EAL.load_asset(p)
    if not isinstance(asset, unreal.StaticMesh):
        continue
    n += 1
    row = {"name": asset.get_name()}
    try:
        ns = asset.get_editor_property("nanite_settings")
        row["nanite"] = bool(ns.get_editor_property("enabled"))
    except Exception as e:
        row["nanite_err"] = str(e)
    try:
        row["lods"] = asset.get_num_lods()
    except Exception:
        try:
            row["lods"] = asset.get_editor_property("num_lods")
        except Exception:
            row["lods"] = None
    try:
        row["verts_lod0"] = asset.get_num_vertices(0)
        row["tris_lod0"] = asset.get_num_triangles(0)
    except Exception:
        pass
    try:
        b = asset.get_bounds()
        o, e = b.origin, b.box_extent
        row["bounds_origin"] = [round(o.x, 1), round(o.y, 1), round(o.z, 1)]
        row["bounds_extent"] = [round(e.x, 1), round(e.y, 1), round(e.z, 1)]
    except Exception:
        pass
    # LOD screen sizes + reduction (auto-LOD evidence)
    try:
        row["lod_screen_sizes"] = [
            round(asset.get_editor_property("lod_screen_size")[i], 4)
            for i in range(row.get("lods") or 0)]
    except Exception:
        pass
    rows.append(row)
    if n <= 8:
        unreal.log(f"[buildprobe] {row}")

nanite_on = sum(1 for r in rows if r.get("nanite"))
multi_lod = sum(1 for r in rows if (r.get("lods") or 0) > 1)
unreal.log(f"[buildprobe] static meshes: {len(rows)}, nanite ON: {nanite_on}, LODs>1: {multi_lod}")

with open(OUT, "w") as f:
    json.dump({"zone": ZONE, "meshes": rows,
               "summary": {"count": len(rows), "nanite_on": nanite_on,
                           "multi_lod": multi_lod}}, f)
unreal.log(f"[buildprobe] wrote {OUT}")
