"""Test: can hair_110 be bound to base_F's skeleton (for modular Leader Pose)?"""
import unreal
import os

PROJECT_DIR = unreal.Paths.project_dir()
PARTS = os.path.normpath(os.path.join(PROJECT_DIR, "SourceAssets", "GLTF", "AVATAR", "PARTS"))
AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary


def imp(glb, dst):
    if EAL.does_directory_exist(dst):
        EAL.delete_directory(dst)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(PARTS, glb))
    t.set_editor_property("destination_path", dst)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    AT.import_asset_tasks([t])
    sm = None
    for a in EAL.list_assets(dst, recursive=True):
        o = EAL.load_asset(a)
        if o and o.get_class().get_name() == "SkeletalMesh":
            sm = o
    return sm


def run():
    base = imp("base_F.glb", "/Game/_modtest/base")
    hair = imp("hair_110.glb", "/Game/_modtest/hair")
    print(f"[test] base mesh: {base.get_name() if base else None}")
    print(f"[test] hair mesh: {hair.get_name() if hair else None}")
    if not (base and hair):
        print("[test] ABORT")
        return

    base_skel = base.get_editor_property("skeleton")
    hair_skel = hair.get_editor_property("skeleton")
    print(f"[test] base skeleton: {base_skel.get_path_name()}")
    print(f"[test] hair skeleton (before): {hair_skel.get_path_name()}")
    print(f"[test] same already? {base_skel.get_path_name()==hair_skel.get_path_name()}")

    # Method A: mark base skeleton compatible with hair's, and vice-versa
    try:
        base_skel.add_compatible_skeleton(hair_skel)
        print("[test] add_compatible_skeleton OK (base<-hair)")
    except Exception as e:
        print(f"[test] add_compatible_skeleton failed: {e}")

    # Method B: reassign hair's skeleton property to base's
    try:
        hair.set_editor_property("skeleton", base_skel)
        EAL.save_asset(hair.get_path_name())
        after = hair.get_editor_property("skeleton")
        print(f"[test] reassign skeleton -> {after.get_path_name()}")
        print(f"[test] reassign matches base: {after.get_path_name()==base_skel.get_path_name()}")
    except Exception as e:
        print(f"[test] reassign failed: {e}")


run()
