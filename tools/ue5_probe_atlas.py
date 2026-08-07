"""
ue5_probe_atlas.py — Verify the atlas/master-material state of the catalog.

Counts, per category: meshes whose every textured slot is an /Game/Atlas/MI
instance of M_RoseMaster vs meshes still on Interchange/legacy materials.
Prints a few offenders per category.  Read-only.
"""
import json
import os
import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
EAL = unreal.EditorAssetLibrary
MI_ROOT = "/Game/Atlas/MI"

with open(os.path.join(TOOLS, "_tmp", "texture_manifest.json"), encoding="utf-8") as f:
    tex_man = json.load(f)

stats = {}
offenders = {}
for entry in tex_man["meshes"]:
    cat = entry["category"]
    s = stats.setdefault(cat, {"atlased": 0, "legacy": 0, "missing": 0, "notex": 0})
    asset = entry["asset"]
    name = asset.rsplit("/", 1)[-1]
    if not EAL.does_asset_exist(asset):
        s["missing"] += 1
        continue
    obj = unreal.load_object(None, f"{asset}.{name}")
    if not obj:
        s["missing"] += 1
        continue
    prop = "materials" if entry["class"] == "SkeletalMesh" else "static_materials"
    mats = obj.get_editor_property(prop)
    has_tex = [i for i, sl in enumerate(entry["slots"]) if sl["tex"]]
    if not has_tex:
        s["notex"] += 1
        continue
    ok = True
    for i in has_tex:
        if i >= len(mats):
            continue
        mi = mats[i].get_editor_property("material_interface")
        if not mi or not mi.get_path_name().startswith(MI_ROOT):
            ok = False
            break
    if ok:
        s["atlased"] += 1
    else:
        s["legacy"] += 1
        offenders.setdefault(cat, [])
        if len(offenders[cat]) < 5:
            offenders[cat].append(name)

mi_count = len([a for a in EAL.list_assets(MI_ROOT, recursive=False)]) \
    if EAL.does_directory_exist(MI_ROOT) else 0
print(f"[probe] unique MIs under {MI_ROOT}: {mi_count}")
for cat, s in sorted(stats.items()):
    print(f"[probe] {cat}: {s}  offenders={offenders.get(cat, [])}")
