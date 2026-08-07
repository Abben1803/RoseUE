"""Create /Game/Characters/Materials/M_Hidden — a masked material with opacity
mask 0 (fully clipped → invisible).  Used to hide the inactive eye-overlay
section while blinking.  Must have the SkeletalMesh usage flag."""
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
DIR = "/Game/Characters/Materials"
PATH = DIR + "/M_Hidden"

if EAL.does_asset_exist(PATH):
    EAL.delete_asset(PATH)

mat = AT.create_asset("M_Hidden", DIR, unreal.Material, unreal.MaterialFactoryNew())
mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
mat.set_editor_property("used_with_skeletal_mesh", True)
mat.set_editor_property("two_sided", True)

# Constant 0 → Opacity Mask  (0 < default clip threshold → everything clipped)
zero = MEL.create_material_expression(mat, unreal.MaterialExpressionConstant, -350, 0)
zero.set_editor_property("r", 0.0)
MEL.connect_material_property(zero, "", unreal.MaterialProperty.MP_OPACITY_MASK)

MEL.recompile_material(mat)
ok = EAL.save_asset(PATH, only_if_is_dirty=False)
print(f"[hidden] M_Hidden created, saved={ok}")
