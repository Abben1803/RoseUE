"""
ue5_fix_map_materials.py — Fix the Interchange-imported map materials so they
render like ROSE: two-sided everywhere (ROSE map geometry is authored
single-face; Interchange imports single-sided -> see-through walls/roofs),
blend mode per the mapforge manifest (MASK -> masked @ its cutoff, BLEND ->
translucent, else opaque).

Run from INSIDE the editor: Tools > Execute Python Script... (safe with the
level open). Re-runnable. Writes a JSON report next to this script.

Env (optional):
  ROSE_MAP_ZONE  zone key (default JPT01)
"""
import os
import json
import unreal

ZONE = os.environ.get("ROSE_MAP_ZONE", "JPT01")
HERE = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(HERE, "..", "mapforge", "exports", f"{ZONE}.glb.materials.json")
SCENE_PKG = f"/Game/Maps/{ZONE}/Scene"
REPORT = os.path.join(HERE, "map_mat_fix_report.json")

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

BLEND = {
    "OPAQUE": unreal.BlendMode.BLEND_OPAQUE,
    "MASK": unreal.BlendMode.BLEND_MASKED,
    "BLEND": unreal.BlendMode.BLEND_TRANSLUCENT,
}

manifest = {m["name"]: m for m in json.load(open(MANIFEST))["materials"]}


def entry_for(asset_name):
    # Interchange may suffix duplicates ("M12_TREE020_1") — match longest prefix.
    if asset_name in manifest:
        return manifest[asset_name]
    best = None
    for name in manifest:
        if asset_name.startswith(name) and (best is None or len(name) > len(best)):
            best = name
    return manifest.get(best)


report = {"zone": ZONE, "fixed": 0, "skipped": [], "classes": {}, "modes": {}}
for path in EAL.list_assets(SCENE_PKG, recursive=True):
    obj = EAL.load_asset(path)
    if not isinstance(obj, unreal.MaterialInterface):
        continue
    name = obj.get_name()
    cls = obj.get_class().get_name()
    report["classes"][cls] = report["classes"].get(cls, 0) + 1
    e = entry_for(name)
    if not e:
        report["skipped"].append(name)
        continue
    mode = e.get("mode", "OPAQUE")
    report["modes"][mode] = report["modes"].get(mode, 0) + 1
    two = bool(e.get("twosided"))
    # record what Interchange originally produced (the diagnostic evidence)
    try:
        base = obj.get_base_material()
        was = "%s/%s two_sided=%s" % (base.get_name(),
                                      base.get_editor_property("blend_mode"),
                                      base.get_editor_property("two_sided"))
    except Exception:
        was = "?"
    report.setdefault("before", {})[was] = report.get("before", {}).get(was, 0) + 1
    try:
        if isinstance(obj, unreal.MaterialInstanceConstant):
            ov = obj.get_editor_property("base_property_overrides")
            ov.set_editor_property("override_two_sided", True)
            ov.set_editor_property("two_sided", two)
            ov.set_editor_property("override_blend_mode", True)
            ov.set_editor_property("blend_mode", BLEND[mode])
            if mode == "MASK":
                ov.set_editor_property("override_opacity_mask_clip_value", True)
                ov.set_editor_property("opacity_mask_clip_value", 0.5)
            obj.set_editor_property("base_property_overrides", ov)
            MEL.update_material_instance(obj)
        elif isinstance(obj, unreal.Material):
            obj.set_editor_property("two_sided", two)
            obj.set_editor_property("blend_mode", BLEND[mode])
            MEL.recompile_material(obj)
        EAL.save_asset(obj.get_path_name().split(".")[0], only_if_is_dirty=False)
        report["fixed"] += 1
    except Exception as ex:
        report["skipped"].append(f"{name}: {ex}")

with open(REPORT, "w") as f:
    json.dump(report, f, indent=1)
unreal.log(f"[matfix] fixed {report['fixed']} materials "
           f"(classes {report['classes']}, modes {report['modes']}, "
           f"{len(report['skipped'])} skipped) -> {REPORT}")
