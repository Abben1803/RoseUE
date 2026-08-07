"""Probe: dump static-switch parameter names/values of a few M_GLTF-parented
MaterialInstanceConstants + which MaterialEditingLibrary APIs exist."""
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

print("[sw] has get_static_switch_names:",
      hasattr(MEL, "get_material_instance_static_switch_parameter_names"))
print("[sw] has get_static_switch_value:",
      hasattr(MEL, "get_material_instance_static_switch_parameter_value"))
print("[sw] has set_static_switch_value:",
      hasattr(MEL, "set_material_instance_static_switch_parameter_value"))

samples = [
    "/Game/Maps/JPT01V2/Scene/JPT01V2/Materials/M0_T030_02",
    "/Game/Maps/JPT01V2/Scene/JPT01V2/Materials/M10_T029_01",
]
# plus one character MI if present
for path in EAL.list_assets("/Game/Characters/Modular/Female/body_1", recursive=True):
    clean = str(path).split(".")[0]
    a = EAL.load_asset(clean)
    if isinstance(a, unreal.MaterialInstanceConstant):
        samples.append(clean)
        break

for s in samples[:4]:
    mi = EAL.load_asset(s)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        print(f"[sw] {s}: not an MIC ({type(mi).__name__})")
        continue
    p = mi.get_editor_property("parent")
    print(f"[sw] {mi.get_name()} parent={p.get_name() if p else None}")
    try:
        names = MEL.get_material_instance_static_switch_parameter_names(mi)
        for n in names:
            try:
                v = MEL.get_material_instance_static_switch_parameter_value(mi, n)
            except Exception as e:
                v = f"<get failed {e}>"
            print(f"[sw]   switch {n} = {v}")
    except Exception as e:
        print(f"[sw]   name enumeration failed: {e}")
