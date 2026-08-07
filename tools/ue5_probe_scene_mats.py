"""Probe a zone's Interchange scene materials: class, effective blend mode,
two-sided, parent. Env: ROSE_ZONE=JPT01V2."""
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "JPT01V2").upper()
SCENE = f"/Game/Maps/{ZONE}/Scene"
EAL = unreal.EditorAssetLibrary

n = 0
counts = {}
for path in EAL.list_assets(SCENE, recursive=True):
    clean = str(path).split(".")[0]
    mat = EAL.load_asset(clean)
    cls = type(mat).__name__
    if not isinstance(mat, unreal.MaterialInterface):
        continue
    blend = "?"
    two = "?"
    parent = ""
    if isinstance(mat, unreal.MaterialInstanceConstant):
        ov = mat.get_editor_property("base_property_overrides")
        blend = str(ov.get_editor_property("blend_mode")) if ov.get_editor_property("override_blend_mode") else "inherit"
        two = str(ov.get_editor_property("two_sided")) if ov.get_editor_property("override_two_sided") else "inherit"
        p = mat.get_editor_property("parent")
        parent = p.get_name() if p else "None"
    elif isinstance(mat, unreal.Material):
        blend = str(mat.get_editor_property("blend_mode"))
        two = str(mat.get_editor_property("two_sided"))
    key = (cls, blend, two, parent)
    counts[key] = counts.get(key, 0) + 1
    if n < 8:
        print(f"[probe] {mat.get_name()}: {cls} blend={blend} two_sided={two} parent={parent}")
        n += 1
print("[probe] summary:")
for k, v in sorted(counts.items(), key=lambda x: -x[1]):
    print(f"[probe]   {v:4d} x {k}")
