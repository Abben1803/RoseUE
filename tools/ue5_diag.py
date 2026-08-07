"""Diagnose: (1) catalog texture build state (checkered = unbuilt), (2) whether
merging a catalog body drops the face/hair sections."""
import unreal
EAL = unreal.EditorAssetLibrary
ROOT = "/Game/Characters/Modular/Female"
base_skel = EAL.load_asset(f"{ROOT}/base/base/SkeletalMeshes/base_Skeleton")


def L(it):
    return EAL.load_asset(f"{ROOT}/{it}/{it}/SkeletalMeshes/{it}")


def texinfo(it):
    texdir = f"{ROOT}/{it}/{it}/Textures"
    for a in EAL.list_assets(texdir):
        t = EAL.load_asset(a)
        if not t:
            continue
        try:
            sx = t.blueprint_get_size_x(); sy = t.blueprint_get_size_y()
        except Exception:
            sx = sy = "?"
        print(f"[diag] tex {it}: {t.get_name()} size={sx}x{sy} class={t.get_class().get_name()}")
        return
    print(f"[diag] tex {it}: NO TEXTURE ASSET")


print("=== textures (body_1 = old RHI import, body_5 = catalog nullrhi) ===")
for it in ["body_1", "body_5", "body_50", "cap_50"]:
    texinfo(it)

print("=== merge: does a catalog body keep face/hair? ===")
def mergetest(label, items):
    meshes = [L(i) for i in items]
    miss = [i for i, m in zip(items, meshes) if not m]
    if miss:
        print(f"[diag] {label}: MISSING {miss}"); return
    p = unreal.SkeletalMeshMergeParams()
    p.set_editor_property("meshes_to_merge", meshes)
    p.set_editor_property("skeleton", base_skel)
    m = unreal.SkeletalMergingLibrary.merge_meshes(p)
    if not m:
        print(f"[diag] {label}: MERGE FAILED (null)"); return
    slots = [str(s.get_editor_property("material_slot_name")) for s in m.get_editor_property("materials")]
    print(f"[diag] {label}: {len(slots)} slots = {slots}")

mergetest("body_1 loadout", ["body_1", "arms_1", "foot_1", "hair_110", "face_1"])
mergetest("body_5 loadout", ["body_5", "arms_1", "foot_1", "hair_110", "face_1"])
mergetest("body_50 loadout", ["body_50", "arms_1", "foot_1", "hair_110", "face_1"])
