"""
ue5_char_material_fix.py — fix modular character parts going BLACK in shadow.

Cause: modular part MIs are instances of the Substrate M_GLTF, whose shading
model is Subsurface Profile with no profile assigned -> unlit/shadow areas render
black (hair AND skin, in-game and on the char-select screen).

Fix: reparent every M_GLTF instance under /Game/Characters/Modular to the known-
good Default-Lit M_RoseChar (already used by 18 character materials that render
correctly), copying each MI's BaseColorTexture (or DiffuseTexture) into M_RoseChar's
"BaseColor" param and clearing its blend/shading overrides so it inherits the
working masked+DefaultLit setup.

Env:
  ROSE_ROOT   default /Game/Characters/Modular
Headless, editor CLOSED, -nullrhi fine.  Re-runnable (converted MIs skipped).
"""
import os
import unreal

MASTER = "/Game/Characters/Materials/M_RoseChar"
ROOT = os.environ.get("ROSE_ROOT", "/Game/Characters/Modular")

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()

master = EAL.load_asset(MASTER)
if not isinstance(master, unreal.Material):
    raise RuntimeError(f"master missing: {MASTER}")


def tex_param(mi, name):
    for tp in mi.get_editor_property("texture_parameter_values"):
        if str(tp.get_editor_property("parameter_info").get_editor_property("name")) == name:
            return tp.get_editor_property("parameter_value")
    return None


def parent_is_gltf(mi):
    p = mi.get_editor_property("parent")
    return p is not None and p.get_name().startswith("M_GLTF")


def clear_overrides(mi):
    ov = mi.get_editor_property("base_property_overrides")
    for f in ("override_blend_mode", "override_two_sided",
              "override_opacity_mask_clip_value", "override_shading_model"):
        ov.set_editor_property(f, False)
    mi.set_editor_property("base_property_overrides", ov)


flt = unreal.ARFilter(
    class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "MaterialInstanceConstant")],
    package_paths=[ROOT], recursive_paths=True, recursive_classes=True)
assets = REG.get_assets(flt) or []
print(f"[charmat] {len(assets)} MIC under {ROOT}")

n = skipped = notex = 0
for ad in assets:
    mi = EAL.load_asset(str(ad.package_name))
    if not isinstance(mi, unreal.MaterialInstanceConstant) or not parent_is_gltf(mi):
        skipped += 1
        continue
    tex = tex_param(mi, "BaseColorTexture") or tex_param(mi, "DiffuseTexture")
    if tex is None:
        notex += 1
        continue
    mi.set_editor_property("parent", master)
    MEL.set_material_instance_texture_parameter_value(mi, "BaseColor", tex)
    clear_overrides(mi)
    MEL.update_material_instance(mi)
    EAL.save_asset(str(ad.package_name))
    n += 1
    if n % 200 == 0:
        print(f"[charmat] ... {n} converted")

print(f"[charmat] DONE: {n} modular part MIs reparented to M_RoseChar "
      f"({skipped} not M_GLTF, {notex} no texture)")
