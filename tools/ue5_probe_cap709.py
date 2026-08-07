import unreal
EAL = unreal.EditorAssetLibrary
for G in ["Female", "Male"]:
    root = f"/Game/Characters/Modular/{G}"
    sm = EAL.load_asset(f"{root}/cap_709/cap_709/SkeletalMeshes/cap_709")
    print(f"=== {G} cap_709 ===")
    if not sm:
        print("  MESH MISSING"); continue
    for s in sm.get_editor_property("materials"):
        mi = s.get_editor_property("material_interface")
        slotname = s.get_editor_property("material_slot_name")
        if not mi:
            print(f"  slot {slotname}: MATERIAL None"); continue
        parent = mi.get_editor_property("parent") if isinstance(mi, unreal.MaterialInstanceConstant) else mi
        bm = parent.get_editor_property("blend_mode") if parent else "?"
        ts = parent.get_editor_property("two_sided") if parent else "?"
        tex = "?"
        if isinstance(mi, unreal.MaterialInstanceConstant):
            for p in mi.get_editor_property("texture_parameter_values"):
                t = p.get_editor_property("parameter_value")
                if t: tex = t.get_name()
            ov = mi.get_editor_property("base_property_overrides")
            obm = ov.get_editor_property("blend_mode"); ots = ov.get_editor_property("two_sided")
            oclip = ov.get_editor_property("opacity_mask_clip_value")
            print(f"  slot={slotname} mat={mi.get_name()} parent={parent.get_name() if parent else '?'}")
            print(f"     override blend={obm} two_sided={ots} clip={oclip} tex={tex}")
    # texture asset list
    texdir = f"{root}/cap_709/cap_709/Textures"
    print("  textures:", [a.split('/')[-1] for a in EAL.list_assets(texdir)] if EAL.does_directory_exist(texdir) else "none")
