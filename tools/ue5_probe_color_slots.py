"""
ue5_probe_color_slots.py — list Scene static-mesh material SLOTS whose material
references no texture (the color-only ROSE collision/blocker materials that
render as pink translucent boxes).  Probe only — prints, changes nothing.

Env: ROSE_ZONE=JPT01 (default).
"""
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "JPT01").upper()
SCENE = f"/Game/Maps/{ZONE}/Scene"

EAL = unreal.EditorAssetLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()
DEP_OPT = unreal.AssetRegistryDependencyOptions()

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


def mat_textured(mat):
    if mat is None:
        return True   # empty slot — leave alone
    if isinstance(mat, unreal.MaterialInstanceConstant):
        if mat.get_editor_property("texture_parameter_values"):
            return True
        p = mat.get_editor_property("parent")
        if p is not None:
            return mat_textured(p)
    return package_has_texture(mat.get_path_name().split(".")[0])


found = 0
meshes = 0
for path in EAL.list_assets(SCENE, recursive=True):
    clean = str(path).split(".")[0]
    sm = EAL.load_asset(clean)
    if not isinstance(sm, unreal.StaticMesh):
        continue
    meshes += 1
    n = sm.get_num_sections(0)
    mats = sm.get_editor_property("static_materials")
    for i, entry in enumerate(mats):
        mat = entry.get_editor_property("material_interface")
        if mat is None or mat_textured(mat):
            continue
        found += 1
        if found <= 40:
            print(f"[probe] {clean.rsplit('/',1)[-1]} slot {i}: {mat.get_name()} "
                  f"({type(mat).__name__})")
print(f"[probe] {ZONE}: {found} color-only slots across {meshes} scene meshes")
