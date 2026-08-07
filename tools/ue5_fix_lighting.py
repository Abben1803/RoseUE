"""
ue5_fix_lighting.py — Retune every map level's lighting rig for the ROSE
classic look: shadows must be soft-filled, not pitch black.

The import rig (ue5_import_map_scene.py) spawned a default-intensity Sun
(10 lux) + real-time-capture SkyLight (scale 1) — a ~10:1 sun:ambient ratio,
so with honest exposure the shadow side of everything went black.  ROSE's
world shading is mostly baked into the textures; it wants a FLAT ratio.

Per level:  Sun intensity → ROSE_SUN (default 4.0 lux)
            SkyLight intensity_scale → ROSE_SKY (default 3.0), RTC on,
            lower_hemisphere_is_black off (ground-bounce fill)
            an unbound PostProcessVolume "RoseExposure" with exposure bias
            ROSE_EVBIAS (default +0.5) + a clamped adaptation range.

Env: ROSE_ZONE=JPT01 for one zone, else ALL /Game/Maps/<Z>/L_<Z> levels.
Run headless, editor CLOSED (-nullrhi fine).  Re-runnable (updates in place).
"""
import os
import unreal

SUN = float(os.environ.get("ROSE_SUN", "4.0"))
SKY = float(os.environ.get("ROSE_SKY", "3.0"))
BIAS = float(os.environ.get("ROSE_EVBIAS", "0.5"))
ONLY = os.environ.get("ROSE_ZONE", "").upper()
SKIP = {z for z in os.environ.get("ROSE_SKIP", "").upper().split(",") if z}

EAL = unreal.EditorAssetLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def levels():
    out = []
    for path in EAL.list_assets("/Game/Maps", recursive=True, include_folder=False):
        clean = str(path).split(".")[0]
        name = clean.rsplit("/", 1)[-1]
        if not name.startswith("L_"):
            continue
        zone = name[2:]
        if ONLY and zone != ONLY:
            continue
        if zone in SKIP:
            continue
        if unreal.AssetRegistryHelpers.get_asset_registry().get_asset_by_object_path(path).asset_class_path.asset_name == "World":
            out.append(clean)
    return out


def fix_level(path):
    if not LES.load_level(path):
        print(f"[light] SKIP (load failed): {path}")
        return
    sun = sky = ppv = None
    for a in EAS.get_all_level_actors():
        if isinstance(a, unreal.DirectionalLight):
            sun = a
        elif isinstance(a, unreal.SkyLight):
            sky = a
        elif isinstance(a, unreal.PostProcessVolume) and a.get_actor_label() == "RoseExposure":
            ppv = a

    if sun:
        c = sun.light_component
        c.set_editor_property("intensity", SUN)
        c.set_editor_property("atmosphere_sun_light", True)
    if sky:
        c = sky.light_component
        c.set_editor_property("real_time_capture", True)
        # UPROPERTY is `intensity` (the Details panel labels it "Intensity Scale").
        c.set_editor_property("intensity", SKY)
        c.set_editor_property("lower_hemisphere_is_black", False)

    if not ppv:
        ppv = EAS.spawn_actor_from_class(unreal.PostProcessVolume, unreal.Vector(0, 0, 0))
        ppv.set_actor_label("RoseExposure")
    ppv.set_editor_property("unbound", True)
    s = ppv.get_editor_property("settings")
    s.set_editor_property("override_auto_exposure_bias", True)
    s.set_editor_property("auto_exposure_bias", BIAS)
    # Clamp adaptation so interiors/night can't over-brighten and noon can't
    # crush — a gentle range around daylight.
    s.set_editor_property("override_auto_exposure_min_brightness", True)
    s.set_editor_property("override_auto_exposure_max_brightness", True)
    s.set_editor_property("auto_exposure_min_brightness", -2.0)
    s.set_editor_property("auto_exposure_max_brightness", 6.0)
    ppv.set_editor_property("settings", s)

    LES.save_current_level()
    print(f"[light] {path}: sun={'ok' if sun else 'MISSING'} sky={'ok' if sky else 'MISSING'} ppv=ok")


lv = levels()
print(f"[light] retuning {len(lv)} levels (SUN={SUN} SKY={SKY} BIAS={BIAS})")
for p in lv:
    fix_level(p)
print("[light] done")
