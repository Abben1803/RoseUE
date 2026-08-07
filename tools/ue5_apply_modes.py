"""Apply the per-material render mode computed from each texture's actual alpha
content (tools/mat_modes.json), keyed by material instance name (= ZMS basename):
  MASKED  - surface is mostly opaque; alpha cleanly cuts transparent bits
  OPAQUE  - surface alpha is low/specular; masking would black it -> show as-is
  TRANSLUCENT - soft back items (wings/capes)
Face eye sections + anything unknown default to OPAQUE.  All two-sided.

ROSE_GENDER=Female|Male (default both)."""
import unreal, os, json

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
HERE = os.path.dirname(os.path.abspath(__file__))
MODES = json.load(open(os.path.join(HERE, "mat_modes.json")))
BM = unreal.BlendMode
MAP = {"OPAQUE": BM.BLEND_OPAQUE, "MASKED": BM.BLEND_MASKED, "TRANSLUCENT": BM.BLEND_TRANSLUCENT}

genv = os.environ.get("ROSE_GENDER", "").capitalize()
ROOTS = [f"/Game/Characters/Modular/{g}" for g in ([genv] if genv in ("Female", "Male") else ["Female", "Male"])]
SLOTS = ("body", "arms", "foot", "cap", "back", "hair", "face")


def apply(mic, mode, clip):
    if not isinstance(mic, unreal.MaterialInstanceConstant):
        return False
    ov = mic.get_editor_property("base_property_overrides")
    ov.set_editor_property("override_two_sided", True)
    ov.set_editor_property("two_sided", True)
    ov.set_editor_property("override_blend_mode", True)
    ov.set_editor_property("blend_mode", MAP[mode])
    if mode == "MASKED":
        ov.set_editor_property("override_opacity_mask_clip_value", True)
        ov.set_editor_property("opacity_mask_clip_value", clip)
    else:
        ov.set_editor_property("override_opacity_mask_clip_value", False)
    mic.set_editor_property("base_property_overrides", ov)
    ME.update_material_instance(mic)
    return True


n = 0; cnt = {"OPAQUE": 0, "MASKED": 0, "TRANSLUCENT": 0}
for root in ROOTS:
    if not EAL.does_directory_exist(root):
        continue
    for d in EAL.list_assets(root, recursive=False, include_folder=True):
        name = d.rstrip("/").rsplit("/", 1)[-1]
        if name.split("_")[0] not in SLOTS:
            continue
        sm = EAL.load_asset(f"{root}/{name}/{name}/SkeletalMeshes/{name}")
        if not sm:
            continue
        for s in sm.get_editor_property("materials"):
            mi = s.get_editor_property("material_interface")
            if not mi:
                continue
            mn = mi.get_name()
            key = mn[3:] if mn.startswith("MI_") else mn
            info = MODES.get(key, {"mode": "OPAQUE", "clip": 0.5})
            if apply(mi, info["mode"], info["clip"]):
                cnt[info["mode"]] += 1
                EAL.save_asset(mi.get_path_name().split(".")[0])
        n += 1
        if n % 200 == 0:
            print(f"[modes] {n} items ... {name}")
print(f"[modes] done: {n} items  {cnt}")
