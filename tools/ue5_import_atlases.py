"""
ue5_import_atlases.py — Import the atlas pages (tools/_tmp/atlas_manifest.json)
to /Game/Atlas/** and create the ONE master material M_RoseMaster.

M_RoseMaster (user's material policy — see docs/ASSET_PIPELINE.md):
  * BaseColor    Texture2D param (an atlas page or a solo texture)
  * UVTransform  Vector param — UV' = UV * RG + BA (the atlas sub-rect;
                 (1,1,0,0) = whole texture, so solo/tiling textures work too)
  * Tint         Vector param multiplied into base color (default white)
  * Texture alpha → OpacityMask (used only when an instance overrides the
    blend mode to Masked, e.g. hair)
  * Two-sided, opaque, roughness 1 / specular 0 (ROSE's flat look),
    usable with skeletal + static meshes.
Everything else is a MaterialInstanceConstant of this master
(ue5_apply_master_materials.py).  Special-purpose masters (water, M_Hidden)
stay separate by design.

Run headless WITH RHI (texture platform data), editor CLOSED.
"""
import json
import os
import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
# Default: catalog atlas manifest.  Override for the map pass:
#   ROSE_ATLAS_MANIFEST=_tmp/map_atlas_manifest_JPT01.json
MANIFEST = os.environ.get("ROSE_ATLAS_MANIFEST", "").strip() or \
    os.path.join(TOOLS, "_tmp", "atlas_manifest.json")
if not os.path.isabs(MANIFEST):
    MANIFEST = os.path.join(TOOLS, MANIFEST)
GAME_ROOT = "/Game/Atlas"
MASTER_PATH = f"{GAME_ROOT}/M_RoseMaster"

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary


def make_master():
    if EAL.does_asset_exist(MASTER_PATH):
        print("[atlas] master exists")
        return EAL.load_asset(MASTER_PATH)
    if not EAL.does_directory_exist(GAME_ROOT):
        EAL.make_directory(GAME_ROOT)
    m = AT.create_asset("M_RoseMaster", GAME_ROOT, unreal.Material,
                        unreal.MaterialFactoryNew())
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_skeletal_mesh", True)
    m.set_editor_property("opacity_mask_clip_value", 0.5)

    tc = ME.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -1000, 0)
    uvt = ME.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -1000, 200)
    uvt.set_editor_property("parameter_name", "UVTransform")
    uvt.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 0.0, 0.0))
    # VectorParameter's main output is RGB (float3) — a BA ComponentMask over
    # it does NOT compile ("Not enough components for mask 0011").  Build the
    # float2s from the scalar pins with AppendVector instead.
    scale = ME.create_material_expression(m, unreal.MaterialExpressionAppendVector, -800, 200)
    offset = ME.create_material_expression(m, unreal.MaterialExpressionAppendVector, -800, 320)
    mul = ME.create_material_expression(m, unreal.MaterialExpressionMultiply, -650, 60)
    add = ME.create_material_expression(m, unreal.MaterialExpressionAdd, -520, 60)
    tex = ME.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -380, 0)
    tex.set_editor_property("parameter_name", "BaseColor")
    tint = ME.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -380, 260)
    tint.set_editor_property("parameter_name", "Tint")
    tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
    tmul = ME.create_material_expression(m, unreal.MaterialExpressionMultiply, -180, 40)
    rough = ME.create_material_expression(m, unreal.MaterialExpressionConstant, -380, 420)
    rough.set_editor_property("r", 1.0)
    spec = ME.create_material_expression(m, unreal.MaterialExpressionConstant, -380, 500)
    spec.set_editor_property("r", 0.0)

    ME.connect_material_expressions(uvt, "R", scale, "A")
    ME.connect_material_expressions(uvt, "G", scale, "B")
    ME.connect_material_expressions(uvt, "B", offset, "A")
    ME.connect_material_expressions(uvt, "A", offset, "B")
    ME.connect_material_expressions(tc, "", mul, "A")
    ME.connect_material_expressions(scale, "", mul, "B")
    ME.connect_material_expressions(mul, "", add, "A")
    ME.connect_material_expressions(offset, "", add, "B")
    ME.connect_material_expressions(add, "", tex, "UVs")
    # float3 * float3 (sampler RGB x param RGB) — types must match exactly.
    ME.connect_material_expressions(tex, "RGB", tmul, "A")
    ME.connect_material_expressions(tint, "", tmul, "B")
    ME.connect_material_property(tmul, "", unreal.MaterialProperty.MP_BASE_COLOR)
    ME.connect_material_property(tex, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    ME.connect_material_property(tex, "A", unreal.MaterialProperty.MP_OPACITY)
    ME.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    ME.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    ME.recompile_material(m)
    EAL.save_asset(MASTER_PATH)
    print("[atlas] created M_RoseMaster")
    return m


def import_pages():
    with open(MANIFEST, "r", encoding="utf-8") as f:
        pages = json.load(f)["pages"]
    # After a REPACK the rects move and page contents change — ROSE_REPLACE=1
    # forces reimport of existing pages (then re-run the apply script, which
    # re-points every mesh at the new rect MIs).
    replace = bool(os.environ.get("ROSE_REPLACE", "").strip())
    done = skipped = 0
    for name, png in sorted(pages.items()):
        asset = f"{GAME_ROOT}/{name}"          # name may contain SOLO/
        if EAL.does_asset_exist(asset) and not replace:
            skipped += 1
            continue
        dst_dir, base = asset.rsplit("/", 1)
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", png)
        t.set_editor_property("destination_path", dst_dir)
        t.set_editor_property("destination_name", base)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        AT.import_asset_tasks([t])
        done += 1
        if done % 20 == 0:
            print(f"[atlas] imported {done} pages ...")
            unreal.SystemLibrary.collect_garbage()
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
    print(f"[atlas] pages: {done} imported, {skipped} existing")


make_master()
import_pages()
print("[atlas] done")
