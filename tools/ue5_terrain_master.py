"""
ue5_terrain_master.py — M_RoseTerrain: the engine's terrain pass, in one
OPAQUE material.

ROSE draws the ground in a SINGLE opaque pass with two texture stages and
lerps them by the second map's alpha (src/engine/shader/terrain.psh):

    tex t0                      // 1st map (bottom tile)
    tex t1                      // 2nd map (top tile)
    mov r0, t0
    lrp r0.rgb, t1.a, t1, r0    // rgb = lerp(bottom, top, top.a)

(fixed-function equivalent: stage 1 ZZ_TOP_BLENDTEXTUREALPHA, see
zz_material_terrain.cpp::set_first_second.)  The blend is CONTINUOUS — the
tile DDS alphas are smooth ramps — and there is no second piece of geometry,
so nothing has to sort.

This replaces the old "opaque ground mesh + translucent overlay mesh floated
12 units above it" scheme, whose translucent layer sorted badly and was
therefore forced to BLEND_MASKED at clip 0.33 (ue5_fix_map_blend.py) — a
binary alpha test, which is precisely the hard-edged patchwork bug.

Parameters (mirrors M_RoseMaster's naming so the atlas apply pass is uniform):
  BaseColor / UVTransform        bottom tile page + sub-rect,  UV set 0
  TopColor  / TopUVTransform     top tile page + sub-rect,     UV set 1
  Lightmap                       per-chunk baked ground light, UV set 2

The lightmap is the shader's third line (mul_x2 r0.rgb, r0, t2).  Its default
must be MID-GREY (0.5): the x2 makes 0.5 neutral, so a zone imported without a
lightmap renders exactly as before instead of double-exposed.  ROSE only ships
<chunk>_PLANELIGHTINGMAP.DDS in the CLASSIC tree — Arua has the .LIT metadata
but none of the referenced textures (0 of 3,070 OBJECT_*.dds, 0 PLANELIGHTING),
so lightmaps are a deliberate classic-only source, like the sounds.

Run headless WITH RHI (shader compile), editor CLOSED:
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=tools/ue5_terrain_master.py
    -unattended -nopause -nosplash -stdout -FullStdOutLogOutput
"""
import unreal

GAME_ROOT = "/Game/Atlas"
NAME = "M_RoseTerrain"
PATH = f"{GAME_ROOT}/{NAME}"

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

if not EAL.does_directory_exist(GAME_ROOT):
    EAL.make_directory(GAME_ROOT)

m = EAL.load_asset(PATH)
if not m:
    m = AT.create_asset(NAME, GAME_ROOT, unreal.Material, unreal.MaterialFactoryNew())
# rebuild the graph IN PLACE (never delete+create — see CLAUDE.md)
ME.delete_all_material_expressions(m)

m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
m.set_editor_property("two_sided", True)
m.set_editor_property("used_with_static_lighting", True)


def expr(cls, x, y):
    return ME.create_material_expression(m, cls, x, y)


def uv_chain(uv_index, param_name, y):
    """TexCoord(uv_index) * Param.RG + Param.BA  ->  atlas sub-rect UVs.

    A ComponentMask over a VectorParameter's float3 output does NOT compile
    ("Not enough components for mask 0011"), so the float2s are assembled from
    the scalar pins with AppendVector — same trick as M_RoseMaster.
    """
    tc = expr(unreal.MaterialExpressionTextureCoordinate, -1400, y)
    tc.set_editor_property("coordinate_index", uv_index)
    p = expr(unreal.MaterialExpressionVectorParameter, -1400, y + 90)
    p.set_editor_property("parameter_name", param_name)
    p.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 0.0, 0.0))
    scale = expr(unreal.MaterialExpressionAppendVector, -1150, y + 60)
    offset = expr(unreal.MaterialExpressionAppendVector, -1150, y + 160)
    mul = expr(unreal.MaterialExpressionMultiply, -960, y)
    add = expr(unreal.MaterialExpressionAdd, -820, y)
    ME.connect_material_expressions(p, "R", scale, "A")
    ME.connect_material_expressions(p, "G", scale, "B")
    ME.connect_material_expressions(p, "B", offset, "A")
    ME.connect_material_expressions(p, "A", offset, "B")
    ME.connect_material_expressions(tc, "", mul, "A")
    ME.connect_material_expressions(scale, "", mul, "B")
    ME.connect_material_expressions(mul, "", add, "A")
    ME.connect_material_expressions(offset, "", add, "B")
    return add


bot_uv = uv_chain(0, "UVTransform", 0)
top_uv = uv_chain(1, "TopUVTransform", 420)

bot = expr(unreal.MaterialExpressionTextureSampleParameter2D, -600, 0)
bot.set_editor_property("parameter_name", "BaseColor")
ME.connect_material_expressions(bot_uv, "", bot, "UVs")

top = expr(unreal.MaterialExpressionTextureSampleParameter2D, -600, 420)
top.set_editor_property("parameter_name", "TopColor")
ME.connect_material_expressions(top_uv, "", top, "UVs")

# rgb = lerp(bottom.rgb, top.rgb, top.a)   <-- terrain.psh line 16
lerp = expr(unreal.MaterialExpressionLinearInterpolate, -280, 120)
ME.connect_material_expressions(bot, "RGB", lerp, "A")
ME.connect_material_expressions(top, "RGB", lerp, "B")
ME.connect_material_expressions(top, "A", lerp, "Alpha")

# rgb *= lightmap * 2   <-- terrain.psh line 17: mul_x2 r0.rgb, r0, t2
#
# ROSE bakes per-chunk ground lighting into <chunk>_PLANELIGHTINGMAP.DDS and
# multiplies it in at DOUBLE intensity (mul_x2 is a D3D8/9 pixel-shader modifier
# that scales the result by 2, so a mid-grey 0.5 lightmap is NEUTRAL, not half
# brightness).  Without it the ground is flat albedo and every tile boundary
# reads as a hard band; with it the ground gets ROSE's baked shading.
#
# Default WHITE so a zone with no lightmap imported is unchanged: white * 2
# would double-expose, so the neutral default is 0.5.
lm_tc = expr(unreal.MaterialExpressionTextureCoordinate, -1400, 860)
lm_tc.set_editor_property("coordinate_index", 2)          # UV2 = chunk-space
lm = expr(unreal.MaterialExpressionTextureSampleParameter2D, -600, 860)
lm.set_editor_property("parameter_name", "Lightmap")
# Mid-grey default: with the x2 below this is NEUTRAL, so a zone that never
# imports a lightmap looks exactly as it did before this stage existed.
lm.set_editor_property("texture", unreal.EditorAssetLibrary.load_asset(
    "/Engine/EngineResources/GreyTexture.GreyTexture"))
ME.connect_material_expressions(lm_tc, "", lm, "UVs")

lm_x2 = expr(unreal.MaterialExpressionMultiply, -420, 900)
lm_two = expr(unreal.MaterialExpressionConstant, -600, 1000)
lm_two.set_editor_property("r", 2.0)
ME.connect_material_expressions(lm, "RGB", lm_x2, "A")
ME.connect_material_expressions(lm_two, "", lm_x2, "B")

lit = expr(unreal.MaterialExpressionMultiply, -280, 240)
ME.connect_material_expressions(lerp, "", lit, "A")
ME.connect_material_expressions(lm_x2, "", lit, "B")

tint = expr(unreal.MaterialExpressionVectorParameter, -280, 340)
tint.set_editor_property("parameter_name", "Tint")
tint.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))
tmul = expr(unreal.MaterialExpressionMultiply, -100, 160)
ME.connect_material_expressions(lit, "", tmul, "A")
ME.connect_material_expressions(tint, "", tmul, "B")

rough = expr(unreal.MaterialExpressionConstant, -600, 800)
rough.set_editor_property("r", 1.0)
spec = expr(unreal.MaterialExpressionConstant, -600, 880)
spec.set_editor_property("r", 0.0)

ME.connect_material_property(tmul, "", unreal.MaterialProperty.MP_BASE_COLOR)
ME.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
ME.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)

ME.recompile_material(m)
EAL.save_asset(PATH)
stats = ME.get_statistics(m)
unreal.log(f"[terrainmat] {NAME} built: opaque, 2 samplers, instr="
           f"{stats.get_editor_property('num_pixel_shader_instructions')} "
           f"samplers={stats.get_editor_property('num_texture_samples')}")
print("[terrainmat] DONE")
