"""
ue5_gear_masters_zsc.py — ZSC-faithful gear master materials.

The single always-MASKED M_RoseGear clipped every item at A>0.5, but 86% of
gear materials are OPAQUE per the ZSC (their DDS alpha holds specular, not
coverage) -> transparent holes / black patches (Substrate masked path).

Builds THREE masters sharing the same graph + parameter names
(AtlasTex / CellScale / CellOffset), so baked MIs and runtime MIDs work
against any of them:

  M_RoseGear        OPAQUE      (the 86% default — alpha ignored)
  M_RoseGearMasked  MASKED@0.5  (ZSC alpha=1 & alpha_test=1)
  M_RoseGearBlend   MASKED@0.33 (ZSC alpha=1, no test — same policy as the
                                 map decals: Substrate TRANSLUCENT renders
                                 black/dithered wings, masked at a low clip
                                 keeps the feather shape and sorts correctly)

All two-sided + used_with_skeletal_mesh. Headless WITH RHI, editor CLOSED.
"""
import unreal

MAT_DIR = "/Game/Characters/Gear"
EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()


def build(name, mode):
    path = f"{MAT_DIR}/{name}"
    m = EAL.load_asset(path)
    if not m:
        m = AT.create_asset(name, MAT_DIR, unreal.Material, unreal.MaterialFactoryNew())
    ME.delete_all_material_expressions(m)

    m.set_editor_property("two_sided", True)
    m.set_editor_property("used_with_skeletal_mesh", True)
    if mode == "masked":
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        m.set_editor_property("opacity_mask_clip_value", 0.5)
    elif mode == "blend":
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        m.set_editor_property("opacity_mask_clip_value", 0.33)
    else:
        m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)

    def expr(cls, x, y):
        return ME.create_material_expression(m, cls, x, y)

    # UV = TexCoord*(CELL-1)/ATLAS + halfTexel + CellOffset.rg
    #
    # REBUILT 2026-07-21 with EXPLICIT component wiring.  The previous graph fed
    # CellOffset through a ComponentMask whose input was connected with an empty
    # output-pin name; that link did NOT take in the compiled shader, so
    # CellOffset was exposed as a parameter but never reached the UV — every item
    # sampled cell (0,0) and all shared-mesh items (wings) rendered identically.
    # Proven by runtime readback: MID stored the right CellOffset, render never
    # changed.  Here CellOffset's R and G scalar outputs are appended explicitly
    # (no ComponentMask), and the constant scale is baked into the TexCoord's
    # tiling so there is one less parameter to mis-wire.
    #
    # The (CELL-1)/ATLAS scale + half-texel inset keep BILINEAR taps inside the
    # cell (ROSE mesh UVs run 0.0011..0.9871, i.e. right to the cell border), so
    # this also fixes the foreign-colour speckling along alpha edges.
    ATLAS_PX, CELL_PX = 2048.0, 128.0
    tc = expr(unreal.MaterialExpressionTextureCoordinate, -1000, 0)
    tc.set_editor_property("u_tiling", (CELL_PX - 1.0) / ATLAS_PX)
    tc.set_editor_property("v_tiling", (CELL_PX - 1.0) / ATLAS_PX)

    # CellScale kept as a (now unused) parameter for backward compat / probes.
    scale = expr(unreal.MaterialExpressionScalarParameter, -1000, 180)
    scale.set_editor_property("parameter_name", "CellScale")
    scale.set_editor_property("default_value", (CELL_PX - 1.0) / ATLAS_PX)

    offset = expr(unreal.MaterialExpressionVectorParameter, -1000, 320)
    offset.set_editor_property("parameter_name", "CellOffset")
    offset.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 0))

    # CellOffset.rg as an explicit float2 (append R,G) — no ComponentMask.
    off_rg = expr(unreal.MaterialExpressionAppendVector, -760, 320)
    ME.connect_material_expressions(offset, "R", off_rg, "A")
    ME.connect_material_expressions(offset, "G", off_rg, "B")

    # half-texel inset
    inset = expr(unreal.MaterialExpressionConstant2Vector, -760, 460)
    inset.set_editor_property("r", 0.5 / ATLAS_PX)
    inset.set_editor_property("g", 0.5 / ATLAS_PX)

    add_off = expr(unreal.MaterialExpressionAdd, -560, 360)
    ME.connect_material_expressions(off_rg, "", add_off, "A")
    ME.connect_material_expressions(inset, "", add_off, "B")

    add = expr(unreal.MaterialExpressionAdd, -460, 120)
    ME.connect_material_expressions(tc, "", add, "A")
    ME.connect_material_expressions(add_off, "", add, "B")

    tex = expr(unreal.MaterialExpressionTextureSampleParameter2D, -380, 0)
    tex.set_editor_property("parameter_name", "AtlasTex")
    if EAL.does_asset_exist("/Game/Characters/Gear/Atlas/T_gear_atlas_00"):
        tex.set_editor_property("texture", EAL.load_asset("/Game/Characters/Gear/Atlas/T_gear_atlas_00"))
    ME.connect_material_expressions(add, "", tex, "UVs")

    rough = expr(unreal.MaterialExpressionConstant, -380, 460)
    rough.set_editor_property("r", 1.0)
    spec = expr(unreal.MaterialExpressionConstant, -380, 540)
    spec.set_editor_property("r", 0.0)

    # Refine glow on merged armor: GlowColor (default BLACK = no glow) drives
    # emissive; RebuildMesh's per-part MID sets it to the grade's authentic RGB.
    # Uses the OPAQUE/MASKED path's emissive pin — no Substrate translucent risk.
    glow = expr(unreal.MaterialExpressionVectorParameter, -640, 620)
    glow.set_editor_property("parameter_name", "GlowColor")
    glow.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 0))

    ME.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    if mode in ("masked", "blend"):
        ME.connect_material_property(tex, "A", unreal.MaterialProperty.MP_OPACITY_MASK)
    ME.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    ME.connect_material_property(spec, "", unreal.MaterialProperty.MP_SPECULAR)
    ME.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    ME.recompile_material(m)
    EAL.save_asset(path)
    stats = ME.get_statistics(m)
    unreal.log(f"[gearzsc] {name}: mode={mode} instr="
               f"{stats.get_editor_property('num_pixel_shader_instructions')}")


build("M_RoseGear", "opaque")
build("M_RoseGearMasked", "masked")
build("M_RoseGearBlend", "blend")
unreal.log("[gearzsc] masters done")
