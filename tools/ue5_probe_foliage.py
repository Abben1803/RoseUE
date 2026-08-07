"""
ue5_probe_foliage.py — REPORT ONLY. Classify map foliage MIs as leaf-CARD
(cutout, needs MASKED) vs SOLID (…B crown/bark, OPAQUE ok) by texture-name
suffix, and cross-reference with each MI's effective blend mode.  The bug we
hunt: a leaf CARD that is NOT masked -> renders its black background as solid
black quads (the universal black-foliage artifact).

Headless, editor CLOSED, -nullrhi fine.  No assets modified.
"""
import unreal
from collections import Counter

EAL = unreal.EditorAssetLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()
KEYS = ("TREE", "GRASS", "LEAF", "VINE", "BUSH", "PLANT", "FLOWER")

def is_foliage(n):
    u = n.upper(); return any(k in u for k in KEYS)

def texbase(mi_name):
    # "M53_TREE001" -> "TREE001"
    return mi_name.split("_", 1)[1] if "_" in mi_name else mi_name

def is_solid(mi_name):
    # …B suffix = bark/crown solid texture (alpha all-255)
    return texbase(mi_name).upper().rstrip("0123456789").endswith("B") is False and \
           texbase(mi_name).upper().endswith("B")

flt = unreal.ARFilter(
    class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "MaterialInstanceConstant")],
    package_paths=["/Game/Maps"], recursive_paths=True, recursive_classes=True)
assets = REG.get_assets(flt) or []

def eff_blend(mi):
    ov = mi.get_editor_property("base_property_overrides")
    if ov.get_editor_property("override_blend_mode"):
        return ov.get_editor_property("blend_mode")
    p = mi.get_editor_property("parent")
    # walk parents for an override / base
    while isinstance(p, unreal.MaterialInstance):
        pov = p.get_editor_property("base_property_overrides")
        if pov.get_editor_property("override_blend_mode"):
            return pov.get_editor_property("blend_mode")
        p = p.get_editor_property("parent")
    if isinstance(p, unreal.Material):
        return p.get_editor_property("blend_mode")
    return None

card_not_masked = []
solid_masked = []
counts = Counter()
for ad in assets:
    name = str(ad.asset_name)
    if not is_foliage(name):
        continue
    tb = texbase(name).upper()
    solid = tb.endswith("B")
    mi = EAL.load_asset(str(ad.package_name))
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        continue
    bm = eff_blend(mi)
    masked = (bm == unreal.BlendMode.BLEND_MASKED)
    kind = "solid" if solid else "card"
    counts[f"{kind}/{'masked' if masked else str(bm)}"] += 1
    if (not solid) and (not masked):
        card_not_masked.append((name, str(bm)))
    if solid and masked:
        solid_masked.append((name, str(bm)))

print("[foliage2] classification x blend:")
for k, v in sorted(counts.items()):
    print(f"    {k:45s} {v}")

# Dump full inventory (name, texbase, effective blend, texture asset + alpha) to JSON
import json, os
inv = []
for ad in assets:
    name = str(ad.asset_name)
    if not is_foliage(name):
        continue
    mi = EAL.load_asset(str(ad.package_name))
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        continue
    bm = eff_blend(mi)
    # find base-color texture parameter (Interchange names vary) -> report first texture + alpha channel presence
    tex_name = None; has_alpha = None
    for tp in mi.get_editor_property("texture_parameter_values"):
        tv = tp.get_editor_property("parameter_value")
        if tv is None:
            continue
        pn = str(tp.get_editor_property("parameter_info").get_editor_property("name")).lower()
        if "basecolor" in pn or "base_color" in pn or "diffuse" in pn or tex_name is None:
            tex_name = tv.get_name()
            try:
                has_alpha = not bool(tv.get_editor_property("compression_no_alpha"))
            except Exception:
                has_alpha = None
            if "basecolor" in pn or "diffuse" in pn:
                break
    inv.append({"mi": str(ad.package_name), "name": name, "texbase": texbase(name),
                "blend": str(bm), "tex": tex_name, "tex_has_alpha": has_alpha})

outp = os.path.join(os.environ.get("TEMP", "C:/Temp"), "foliage_inventory.json")
try:
    with open(r"C:\rose-next-classic\tools\_tmp\foliage_inventory.json", "w") as f:
        json.dump(inv, f, indent=0)
    print(f"[foliage2] wrote inventory: tools/_tmp/foliage_inventory.json ({len(inv)} MIs)")
except Exception as e:
    print(f"[foliage2] inventory write error: {e}")
