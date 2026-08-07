"""List scalar + static-switch + vector parameter names for a few catalog
material instances, so we can find the base-color-alpha control by its real name.
ROSE_GENDER defaults to both; prints the first ~6 instances it finds."""
import unreal, os

EAL = unreal.EditorAssetLibrary
genv = os.environ.get("ROSE_GENDER", "").capitalize()
ROOTS = [f"/Game/Characters/Modular/{g}" for g in ([genv] if genv in ("Female", "Male") else ["Female", "Male"])]
SLOTS = ("body", "arms", "foot", "cap", "back", "hair", "face")

shown = 0
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
            print(f"=== {name} :: {mi.get_name()}  parent={mi.get_editor_property('parent')}")
            for prop, label in (("scalar_parameter_values", "scalar"),
                                ("static_switch_parameters", "switch"),
                                ("vector_parameter_values", "vector")):
                try:
                    vals = mi.get_editor_property(prop)
                except Exception:
                    vals = []
                for p in vals:
                    pn = p.get_editor_property("parameter_info").get_editor_property("name")
                    print(f"    [{label}] {pn} = {p.get_editor_property('parameter_value')}")
            shown += 1
            if shown >= 6:
                raise SystemExit(0)
print("done")
