"""Apply per-material render state straight from the ZSC flags (tools/mat_zsc_items.json,
keyed by gender -> item -> material-basename).  Faithful to ROSE (engine gates alpha
test/blend on is_alpha; see zz_material.cpp):
  is_alpha=0            -> Opaque
  is_alpha=1, z_write=1 -> Masked   (clip = alpha_ref/255)
  is_alpha=1, z_write=0 -> Translucent
  two_sided = is_2side  (per material, NOT forced)
Face is the one UE exception: opaque + two-sided (the face mesh isn't watertight and
its eye-overlay sections must stay opaque so the eyes don't reveal the dark sockets).

ROSE_GENDER=Female|Male (default both)."""
import unreal, os, json

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
HERE = os.path.dirname(os.path.abspath(__file__))
DATA = json.load(open(os.path.join(HERE, "mat_zsc_items.json")))
BM = unreal.BlendMode
MAP = {"OPAQUE": BM.BLEND_OPAQUE, "MASKED": BM.BLEND_MASKED, "TRANSLUCENT": BM.BLEND_TRANSLUCENT}

genv = os.environ.get("ROSE_GENDER", "").capitalize()
GENDERS = [genv] if genv in ("Female", "Male") else ["Female", "Male"]
SLOTS = ("body", "arms", "foot", "cap", "back", "hair", "face")
_only = os.environ.get("ROSE_SLOTS", "").strip()
ONLY_SLOTS = set(s.strip() for s in _only.split(",") if s.strip()) if _only else None


def apply(mic, mode, clip, two_sided):
    if not isinstance(mic, unreal.MaterialInstanceConstant):
        return False
    ov = mic.get_editor_property("base_property_overrides")
    ov.set_editor_property("override_blend_mode", True)
    ov.set_editor_property("blend_mode", MAP[mode])
    ov.set_editor_property("override_two_sided", True)
    ov.set_editor_property("two_sided", two_sided)
    if mode == "MASKED":
        ov.set_editor_property("override_opacity_mask_clip_value", True)
        ov.set_editor_property("opacity_mask_clip_value", clip)
    else:
        ov.set_editor_property("override_opacity_mask_clip_value", False)
    mic.set_editor_property("base_property_overrides", ov)
    ME.update_material_instance(mic)
    return True


for gender in GENDERS:
    root = f"/Game/Characters/Modular/{gender}"
    if not EAL.does_directory_exist(root):
        continue
    items = DATA.get(gender, {})
    n = 0; cnt = {"OPAQUE": 0, "MASKED": 0, "TRANSLUCENT": 0}
    for d in EAL.list_assets(root, recursive=False, include_folder=True):
        item = d.rstrip("/").rsplit("/", 1)[-1]
        slot = item.split("_")[0]
        if slot not in SLOTS:
            continue
        if ONLY_SLOTS and slot not in ONLY_SLOTS:
            continue
        sm = EAL.load_asset(f"{root}/{item}/{item}/SkeletalMeshes/{item}")
        if not sm:
            continue
        parts = items.get(item, {})
        for s in sm.get_editor_property("materials"):
            mi = s.get_editor_property("material_interface")
            if not mi:
                continue
            mn = mi.get_name()
            key = mn[3:] if mn.startswith("MI_") else mn
            if slot == "face":
                mode, clip, two = "OPAQUE", 0.5, True          # UE exception
            else:
                info = parts.get(key)
                if info:
                    mode, clip, two = info["mode"], info["clip"], info["two_sided"]
                else:
                    mode, clip, two = "OPAQUE", 0.5, False      # default (base, unmapped)
            if apply(mi, mode, clip, two):
                cnt[mode] += 1
                EAL.save_asset(mi.get_path_name().split(".")[0])
        n += 1
        if n % 250 == 0:
            print(f"[zsc] {gender} {n} items ... {item}")
    print(f"[zsc] {gender} done: {n} items {cnt}")
print("[zsc] ALL DONE")
