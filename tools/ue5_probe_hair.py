"""Dump blend mode / two-sided / parent for hair + face materials to diagnose
the see-through hair."""
import unreal
EAL = unreal.EditorAssetLibrary
ROOT = "/Game/Characters/Modular/Female"

def describe(mi):
    name = mi.get_name()
    if isinstance(mi, unreal.MaterialInstanceConstant):
        p = mi.get_editor_property("parent")
        base = p
    else:
        base = mi
    bm = base.get_editor_property("blend_mode") if base else "?"
    ts = base.get_editor_property("two_sided") if base else "?"
    return f"{name} parent={base.get_name() if base else '?'} blend={bm} two_sided={ts}"

for item in ["hair_110", "face_1"]:
    sm = EAL.load_asset(f"{ROOT}/{item}/{item}/SkeletalMeshes/{item}")
    if not sm:
        print(f"[hair] {item}: MISSING"); continue
    for s in sm.get_editor_property("materials"):
        mi = s.get_editor_property("material_interface")
        print(f"[hair] {item}: {describe(mi) if mi else 'None'}")
