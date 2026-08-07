"""Audit every imported catalog item: does each material slot have a base-color
texture?  Reports per-slot textured/untextured counts and lists the bad ones."""
import unreal, os

ROOT = f"/Game/Characters/Modular/{os.environ.get('ROSE_GENDER','Female').capitalize()}"
EAL = unreal.EditorAssetLibrary
SLOTS = ["body", "arms", "foot", "cap", "back", "hair", "face"]


def has_tex(mi):
    if not mi:
        return False
    if isinstance(mi, unreal.MaterialInstanceConstant):
        for p in mi.get_editor_property("texture_parameter_values"):
            if p.get_editor_property("parameter_value"):
                return True
        # instance of M_GLTF: textures may be on the parent's expressions
        try:
            used = unreal.MaterialEditingLibrary.get_used_textures(mi)
            return len(used) > 0
        except Exception:
            return False
    try:
        return len(unreal.MaterialEditingLibrary.get_used_textures(mi)) > 0
    except Exception:
        return False


bad = {s: [] for s in SLOTS}
counts = {s: [0, 0] for s in SLOTS}   # [textured_items, total_items]
for d in EAL.list_assets(ROOT, recursive=False, include_folder=True):
    name = d.rstrip("/").rsplit("/", 1)[-1]
    slot = name.split("_")[0]
    if slot not in SLOTS:
        continue
    sm = EAL.load_asset(f"{ROOT}/{name}/{name}/SkeletalMeshes/{name}")
    if not sm:
        continue
    mats = sm.get_editor_property("materials")
    ok = all(has_tex(s.get_editor_property("material_interface")) for s in mats) and len(mats) > 0
    counts[slot][1] += 1
    if ok:
        counts[slot][0] += 1
    else:
        bad[slot].append(name)

print(f"[audit] {ROOT}")
for s in SLOTS:
    t, n = counts[s]
    print(f"[audit] {s}: {t}/{n} textured" + (f"  BAD: {bad[s][:15]}" if bad[s] else ""))
total_bad = sum(len(v) for v in bad.values())
print(f"[audit] total untextured items: {total_bad}")
