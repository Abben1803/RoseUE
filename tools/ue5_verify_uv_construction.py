"""ue5_verify_uv_construction.py — settle whether the atlas UV construction works.

Builds a THROWAWAY unlit material whose EMISSIVE = AtlasTex sampled at
UV = TexCoord*(127/2048) + append(CellOffset.R, CellOffset.G) + halfTexel,
then renders it at two CellOffset values.  draw_material_to_render_target shows
emissive, so this actually reads back a colour (unlike a lit BaseColor material,
which renders black).

DIFFERENT colours  => the append/CellOffset construction works; the real master
                      (same nodes) is correct and the in-game failure was a
                      stale cached master — a fresh editor will render right.
IDENTICAL colours  => the construction itself does not consume CellOffset; the
                      master graph is unfixable this way -> per-item textures.

Headless, editor CLOSED, WITH RHI.
"""
import unreal

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
RL = unreal.RenderingLibrary


def log(m):
    unreal.log(f"[uvc] {m}")


ATLAS_PX, CELL_PX = 2048.0, 128.0
DIAG = "/Game/Characters/Gear/_UVDIAG"

# clean rebuild
if EAL.does_asset_exist(DIAG):
    EAL.delete_asset(DIAG)
m = AT.create_asset("_UVDIAG", "/Game/Characters/Gear", unreal.Material,
                    unreal.MaterialFactoryNew())
m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)


def expr(cls, x, y):
    return ME.create_material_expression(m, cls, x, y)


tc = expr(unreal.MaterialExpressionTextureCoordinate, -1000, 0)
tc.set_editor_property("u_tiling", (CELL_PX - 1.0) / ATLAS_PX)
tc.set_editor_property("v_tiling", (CELL_PX - 1.0) / ATLAS_PX)

offset = expr(unreal.MaterialExpressionVectorParameter, -1000, 320)
offset.set_editor_property("parameter_name", "CellOffset")

off_rg = expr(unreal.MaterialExpressionAppendVector, -760, 320)
ME.connect_material_expressions(offset, "R", off_rg, "A")
ME.connect_material_expressions(offset, "G", off_rg, "B")

inset = expr(unreal.MaterialExpressionConstant2Vector, -760, 460)
inset.set_editor_property("r", 0.5 / ATLAS_PX)
inset.set_editor_property("g", 0.5 / ATLAS_PX)

add_off = expr(unreal.MaterialExpressionAdd, -560, 360)
ME.connect_material_expressions(off_rg, "", add_off, "A")
ME.connect_material_expressions(inset, "", add_off, "B")

add = expr(unreal.MaterialExpressionAdd, -460, 120)
ME.connect_material_expressions(tc, "", add, "A")
ME.connect_material_expressions(add_off, "", add, "B")

tex = expr(unreal.MaterialExpressionTextureSampleParameter2D, -300, 0)
tex.set_editor_property("parameter_name", "AtlasTex")
atlas = EAL.load_asset("/Game/Characters/Gear/Atlas/T_gear_atlas_29")
tex.set_editor_property("texture", atlas)
ME.connect_material_expressions(add, "", tex, "UVs")
ME.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

ME.recompile_material(m)
EAL.save_asset(DIAG)

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
rt = RL.create_render_target2d(world, 64, 64, unreal.TextureRenderTargetFormat.RTF_RGBA8)
mid = unreal.MaterialLibrary.create_dynamic_material_instance(world, m)
mid.set_texture_parameter_value("AtlasTex", atlas)

cases = {"Heart(.625,.625)": (0.6250, 0.6250), "Gloom(.3125,.6875)": (0.3125, 0.6875)}
got = {}
for label, (u, v) in cases.items():
    mid.set_vector_parameter_value("CellOffset", unreal.LinearColor(u, v, 0, 0))
    RL.draw_material_to_render_target(world, rt, mid)
    px = RL.read_render_target_pixel(world, rt, 32, 32)
    got[label] = (round(px.r, 3), round(px.g, 3), round(px.b, 3))
    log(f"{label}: {got[label]}")

vals = list(got.values())
log("RESULT: " + ("DIFFERENT — construction WORKS (fix real master; in-game was stale cache)"
                  if vals[0] != vals[1] else
                  "IDENTICAL — construction ignores CellOffset -> per-item textures"))
EAL.delete_asset(DIAG)
log("DONE")
