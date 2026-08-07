"""Create the skill-FX assets: /Game/Effects/M_RoseFX — an unlit translucent
emissive material driven by two runtime params ("Color" vector, "Alpha" scalar).
ARoseSkillFX makes a dynamic instance per effect and animates the params.

The soft-orb look: emissive = Color; opacity = Alpha * (1 - fresnel), so the
shape's rim fades out (no hard silhouette) — reads as a glow burst on the
engine sphere without any texture.

Run headlessly (editor CLOSED, needs RHI for the shader compile):
  UnrealEditor-Cmd.exe "<RoseUE.uproject>" -ExecutePythonScript="tools/ue5_make_fx_assets.py" ...
Re-runnable: rebuilds the expression graph in place.
"""
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

DST = "/Game/Effects"
NAME = "M_RoseFX"
PATH = f"{DST}/{NAME}"

if EAL.does_asset_exist(PATH):
    mat = EAL.load_asset(PATH)
    print(f"[fx] {NAME}: exists, rebuilding graph")
else:
    mat = AT.create_asset(NAME, DST, unreal.Material, unreal.MaterialFactoryNew())
    print(f"[fx] {NAME}: created")

mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
mat.set_editor_property("two_sided", True)

MEL.delete_all_material_expressions(mat)

color = MEL.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -500, -100)
color.set_editor_property("parameter_name", "Color")
color.set_editor_property("default_value", unreal.LinearColor(1.0, 1.0, 1.0, 1.0))

alpha = MEL.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -500, 150)
alpha.set_editor_property("parameter_name", "Alpha")
alpha.set_editor_property("default_value", 1.0)

fres = MEL.create_material_expression(mat, unreal.MaterialExpressionFresnel, -500, 300)
fres.set_editor_property("exponent", 2.0)

inv = MEL.create_material_expression(mat, unreal.MaterialExpressionOneMinus, -350, 300)
MEL.connect_material_expressions(fres, "", inv, "")

mul = MEL.create_material_expression(mat, unreal.MaterialExpressionMultiply, -200, 200)
MEL.connect_material_expressions(alpha, "", mul, "A")
MEL.connect_material_expressions(inv, "", mul, "B")

MEL.connect_material_property(color, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
MEL.connect_material_property(mul, "", unreal.MaterialProperty.MP_OPACITY)

MEL.recompile_material(mat)
EAL.save_asset(PATH)
print(f"[fx] {NAME}: saved")


# ─────────────────────────────────────────────────────────────────────────────
# ROSE particle materials — used by URoseParticleSeqComponent (RoseEffect.cpp)
# on instanced quads.  Per-instance custom data floats:
#   0..2 = RGB tint   3 = alpha   4 = sprite-sheet frame index
# Material params per sequence: "Tex" (the particle texture), "GridW"/"GridH"
# (sprite sheet layout).  Two blend variants, matching the PTL's D3D dest
# blend: ONE → M_RoseParticle_Add (additive), INVSRCALPHA → _Alpha.
# ─────────────────────────────────────────────────────────────────────────────
def build_particle_material(name, additive):
    path = f"{DST}/{name}"
    if EAL.does_asset_exist(path):
        m = EAL.load_asset(path)
        print(f"[fx] {name}: exists, rebuilding graph")
    else:
        m = AT.create_asset(name, DST, unreal.Material, unreal.MaterialFactoryNew())
        print(f"[fx] {name}: created")

    m.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    m.set_editor_property("blend_mode",
        unreal.BlendMode.BLEND_ADDITIVE if additive else unreal.BlendMode.BLEND_TRANSLUCENT)
    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_instanced_static_meshes", True)

    MEL.delete_all_material_expressions(m)

    tex = MEL.create_material_expression(m, unreal.MaterialExpressionTextureSampleParameter2D, -400, -150)
    tex.set_editor_property("parameter_name", "Tex")

    gw = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -1100, 100)
    gw.set_editor_property("parameter_name", "GridW")
    gw.set_editor_property("default_value", 1.0)
    gh = MEL.create_material_expression(m, unreal.MaterialExpressionScalarParameter, -1100, 200)
    gh.set_editor_property("parameter_name", "GridH")
    gh.set_editor_property("default_value", 1.0)

    # Per-instance custom data: RGB / A / Frame.
    def cdata(idx, y):
        n = MEL.create_material_expression(m, unreal.MaterialExpressionPerInstanceCustomData, -1100, y)
        n.set_editor_property("data_index", idx)
        return n

    cr, cg, cb = cdata(0, 320), cdata(1, 400), cdata(2, 480)
    ca = cdata(3, 560)
    cf = cdata(4, 640)

    rgb1 = MEL.create_material_expression(m, unreal.MaterialExpressionAppendVector, -950, 360)
    MEL.connect_material_expressions(cr, "", rgb1, "A")
    MEL.connect_material_expressions(cg, "", rgb1, "B")
    rgb = MEL.create_material_expression(m, unreal.MaterialExpressionAppendVector, -820, 380)
    MEL.connect_material_expressions(rgb1, "", rgb, "A")
    MEL.connect_material_expressions(cb, "", rgb, "B")

    # Sprite-sheet UV: uv = (TexCoord + (fmod(F,GridW), floor(F/GridH))) / (GridW,GridH)
    # (engine formula: x = idx % w, y = idx / h — zz_particle_event_sequence.cpp:641).
    uv0 = MEL.create_material_expression(m, unreal.MaterialExpressionTextureCoordinate, -1100, -60)
    fx = MEL.create_material_expression(m, unreal.MaterialExpressionFmod, -950, 620)
    MEL.connect_material_expressions(cf, "", fx, "A")
    MEL.connect_material_expressions(gw, "", fx, "B")
    fdiv = MEL.create_material_expression(m, unreal.MaterialExpressionDivide, -950, 700)
    MEL.connect_material_expressions(cf, "", fdiv, "A")
    MEL.connect_material_expressions(gh, "", fdiv, "B")
    fy = MEL.create_material_expression(m, unreal.MaterialExpressionFloor, -850, 700)
    MEL.connect_material_expressions(fdiv, "", fy, "")
    frame = MEL.create_material_expression(m, unreal.MaterialExpressionAppendVector, -750, 640)
    MEL.connect_material_expressions(fx, "", frame, "A")
    MEL.connect_material_expressions(fy, "", frame, "B")
    uvsum = MEL.create_material_expression(m, unreal.MaterialExpressionAdd, -650, -20)
    MEL.connect_material_expressions(uv0, "", uvsum, "A")
    MEL.connect_material_expressions(frame, "", uvsum, "B")
    grid = MEL.create_material_expression(m, unreal.MaterialExpressionAppendVector, -650, 140)
    MEL.connect_material_expressions(gw, "", grid, "A")
    MEL.connect_material_expressions(gh, "", grid, "B")
    uvfin = MEL.create_material_expression(m, unreal.MaterialExpressionDivide, -520, -20)
    MEL.connect_material_expressions(uvsum, "", uvfin, "A")
    MEL.connect_material_expressions(grid, "", uvfin, "B")
    MEL.connect_material_expressions(uvfin, "", tex, "UVs")

    # Emissive = Tex.rgb * tint;  Opacity = Tex.a * instance alpha.
    emis = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -150, -100)
    MEL.connect_material_expressions(tex, "RGB", emis, "A")
    MEL.connect_material_expressions(rgb, "", emis, "B")
    opac = MEL.create_material_expression(m, unreal.MaterialExpressionMultiply, -150, 150)
    MEL.connect_material_expressions(tex, "A", opac, "A")
    MEL.connect_material_expressions(ca, "", opac, "B")

    MEL.connect_material_property(emis, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MEL.connect_material_property(opac, "", unreal.MaterialProperty.MP_OPACITY)

    MEL.recompile_material(m)
    EAL.save_asset(path)
    print(f"[fx] {name}: saved")


build_particle_material("M_RoseParticle_Add", additive=True)
build_particle_material("M_RoseParticle_Alpha", additive=False)
