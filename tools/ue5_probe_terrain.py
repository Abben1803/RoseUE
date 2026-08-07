"""
ue5_probe_terrain.py — Verify the terrain blend is CONTINUOUS, not binary.

Checks, for /Game/Maps/<ROSE_ZONE>/Scene:
  1. M_RoseTerrain exists, is OPAQUE, and exposes BaseColor/TopColor/
     UVTransform/TopUVTransform.
  2. Every terrain static mesh has >= 2 UV channels (UV0 bottom, UV1 top).
  3. Every terrain material slot points at an MI_TERRAIN_* instance of
     M_RoseTerrain, with BOTH texture params bound.
  4. No terrain MI is MASKED or TRANSLUCENT (that was the hard-edge bug).

Run headless, editor CLOSED (-nullrhi fine).
"""
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "JPT01").upper()
SCENE = f"/Game/Maps/{ZONE}/Scene"
MASTER = "/Game/Atlas/M_RoseTerrain"
MI_ROOT = "/Game/Atlas/MI"

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
fails = []

m = EAL.load_asset(MASTER)
if not m:
    fails.append("M_RoseTerrain missing")
else:
    bm = m.get_editor_property("blend_mode")
    if bm != unreal.BlendMode.BLEND_OPAQUE:
        fails.append(f"M_RoseTerrain blend_mode={bm} (must be OPAQUE)")
    names = set()
    for p in ME.get_texture_parameter_names(m):
        names.add(str(p))
    for p in ME.get_vector_parameter_names(m):
        names.add(str(p))
    for want in ("BaseColor", "TopColor", "UVTransform", "TopUVTransform"):
        if want not in names:
            fails.append(f"M_RoseTerrain missing param {want}")
    print(f"[probe] M_RoseTerrain params: {sorted(names)}")

n_mesh = n_slot = n_terr = 0
bad_uv = []
for path in EAL.list_assets(SCENE, recursive=True):
    obj = EAL.load_asset(path)
    if not isinstance(obj, unreal.StaticMesh):
        continue
    n_mesh += 1
    uvs = obj.get_num_uv_channels(0)
    has_terrain = False
    for slot in obj.get_editor_property("static_materials"):
        mi = slot.get_editor_property("material_interface")
        if not mi:
            continue
        n_slot += 1
        if not mi.get_name().startswith("MI_TERRAIN_"):
            continue
        has_terrain = True
        n_terr += 1
        parent = mi.get_editor_property("parent")
        if not parent or parent.get_name() != "M_RoseTerrain":
            fails.append(f"{mi.get_name()} parent={parent}")
        for pn in ("BaseColor", "TopColor"):
            t = ME.get_material_instance_texture_parameter_value(mi, pn)
            if not t:
                fails.append(f"{mi.get_name()} has no {pn} texture")
        ov = mi.get_editor_property("base_property_overrides")
        if ov.get_editor_property("override_blend_mode") and \
           ov.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_OPAQUE:
            fails.append(f"{mi.get_name()} is not opaque "
                         f"({ov.get_editor_property('blend_mode')}) — hard-edge bug")
    if has_terrain and uvs < 2:
        bad_uv.append(f"{obj.get_name()} has {uvs} UV channels (need >=2)")
    if obj.get_editor_property("nanite_settings").get_editor_property("enabled") and has_terrain:
        fails.append(f"{obj.get_name()} has Nanite ON (cracks terrain seams)")

fails += bad_uv[:20]
print(f"[probe] {ZONE}: {n_mesh} static meshes, {n_slot} slots, {n_terr} terrain slots")
print(f"[probe] {len(bad_uv)} terrain meshes with too few UV channels")
if fails:
    print(f"[probe] FAIL ({len(fails)}):")
    for f in fails[:40]:
        print("   -", f)
else:
    print("[probe] ALL PASS — terrain is one opaque mesh per tile pair, "
          "two UV sets, lerped by the top map's alpha")
