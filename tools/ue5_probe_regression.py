"""Confirm (1) white-leaf cause: M_RoseFoliage instances whose real color is in
DiffuseTexture (spec-gloss) not BaseColorTexture; (2) modular character part
materials. REPORT ONLY."""
import unreal
EAL = unreal.EditorAssetLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()


def tex_param(mi, name):
    for tp in mi.get_editor_property("texture_parameter_values"):
        if str(tp.get_editor_property("parameter_info").get_editor_property("name")) == name:
            return tp.get_editor_property("parameter_value")
    return None


def switch_param(mi, name):
    for sp in mi.get_editor_property("static_switch_parameter_values") \
            if hasattr(mi, "static_switch_parameter_values") else []:
        pass
    return None


# ── (1) white leaves: spec-gloss check on M_RoseFoliage instances ─────────────
flt = unreal.ARFilter(
    class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "MaterialInstanceConstant")],
    package_paths=["/Game/Maps"], recursive_paths=True, recursive_classes=True)
onfol = has_diffuse = base_eq_diffuse = base_diff_mismatch = 0
mismatch_ex = []
for ad in (REG.get_assets(flt) or []):
    mi = EAL.load_asset(str(ad.package_name))
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        continue
    p = mi.get_editor_property("parent")
    if p is None or p.get_name() != "M_RoseFoliage":
        continue
    onfol += 1
    base = tex_param(mi, "BaseColor")
    diff = tex_param(mi, "DiffuseTexture")
    if diff is not None:
        has_diffuse += 1
        bn = base.get_name() if base else None
        dn = diff.get_name()
        if bn == dn:
            base_eq_diffuse += 1
        else:
            base_diff_mismatch += 1
            if len(mismatch_ex) < 10:
                mismatch_ex.append((str(ad.asset_name), bn, dn))

print(f"[reg] M_RoseFoliage instances: {onfol}")
print(f"[reg]   with DiffuseTexture (spec-gloss): {has_diffuse}")
print(f"[reg]   BaseColor==DiffuseTexture: {base_eq_diffuse}   MISMATCH (white suspect): {base_diff_mismatch}")
for e in mismatch_ex:
    print(f"[reg]      mismatch: name={e[0]} BaseColor={e[1]} Diffuse={e[2]}")

# ── (2) modular character parts (the char-select avatar parts) ────────────────
print("\n[reg] modular Female part materials:")
for slot, pkgdir in (("body_1", "body_1"), ("hair_110", "hair_110"),
                     ("foot_1", "foot_1"), ("arms_1", "arms_1"), ("face_1", "face_1")):
    # scan the modular folder for its MI
    mflt = unreal.ARFilter(
        class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "MaterialInstanceConstant")],
        package_paths=[f"/Game/Characters/Modular/Female/{pkgdir}"],
        recursive_paths=True, recursive_classes=True)
    found = REG.get_assets(mflt) or []
    for ad in found[:2]:
        mi = EAL.load_asset(str(ad.package_name))
        if not isinstance(mi, unreal.MaterialInstanceConstant):
            continue
        p = mi.get_editor_property("parent")
        ov = mi.get_editor_property("base_property_overrides")
        bm = ov.get_editor_property("blend_mode") if ov.get_editor_property("override_blend_mode") else "inherit"
        base = tex_param(mi, "BaseColorTexture") or tex_param(mi, "DiffuseTexture")
        print(f"[reg]   {slot}: {ad.asset_name} parent={p.get_name() if p else None} "
              f"blend={bm} tex={base.get_name() if base else None}")
