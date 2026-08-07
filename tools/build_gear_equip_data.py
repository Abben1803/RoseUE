#!/usr/bin/env python3
"""
build_gear_equip_data.py — Phase 6 data: the equip resolution table.

From tools/_tmp/gear_manifest.json, emit a compact JSON the runtime equip system
loads to turn (gender, slot, id) into its visual parts:

  Content/DataTables/gear_equip.json
  { "F/BODY/123": [ {"mesh":1204,"page":5,"u0":..,"v0":..,"u1":..,"v1":..,
                     "two":true,"alpha":true}, ... ], ... }

mesh -> imported /Game/Characters/Gear/mesh_<mesh> ; page -> gear_atlas_<page> ;
u/v -> the atlas cell the gear master material's UVTransform maps onto.
Offline.
"""
import json
import os

_TOOLS = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(_TOOLS, "_tmp", "gear_manifest.json")
OUT = os.path.normpath(os.path.join(
    _TOOLS, "..", "unreal-engine rose", "RoseUE", "Content", "DataTables", "gear_equip.json"))

man = json.load(open(MANIFEST))
atlas = {int(k): v for k, v in man["atlas"].items()}

out = {}
for key, parts in man["items"].items():
    rows = []
    for p in parts:
        cell = atlas.get(p["tex"])
        if cell is None:
            page, u0, v0, u1, v1 = 0, 0.0, 0.0, 1.0, 1.0
        else:
            page, u0, v0, u1, v1 = cell
        rows.append({"mesh": p["mesh"], "page": page,
                     "u0": round(u0, 6), "v0": round(v0, 6),
                     "u1": round(u1, 6), "v1": round(v1, 6),
                     "two": bool(p["two_side"]), "alpha": bool(p["alpha"]),
                     # zz_material: masked only when alpha AND alpha_test
                     "atest": bool(p.get("alpha_test"))})
    out[key] = rows

os.makedirs(os.path.dirname(OUT), exist_ok=True)
json.dump(out, open(OUT, "w"), separators=(",", ":"))
print(f"[equip] {len(out)} items -> {OUT} ({os.path.getsize(OUT)//1024} KB)")
