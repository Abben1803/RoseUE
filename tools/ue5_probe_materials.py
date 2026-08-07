"""Dump the AnimRig skeletal mesh's material slots + their actual texture params."""
import unreal

SM = "/Game/Characters/Female/AnimRig/FEMALE_anims/SkeletalMeshes/FEMALE_anims"

sm = unreal.EditorAssetLibrary.load_asset(SM)
print(f"[probe] skeletal mesh loaded: {sm is not None}")
if sm:
    mats = sm.get_editor_property("materials")
    print(f"[probe] {len(mats)} material slots:")
    for i, m in enumerate(mats):
        mi = m.get_editor_property("material_interface")
        slot = m.get_editor_property("material_slot_name")
        info = []
        if mi and isinstance(mi, unreal.MaterialInstanceConstant):
            tpv = mi.get_editor_property("texture_parameter_values")
            for p in tpv:
                pname = p.get_editor_property("parameter_info").get_editor_property("name")
                tex = p.get_editor_property("parameter_value")
                tw = tex.blueprint_get_size_x() if tex else 0
                info.append(f"{pname}={tex.get_name() if tex else None}({tw}px)")
            # also base color / parent
            parent = mi.get_editor_property("parent")
            info.append(f"parent={parent.get_name() if parent else None}")
        print(f"[probe]   slot {i} '{slot}': {info}")
