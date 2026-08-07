"""
ue5_fix_map_nanite.py — Disable Nanite on imported map-scene static meshes.

Interchange (UE 5.8 glTF) builds every imported mesh with Nanite enabled. The
map meshes are tiny (a few hundred tris) and the terrain patch meshes have
vertices ~5e7 units from the mesh origin (world-baked cm x100, actors at
origin with 0.01 scale): Nanite quantizes positions against those huge bounds
and simplifies each patch independently -> visible gaps between terrain
patches. Nanite buys nothing on ROSE-era poly counts, so turn it off.

Env:
  ROSE_ZONES   comma list of zone keys, or "ALL" (default JPT01)

Run headless (editor CLOSED):
  UnrealEditor-Cmd.exe <uproject> -ExecutePythonScript=tools/ue5_fix_map_nanite.py ...
"""
import os
import unreal

ZONES = os.environ.get("ROSE_ZONES", "JDT01").strip()

EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log(f"[naniteoff] {m}")


if ZONES.upper() == "ALL":
    zones = [d.rstrip("/").rsplit("/", 1)[-1]
             for d in EAL.list_assets("/Game/Maps", recursive=False, include_folder=True)
             if d.endswith("/")]
else:
    zones = [z.strip() for z in ZONES.split(",") if z.strip()]

total_off = total_seen = 0
for zone in zones:
    scene = f"/Game/Maps/{zone}/Scene"
    if not EAL.does_directory_exist(scene):
        log(f"skip {zone}: no {scene}")
        continue
    n_off = n_seen = 0
    for path in EAL.list_assets(scene, recursive=True):
        asset = EAL.load_asset(path)
        if not isinstance(asset, unreal.StaticMesh):
            continue
        n_seen += 1
        ns = asset.get_editor_property("nanite_settings")
        if not ns.get_editor_property("enabled"):
            continue
        ns.set_editor_property("enabled", False)
        asset.set_editor_property("nanite_settings", ns)
        n_off += 1
    saved = unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
    log(f"{zone}: {n_off}/{n_seen} meshes nanite->off, save={saved}")
    total_off += n_off
    total_seen += n_seen

log(f"DONE: {total_off}/{total_seen} meshes across {len(zones)} zones")
