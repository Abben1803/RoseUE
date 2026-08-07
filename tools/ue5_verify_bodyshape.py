"""Verify the chest-bulge wiring reached M_RoseChar.

A material that compiles is not proof the graph connected: an unconnected
WorldPositionOffset pin, or a parameter that failed to register, both leave the
slider silently doing nothing.  Checks the parameters exist AND that a body mesh
actually uses this master.
"""
import unreal

EAL = unreal.EditorAssetLibrary
MASTER = "/Game/Rose/Characters/M_RoseChar"

if not EAL.does_asset_exist(MASTER):
    print("[body] FAIL - M_RoseChar missing")
else:
    mat = EAL.load_asset(MASTER)
    print(f"[body] {MASTER} loaded: {type(mat).__name__}")

    names = [str(n) for n in
             unreal.MaterialEditingLibrary.get_scalar_parameter_names(mat)]
    vnames = [str(n) for n in
              unreal.MaterialEditingLibrary.get_vector_parameter_names(mat)]
    print(f"[body] scalar params: {sorted(names)}")
    print(f"[body] vector params: {sorted(vnames)}")

    want_s = ["ChestBulge", "ChestRadius"]
    want_v = ["ChestCenter"]
    missing = [p for p in want_s if p not in names] + \
              [p for p in want_v if p not in vnames]
    print("[body] PASS - all chest params present" if not missing
          else f"[body] FAIL - missing: {missing}")

    # And confirm a body mesh actually points at this master.
    body = "/Game/Rose/Characters/F/BODY/SK_F_BODY_1"
    if EAL.does_asset_exist(body):
        sk = EAL.load_asset(body)
        mats = sk.get_editor_property("materials")
        used = set()
        for m in mats:
            mi = m.get_editor_property("material_interface")
            if not mi:
                continue
            p = mi.get_path_name()
            used.add(p)
            # walk instance -> parent
            par = mi.get_editor_property("parent") if isinstance(
                mi, unreal.MaterialInstanceConstant) else None
            if par:
                used.add(par.get_path_name())
        hit = any(MASTER in u for u in used)
        print(f"[body] SK_F_BODY_1 materials: {sorted(used)}")
        print("[body] body mesh DOES use M_RoseChar" if hit
              else "[body] WARNING body mesh does NOT use M_RoseChar")
    else:
        print(f"[body] {body} not imported")
