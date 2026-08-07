"""Dump M_RoseChar / M_RoseMaster (known-good masked Substrate materials) to
model a foliage master on. REPORT ONLY."""
import unreal
EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary

for path in ("/Game/Characters/Materials/M_RoseChar", "/Game/Atlas/M_RoseMaster"):
    m = EAL.load_asset(path)
    if not isinstance(m, unreal.Material):
        print(f"[rm] {path}: not a Material ({m})"); continue
    print(f"\n[rm] {path}")
    print(f"     blend_mode   = {m.get_editor_property('blend_mode')}")
    print(f"     shading_model= {m.get_editor_property('shading_model')}")
    print(f"     two_sided    = {m.get_editor_property('two_sided')}")
    print(f"     opacity_clip = {m.get_editor_property('opacity_mask_clip_value')}")
    for g in ("get_texture_parameter_names","get_scalar_parameter_names",
              "get_static_switch_parameter_names","get_vector_parameter_names"):
        try:
            print(f"     {g[18:]}: {[str(n) for n in getattr(MEL,g)(m)]}")
        except Exception as e:
            print(f"     {g}: err {e}")
