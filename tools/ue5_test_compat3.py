"""Multi-mesh compat test. body_1/arms_1/foot_1 are compat-marked; body_100 is
NOT.  Does adding a non-compat item to a multi-mesh merge break it?"""
import unreal
EAL = unreal.EditorAssetLibrary
ROOT = "/Game/Characters/Modular/Female"
base_skel = EAL.load_asset(f"{ROOT}/base/base/SkeletalMeshes/base_Skeleton")

def L(item):
    return EAL.load_asset(f"{ROOT}/{item}/{item}/SkeletalMeshes/{item}")

def merge(label, items):
    meshes = [L(i) for i in items]
    if any(m is None for m in meshes):
        print(f"[c3] {label}: a mesh is MISSING {[i for i,m in zip(items,meshes) if not m]}"); return
    p = unreal.SkeletalMeshMergeParams()
    p.set_editor_property("meshes_to_merge", meshes)
    p.set_editor_property("skeleton", base_skel)
    m = unreal.SkeletalMergingLibrary.merge_meshes(p)
    print(f"[c3] {label} {items}: merge={'OK' if m else 'FAIL'}")

merge("all-compat", ["body_1", "arms_1", "foot_1"])
merge("with non-compat body_100", ["body_1", "arms_1", "body_100"])
merge("two non-compat", ["body_100", "arms_1", "foot_1"])
