"""Clean compat test: load already-saved meshes from disk (no fresh import) and
merge each alone onto base_Skeleton.  body_1 is compat-marked; body_10 is not."""
import unreal
EAL = unreal.EditorAssetLibrary
ROOT = "/Game/Characters/Modular/Female"
base_skel = EAL.load_asset(f"{ROOT}/base/base/SkeletalMeshes/base_Skeleton")

def merge_alone(item):
    sm = EAL.load_asset(f"{ROOT}/{item}/{item}/SkeletalMeshes/{item}")
    if not sm:
        print(f"[c2] {item}: MISSING"); return
    p = unreal.SkeletalMeshMergeParams()
    p.set_editor_property("meshes_to_merge", [sm])
    p.set_editor_property("skeleton", base_skel)
    m = unreal.SkeletalMergingLibrary.merge_meshes(p)
    print(f"[c2] {item}: merge={'OK' if m else 'FAIL'}")

for it in ["body_1", "body_10", "body_100", "arms_1"]:
    merge_alone(it)

# Now mark body_10 compatible + save, then merge again (same process, post-save)
sm10 = EAL.load_asset(f"{ROOT}/body_10/body_10/SkeletalMeshes/body_10")
if sm10:
    sk10 = sm10.get_editor_property("skeleton")
    sk10.add_compatible_skeleton(base_skel)
    base_skel.add_compatible_skeleton(sk10)
    EAL.save_asset(sk10.get_path_name())
    EAL.save_asset(base_skel.get_path_name())
    merge_alone("body_10")  # after compat+save
