"""
ue5_hide_color_slots.py — swap the color-only ROSE blocker material SLOTS on the
map Scene meshes to the invisible M_Hidden material.

ROSE zones carry gameplay-blocker geometry whose ZSC materials are COLOR-ONLY
(no texture) — never meant to render.  Interchange imported them as `M*_color`
MaterialInstanceConstants that draw as pink translucent boxes in game.  The fix
is per-SLOT on the mesh ASSET (fixes every placed instance, keeps collision —
traces hit triangles regardless of material).

Detection = name ends "_color" AND the material references no texture (asset
registry dependency walk).  Verified on JPT01: exactly the 47 slots the atlas
pass left untouched ("color-only collision materials").

Env: ROSE_ZONE=JPT01 for one zone, else ALL /Game/Maps/<Z>/Scene folders.
Run headless, editor CLOSED (-nullrhi fine).  Re-runnable.
"""
import os
import unreal

ONLY = os.environ.get("ROSE_ZONE", "").upper()
HIDDEN_PATH = "/Game/Characters/Materials/M_Hidden"

EAL = unreal.EditorAssetLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()
DEP_OPT = unreal.AssetRegistryDependencyOptions()

hidden_mat = EAL.load_asset(HIDDEN_PATH)
if not hidden_mat:
    raise RuntimeError(f"M_Hidden missing at {HIDDEN_PATH}")

_tex_cache = {}


def package_has_texture(pkg):
    if pkg in _tex_cache:
        return _tex_cache[pkg]
    r = False
    for dep in REG.get_dependencies(unreal.Name(pkg), DEP_OPT) or []:
        d = str(dep)
        if not (d.startswith("/Game") or d.startswith("/Engine")):
            continue
        for asset in REG.get_assets_by_package_name(unreal.Name(d)) or []:
            if "Texture" in str(asset.asset_class_path.asset_name):
                r = True
                break
        if r:
            break
    _tex_cache[pkg] = r
    return r


def is_color_only(mat):
    if mat is None:
        return False
    if not mat.get_name().endswith("_color"):
        return False
    if isinstance(mat, unreal.MaterialInstanceConstant):
        if mat.get_editor_property("texture_parameter_values"):
            return False
    return not package_has_texture(mat.get_path_name().split(".")[0])


def zones():
    out = []
    for folder in EAL.list_assets("/Game/Maps", recursive=False, include_folder=True):
        f = str(folder).rstrip("/")
        z = f.rsplit("/", 1)[-1]
        if ONLY and z != ONLY:
            continue
        if EAL.does_directory_exist(f + "/Scene"):
            out.append((z, f + "/Scene"))
    return out


total = 0
for zone, scene in zones():
    fixed = 0
    for path in EAL.list_assets(scene, recursive=True):
        clean = str(path).split(".")[0]
        sm = EAL.load_asset(clean)
        if not isinstance(sm, unreal.StaticMesh):
            continue
        mats = sm.get_editor_property("static_materials")
        changed = False
        for i in range(len(mats)):
            m = mats[i].get_editor_property("material_interface")
            if is_color_only(m):
                sm.set_material(i, hidden_mat)
                changed = True
                fixed += 1
        if changed:
            EAL.save_asset(clean)
    total += fixed
    print(f"[colorslots] {zone}: {fixed} slots -> M_Hidden")
print(f"[colorslots] done: {total} color-only slots hidden")
