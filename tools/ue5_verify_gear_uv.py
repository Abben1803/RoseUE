"""ue5_verify_gear_uv.py — prove whether M_RoseGearBlend USES CellOffset.

Renders the master (as a MID) at two different CellOffset values and samples the
centre pixel.  Different pixels => CellOffset reaches the sampler (per-item MIs
will work).  Identical => the master's UV is still broken and we must go to
per-item cropped textures instead.

Robust to the Python rendering-API name differing across builds.
Headless, editor CLOSED, WITH RHI.
"""
import unreal

EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log(f"[uvverify] {m}")


# Find the rendering library however it is exposed in this build.
KR = None
for nm in ("KismetRenderingLibrary", "RenderingLibrary"):
    KR = getattr(unreal, nm, None)
    if KR:
        log(f"using unreal.{nm}")
        break
if not KR:
    cands = [n for n in dir(unreal) if "ender" in n.lower()]
    log(f"NO rendering library found. candidates: {cands}")
    raise SystemExit

world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
master = EAL.load_asset("/Game/Characters/Gear/M_RoseGearBlend")
atlas = EAL.load_asset("/Game/Characters/Gear/Atlas/T_gear_atlas_29")
log(f"world={bool(world)} master={bool(master)} atlas={bool(atlas)}")

rt = KR.create_render_target2d(world, 64, 64, unreal.TextureRenderTargetFormat.RTF_RGBA8)
mid = unreal.MaterialLibrary.create_dynamic_material_instance(world, master)
mid.set_texture_parameter_value("AtlasTex", atlas)

cases = {
    "Heart(0.625,0.625)":   unreal.LinearColor(0.6250, 0.6250, 0, 0),
    "Gloom(0.3125,0.6875)": unreal.LinearColor(0.3125, 0.6875, 0, 0),
}
samples = {}
for label, cell in cases.items():
    mid.set_vector_parameter_value("CellOffset", cell)
    KR.clear_render_target2d(world, rt, unreal.LinearColor(0, 0, 0, 1))
    KR.draw_material_to_render_target(world, rt, mid)
    px = KR.read_render_target_pixel(world, rt, 32, 32)
    samples[label] = (round(px.r, 3), round(px.g, 3), round(px.b, 3))
    log(f"{label}: centre = {samples[label]}")

vals = list(samples.values())
log("RESULT: " + ("DIFFERENT — master uses CellOffset (per-item MIs viable)"
                  if vals[0] != vals[1] else
                  "IDENTICAL — master ignores CellOffset (go per-item textures)"))
log("DONE")
