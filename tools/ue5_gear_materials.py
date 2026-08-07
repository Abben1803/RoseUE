"""
ue5_gear_materials.py — Phase 5: gear atlases + the gear master material.

  * imports the 32 SourceAssets/.../GEAR_ATLAS/gear_atlas_<page>.png as
    /Game/Characters/Gear/Atlas/T_gear_atlas_<page>
  * builds /Game/Characters/Gear/M_RoseGear — a masked two-sided material that
    samples an atlas page at a UV CELL:
        UV = TexCoord * CellScale + CellOffset
    params: AtlasTex (Texture2D), CellOffset (RG = u0,v0), CellScale (1/16).
    RGB -> BaseColor, A -> OpacityMask.

The runtime equip makes a dynamic instance per part: set AtlasTex to the item's
page and CellOffset to its (u0,v0) from gear_equip.json.

Headless WITH RHI (material compile), editor CLOSED.
"""
import os
import unreal

PROJECT_DIR = unreal.Paths.project_dir()
ATLAS_SRC = os.path.normpath(os.path.join(
    PROJECT_DIR, "SourceAssets", "GLTF", "AVATAR", "GEAR_ATLAS"))
ATLAS_DST = "/Game/Characters/Gear/Atlas"
MAT_DIR = "/Game/Characters/Gear"
CELL_SCALE = 1.0 / 16.0

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary

# ── 1. import the 32 atlas PNGs as textures ──────────────────────────────────
if not EAL.does_directory_exist(ATLAS_DST):
    EAL.make_directory(ATLAS_DST)
pngs = sorted(f for f in os.listdir(ATLAS_SRC) if f.lower().endswith(".png"))
tasks = []
for f in pngs:
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(ATLAS_SRC, f))
    t.set_editor_property("destination_path", ATLAS_DST)
    t.set_editor_property("destination_name", "T_" + os.path.splitext(f)[0])
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    tasks.append(t)
AT.import_asset_tasks(tasks)
print(f"[gearmat] imported {len(tasks)} atlas textures -> {ATLAS_DST}")

# ── 2. build M_RoseGear ──────────────────────────────────────────────────────
mp = f"{MAT_DIR}/M_RoseGear"
if EAL.does_asset_exist(mp):
    EAL.delete_asset(mp)
m = AT.create_asset("M_RoseGear", MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
m.set_editor_property("two_sided", True)
m.set_editor_property("opacity_mask_clip_value", 0.5)

def expr(cls, x, y):
    return ME.create_material_expression(m, cls, x, y)

tc     = expr(unreal.MaterialExpressionTextureCoordinate, -900, 0)
scale  = expr(unreal.MaterialExpressionScalarParameter, -900, 150)
scale.set_editor_property("parameter_name", "CellScale")
scale.set_editor_property("default_value", CELL_SCALE)
offset = expr(unreal.MaterialExpressionVectorParameter, -900, 300)
offset.set_editor_property("parameter_name", "CellOffset")
offset.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 0))

mul = expr(unreal.MaterialExpressionMultiply, -650, 60)
ME.connect_material_expressions(tc, "", mul, "A")
ME.connect_material_expressions(scale, "", mul, "B")
omask = expr(unreal.MaterialExpressionComponentMask, -650, 300)
omask.set_editor_property("r", True); omask.set_editor_property("g", True)
omask.set_editor_property("b", False); omask.set_editor_property("a", False)
ME.connect_material_expressions(offset, "", omask, "")
add = expr(unreal.MaterialExpressionAdd, -450, 120)
ME.connect_material_expressions(mul, "", add, "A")
ME.connect_material_expressions(omask, "", add, "B")

tex = expr(unreal.MaterialExpressionTextureSampleParameter2D, -250, 0)
tex.set_editor_property("parameter_name", "AtlasTex")
atlas0 = EAL.load_asset(f"{ATLAS_DST}/T_gear_atlas_00")
if atlas0:
    tex.set_editor_property("texture", atlas0)
ME.connect_material_expressions(add, "", tex, "UVs")
ME.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
ME.connect_material_property(tex, "A",   unreal.MaterialProperty.MP_OPACITY_MASK)

ME.recompile_material(m)
EAL.save_asset(mp)
print(f"[gearmat] built {mp} (masked two-sided, atlas UV-cell)")
