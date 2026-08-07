"""
ue5_test_shared_skeleton.py — Probe: import ONE body part onto the shared
AnimRig skeleton and report whether it bound to that skeleton (vs creating a new
one).  Run headless first to validate the API before batching all parts.
"""
import unreal
import os

PROJECT_DIR = unreal.Paths.project_dir()
GLTF_FEMALE = os.path.normpath(os.path.join(
    PROJECT_DIR, "SourceAssets", "GLTF", "AVATAR", "FEMALE"))

SHARED_SKELETON = "/Game/Characters/Female/AnimRig/FEMALE_anims/SkeletalMeshes/FEMALE_anims_Skeleton"
TEST_DEST = "/Game/_SkelTest"


def _first_wbody():
    for f in sorted(os.listdir(GLTF_FEMALE)):
        if f.upper().startswith("LIST_WBODY") and f.lower().endswith(".glb"):
            return os.path.join(GLTF_FEMALE, f)
    return None


def run():
    skeleton = unreal.EditorAssetLibrary.load_asset(SHARED_SKELETON)
    print(f"[probe] shared skeleton loaded: {skeleton is not None} ({SHARED_SKELETON})")
    if skeleton is None:
        print("[probe] ABORT: shared skeleton not found")
        return

    glb = _first_wbody()
    print(f"[probe] test GLB: {glb}")

    # Clean any prior test output
    if unreal.EditorAssetLibrary.does_directory_exist(TEST_DEST):
        unreal.EditorAssetLibrary.delete_directory(TEST_DEST)

    task = unreal.AssetImportTask()
    task.set_editor_property("filename", glb)
    task.set_editor_property("destination_path", TEST_DEST)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)

    # Try attaching an Interchange pipeline with the target skeleton set.
    pipeline = None
    try:
        pipeline = unreal.InterchangeGenericAssetsPipeline()
        props = pipeline.get_editor_property("common_skeletal_meshes_and_animations_properties")
        props.set_editor_property("skeleton", skeleton)
        # don't import animations from a body part
        try:
            props.set_editor_property("import_only_animations", False)
        except Exception:
            pass
        task.set_editor_property("options", pipeline)
        print("[probe] attached InterchangeGenericAssetsPipeline with skeleton set")
    except Exception as e:
        print(f"[probe] could NOT build/attach pipeline: {e}")

    unreal.AssetToolsHelpers.get_asset_tools().import_asset_tasks([task])

    # Inspect results
    imported = unreal.EditorAssetLibrary.list_assets(TEST_DEST, recursive=True)
    print(f"[probe] imported {len(imported)} assets:")
    new_skeletons = 0
    for a in imported:
        obj = unreal.EditorAssetLibrary.load_asset(a)
        cls = obj.get_class().get_name() if obj else "?"
        print(f"    {cls:18s} {a}")
        if cls == "Skeleton":
            new_skeletons += 1
        if cls == "SkeletalMesh" and obj:
            sk = obj.get_editor_property("skeleton")
            sk_path = sk.get_path_name() if sk else "None"
            print(f"        -> skeletal mesh uses skeleton: {sk_path}")
            print(f"        -> MATCHES shared skeleton: {SHARED_SKELETON in sk_path}")

    print(f"[probe] RESULT: new Skeleton assets created = {new_skeletons} "
          f"(want 0 if it reused the shared skeleton)")


run()
