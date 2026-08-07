"""
ue5_assign_gear_mats.py — assign each imported gear mesh a DEFAULT atlas
material instance.

The 5,510 deduped meshes import with the engine default material (their GLBs
carry no texture).  At runtime RebuildMesh overrides per equipped item with a
dynamic M_RoseGear (a mesh can be shared by several recolors), but the ASSET
itself should carry a sensible default: an MI of M_RoseGear pointing at the
FIRST atlas cell that uses this mesh (for most meshes there is exactly one).
This makes the meshes render textured in the editor/preview and acts as the
fallback material wherever no runtime override is applied.

Creates /Game/Characters/Gear/mesh_<id>/MI_gear_<id> and sets it on every
material slot of the skeletal mesh.

Reads tools/_tmp/gear_manifest.json (mesh -> first tex cell).
Env: ROSE_LIMIT (batch size, 0 = all remaining).  Headless, editor CLOSED,
-nullrhi fine.  Re-runnable (skips meshes whose MI already exists).
"""
import json
import os
import unreal

LIMIT = int(os.environ.get("ROSE_LIMIT", "0"))
MANIFEST = r"C:\rose-next-classic\tools\_tmp\gear_manifest.json"
GAME_ROOT = "/Game/Characters/Gear"
MASTER = "/Game/Characters/Gear/M_RoseGear"
ATLAS = "/Game/Characters/Gear/Atlas"

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

master = EAL.load_asset(MASTER)
if not master:
    raise RuntimeError(f"missing {MASTER}")

man = json.load(open(MANIFEST))
atlas = {int(k): v for k, v in man["atlas"].items()}

# mesh id -> first (page, u0, v0) that uses it
mesh_cell = {}
for parts in man["items"].values():
    for p in parts:
        if p["mesh"] not in mesh_cell and p["tex"] >= 0:
            cell = atlas.get(p["tex"])
            if cell:
                mesh_cell[p["mesh"]] = (cell[0], cell[1], cell[2])

print(f"[gearmi] {len(mesh_cell)} meshes have a default cell")

tex_cache = {}
def atlas_tex(page):
    if page not in tex_cache:
        tex_cache[page] = EAL.load_asset(f"{ATLAS}/T_gear_atlas_{page:02d}")
    return tex_cache[page]

done = skipped = missing = 0
for mid in sorted(mesh_cell):
    mi_path = f"{GAME_ROOT}/mesh_{mid}/MI_gear_{mid}"
    if EAL.does_asset_exist(mi_path):
        skipped += 1
        continue
    sm_path = f"{GAME_ROOT}/mesh_{mid}/mesh_{mid}/SkeletalMeshes/mesh_{mid}"
    sm = EAL.load_asset(sm_path)
    if not sm:
        missing += 1
        continue
    page, u0, v0 = mesh_cell[mid]
    mi = AT.create_asset(f"MI_gear_{mid}", f"{GAME_ROOT}/mesh_{mid}",
                         unreal.MaterialInstanceConstant,
                         unreal.MaterialInstanceConstantFactoryNew())
    MEL.set_material_instance_parent(mi, master)
    tex = atlas_tex(page)
    if tex:
        MEL.set_material_instance_texture_parameter_value(mi, "AtlasTex", tex)
    MEL.set_material_instance_vector_parameter_value(
        mi, "CellOffset", unreal.LinearColor(u0, v0, 0.0, 0.0))
    EAL.save_asset(mi_path)

    mats = sm.get_editor_property("materials")
    out = []
    for slot in mats:
        slot.set_editor_property("material_interface", mi)
        out.append(slot)
    sm.set_editor_property("materials", out)
    EAL.save_asset(sm_path)
    done += 1
    if done % 200 == 0:
        print(f"[gearmi] ... {done} assigned")
        unreal.SystemLibrary.collect_garbage()
    if LIMIT and done >= LIMIT:
        break

print(f"[gearmi] DONE: {done} assigned, {skipped} already had MI, {missing} mesh missing")
