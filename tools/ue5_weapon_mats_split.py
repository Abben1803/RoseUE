"""ue5_weapon_mats_split.py — corrective pass over WeaponsStatic materials.

A blanket M_RoseFoliage reparent hit ALL 1,686 weapon materials, but only the
94 true-cutout weapons (is_alpha && alpha_test — tools/_tmp/weapon_mask_ids.json)
should be MASKED.  On every other weapon the DDS alpha is SPECULAR, so masking
punches holes wherever specular < the clip value.

Split: weapon id in the mask set -> keep M_RoseFoliage (masked, correct);
otherwise -> reparent to M_RoseItemOpaque (dup of M_RoseChar forced OPAQUE —
same "BaseColor" texture param, alpha ignored).  Re-runnable.

Headless, editor CLOSED, -nullrhi fine.
"""
import json
import os
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()

ROOT = "/Game/Characters/Modular/WeaponsStatic"
FOLIAGE = "/Game/Materials/M_RoseFoliage"
OPAQUE = "/Game/Materials/M_RoseItemOpaque"
SRC_MASTER = "/Game/Characters/Materials/M_RoseChar"
IDS_JSON = r"C:\rose-next-classic\tools\_tmp\weapon_mask_ids.json"

mask_ids = {int(i) for i in json.load(open(IDS_JSON))}
unreal.log(f"[wsplit] {len(mask_ids)} cutout weapons keep MASKED")


def ensure_opaque():
    m = EAL.load_asset(OPAQUE)
    if isinstance(m, unreal.Material):
        return m
    m = EAL.duplicate_asset(SRC_MASTER, OPAQUE)
    if not m:
        raise RuntimeError("could not dup M_RoseChar -> M_RoseItemOpaque")
    m.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    MEL.recompile_material(m)
    EAL.save_asset(OPAQUE)
    unreal.log("[wsplit] created M_RoseItemOpaque")
    return m


def weapon_id_of(pkg):
    # .../WeaponsStatic/weapon_<id>[_off]/...
    for seg in pkg.split("/"):
        if seg.startswith("weapon_"):
            tail = seg[7:]
            if tail.endswith("_off"):
                tail = tail[:-4]
            try:
                return int(tail)
            except ValueError:
                return -1
    return -1


op = ensure_opaque()
fol = EAL.load_asset(FOLIAGE)

flt = unreal.ARFilter(
    class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "MaterialInstanceConstant")],
    package_paths=[ROOT], recursive_paths=True, recursive_classes=True)

to_opaque = kept = other = 0
for ad in (REG.get_assets(flt) or []):
    pkg = str(ad.package_name)
    mi = EAL.load_asset(pkg)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        continue
    parent = mi.get_editor_property("parent")
    if parent is None or parent.get_name() != "M_RoseFoliage":
        other += 1
        continue
    if weapon_id_of(pkg) in mask_ids:
        kept += 1                    # true cutout — masked is right
        continue
    mi.set_editor_property("parent", op)   # texture param name is shared
    MEL.update_material_instance(mi)
    EAL.save_asset(pkg)
    to_opaque += 1
    if to_opaque % 200 == 0:
        unreal.log(f"[wsplit] ... {to_opaque} -> opaque")

unreal.log(f"[wsplit] DONE: {to_opaque} -> M_RoseItemOpaque, {kept} kept masked, {other} untouched")
