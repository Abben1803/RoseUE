"""Force every catalog material to OPAQUE via the Interchange glTF parent's
own control: the M_GLTF (Substrate) instances expose a scalar 'AlphaMode'
(glTF: 0=OPAQUE, 1=MASK, 2=BLEND).  Setting it to 0 makes the base-color
alpha fully ignored -> opaque, instead of overriding the engine blend mode.

ROSE_GENDER=Female|Male (default both)."""
import unreal, os

EAL = unreal.EditorAssetLibrary
ME = unreal.MaterialEditingLibrary
genv = os.environ.get("ROSE_GENDER", "").capitalize()
ROOTS = [f"/Game/Characters/Modular/{g}" for g in ([genv] if genv in ("Female", "Male") else ["Female", "Male"])]
# Default scope: feet, cap, body, hands.  Override with ROSE_SLOTS="weapon" etc.
SLOTS = tuple(s.strip() for s in
              os.environ.get("ROSE_SLOTS", "body,arms,foot,cap").split(",") if s.strip())
# Weapons are gender-neutral and live in their own shared folder, not the gendered
# trees — scan it when the weapon slot is in scope.
if "weapon" in SLOTS:
    ROOTS = ROOTS + ["/Game/Characters/Modular/Weapons"]

n = setc = 0
seen = set()
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
            if not isinstance(mi, unreal.MaterialInstanceConstant):
                continue
            path = mi.get_path_name().split(".")[0]
            if path in seen:
                continue
            seen.add(path)
            ME.set_material_instance_scalar_parameter_value(mi, "AlphaMode", 0.0)
            ov = mi.get_editor_property("base_property_overrides")
            ov.set_editor_property("override_two_sided", True)
            ov.set_editor_property("two_sided", True)
            mi.set_editor_property("base_property_overrides", ov)
            ME.update_material_instance(mi)
            EAL.save_asset(path)
            setc += 1
        n += 1
        if n % 200 == 0:
            print(f"[opaque] {n} items ... {name}")
print(f"[opaque] done: {n} items  AlphaMode=0 set on {setc} unique materials")
