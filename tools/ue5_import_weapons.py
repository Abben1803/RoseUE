"""
ue5_import_weapons.py — Import weapon GLBs straight into the shared, gender-neutral
/Game/Characters/Modular/Weapons folder (NOT the Female tree), so there is no
filesystem move afterwards and therefore no broken cross-references.

Weapons keep Interchange's embedded-texture M_GLTF materials (no re-skin).  After a
full import run `ue5_alpha_opaque.py ROSE_SLOTS=weapon` for opaque + two-sided
(weapon materials are alpha_test=1 and would otherwise import masked).

  ROSE_ONLY="weapon_9001,weapon_9002"  limit to specific items (for grip tuning)
  ROSE_SKIP_EXISTING=1                 skip already-imported weapons (resumable)
  ROSE_LIMIT=<n>                       stop after n imports this session

The full catalog is 1,344 weapons and these GLBs embed their textures, so the
import needs RHI and compiles shaders — doing them all in one session trips the
GPU watchdog (TDR).  Drive it in resumable chunks (SKIP_EXISTING + LIMIT ~25) so
a device-removed crash costs one chunk instead of the whole run.

Editor must be CLOSED.  Source GLBs come from the FEMALE GLTF folder (weapons are
gender-neutral; built there by build_weapons.py / tune_weapon_grip.py).
"""
import os
import unreal

PROJECT_DIR = unreal.Paths.project_dir()
SRC_DIR = os.path.normpath(os.path.join(
    PROJECT_DIR, "SourceAssets", "GLTF", "AVATAR", "MODULAR", "FEMALE"))
DEST_ROOT = "/Game/Characters/Modular/Weapons"

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
ONLY = {x for x in os.environ.get("ROSE_ONLY", "").split(",") if x.strip()}


def import_glb(glb, dst):
    if EAL.does_directory_exist(dst):
        EAL.delete_directory(dst)   # clean replace
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", glb)
    t.set_editor_property("destination_path", dst)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    AT.import_asset_tasks([t])
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)


def run():
    glbs = sorted(f for f in os.listdir(SRC_DIR)
                  if f.lower().endswith(".glb") and f.startswith("weapon_"))
    if ONLY:
        glbs = [g for g in glbs if os.path.splitext(g)[0] in ONLY]
    skip_existing = bool(os.environ.get("ROSE_SKIP_EXISTING", "").strip())
    limit = int(os.environ.get("ROSE_LIMIT", "0") or 0)   # 0 = no limit
    print(f"[wimp] importing {len(glbs)} weapon GLBs -> {DEST_ROOT}"
          f"{' (resumable)' if skip_existing else ''}")
    done = skipped = 0
    for glb in glbs:
        name = os.path.splitext(glb)[0]
        dst = f"{DEST_ROOT}/{name}"
        if skip_existing and EAL.does_asset_exist(f"{dst}/{name}/SkeletalMeshes/{name}"):
            skipped += 1
            continue
        if limit and done >= limit:
            break
        import_glb(os.path.join(SRC_DIR, glb), dst)
        done += 1
        if done % 25 == 0:
            print(f"[wimp] {done} (skipped {skipped}) ... {name}")
            unreal.SystemLibrary.collect_garbage()
    print(f"[wimp] import phase: {done} imported, {skipped} skipped")


run()
