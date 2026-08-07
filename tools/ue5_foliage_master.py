"""
ue5_foliage_master.py — fix the universal black-between-leaves on masked foliage.

Root cause: foliage MIs are instances of the Substrate M_GLTF whose MASKED path
does not drive coverage from the base-color alpha, so masked foliage renders its
black background opaque (see memory: foliage-masked-black).

Fix: reparent the masked foliage MIs to M_RoseFoliage — a duplicate of the known-
good M_RoseChar (BLEND_MASKED, two-sided, DefaultLit, texture param "BaseColor"
whose alpha drives the opacity mask; it already cuts out character hair correctly
in this Substrate project).  Each MI keeps its own texture (copied from its old
M_GLTF "BaseColorTexture" param into the new "BaseColor" param), and its blend
overrides are cleared so it inherits the master's working masked setup.

The same Substrate M_GLTF masked bug hits EVERY masked instance (not just
foliage — buildings/props/decals with alpha cutouts show the same black), so
ROSE_ALLMASKED sweeps all of them.

Env:
  ROSE_ALL=1        sweep masked MIs under /Game/Maps
  ROSE_ALLMASKED=1  with ROSE_ALL: reparent EVERY masked M_GLTF MI (drop the
                    foliage-name filter) — fixes the black on all cutout materials
  (default)         apply only to ROSE_TARGET
  ROSE_TARGET=...   package path of one MI to test
                    (default /Game/Maps/JG07/Scene/JG07/Materials/M53_TREE001)

Headless, editor CLOSED, -nullrhi fine.  Re-runnable (converted MIs no longer
have a masked blend override / M_GLTF parent, so they're skipped next pass).
"""
import os
import unreal

SRC_MASTER = "/Game/Characters/Materials/M_RoseChar"
MASTER = "/Game/Materials/M_RoseFoliage"
ALL = os.environ.get("ROSE_ALL", "0") == "1"
ALLMASKED = os.environ.get("ROSE_ALLMASKED", "0") == "1"
TARGET = os.environ.get("ROSE_TARGET",
                        "/Game/Maps/JG07/Scene/JG07/Materials/M53_TREE001")

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()
KEYS = ("TREE", "GRASS", "LEAF", "VINE", "BUSH", "PLANT", "FLOWER")


def ensure_master():
    m = EAL.load_asset(MASTER)
    if isinstance(m, unreal.Material):
        return m
    if not EAL.does_asset_exist(SRC_MASTER):
        raise RuntimeError(f"source master missing: {SRC_MASTER}")
    m = EAL.duplicate_asset(SRC_MASTER, MASTER)
    if not m:
        raise RuntimeError("duplicate M_RoseChar -> M_RoseFoliage failed")
    # foliage-appropriate cutout threshold
    m.set_editor_property("opacity_mask_clip_value", 0.5)
    MEL.recompile_material(m)
    EAL.save_asset(MASTER)
    print(f"[folmat] created {MASTER} (dup of M_RoseChar)")
    return m


def base_texture(mi):
    for tp in mi.get_editor_property("texture_parameter_values"):
        nm = str(tp.get_editor_property("parameter_info").get_editor_property("name"))
        if nm == "BaseColorTexture":
            return tp.get_editor_property("parameter_value")
    return None


def clear_blend_overrides(mi):
    ov = mi.get_editor_property("base_property_overrides")
    ov.set_editor_property("override_blend_mode", False)
    ov.set_editor_property("override_two_sided", False)
    ov.set_editor_property("override_opacity_mask_clip_value", False)
    ov.set_editor_property("override_shading_model", False)
    mi.set_editor_property("base_property_overrides", ov)


def parent_is_gltf(mi):
    p = mi.get_editor_property("parent")
    return p is not None and p.get_name().startswith("M_GLTF")


def is_cutout(mi):
    # The glTF-authored AlphaMode: 1.0 = MASK (cutout).  This is the reliable
    # signal — some cutout foliage got imported as Translucent/colored-
    # transmittance (wrong blend), so selecting by blend mode misses them, but
    # their AlphaMode scalar is still 1.  (0 = Opaque, 2 = genuine Blend.)
    for sp in mi.get_editor_property("scalar_parameter_values"):
        if str(sp.get_editor_property("parameter_info").get_editor_property("name")) == "AlphaMode":
            return abs(float(sp.get_editor_property("parameter_value")) - 1.0) < 0.01
    # fallback: a masked blend override
    ov = mi.get_editor_property("base_property_overrides")
    return ov.get_editor_property("override_blend_mode") and \
        ov.get_editor_property("blend_mode") == unreal.BlendMode.BLEND_MASKED


def convert(pkg, master):
    mi = EAL.load_asset(pkg)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        print(f"[folmat] not a MIC: {pkg}"); return False
    if not parent_is_gltf(mi):
        return False                       # already converted / not an M_GLTF instance
    tex = base_texture(mi)
    if tex is None:
        print(f"[folmat] no BaseColorTexture on {pkg} — skipped"); return False
    mi.set_editor_property("parent", master)
    MEL.set_material_instance_texture_parameter_value(mi, "BaseColor", tex)
    clear_blend_overrides(mi)
    MEL.update_material_instance(mi)
    EAL.save_asset(pkg)
    return True


master = ensure_master()

if not ALL:
    ok = convert(TARGET, master)
    print(f"[folmat] TEST applied to {TARGET}: {ok}")
else:
    flt = unreal.ARFilter(
        class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "MaterialInstanceConstant")],
        package_paths=["/Game/Maps"], recursive_paths=True, recursive_classes=True)
    n = 0
    for ad in (REG.get_assets(flt) or []):
        name = str(ad.asset_name).upper()
        if not ALLMASKED and not any(k in name for k in KEYS):
            continue                       # foliage-only unless ROSE_ALLMASKED
        mi = EAL.load_asset(str(ad.package_name))
        if not isinstance(mi, unreal.MaterialInstanceConstant):
            continue
        if not parent_is_gltf(mi) or not is_cutout(mi):
            continue                       # leave opaque + genuine-blend materials
        if convert(str(ad.package_name), master):
            n += 1
            if n % 100 == 0:
                print(f"[folmat] ... {n} converted")
    scope = "masked M_GLTF" if ALLMASKED else "masked foliage"
    print(f"[folmat] DONE: {n} {scope} MIs reparented to {MASTER}")
