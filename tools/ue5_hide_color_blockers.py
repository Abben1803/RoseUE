"""
ue5_hide_color_blockers.py — Hide the untextured ROSE blocker meshes that show
as giant pink/translucent boxes in game.

ROSE zones carry gameplay-blocker geometry with COLOR-ONLY materials (no
texture).  The map import hides the ones under the glTF "Collision" group /
"WalkBlocked" label, but some ship inside "Objects" and stayed visible —
Interchange gives their colour materials a translucent pink-ish look.

Detection: a visible StaticMeshActor is a blocker when NONE of its material
slots' material packages reference a Texture* asset (asset-registry dependency
walk — no material-graph poking).  Water actors are skipped (special master).
Blockers keep collision, lose visibility.

Env: ROSE_ZONE=JPT01 for one zone, else ALL /Game/Maps levels.
Run headless, editor CLOSED (-nullrhi fine).  Re-runnable.
"""
import os
import unreal

ONLY = os.environ.get("ROSE_ZONE", "").upper()

EAL = unreal.EditorAssetLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
REG = unreal.AssetRegistryHelpers.get_asset_registry()
DEP_OPT = unreal.AssetRegistryDependencyOptions()

_tex_cache = {}


def package_has_texture(pkg_name):
    """True if the package (or its direct deps) contains/references a texture."""
    if pkg_name in _tex_cache:
        return _tex_cache[pkg_name]
    result = False
    for dep in REG.get_dependencies(unreal.Name(pkg_name), DEP_OPT) or []:
        d = str(dep)
        if not d.startswith("/Game") and not d.startswith("/Engine"):
            continue
        for asset in REG.get_assets_by_package_name(unreal.Name(d)) or []:
            if "Texture" in str(asset.asset_class_path.asset_name):
                result = True
                break
        if result:
            break
    _tex_cache[pkg_name] = result
    return result


def material_is_textured(mat):
    if mat is None:
        return False
    # MI with a texture parameter (our atlas MIs) — cheap positive.
    if isinstance(mat, unreal.MaterialInstanceConstant):
        if mat.get_editor_property("texture_parameter_values"):
            return True
        parent = mat.get_editor_property("parent")
        if parent is not None:
            return material_is_textured(parent)
    return package_has_texture(mat.get_path_name().split(".")[0])


def levels():
    out = []
    for path in EAL.list_assets("/Game/Maps", recursive=True, include_folder=False):
        clean = str(path).split(".")[0]
        name = clean.rsplit("/", 1)[-1]
        if not name.startswith("L_"):
            continue
        if ONLY and name[2:] != ONLY:
            continue
        a = REG.get_asset_by_object_path(path)
        if a and a.asset_class_path.asset_name == "World":
            out.append(clean)
    return out


def fix_level(path):
    if not LES.load_level(path):
        print(f"[blockers] SKIP (load failed): {path}")
        return
    hidden = 0
    for a in EAS.get_all_level_actors():
        if not isinstance(a, unreal.StaticMeshActor):
            continue
        lbl = a.get_actor_label()
        if lbl.startswith("Water") or a.is_hidden_ed():
            continue
        comp = a.static_mesh_component
        if a.get_editor_property("hidden") or comp is None:
            continue
        n = comp.get_num_materials()
        if n <= 0:
            continue
        if any(material_is_textured(comp.get_material(i)) for i in range(n)):
            continue
        a.set_actor_hidden_in_game(True)   # keep collision, hide the pink box
        hidden += 1
    if hidden:
        LES.save_current_level()
    print(f"[blockers] {path}: {hidden} color-only blockers hidden")


lv = levels()
print(f"[blockers] scanning {len(lv)} levels")
for p in lv:
    fix_level(p)
print("[blockers] done")
