"""Probe specific modular items: material->texture and mesh height bounds."""
import unreal

ITEMS = ["hair_110", "face_1", "cap_2", "body_1", "arms_1"]
ROOT = "/Game/Characters/Modular/Female"
EAL = unreal.EditorAssetLibrary


def tex_of(mi):
    if isinstance(mi, unreal.MaterialInstanceConstant):
        for p in mi.get_editor_property("texture_parameter_values"):
            n = str(p.get_editor_property("parameter_info").get_editor_property("name"))
            if n.lower().startswith("basecolor"):
                t = p.get_editor_property("parameter_value")
                return t.get_name() if t else "NONE"
    return "(not MIC)"


for it in ITEMS:
    path = f"{ROOT}/{it}/{it}/SkeletalMeshes/{it}"
    sm = EAL.load_asset(path)
    if not sm:
        print(f"[probe] {it}: MESH NOT FOUND ({path})")
        continue
    mats = sm.get_editor_property("materials")
    info = []
    for slot in mats:
        mi = slot.get_editor_property("material_interface")
        parent = mi.get_editor_property("parent").get_name() if (mi and isinstance(mi, unreal.MaterialInstanceConstant)) else "?"
        info.append(f"{mi.get_name() if mi else None}(parent={parent}, tex={tex_of(mi)})")
    bounds = sm.get_bounds()
    box = bounds.box_extent
    org = bounds.origin
    print(f"[probe] {it}: Zspan~[{org.z-box.z:.1f},{org.z+box.z:.1f}] mats={info}")
