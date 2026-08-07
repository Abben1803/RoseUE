"""
ue5_remove_metallic_factor.py — REMOVE the MetallicFactor scalar parameter
OVERRIDE from every Interchange (M_GLTF-parented) MaterialInstanceConstant —
the equivalent of un-checking the parameter in the Details panel.

Interchange stamps MetallicFactor per instance from the glTF PBR block; ROSE
assets are classic diffuse and shouldn't carry it (the user un-checked a few by
hand — this does the rest).  bHasMetallicRoughness was already verified off on
all instances, so this has no visual effect beyond cleaning the override.

Env: ROSE_PARAM (default MetallicFactor), ROSE_ROOT (default /Game),
     ROSE_GLTF_ONLY (default 0 — set 1 to restrict to M_GLTF-parented instances).
Headless, editor CLOSED (-nullrhi fine).  Re-runnable (second run = 0 changes).
"""
import os
import unreal

PARAM = os.environ.get("ROSE_PARAM", "MetallicFactor")
ROOT = os.environ.get("ROSE_ROOT", "/Game")
GLTF_ONLY = os.environ.get("ROSE_GLTF_ONLY", "0") == "1"

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
print(f"[metalfac] {len(assets)} MaterialInstanceConstants under {ROOT}")

fixed = skipped = 0
for ad in assets:
    path = str(ad.package_name)
    mi = EAL.load_asset(path)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        continue
    if GLTF_ONLY and not parent_is_gltf(mi):
        continue
    vals = list(mi.get_editor_property("scalar_parameter_values"))
    kept = [v for v in vals
            if str(v.get_editor_property("parameter_info").get_editor_property("name")) != PARAM]
    if len(kept) == len(vals):
        skipped += 1
        continue
    mi.set_editor_property("scalar_parameter_values", kept)
    MEL.update_material_instance(mi)
    EAL.save_asset(path)
    fixed += 1
    if fixed % 500 == 0:
        print(f"[metalfac] ... {fixed} cleaned so far")

print(f"[metalfac] done: {fixed} instances had the {PARAM} override removed "
      f"({skipped} already clean)")
