"""
ue5_foliage_nocollision.py — make foliage non-blocking.  In ROSE the visual
foliage never blocks movement (that comes from the separate MOV/IFO collision
grid the map import bakes as hidden collision); flowers/bushes/grass/trees
should let the player walk straight through.

Per map level: every StaticMeshComponent whose material is foliage (name has a
TREE/GRASS/FLOWER/BUSH/PLANT/VINE/LEAF keyword) gets collision disabled.

Env:
  ROSE_ZONE   zone key, or ALL (default) = every /Game/Maps/<Z>/L_<Z>

Headless, editor CLOSED.  Re-runnable.
"""
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "ALL").upper()
KEYS = ("TREE", "GRASS", "LEAF", "VINE", "BUSH", "PLANT", "FLOWER")

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
EAL = unreal.EditorAssetLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m): unreal.log(f"[folcol] {m}")


def is_foliage_comp(comp):
    for m in comp.get_materials():
        if m and any(k in m.get_name().upper() for k in KEYS):
            return True
    return False


def do_zone(level_path):
    if not EAL.does_asset_exist(level_path):
        return
    if not LES.load_level(level_path):
        log(f"could not load {level_path}"); return
    changed = 0
    for a in EAS.get_all_level_actors():
        for comp in a.get_components_by_class(unreal.StaticMeshComponent):
            if comp.get_collision_enabled() == unreal.CollisionEnabled.NO_COLLISION:
                continue
            if is_foliage_comp(comp):
                comp.set_collision_enabled(unreal.CollisionEnabled.NO_COLLISION)
                changed += 1
    if changed:
        LES.save_current_level()
    log(f"{level_path.split('/')[-1]}: {changed} foliage components -> NoCollision")


def all_levels():
    flt = unreal.ARFilter(
        class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "World")],
        package_paths=["/Game/Maps"], recursive_paths=True, recursive_classes=True)
    out = []
    for ad in (REG.get_assets(flt) or []):
        name = str(ad.asset_name)
        if name.startswith("L_"):
            out.append(str(ad.package_name))
    return sorted(out)


if ZONE == "ALL":
    for lvl in all_levels():
        do_zone(lvl)
else:
    do_zone(f"/Game/Maps/{ZONE}/L_{ZONE}")
log("DONE")
