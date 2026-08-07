"""Verify USkeletalMergingLibrary merges a part whose skeleton is NOT registered
compatible with the target — i.e. compat marking is unnecessary for the merge."""
import unreal, os

GENDER = "FEMALE"
PROJECT_DIR = unreal.Paths.project_dir()
MODULAR_DIR = os.path.normpath(os.path.join(
    PROJECT_DIR, "SourceAssets", "GLTF", "AVATAR", "MODULAR", GENDER))
ROOT = "/Game/Characters/Modular/Female"
EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()

# find a body GLB whose UE asset doesn't exist yet
pick = None
for f in sorted(os.listdir(MODULAR_DIR)):
    if f.startswith("body_") and f.endswith(".glb"):
        name = f[:-4]
        if not EAL.does_asset_exist(f"{ROOT}/{name}/{name}/SkeletalMeshes/{name}"):
            pick = name; break
if not pick:
    print("[nocompat] no un-imported body to test with"); raise SystemExit

print(f"[nocompat] importing {pick} (no compat marking)")
t = unreal.AssetImportTask()
t.set_editor_property("filename", os.path.join(MODULAR_DIR, pick + ".glb"))
t.set_editor_property("destination_path", f"{ROOT}/{pick}")
t.set_editor_property("automated", True)
t.set_editor_property("replace_existing", True)
AT.import_asset_tasks([t])
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)

part = EAL.load_asset(f"{ROOT}/{pick}/{pick}/SkeletalMeshes/{pick}")
base_skel = EAL.load_asset(f"{ROOT}/base/base/SkeletalMeshes/base_Skeleton")
part_skel = part.get_editor_property("skeleton")

def try_merge(label):
    params = unreal.SkeletalMeshMergeParams()
    params.set_editor_property("meshes_to_merge", [part])
    params.set_editor_property("skeleton", base_skel)
    m = unreal.SkeletalMergingLibrary.merge_meshes(params)
    print(f"[nocompat] {label}: merge={'OK' if m else 'FAIL'}")
    return m is not None

# Mark both directions, SAVE, then merge (matches the proven import flow).
part_skel.add_compatible_skeleton(base_skel)
base_skel.add_compatible_skeleton(part_skel)
EAL.save_asset(part_skel.get_path_name())
EAL.save_asset(base_skel.get_path_name())
saved = try_merge("both + saved")
print(f"[nocompat] RESULT: both+saved={saved}")
