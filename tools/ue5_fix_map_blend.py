"""
ue5_fix_map_blend.py — Convert the already-created TRANSLUCENT map MIs (the
"_T" BLEND decals: terrain grass layers, foliage cutouts) to MASKED.

Why: translucent materials don't write depth and sort per-object, so the map's
decal layers drew over/through each other — the "overlapping textures" bug.
Masked writes depth (perfect sorting) and matches the classic client, which
renders these with alpha test + z-write.  Water is untouched (its own master).

MIs update IN PLACE (never delete+create).  Run headless, editor CLOSED
(-nullrhi fine — the blend-mode change recompiles when the editor next loads).
"""
import unreal

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
MI_ROOT = "/Game/Atlas/MI"
# Terrain is NO LONGER a decal layer. It is one opaque mesh per tile whose
# material (M_RoseTerrain) lerps the two tile maps by the top map's alpha,
# exactly as src/engine/shader/terrain.psh does — a continuous blend, nothing
# to sort. Masking it at 0.33 is what produced the hard-edged ground patches.
# Foliage/cutout decals still belong here (see also M_RoseFoliage).
TERRAIN_PREFIX = "MI_TERRAIN_"

fixed = skipped = terrain = 0
for path in EAL.list_assets(MI_ROOT, recursive=False):
    clean = str(path).split(".")[0]
    if clean.rsplit("/", 1)[-1].startswith(TERRAIN_PREFIX):
        terrain += 1
        continue
    mi = EAL.load_asset(clean)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        continue
    ov = mi.get_editor_property("base_property_overrides")
    if not ov.get_editor_property("override_blend_mode") \
       or ov.get_editor_property("blend_mode") != unreal.BlendMode.BLEND_TRANSLUCENT:
        skipped += 1
        continue
    ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    ov.set_editor_property("override_opacity_mask_clip_value", True)
    ov.set_editor_property("opacity_mask_clip_value", 0.33)
    mi.set_editor_property("base_property_overrides", ov)
    ME.update_material_instance(mi)
    EAL.save_asset(clean)
    fixed += 1

print(f"[mapblend] {fixed} translucent map MIs -> MASKED (clip 0.33); "
      f"{skipped} already fine; {terrain} terrain MIs left opaque (by design)")
