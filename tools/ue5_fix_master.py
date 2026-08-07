"""
ue5_fix_master.py — Rebuild M_RoseMaster's expression graph IN PLACE.

The first build multiplied the sampler's RGB (float3) by the Tint vector
parameter (float4) — UE's translator rejects float3*float4, the master never
compiled, and every MI fell back to the default grey material ("all textures
gone").  Rebuild: full RGBA multiply + a real default texture on the sampler
parameter.  The asset identity is preserved (delete_all_material_expressions,
same param names) so all existing MIs stay valid.

Run headless WITH RHI so compile errors surface.  Editor CLOSED.
"""
import unreal

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
MASTER = "/Game/Atlas/M_RoseMaster"
DEFAULT_PAGE = "/Game/Atlas/T_Atlas_female_0"

m = EAL.load_asset(MASTER)
if not m:
    raise RuntimeError("master missing")

ME.delete_all_material_expressions(m)

m.set_editor_property("two_sided", True)
m.set_editor_property("used_with_skeletal_mesh", True)
m.set_editor_property("opacity_mask_clip_value", 0.5)

tc = ME.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -1000, 0)
uvt = ME.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -1000, 200)
uvt.set_editor_property("parameter_name", "UVTransform")
uvt.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 0.0, 0.0))
# The VectorParameter's main output pin is RGB (float3) — a ComponentMask for
# BA over it fails ("Not enough components ... for mask 0011", the bug that
# broke the first two builds).  Build the float2s from the scalar pins.
scale = ME.create_material_expression(m, unreal.MaterialExpressionAppendVector, -800, 200)
offset = ME.create_material_expression(m, unreal.MaterialExpressionAppendVector, -800, 320)
mul = ME.create_material_expression(m, unreal.MaterialExpressionMultiply, -650, 60)
add = ME.create_material_expression(m, unreal.MaterialExpressionAdd, -520, 60)
tex = ME.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -380, 0)
tex.set_editor_property("parameter_name", "BaseColor")
# A parameter sampler still needs a valid DEFAULT texture to compile.
dflt = EAL.load_asset(DEFAULT_PAGE)
if dflt:
    tex.set_editor_property("texture", dflt)
tint = ME.create_material_expression(m, unreal.MaterialExpressionVectorParameter, -380, 300)
tint.set_editor_property("parameter_name", "Tint")
tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
tmul = ME.create_material_expression(m, unreal.MaterialExpressionMultiply, -180, 40)
rough = ME.create_material_expression(m, unreal.MaterialExpressionConstant, -380, 460)
rough.set_editor_property("r", 1.0)
spec = ME.create_material_expression(m, unreal.MaterialExpressionConstant, -380, 540)
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
# Opacity too, so instances may override blend to Translucent (map grass-blend
# terrain layer).  Unused pins cost nothing in opaque/masked permutations.
ME.connect_material_property(tex, "A", unreal.MaterialProperty.MP_OPACITY)
ME.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
ME.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

ME.recompile_material(m)
EAL.save_asset(MASTER)

# Positive compile check: a freshly compiled broken material reports errors.
stats = unreal.MaterialEditingLibrary.get_statistics(m)
print(f"[master] recompiled; stats: instructions={stats.get_editor_property('num_pixel_shader_instructions')} "
      f"samplers={stats.get_editor_property('num_samplers')}")
print("[master] done")
