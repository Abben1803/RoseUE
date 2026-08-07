"""
ue5_fix_metallic_switch.py — turn OFF the bHasMetallicRoughness static switch on
every Interchange (M_GLTF-parented) MaterialInstanceConstant in the project.

ROSE textures are classic diffuse; the glTF metallic-roughness path makes them
render PBR-shiny/dark.  Only instances where the switch currently evaluates
True are touched (each flip = a shader permutation recompile on next load).

Env: ROSE_SWITCH (default bHasMetallicRoughness), ROSE_ROOT (default /Game).
Headless, editor CLOSED (-nullrhi fine).  Re-runnable (second run = 0 changes).
"""
import os
import unreal

SWITCH = os.environ.get("ROSE_SWITCH", "bHasMetallicRoughness")
ROOT = os.environ.get("ROSE_ROOT", "/Game")

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()

def parent_is_gltf(mi, depth=0):
    if depth > 4 or mi is None:
        return False
    p = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else None
    if p is None:
        return False
    if p.get_name().startswith("M_GLTF"):
        return True
    return parent_is_gltf(p, depth + 1)


ar_filter = unreal.ARFilter(
    class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "MaterialInstanceConstant")],
    package_paths=[ROOT], recursive_paths=True, recursive_classes=True)
assets = REG.get_assets(ar_filter) or []
print(f"[metal] {len(assets)} MaterialInstanceConstants under {ROOT}")

first_dump = True
fixed = skipped = 0
for ad in assets:
    path = str(ad.package_name)
    mi = EAL.load_asset(path)
    if not isinstance(mi, unreal.MaterialInstanceConstant) or not parent_is_gltf(mi):
        continue
    if first_dump:
        first_dump = False
        sw_api = [m for m in dir(mi) if "switch" in m.lower() or "static" in m.lower()]
        print(f"[metal] sample {mi.get_name()}: switch-ish members: {sw_api[:8]}")
        print(f"[metal] sample get({SWITCH}) = "
              f"{MEL.get_material_instance_static_switch_parameter_value(mi, SWITCH)}")
    try:
        cur = MEL.get_material_instance_static_switch_parameter_value(mi, SWITCH)
    except Exception:
        cur = False
    if not cur:
        skipped += 1
        continue
    MEL.set_material_instance_static_switch_parameter_value(mi, SWITCH, False)
    MEL.update_material_instance(mi)
    EAL.save_asset(path)
    fixed += 1
    if fixed % 200 == 0:
        print(f"[metal] ... {fixed} flipped so far")

print(f"[metal] done: {fixed} instances -> {SWITCH}=False ({skipped} already off)")
