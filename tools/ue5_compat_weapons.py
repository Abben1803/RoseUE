"""Mark every weapon skeleton compatible (both directions) with base_Skeleton so
the held-weapon Leader-Pose component actually DRIVES the weapon (a follower whose
skeleton is neither identical nor compatible with the leader just shows its own
static ref pose — i.e. the weapon frozen at its ZSC grip position).

The weapons were imported with ROSE_NOCOMPAT=1, so this was never done; and base
was later re-imported (new base_Skeleton), so any old compat is stale anyway.

ROSE_GENDER=Female|Male (default Female).  Editor must be CLOSED."""
import unreal, os

EAL = unreal.EditorAssetLibrary
G = os.environ.get("ROSE_GENDER", "Female").capitalize()
ROOT = f"/Game/Characters/Modular/{G}"
BASE = f"{ROOT}/base/base/SkeletalMeshes/base_Skeleton"

base_sk = EAL.load_asset(BASE)
if not base_sk:
    raise SystemExit(f"[compat] base skeleton not found: {BASE}")

done = miss = 0
for d in EAL.list_assets(ROOT, recursive=False, include_folder=True):
    name = d.rstrip("/").rsplit("/", 1)[-1]
    if not name.startswith("weapon_"):
        continue
    sk = EAL.load_asset(f"{ROOT}/{name}/{name}/SkeletalMeshes/{name}_Skeleton")
    if not sk:
        miss += 1
        continue
    try:
        base_sk.add_compatible_skeleton(sk)
        sk.add_compatible_skeleton(base_sk)
        EAL.save_asset(sk.get_path_name().split(".")[0])
        done += 1
    except Exception as e:
        print(f"[compat] {name}: {e}")
    if done % 100 == 0 and done:
        print(f"[compat] {done} ...")

EAL.save_asset(base_sk.get_path_name().split(".")[0])
print(f"[compat] done: {done} weapon skeletons compatible with base ({miss} missing)")
