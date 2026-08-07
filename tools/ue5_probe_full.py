"""Probe: assigned material+texture of each default-loadout item, AND whether
SkeletalMeshMerge preserves textures."""
import unreal

ROOT = "/Game/Characters/Modular/Female"
LOADOUT = [("body", 1), ("arms", 1), ("foot", 1), ("face", 1), ("hair", 110)]
EAL = unreal.EditorAssetLibrary


def mat_tex(mi):
    if not mi:
        return "MATERIAL=None"
    name = mi.get_name()
    tex = "?"
    if isinstance(mi, unreal.MaterialInstanceConstant):
        for p in mi.get_editor_property("texture_parameter_values"):
            n = str(p.get_editor_property("parameter_info").get_editor_property("name"))
            if n.lower().startswith("basecolor"):
                t = p.get_editor_property("parameter_value")
                tex = t.get_name() if t else "NONE"
        parent = mi.get_editor_property("parent")
        return f"{name}(MIC parent={parent.get_name() if parent else '?'} tex={tex})"
    # plain Material: list used textures
    try:
        used = unreal.MaterialEditingLibrary.get_used_textures(mi)
        tex = ",".join(t.get_name() for t in used) if used else "NONE"
    except Exception:
        pass
    return f"{name}(Material tex={tex})"


def load_part(slot, pid):
    name = f"{slot}_{pid}"
    return EAL.load_asset(f"{ROOT}/{name}/{name}/SkeletalMeshes/{name}")


print("=== individual item materials ===")
meshes = []
for slot, pid in LOADOUT:
    sm = load_part(slot, pid)
    if not sm:
        print(f"[full] {slot}_{pid}: MESH MISSING")
        continue
    meshes.append(sm)
    mats = sm.get_editor_property("materials")
    desc = [mat_tex(s.get_editor_property("material_interface")) for s in mats]
    print(f"[full] {slot}_{pid}: {desc}")

print("=== test merge (does it keep materials/textures?) ===")
skel = EAL.load_asset(f"{ROOT}/base/base/SkeletalMeshes/base_Skeleton")
params = unreal.SkeletalMeshMergeParams()
params.set_editor_property("meshes_to_merge", meshes)
params.set_editor_property("skeleton", skel)
merged = unreal.SkeletalMergingLibrary.merge_meshes(params)
if merged:
    mats = merged.get_editor_property("materials")
    print(f"[full] merged: {len(mats)} slots")
    for s in mats:
        print(f"[full]   {mat_tex(s.get_editor_property('material_interface'))}")
else:
    print("[full] merge returned None")
