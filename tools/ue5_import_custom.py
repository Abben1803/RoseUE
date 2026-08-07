"""
ue5_import_custom.py — Import every GLB in SourceAssets/GLTF/AVATAR/CUSTOM/ into
/Game/Characters/Custom/<name>/ and re-skin each with the M_RoseChar master
material (two-sided + masked), reusing imported textures.

Run headless (with RHI for material compile):
    UnrealEditor-Cmd <proj> -ExecutePythonScript=ue5_import_custom.py
"""
import unreal
import os

PROJECT_DIR = unreal.Paths.project_dir()
CUSTOM_DIR = os.path.normpath(os.path.join(
    PROJECT_DIR, "SourceAssets", "GLTF", "AVATAR", "CUSTOM"))

MAT_PATH = "/Game/Characters/Materials"
MASTER_NAME = "M_RoseChar"

ME = unreal.MaterialEditingLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary


def get_or_create_master():
    full = f"{MAT_PATH}/{MASTER_NAME}"
    if EAL.does_asset_exist(full):
        return EAL.load_asset(full)
    if not EAL.does_directory_exist(MAT_PATH):
        EAL.make_directory(MAT_PATH)
    mat = AT.create_asset(MASTER_NAME, MAT_PATH, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("two_sided", True)
    mat.set_editor_property("opacity_mask_clip_value", 0.5)
    tex = ME.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -400, 0)
    tex.set_editor_property("parameter_name", "BaseColor")
    ME.connect_material_property(tex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    ME.connect_material_property(tex, "A",   unreal.MaterialProperty.MP_OPACITY_MASK)
    ME.recompile_material(mat)
    EAL.save_asset(full)
    return mat


def _section_texture(mi):
    if isinstance(mi, unreal.MaterialInstanceConstant):
        tpv = mi.get_editor_property("texture_parameter_values")
        for p in tpv:
            n = str(p.get_editor_property("parameter_info").get_editor_property("name"))
            if n.lower().startswith("basecolor"):
                return p.get_editor_property("parameter_value")
        if tpv:
            return tpv[0].get_editor_property("parameter_value")
    return None


def reskin(sm_path, master):
    sm = EAL.load_asset(sm_path)
    if not sm:
        return
    inst_dir = sm_path.rsplit("/SkeletalMeshes/", 1)[0] + "/Materials"
    if not EAL.does_directory_exist(inst_dir):
        EAL.make_directory(inst_dir)
    mats = sm.get_editor_property("materials")
    new_mats = []
    for slot in mats:
        old = slot.get_editor_property("material_interface")
        slot_name = str(slot.get_editor_property("material_slot_name"))
        tex = _section_texture(old)
        inst_path = f"{inst_dir}/MI_{slot_name}"
        if EAL.does_asset_exist(inst_path):
            EAL.delete_asset(inst_path)
        inst = AT.create_asset(f"MI_{slot_name}", inst_dir,
                               unreal.MaterialInstanceConstant,
                               unreal.MaterialInstanceConstantFactoryNew())
        ME.set_material_instance_parent(inst, master)
        if tex:
            ME.set_material_instance_texture_parameter_value(inst, "BaseColor", tex)
        EAL.save_asset(inst_path)
        slot.set_editor_property("material_interface", inst)
        new_mats.append(slot)
    sm.set_editor_property("materials", new_mats)
    EAL.save_asset(sm_path)


def run():
    if not os.path.isdir(CUSTOM_DIR):
        print(f"[custom] no CUSTOM dir: {CUSTOM_DIR}")
        return
    master = get_or_create_master()
    glbs = sorted(f for f in os.listdir(CUSTOM_DIR) if f.lower().endswith(".glb"))
    for glb in glbs:
        name = os.path.splitext(glb)[0]
        dst = f"/Game/Characters/Custom/{name}"
        if EAL.does_directory_exist(dst):
            EAL.delete_directory(dst)
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", os.path.join(CUSTOM_DIR, glb))
        task.set_editor_property("destination_path", dst)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        AT.import_asset_tasks([task])
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
        print(f"[custom] imported {name}")
        # re-skin the resulting skeletal mesh(es)
        for a in unreal.EditorAssetLibrary.list_assets(dst, recursive=True):
            obj = EAL.load_asset(a)
            if obj and obj.get_class().get_name() == "SkeletalMesh":
                reskin(a, master)
                print(f"[custom]   re-skinned {a}")
        unreal.SystemLibrary.collect_garbage()
    print("[custom] done")


run()
