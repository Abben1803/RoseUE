"""ue5_back_masked_fix.py — make static BACK-item cutouts actually transparent.

Interchange imports the back items' masked materials as instances of the
Substrate M_GLTF, whose MASKED path does NOT drive coverage from base-color
alpha in this UE 5.8 build (see memory: foliage-masked-black).  Result: the
wing's transparent background renders opaque (black/white quad around the wing).

Fix = the proven foliage fix, scoped to the back items: reparent each masked
M_GLTF instance to the known-good masked master M_RoseFoliage (dup of
M_RoseChar: BLEND_MASKED, two-sided, DefaultLit, "BaseColor" texture whose alpha
drives the opacity mask), copying its BaseColorTexture into "BaseColor" and
clearing the blend overrides.  Re-runnable (converted MIs no longer parent to
M_GLTF, so they're skipped).

Headless, editor CLOSED, -nullrhi fine.
"""
import unreal

import os
MASTER = "/Game/Materials/M_RoseFoliage"           # already exists (foliage fix)
SRC_MASTER = "/Game/Characters/Materials/M_RoseChar"
# Also used for other masked static-item families (ROSE_MASKED_ROOT env), e.g.
# /Game/Characters/Modular/SubWpnStatic and .../WeaponsStatic after their
# masked rebuilds.
BACK_ROOT = os.environ.get("ROSE_MASKED_ROOT", "/Game/Characters/Modular/BackStatic")

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()


def ensure_master():
    m = EAL.load_asset(MASTER)
    if isinstance(m, unreal.Material):
        return m
    m = EAL.duplicate_asset(SRC_MASTER, MASTER)
    if not m:
        raise RuntimeError("could not create M_RoseFoliage from M_RoseChar")
    m.set_editor_property("opacity_mask_clip_value", 0.5)
    MEL.recompile_material(m)
    EAL.save_asset(MASTER)
    print(f"[backmat] created {MASTER}")
    return m


def base_texture(mi):
    for tp in mi.get_editor_property("texture_parameter_values"):
        nm = str(tp.get_editor_property("parameter_info").get_editor_property("name"))
        if nm == "BaseColorTexture":
            return tp.get_editor_property("parameter_value")
    return None


def clear_blend_overrides(mi):
    ov = mi.get_editor_property("base_property_overrides")
    for p in ("override_blend_mode", "override_two_sided",
              "override_opacity_mask_clip_value", "override_shading_model"):
        ov.set_editor_property(p, False)
    mi.set_editor_property("base_property_overrides", ov)


def parent_is_gltf(mi):
    p = mi.get_editor_property("parent")
    return p is not None and p.get_name().startswith("M_GLTF")


master = ensure_master()

flt = unreal.ARFilter(
    class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "MaterialInstanceConstant")],
    package_paths=[BACK_ROOT], recursive_paths=True, recursive_classes=True)

n = skipped = notex = 0
for ad in (REG.get_assets(flt) or []):
    pkg = str(ad.package_name)
    mi = EAL.load_asset(pkg)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        continue
    if not parent_is_gltf(mi):
        skipped += 1
        continue
    tex = base_texture(mi)
    if tex is None:
        print(f"[backmat] no BaseColorTexture: {pkg}")
        notex += 1
        continue
    mi.set_editor_property("parent", master)
    MEL.set_material_instance_texture_parameter_value(mi, "BaseColor", tex)
    clear_blend_overrides(mi)
    MEL.update_material_instance(mi)
    EAL.save_asset(pkg)
    n += 1
    if n % 100 == 0:
        print(f"[backmat] ... {n} reparented")

print(f"[backmat] DONE: {n} reparented, {skipped} already-converted, {notex} no-texture")
