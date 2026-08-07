"""ue5_refine_glow_master.py — create M_RoseRefineGlow, the refine-glow overlay.

Classic ROSE renders a refined item with an extra ADDITIVE pass tinted by the
grade's RGB (LIST_GRADE.STB "RGB GLOW").  The UE analog: an overlay material
(UMeshComponent::SetOverlayMaterial) that re-renders the mesh additively.

Graph:  Emissive = GlowColor(param) * GlowIntensity(param) * pulse
        pulse = 0.85 + 0.15 * sin(Time * 4)   (the classic slow shimmer)
Additive + Unlit + two-sided; the MID sets GlowColor per grade at runtime.

Headless, editor CLOSED, WITH RHI (shader compile).
"""
import unreal

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

PATH_DIR = "/Game/Characters/Materials"
NAME = "M_RoseRefineGlow"
PATH = f"{PATH_DIR}/{NAME}"

m = EAL.load_asset(PATH)
if not m:
    m = AT.create_asset(NAME, PATH_DIR, unreal.Material, unreal.MaterialFactoryNew())
ME.delete_all_material_expressions(m)

m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
m.set_editor_property("two_sided", True)


def expr(cls, x, y):
    return ME.create_material_expression(m, cls, x, y)


color = expr(unreal.MaterialExpressionVectorParameter, -900, 0)
color.set_editor_property("parameter_name", "GlowColor")
color.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 0))

inten = expr(unreal.MaterialExpressionScalarParameter, -900, 220)
inten.set_editor_property("parameter_name", "GlowIntensity")
inten.set_editor_property("default_value", 0.35)

# pulse = 0.85 + 0.15 * sin(Time * 4)
t = expr(unreal.MaterialExpressionTime, -900, 380)
tmul = expr(unreal.MaterialExpressionMultiply, -760, 380)
tconst = expr(unreal.MaterialExpressionConstant, -900, 470)
tconst.set_editor_property("r", 4.0)
ME.connect_material_expressions(t, "", tmul, "A")
ME.connect_material_expressions(tconst, "", tmul, "B")
sine = expr(unreal.MaterialExpressionSine, -620, 380)
ME.connect_material_expressions(tmul, "", sine, "")
samp = expr(unreal.MaterialExpressionMultiply, -480, 380)
sconst = expr(unreal.MaterialExpressionConstant, -620, 470)
sconst.set_editor_property("r", 0.15)
ME.connect_material_expressions(sine, "", samp, "A")
ME.connect_material_expressions(sconst, "", samp, "B")
base = expr(unreal.MaterialExpressionAdd, -340, 380)
bconst = expr(unreal.MaterialExpressionConstant, -480, 470)
bconst.set_editor_property("r", 0.85)
ME.connect_material_expressions(samp, "", base, "A")
ME.connect_material_expressions(bconst, "", base, "B")

ci = expr(unreal.MaterialExpressionMultiply, -620, 100)
ME.connect_material_expressions(color, "", ci, "A")
ME.connect_material_expressions(inten, "", ci, "B")
out = expr(unreal.MaterialExpressionMultiply, -340, 160)
ME.connect_material_expressions(ci, "", out, "A")
ME.connect_material_expressions(base, "", out, "B")

ME.connect_material_property(out, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

ME.recompile_material(m)
EAL.save_asset(PATH)
stats = ME.get_statistics(m)
unreal.log(f"[refineglow] {NAME}: instr="
           f"{stats.get_editor_property('num_pixel_shader_instructions')}")
unreal.log("[refineglow] DONE")
