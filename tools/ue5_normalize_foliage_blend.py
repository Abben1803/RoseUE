"""
ue5_normalize_foliage_blend.py — Set every map foliage MI's blend mode to the
TEXTURE TRUTH: cutout leaf-cards -> MASKED (clip 0.5, two-sided), solid
crown/bark -> OPAQUE.  Fixes 45 leaf-cards stuck on Translucent/Substrate
(dark, bad-sorting foliage) and normalises stray masked solids.

Reads the plan built offline against the source DDS alpha:
    tools/_tmp/foliage_blend_plan.json = { "/Game/Maps/.../MI" : "MASKED"|"OPAQUE" }

MIs update IN PLACE (never delete+create).  Headless, editor CLOSED, -nullrhi
fine (blend change recompiles when the editor next loads).  Re-runnable.
"""
import json, os
import unreal

PLAN = r"C:\rose-next-classic\tools\_tmp\foliage_blend_plan.json"
EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
BM = unreal.BlendMode

plan = json.load(open(PLAN))
print(f"[folblend] plan: {len(plan)} MIs")

done = {"MASKED": 0, "OPAQUE": 0}
missing = 0
for pkg, want in plan.items():
    mi = EAL.load_asset(pkg)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        missing += 1
        continue
    ov = mi.get_editor_property("base_property_overrides")
    ov.set_editor_property("override_blend_mode", True)
    if want == "MASKED":
        ov.set_editor_property("blend_mode", BM.BLEND_MASKED)
        ov.set_editor_property("override_opacity_mask_clip_value", True)
        ov.set_editor_property("opacity_mask_clip_value", 0.5)
        ov.set_editor_property("override_two_sided", True)
        ov.set_editor_property("two_sided", True)
    else:  # OPAQUE
        ov.set_editor_property("blend_mode", BM.BLEND_OPAQUE)
    mi.set_editor_property("base_property_overrides", ov)
    ME.update_material_instance(mi)
    EAL.save_asset(pkg)
    done[want] += 1

print(f"[folblend] done: {done['MASKED']} -> MASKED, {done['OPAQUE']} -> OPAQUE, {missing} missing")
