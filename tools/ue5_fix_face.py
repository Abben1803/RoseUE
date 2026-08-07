"""Fix the face: the eye overlay must be OPAQUE (the face texture has dark eye
sockets that the overlay covers; masking clips the overlay and reveals the black
sockets -> scary black eyes).  The face only needs TWO-SIDED (to stop being
see-through).  So: revert blend-mode override (inherit M_GLTF opaque) + force
two-sided.  Names unchanged → blink name-match still works.

ROSE_FIX="face_1,..."  (default: all face_* in both gender folders)"""
import unreal, os

ROOTS = ["/Game/Characters/Modular/Female", "/Game/Characters/Modular/Male"]
EAL = unreal.EditorAssetLibrary


def fix(mic):
    if not isinstance(mic, unreal.MaterialInstanceConstant):
        return False
    ov = mic.get_editor_property("base_property_overrides")
    ov.set_editor_property("override_blend_mode", False)            # inherit opaque (un-mask)
    ov.set_editor_property("override_opacity_mask_clip_value", False)
    ov.set_editor_property("override_two_sided", True)
    ov.set_editor_property("two_sided", True)
    mic.set_editor_property("base_property_overrides", ov)
    unreal.MaterialEditingLibrary.update_material_instance(mic)
    return True


env = os.environ.get("ROSE_FIX", "").strip()
wanted = [x.strip() for x in env.split(",") if x.strip()] if env else None
n = 0
for root in ROOTS:
    if not EAL.does_directory_exist(root):
        continue
    for d in EAL.list_assets(root, recursive=False, include_folder=True):
        name = d.rstrip("/").rsplit("/", 1)[-1]
        if not name.startswith("face_"):
            continue
        if wanted is not None and name not in wanted:
            continue
        sm = EAL.load_asset(f"{root}/{name}/{name}/SkeletalMeshes/{name}")
        if not sm:
            continue
        for slot in sm.get_editor_property("materials"):
            mi = slot.get_editor_property("material_interface")
            if mi and fix(mi):
                EAL.save_asset(mi.get_path_name().split(".")[0])
        n += 1
        if n % 25 == 0:
            print(f"[face] fixed {n} ... {name}")
print(f"[face] done: {n} face items set to opaque + two-sided")
