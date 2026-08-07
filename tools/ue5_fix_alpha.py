"""Interchange imports glTF MASK materials as opaque single-sided.  For items
that need alpha cutout / two-sidedness (hair, face, etc.), flip the EXISTING
assigned material instance's base-property overrides to Masked + two-sided in
place — no create_asset (which corrupts on these folders).

Face items have 3 sections each ("...eyeclosed", "...main", "...eyeopen"); all
three get two_sided=True.  This is a base-property override only, so material
names are unchanged and ARoseCharacter::SetupBlink() (which matches eye sections
by name containing "eyeopen"/"eyeclosed") keeps working.

ROSE_FIX="hair_110,face_1,..."  (default: all hair_* and face_* in every
                                 gender folder under /Game/Characters/Modular)"""
import unreal, os

ROOTS = ["/Game/Characters/Modular/Female", "/Game/Characters/Modular/Male"]
PREFIXES = ("hair_", "face_")
EAL = unreal.EditorAssetLibrary


def targets():
    """Yield (root, item) pairs to fix."""
    env = os.environ.get("ROSE_FIX", "").strip()
    wanted = [x.strip() for x in env.split(",") if x.strip()] if env else None
    for root in ROOTS:
        if not EAL.does_directory_exist(root):
            continue
        for d in EAL.list_assets(root, recursive=False, include_folder=True):
            n = d.rstrip("/").rsplit("/", 1)[-1]
            if wanted is not None:
                if n in wanted:
                    yield root, n
            elif n.startswith(PREFIXES):
                yield root, n


def fix_instance(mic):
    if not isinstance(mic, unreal.MaterialInstanceConstant):
        return False
    ov = mic.get_editor_property("base_property_overrides")
    ov.set_editor_property("override_blend_mode", True)
    ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    ov.set_editor_property("override_two_sided", True)
    ov.set_editor_property("two_sided", True)
    ov.set_editor_property("override_opacity_mask_clip_value", True)
    ov.set_editor_property("opacity_mask_clip_value", 0.33)
    mic.set_editor_property("base_property_overrides", ov)
    unreal.MaterialEditingLibrary.update_material_instance(mic)
    return True


seen = 0
for root, it in sorted(set(targets())):
    seen += 1
    sm = EAL.load_asset(f"{root}/{it}/{it}/SkeletalMeshes/{it}")
    if not sm:
        print(f"[alpha] {it}: mesh missing"); continue
    for slot in sm.get_editor_property("materials"):
        mi = slot.get_editor_property("material_interface")
        if mi and fix_instance(mi):
            EAL.save_asset(mi.get_path_name().split(".")[0])
            print(f"[alpha] {it}: {mi.get_name()} -> Masked+TwoSided")
        else:
            print(f"[alpha] {it}: {mi.get_name() if mi else None} (not a MIC, skipped)")
if not seen:
    print("[alpha] no matching items found")
print("[alpha] done")
