"""
ue5_import_gear.py — Phase 4: import the deduped gear/back mesh GLBs.

Imports SourceAssets/GLTF/AVATAR/GEAR/mesh_<id>.glb -> /Game/Characters/Gear/
mesh_<id>/ as skeletal meshes.  The GLBs carry NO texture, so Interchange assigns
the engine default material (no shader compile) — which means this runs fine
under -nullrhi with NO GPU/TDR risk.  The atlas material instance is assigned in
Phase 5 (from the manifest), and the merge system matches parts by bone name, so
no per-part skeleton-compat marking is needed here.

Batched + resumable for a safe large run:
  ROSE_OFFSET  first mesh id in this batch (default 0)
  ROSE_LIMIT   how many to import this batch (default 0 = all remaining)
Headless, editor CLOSED, -nullrhi.
"""
import os
import unreal

OFFSET = int(os.environ.get("ROSE_OFFSET", "0"))
LIMIT = int(os.environ.get("ROSE_LIMIT", "0"))
PROJECT_DIR = unreal.Paths.project_dir()
GEAR_DIR = os.path.normpath(os.path.join(
    PROJECT_DIR, "SourceAssets", "GLTF", "AVATAR", "GEAR"))
GAME_ROOT = "/Game/Characters/Gear"

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary


def import_one(glb, dst):
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", glb)
    t.set_editor_property("destination_path", dst)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    AT.import_asset_tasks([t])


# gather mesh_<id>.glb sorted by id
glbs = []
for f in os.listdir(GEAR_DIR):
    if f.lower().endswith(".glb") and f.startswith("mesh_"):
        try:
            glbs.append((int(f[5:-4]), f))
        except ValueError:
            pass
glbs.sort()
if OFFSET:
    glbs = [g for g in glbs if g[0] >= OFFSET]

# Skip already-imported BEFORE applying the limit, so a plain re-run with the
# same ROSE_LIMIT imports the NEXT N not-yet-imported (loop until 0 imported).
remaining = [(mid, f) for mid, f in glbs
             if not EAL.does_asset_exist(
                 f"{GAME_ROOT}/mesh_{mid}/mesh_{mid}/SkeletalMeshes/mesh_{mid}")]
total_remaining = len(remaining)
if LIMIT:
    remaining = remaining[:LIMIT]

print(f"[gear] {total_remaining} remaining; this batch -> {len(remaining)} GLBs")

done = skipped = 0
for mid, fname in remaining:
    dst = f"{GAME_ROOT}/mesh_{mid}"
    import_one(os.path.join(GEAR_DIR, fname), dst)
    done += 1
    if done % 50 == 0:
        print(f"[gear] ... imported {done} (skipped {skipped})")
        unreal.SystemLibrary.collect_garbage()

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
print(f"[gear] DONE batch: imported {done}, skipped {skipped}")
