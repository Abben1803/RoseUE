"""ue5_probe_wings.py — Why are all Astarot wings the same art (and black)?

All Astarot wings share mesh_5175, so they share ONE baked MI_gear_5175 with a
single fixed CellOffset; they differ ONLY by the per-item cell that
ARoseCharacter::RebuildMesh is supposed to push into a dynamic MID at runtime.
If that MID never lands, every recolor falls back to the baked cell -> identical
art, and a wrong/empty cell renders black.

So: print the baked CellOffset and compare it against gear_equip.json.  If the
baked cell equals what every wing displays, the runtime MID is not being
applied and the bug is in the merged-slot pairing, not in the data or textures.

UE 5.8 notes: Texture2D.Source is protected, and SkeletalMesh has no
get_materials() — the material list is the 'materials' editor property.

Headless, editor CLOSED.
"""
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary


def show(label, fn):
    try:
        print(f"    {label:14} = {fn()}")
    except Exception as e:
        print(f"    {label:14} = <{type(e).__name__}: {e}>")


print("=== baked MI on the shared wing meshes ===")
for mesh_id in (5174, 5175):
    mp = f"/Game/Characters/Gear/mesh_{mesh_id}/mesh_{mesh_id}/SkeletalMeshes/mesh_{mesh_id}"
    sm = EAL.load_asset(mp)
    if not sm:
        print(f"  MISSING mesh_{mesh_id}")
        continue
    try:
        mats = sm.get_editor_property("materials")
    except Exception as e:
        print(f"  mesh_{mesh_id}: materials property failed: {e}")
        continue
    print(f"  mesh_{mesh_id}: {len(mats)} material slot(s)")
    for sk_mat in mats:
        try:
            mi = sk_mat.get_editor_property("material_interface")
        except Exception:
            mi = getattr(sk_mat, "material_interface", None)
        if not mi:
            print("    <null material>")
            continue
        print(f"    {mi.get_name()}  class={mi.get_class().get_name()}")
        if isinstance(mi, unreal.MaterialInstanceConstant):
            par = mi.get_editor_property("parent")
            print(f"    {'parent':14} = {par.get_name() if par else None}")
            show("CellOffset", lambda: MEL.get_material_instance_vector_parameter_value(mi, "CellOffset"))
            show("CellScale", lambda: MEL.get_material_instance_scalar_parameter_value(mi, "CellScale"))
            show("AtlasTex", lambda: (lambda t: t.get_name() if t else None)(
                MEL.get_material_instance_texture_parameter_value(mi, "AtlasTex")))

print("[wings] done")
