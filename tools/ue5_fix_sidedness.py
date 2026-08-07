"""Make all catalog materials two-sided (ROSE armor/hair is single-sided in the
ZSC but renders two-sided in-engine; M_GLTF imports single-sided → see-through).
  - hair  -> masked + two-sided (cutout strands)
  - face  -> opaque + two-sided (masking reveals black eye sockets; keep opaque)
  - rest  -> opaque + two-sided
Base-property override only → material names unchanged, blink name-match intact.

ROSE_GENDER=Female|Male (default both)."""
import unreal, os

EAL = unreal.EditorAssetLibrary
genv = os.environ.get("ROSE_GENDER", "").capitalize()
ROOTS = [f"/Game/Characters/Modular/{g}" for g in (([genv] if genv in ("Female", "Male") else ["Female", "Male"]))]
SLOTS = ("body", "arms", "foot", "cap", "back", "hair", "face")
ONLY_SLOT = os.environ.get("ROSE_SLOT", "").strip()   # e.g. "back" → only that slot


def fix(mic, masked):
    if not isinstance(mic, unreal.MaterialInstanceConstant):
        return False
    ov = mic.get_editor_property("base_property_overrides")
    ov.set_editor_property("override_two_sided", True)
    ov.set_editor_property("two_sided", True)
    if masked:
        ov.set_editor_property("override_blend_mode", True)
        ov.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        ov.set_editor_property("override_opacity_mask_clip_value", True)
        ov.set_editor_property("opacity_mask_clip_value", 0.33)
    else:
        ov.set_editor_property("override_blend_mode", False)
        ov.set_editor_property("override_opacity_mask_clip_value", False)
    mic.set_editor_property("base_property_overrides", ov)
    unreal.MaterialEditingLibrary.update_material_instance(mic)
    return True


n = 0
for root in ROOTS:
    if not EAL.does_directory_exist(root):
        continue
    for d in EAL.list_assets(root, recursive=False, include_folder=True):
        name = d.rstrip("/").rsplit("/", 1)[-1]
        slot = name.split("_")[0]
        if slot not in SLOTS:
            continue
        if ONLY_SLOT and slot != ONLY_SLOT:
            continue
        sm = EAL.load_asset(f"{root}/{name}/{name}/SkeletalMeshes/{name}")
        if not sm:
            continue
        # Opaque by default.  Most ROSE avatar textures store specular/other data
        # in the alpha channel — NOT opacity — so masking by alpha clips the real
        # surface to black (e.g. cap 709: 98% of the surface clipped).  Only HAIR
        # has genuine alpha cutout, so only hair is masked; everything else is
        # opaque + two-sided.  (Genuine-cutout non-hair items are rare and handled
        # case-by-case, since their alpha can't be told apart globally.)
        masked = (slot == "hair")
        for s in sm.get_editor_property("materials"):
            mi = s.get_editor_property("material_interface")
            if mi and fix(mi, masked):
                EAL.save_asset(mi.get_path_name().split(".")[0])
        n += 1
        if n % 100 == 0:
            print(f"[2side] {n} ... {name}")
print(f"[2side] done: {n} items two-sided (hair masked, rest opaque)")
