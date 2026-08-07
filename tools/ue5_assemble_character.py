"""
ue5_assemble_character.py — Merge ROSE avatar parts onto the shared AnimRig
skeleton to produce one full-body SkeletalMesh that plays the ROSE animations.

Uses unreal.SkeletalMergingLibrary (UE's modular-character mesh-merge tool).
All part meshes share the identical 21-bone hierarchy, so they merge cleanly
onto the shared skeleton.

Output: /Game/Characters/Female/Assembled/SK_Female_Default
"""
import unreal

SHARED_SKELETON = "/Game/Characters/Female/AnimRig/FEMALE_anims/SkeletalMeshes/FEMALE_anims_Skeleton"

DEFAULT_PARTS = [
    "/Game/Characters/Female/Parts/LIST_WBODY_body2_00100/SkeletalMeshes/LIST_WBODY_body2_00100",
    "/Game/Characters/Female/Parts/LIST_WFACE_face2_00100/SkeletalMeshes/LIST_WFACE_face2_00100",
    "/Game/Characters/Female/Parts/LIST_WHAIR_HAIR02_00100/SkeletalMeshes/LIST_WHAIR_HAIR02_00100",
    "/Game/Characters/Female/Parts/LIST_WFOOT_foot2_00100/SkeletalMeshes/LIST_WFOOT_foot2_00100",
]

OUT_PATH = "/Game/Characters/Female/Assembled"
OUT_NAME = "SK_Female_Default"


def run():
    skeleton = unreal.EditorAssetLibrary.load_asset(SHARED_SKELETON)
    print(f"[assemble] shared skeleton: {skeleton is not None}")

    meshes = []
    for p in DEFAULT_PARTS:
        m = unreal.EditorAssetLibrary.load_asset(p)
        print(f"[assemble] part {('OK ' if m else 'MISS')} {p}")
        if m:
            meshes.append(m)

    if not meshes:
        print("[assemble] ABORT: no part meshes loaded")
        return

    # Inspect the merge params API
    params = unreal.SkeletalMeshMergeParams()
    print("[assemble] SkeletalMeshMergeParams properties:")
    try:
        for prop in ["meshes_to_merge", "skeleton", "skeleton_before",
                     "strip_top_lo_ds", "needs_cpu_access", "mesh_section_mappings"]:
            try:
                _ = params.get_editor_property(prop)
                print(f"    has '{prop}'")
            except Exception:
                pass
    except Exception as e:
        print(f"    (introspection failed: {e})")

    params.set_editor_property("meshes_to_merge", meshes)
    try:
        params.set_editor_property("skeleton", skeleton)
    except Exception as e:
        print(f"[assemble] could not set 'skeleton': {e}")

    merged = unreal.SkeletalMergingLibrary.merge_meshes(params)
    if not merged:
        print("[assemble] FAILED: merge_meshes returned None")
        return

    # merge_meshes returns a TRANSIENT object. AssetTools.duplicate_asset with the
    # OBJECT (not a path) creates a proper persisted asset from it.
    if not unreal.EditorAssetLibrary.does_directory_exist(OUT_PATH):
        unreal.EditorAssetLibrary.make_directory(OUT_PATH)
    dst = f"{OUT_PATH}/{OUT_NAME}"
    if unreal.EditorAssetLibrary.does_asset_exist(dst):
        unreal.EditorAssetLibrary.delete_asset(dst)

    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()
    new_asset = asset_tools.duplicate_asset(OUT_NAME, OUT_PATH, merged)
    print(f"[assemble] duplicate_asset -> {new_asset.get_path_name() if new_asset else None}")
    if new_asset:
        unreal.EditorAssetLibrary.save_asset(new_asset.get_path_name(), only_if_is_dirty=False)

    final = new_asset or merged
    sk = final.get_editor_property("skeleton")
    print(f"[assemble] merged mesh skeleton: {sk.get_path_name() if sk else None}")
    print(f"[assemble] matches shared: {sk and SHARED_SKELETON in sk.get_path_name()}")
    print(f"[assemble] exists on disk: {unreal.EditorAssetLibrary.does_asset_exist(dst)}")
    print(f"[assemble] DONE -> {dst}")


run()
