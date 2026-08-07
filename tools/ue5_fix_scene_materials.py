"""
ue5_fix_scene_materials.py — apply the ROSE material policy to a zone's RAW
Interchange scene materials (zones that skipped the atlas/master-material pass,
e.g. JPT01V2 from the modern client):

  * TRANSLUCENT (glTF BLEND) → MASKED clip 0.33 — translucent doesn't write
    depth and sorts per-object = see-through/overlap artifacts; the classic
    client renders these alpha-test + z-write.
  * EVERYTHING → TWO-SIDED — glTF imports single-sided; ROSE meshes rely on
    two-sided rendering (see-through backfaces otherwise).  Same policy as
    M_RoseMaster on the atlased zones.

Works on both MaterialInstanceConstants (BasePropertyOverrides) and plain
Materials.  "*_color" materials are skipped (already slot-swapped to M_Hidden).

Env: ROSE_ZONE=JPT01V2 (default).  Headless, editor CLOSED (-nullrhi fine —
shaders recompile on next editor load).  Re-runnable.
"""
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "JPT01V2").upper()
SCENE = f"/Game/Maps/{ZONE}/Scene"

EAL = unreal.EditorAssetLibrary


def effective_blend(mat):
    if isinstance(mat, unreal.MaterialInstanceConstant):
        ov = mat.get_editor_property("base_property_overrides")
        if ov.get_editor_property("override_blend_mode"):
            return ov.get_editor_property("blend_mode")
        parent = mat.get_editor_property("parent")
        return effective_blend(parent) if parent else unreal.BlendMode.BLEND_OPAQUE
    if isinstance(mat, unreal.Material):
        return mat.get_editor_property("blend_mode")
    return unreal.BlendMode.BLEND_OPAQUE


def is_translucent(blend):
    # With Substrate on, Interchange imports glTF MASK/BLEND as
    # BLEND_TRANSLUCENT_COLORED_TRANSMITTANCE (enum 7), not BLEND_TRANSLUCENT —
    # match any translucent variant by name.
    return "TRANSLUCENT" in str(blend).upper()


masked = twosided = 0
for path in EAL.list_assets(SCENE, recursive=True):
    clean = str(path).split(".")[0]
    if clean.rsplit("/", 1)[-1].endswith("_color"):
        continue
    mat = EAL.load_asset(clean)
    changed = False

    if isinstance(mat, unreal.MaterialInstanceConstant):
        ov = mat.get_editor_property("base_property_overrides")
        if is_translucent(effective_blend(mat)):
            ov.set_editor_property("override_blend_mode", True)
            ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
            ov.set_editor_property("override_opacity_mask_clip_value", True)
            ov.set_editor_property("opacity_mask_clip_value", 0.33)
            masked += 1
            changed = True
        if not ov.get_editor_property("override_two_sided") \
           or not ov.get_editor_property("two_sided"):
            ov.set_editor_property("override_two_sided", True)
            ov.set_editor_property("two_sided", True)
            twosided += 1
            changed = True
        if changed:
            mat.set_editor_property("base_property_overrides", ov)
            unreal.MaterialEditingLibrary.update_material_instance(mat)

    elif isinstance(mat, unreal.Material):
        if is_translucent(mat.get_editor_property("blend_mode")):
            mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
            mat.set_editor_property("opacity_mask_clip_value", 0.33)
            masked += 1
            changed = True
        if not mat.get_editor_property("two_sided"):
            mat.set_editor_property("two_sided", True)
            twosided += 1
            changed = True
        if changed:
            unreal.MaterialEditingLibrary.recompile_material(mat)

    if changed:
        EAL.save_asset(clean)

print(f"[scenemat] {ZONE}: {masked} translucent->masked, {twosided} set two-sided")
