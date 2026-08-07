"""
ue5_fix_gear_master.py — rebuild M_RoseGear in place with the PROVEN master
recipe (ue5_fix_master.py).  The original authoring missed
`used_with_skeletal_mesh=True`, so every gear skeletal mesh rendered default
grey.  Keeps the same asset + parameter names (AtlasTex / CellScale /
CellOffset), so the 5,510 baked MIs and the runtime MIDs keep working.

Headless WITH RHI (shader compile), editor CLOSED.
"""
import unreal

MASTER = "/Game/Characters/Gear/M_RoseGear"
EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary

m = EAL.load_asset(MASTER)
if not m:
    raise RuntimeError("M_RoseGear missing")

ME.delete_all_material_expressions(m)

m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
m.set_editor_property("two_sided", True)
m.set_editor_property("used_with_skeletal_mesh", True)   # THE missing flag
m.set_editor_property("opacity_mask_clip_value", 0.5)

def expr(cls, x, y):
    return ME.create_material_expression(m, cls, x, y)

tc = expr(unreal.MaterialExpressionTextureCoordinate, -1000, 0)
scale = expr(unreal.MaterialExpressionScalarParameter, -1000, 180)
scale.set_editor_property("parameter_name", "CellScale")
scale.set_editor_property("default_value", 1.0 / 16.0)
offset = expr(unreal.MaterialExpressionVectorParameter, -1000, 320)
offset.set_editor_property("parameter_name", "CellOffset")
offset.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 0))

mul = expr(unreal.MaterialExpressionMultiply, -760, 60)
ME.connect_material_expressions(tc, "", mul, "A")
ME.connect_material_expressions(scale, "", mul, "B")

om = expr(unreal.MaterialExpressionComponentMask, -760, 300)
om.set_editor_property("r", True)
om.set_editor_property("g", True)
om.set_editor_property("b", False)
om.set_editor_property("a", False)
ME.connect_material_expressions(offset, "", om, "")

add = expr(unreal.MaterialExpressionAdd, -560, 120)
ME.connect_material_expressions(mul, "", add, "A")
ME.connect_material_expressions(om, "", add, "B")

tex = expr(unreal.MaterialExpressionTextureSampleParameter2D, -380, 0)
tex.set_editor_property("parameter_name", "AtlasTex")
if unreal.Texture2D and EAL.does_asset_exist("/Game/Characters/Gear/Atlas/T_gear_atlas_00"):
    tex.set_editor_property("texture", EAL.load_asset("/Game/Characters/Gear/Atlas/T_gear_atlas_00"))
ME.connect_material_expressions(add, "", tex, "UVs")

rough = expr(unreal.MaterialExpressionConstant, -380, 460)
rough.set_editor_property("r", 1.0)
spec = expr(unreal.MaterialExpressionConstant, -380, 540)
spec.set_editor_property("r", 0.0)

ME.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
ME.connect_material_property(tex, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
ME.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
ME.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

ME.recompile_material(m)
EAL.save_asset(MASTER)

# Positive compile check (a broken material reports 0/garbage stats).
stats = ME.get_statistics(m)
print(f"[gearfix] recompiled M_RoseGear: instructions="
      f"{stats.get_editor_property('num_pixel_shader_instructions')} "
      f"samplers={stats.get_editor_property('num_samplers')} "
      f"used_with_skeletal_mesh={m.get_editor_property('used_with_skeletal_mesh')}")
print("[gearfix] done")
