"""Set each catalog material's blend mode from the ROSE ZSC flags (the faithful
model):
  - is_alpha=True       -> Translucent   (wings, capes, glass; opaque shows a
                                           black rectangle, masking clips them)
  - hair                 -> Masked        (genuine alpha cutout)
  - everything else      -> Opaque        (most armor; their alpha is specular/
                                           non-opacity, so masking blacks them)
All two-sided.  mat_flags.json (blend set) is keyed by material instance name
(= the GLB/glTF material name = ZMS basename, which is how Interchange names them).

ROSE_GENDER=Female|Male (default both)."""
import unreal, os, json

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
HERE = os.path.dirname(os.path.abspath(__file__))
BLEND = set(json.load(open(os.path.join(HERE, "mat_flags.json")))["blend"])

genv = os.environ.get("ROSE_GENDER", "").capitalize()
ROOTS = [f"/Game/Characters/Modular/{g}" for g in ([genv] if genv in ("Female", "Male") else ["Female", "Male"])]
SLOTS = ("body", "arms", "foot", "cap", "back", "hair", "face")
BM = unreal.BlendMode


def apply(mic, mode, clip=None):
    if not isinstance(mic, unreal.MaterialInstanceConstant):
        return False
    ov = mic.get_editor_property("base_property_overrides")
    ov.set_editor_property("override_two_sided", True)
    ov.set_editor_property("two_sided", True)
    ov.set_editor_property("override_blend_mode", True)
    ov.set_editor_property("blend_mode", mode)
    if clip is not None:
        ov.set_editor_property("override_opacity_mask_clip_value", True)
        ov.set_editor_property("opacity_mask_clip_value", clip)
    else:
        ov.set_editor_property("override_opacity_mask_clip_value", False)
    mic.set_editor_property("base_property_overrides", ov)
    ME.update_material_instance(mic)
    return True


n = t = m = o = 0
for root in ROOTS:
    if not EAL.does_directory_exist(root):
        continue
    for d in EAL.list_assets(root, recursive=False, include_folder=True):
        name = d.rstrip("/").rsplit("/", 1)[-1]
        slot = name.split("_")[0]
        if slot not in SLOTS:
            continue
        sm = EAL.load_asset(f"{root}/{name}/{name}/SkeletalMeshes/{name}")
        if not sm:
            continue
        for s in sm.get_editor_property("materials"):
            mi = s.get_editor_property("material_interface")
            if not mi:
                continue
            mn = mi.get_name()
            # match the ZMS-basename material name (strip any MI_ prefix just in case)
            key = mn[3:] if mn.startswith("MI_") else mn
            if key in BLEND:
                ok = apply(mi, BM.BLEND_TRANSLUCENT); t += ok
            elif slot == "hair":
                ok = apply(mi, BM.BLEND_MASKED, 0.33); m += ok
            else:
                ok = apply(mi, BM.BLEND_OPAQUE); o += ok
            if ok:
                EAL.save_asset(mi.get_path_name().split(".")[0])
        n += 1
        if n % 200 == 0:
            print(f"[blend] {n} items ... {name}")
print(f"[blend] done: {n} items  translucent={t} masked={m} opaque={o}")
