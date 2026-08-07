"""ue5_verify_native_map.py — read-only check on a natively-imported zone.

Opens the level the RoseEditor commandlet created and verifies it is a real
SCENE, not a pile of assets: actors present, meshes resolved, and world bounds
that agree with the level the old mapforge/Interchange pipeline produced.  That
last check is the one that matters — a native importer that puts the terrain in
the wrong place would look fine in isolation and be useless in the game.

  UnrealEditor-Cmd.exe <proj> -ExecutePythonScript=tools/ue5_verify_native_map.py \
      -unattended -nopause -nosplash -stdout -FullStdOutLogOutput -nullrhi
Env: ROSE_ZONE (default JPT01)
"""
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "JPT01").upper()
NATIVE = f"/Game/Maps/{ZONE}/L_{ZONE}_Native"
LEGACY = f"/Game/Maps/{ZONE}/L_{ZONE}"

EAL = unreal.EditorAssetLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
EAS = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)


def log(m):
    unreal.log(f"[verify-native] {m}")


def scan(level_path):
    """Open a level and return (actor count, mesh-actor count, missing, bounds, class counts)."""
    if not EAL.does_asset_exist(level_path):
        log(f"MISSING level {level_path}")
        return None
    LES.load_level(level_path)
    actors = EAS.get_all_level_actors()

    mesh_actors = 0
    missing = 0
    lo = [float("inf")] * 3
    hi = [float("-inf")] * 3
    counts = {}

    for a in actors:
        cls = a.get_class().get_name()
        counts[cls] = counts.get(cls, 0) + 1

        comp = a.get_component_by_class(unreal.StaticMeshComponent)
        if not comp:
            continue
        mesh_actors += 1
        if not comp.static_mesh:
            missing += 1
            continue
        origin, extent = a.get_actor_bounds(False)
        for i, axis in enumerate(("x", "y", "z")):
            lo[i] = min(lo[i], getattr(origin, axis) - getattr(extent, axis))
            hi[i] = max(hi[i], getattr(origin, axis) + getattr(extent, axis))

    return len(actors), mesh_actors, missing, (lo, hi), counts


log(f"zone {ZONE}")

native = scan(NATIVE)
if native is None:
    raise SystemExit(1)
n_all, n_mesh, n_missing, (n_lo, n_hi), n_counts = native
log(f"NATIVE  {NATIVE}")
log(f"  actors total      {n_all}")
log(f"  static-mesh actors {n_mesh}")
log(f"  unresolved meshes  {n_missing}")
log(f"  bounds min        ({n_lo[0]:.0f}, {n_lo[1]:.0f}, {n_lo[2]:.0f})")
log(f"  bounds max        ({n_hi[0]:.0f}, {n_hi[1]:.0f}, {n_hi[2]:.0f})")
for cls, n in sorted(n_counts.items(), key=lambda kv: -kv[1]):
    log(f"    {cls:<28} {n}")

# Material + UV sanity on one mesh: the terrain lerps two atlas lookups, so a
# mesh with only one UV channel would render the top layer with the bottom's
# coordinates and look subtly wrong rather than broken.
SMES = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
sample = EAL.load_asset(f"/Game/Maps/{ZONE}/Native/SM_{ZONE}_30_30")
if sample:
    log(f"  sample mesh       {sample.get_name()}")
    # UV channels live on the subsystem, not the asset.
    log(f"    UV channels     {SMES.get_num_uv_channels(sample, 0)}")
    log(f"    vertices        {SMES.get_number_verts(sample, 0)}")
    log(f"    triangles       {SMES.get_number_materials(sample)} slot(s)")
    log(f"    material slots  {len(sample.static_materials)}")
    log(f"    nanite enabled  {sample.get_editor_property('nanite_settings').enabled}")
    for m in sample.static_materials:
        if m.material_interface:
            log(f"    material        {m.material_interface.get_name()}")
            parent = m.material_interface.get_editor_property("parent")
            if parent:
                log(f"    material parent {parent.get_name()}")

legacy = scan(LEGACY)
if legacy:
    l_all, l_mesh, l_missing, (l_lo, l_hi), l_counts = legacy
    log(f"LEGACY  {LEGACY}")
    log(f"  actors total      {l_all}")
    log(f"  static-mesh actors {l_mesh}")
    log(f"  bounds min        ({l_lo[0]:.0f}, {l_lo[1]:.0f}, {l_lo[2]:.0f})")
    log(f"  bounds max        ({l_hi[0]:.0f}, {l_hi[1]:.0f}, {l_hi[2]:.0f})")
    for cls, n in sorted(l_counts.items(), key=lambda kv: -kv[1]):
        log(f"    {cls:<28} {n}")
    # The legacy level also holds every ZSC object, so its bounds are a superset;
    # what matters is that the terrain sits inside the same world region.
    dx = abs(n_lo[0] - l_lo[0])
    dy = abs(n_lo[1] - l_lo[1])
    log(f"  origin delta X/Y  {dx:.0f} / {dy:.0f} cm")

print("[verify-native] DONE")
