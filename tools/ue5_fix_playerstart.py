"""
ue5_fix_playerstart.py — move each map's PlayerStart to the ROSE zone's real
spawn: the ZON 'start' event point (fallback 'restore'), precomputed by
tools/export_zone_starts.py into tools/_tmp/zone_starts.json as UE (x, y).
Zones without an entry fall back to the zone origin (520000, -520000).

Ground-traces the point and drops the PlayerStart on it; extra PlayerStarts are
removed so RestartPlayer is deterministic.

Env:
  ROSE_ZONE   zone key, or ALL (default) = every /Game/Maps/<Z>/L_<Z>
  ROSE_SKIP   extra zones to skip (comma list)
Headless, editor CLOSED.  Re-runnable.
"""
import json
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "ALL").upper()
# Non-gameplay levels whose PlayerStart is intentional (char-select camera pawn).
SKIP = {"L_CHARACTERSELECT", "L_TITLE_JPT"} | \
    {f"L_{z.strip().upper()}" for z in os.environ.get("ROSE_SKIP", "").split(",") if z.strip()}
ORIGIN = (520000.0, -520000.0)
STARTS_JSON = r"C:\rose-next-classic\tools\_tmp\zone_starts.json"

STARTS = {}
try:
    STARTS = json.load(open(STARTS_JSON))
except Exception as e:
    unreal.log_warning(f"[pstart] zone_starts.json unavailable ({e}) — using origin")

LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)
EAL = unreal.EditorAssetLibrary
REG = unreal.AssetRegistryHelpers.get_asset_registry()


def log(m): unreal.log(f"[pstart] {m}")


def ground_z(world, x, y, fallback):
    """Drop a vertical trace onto the terrain and return a stand-on Z.

    UWorld has no line_trace_single_by_channel in the UE 5.8 Python API — the
    old call raised AttributeError, was swallowed by the except, and every zone
    silently kept its FALLBACK z (for JPT01 that was the scene import's -693,
    i.e. the PlayerStart was placed at an arbitrary height).  SystemLibrary is
    the exposed tracing entry point; keep the old call as a fallback in case a
    future version restores it.
    """
    start = unreal.Vector(x, y, 200000.0)
    end = unreal.Vector(x, y, -200000.0)
    try:
        # trace_complex MUST be True: the terrain uses complex-as-simple
        # collision (ue5_import_map_scene.py), so a simple-geometry trace hits
        # nothing and every zone silently kept its fallback Z.  This matches the
        # proven ground trace the scene import itself uses.
        hit = unreal.SystemLibrary.line_trace_single(
            world, start, end,
            unreal.TraceTypeQuery.TRACE_TYPE_QUERY1, True, [],
            unreal.DrawDebugTrace.NONE, True)
        if hit:
            try:
                z = hit.get_editor_property("impact_point").z
            except Exception:
                z = hit.to_tuple()[4].z
            return float(z) + 90.0
        log(f"trace: no blocking hit at ({x:.0f}, {y:.0f}) — using fallback")
    except Exception as e:
        log(f"trace error (SystemLibrary): {e}")
    return fallback


def do_zone(level_path):
    if not EAL.does_asset_exist(level_path):
        return
    zone = level_path.rsplit("/", 1)[-1][2:].upper()   # "L_JPT01" -> "JPT01"
    xy = STARTS.get(zone)
    src = "ZON start" if xy else "origin fallback"
    x, y = (float(xy[0]), float(xy[1])) if xy else ORIGIN

    if not LES.load_level(level_path):
        log(f"could not load {level_path}"); return
    world = UES.get_editor_world()

    starts = [a for a in EAS.get_all_level_actors() if isinstance(a, unreal.PlayerStart)]
    if not starts:
        starts = [EAS.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(x, y, 500.0))]
    z = ground_z(world, x, y, starts[0].get_actor_location().z)
    starts[0].set_actor_location(unreal.Vector(x, y, z), False, False)
    for extra in starts[1:]:
        EAS.destroy_actor(extra)
    LES.save_current_level()
    log(f"{zone}: PlayerStart -> ({x:.0f}, {y:.0f}, {z:.0f})  [{src}]")


def all_levels():
    flt = unreal.ARFilter(
        class_paths=[unreal.TopLevelAssetPath("/Script/Engine", "World")],
        package_paths=["/Game/Maps"], recursive_paths=True, recursive_classes=True)
    return sorted(str(ad.package_name) for ad in (REG.get_assets(flt) or [])
                  if str(ad.asset_name).startswith("L_")
                  and str(ad.asset_name).upper() not in SKIP)


if ZONE == "ALL":
    for lvl in all_levels():
        do_zone(lvl)
else:
    do_zone(f"/Game/Maps/{ZONE}/L_{ZONE}")
log("DONE")
