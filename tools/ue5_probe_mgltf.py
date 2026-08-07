"""ue5_probe_mgltf.py — dump Substrate M_GLTF params + one black foliage
instance's current mask-related state, to design the minimal masked fix."""
import unreal
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()

base = EAL.load_asset("/InterchangeAssets/gltf/Substrate/M_GLTF")
print(f"[mg] base = {base.get_path_name() if base else None}")
if base:
    for getter in ("get_scalar_parameter_names","get_static_switch_parameter_names",
                   "get_texture_parameter_names","get_vector_parameter_names"):
        try:
            names = getattr(MEL, getter)(base)
            print(f"[mg] {getter}: {list(names)}")
        except Exception as e:
            print(f"[mg] {getter}: err {e}")
    # material-level flags
    print(f"[mg] base blend={base.get_editor_property('blend_mode')} "
          f"shading={base.get_editor_property('shading_model')} "
          f"two_sided={base.get_editor_property('two_sided')}")

# find a black foliage MI (masked, tree/grass)
flt = unreal.ARFilter(
    class_paths=[unreal.TopLevelAssetPath("/Script/Engine","MaterialInstanceConstant")],
    package_paths=["/Game/Maps"], recursive_paths=True, recursive_classes=True)
target = None
for ad in (REG.get_assets(flt) or []):
    n=str(ad.asset_name).upper()
    if ("TREE001" in n or "GRASS001" in n) and "B" != n[-1]:
        target = str(ad.package_name); break
print(f"\n[mg] sample instance = {target}")
mi = EAL.load_asset(target)
if isinstance(mi, unreal.MaterialInstanceConstant):
    ov = mi.get_editor_property("base_property_overrides")
    print(f"[mg] override_blend={ov.get_editor_property('override_blend_mode')} "
          f"blend={ov.get_editor_property('blend_mode')} "
          f"clip={ov.get_editor_property('opacity_mask_clip_value')} "
          f"override_two={ov.get_editor_property('override_two_sided')} "
          f"two={ov.get_editor_property('two_sided')}")
    print("[mg] scalar params:")
    for sp in mi.get_editor_property("scalar_parameter_values"):
        nm=sp.get_editor_property("parameter_info").get_editor_property("name")
        print(f"      {nm} = {sp.get_editor_property('parameter_value')}")
    print("[mg] static switch params:")
    try:
        for sp in mi.get_editor_property("static_switch_parameters"):
            nm=sp.get_editor_property("parameter_info").get_editor_property("name")
            print(f"      {nm} = {sp.get_editor_property('value')}")
    except Exception as e:
        print("      err", e)
    # texture params
    print("[mg] texture params:")
    for tp in mi.get_editor_property("texture_parameter_values"):
        nm=tp.get_editor_property("parameter_info").get_editor_property("name")
        tv=tp.get_editor_property("parameter_value")
        print(f"      {nm} = {tv.get_name() if tv else None}")
