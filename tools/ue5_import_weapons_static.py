"""Import the raw static-mesh weapons (WPNSTATIC/*.glb) as Static Meshes under
/Game/Characters/Modular/WeaponsStatic/weapon_<id>.  These socket onto the
character's hand bone (orientation = a live-tuned relative transform), so no
skeleton/skin/compat is needed.  Keeps Interchange's textured materials.

Weapons are gender-neutral, so the destination is a SHARED folder (matches
ARoseCharacter::LoadWeaponStatic).  Source GLBs come from the Female WPNSTATIC
build (build_weapons_static.py --gender F).

ROSE_ONLY="weapon_2,..." to limit.  Editor must be CLOSED.

The full catalog is 1,646 GLBs (1,344 + dual-wield _off variants) and these embed
their textures, so the import needs RHI and compiles shaders — one session trips
the GPU watchdog (TDR).  Drive it in resumable chunks:
  ROSE_SKIP_EXISTING=1   skip already-imported weapons
  ROSE_LIMIT=<n>         stop after n imports this session (~25 is safe)
Packages are saved after every chunk of 25 so a device-removed crash costs at
most that chunk."""
import unreal, os

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
PROJ = unreal.Paths.project_dir()
SRC = os.path.normpath(os.path.join(PROJ, "SourceAssets", "GLTF", "AVATAR", "MODULAR",
                                    "FEMALE", "WPNSTATIC"))
DST_ROOT = "/Game/Characters/Modular/WeaponsStatic"
ONLY = [x for x in os.environ.get("ROSE_ONLY", "").split(",") if x.strip()]
SKIP_EXISTING = bool(os.environ.get("ROSE_SKIP_EXISTING", "").strip())
LIMIT = int(os.environ.get("ROSE_LIMIT", "0") or 0)   # 0 = no limit

n = skipped = 0
for f in sorted(os.listdir(SRC)):
    if not f.lower().endswith(".glb"):
        continue
    name = os.path.splitext(f)[0]
    if ONLY and name not in ONLY:
        continue
    dst = f"{DST_ROOT}/{name}"
    # Resumable: a previous chunk already produced assets here.
    if SKIP_EXISTING and EAL.does_directory_exist(dst) and EAL.list_assets(dst, recursive=True):
        skipped += 1
        continue
    if LIMIT and n >= LIMIT:
        break
    if EAL.does_directory_exist(dst):
        EAL.delete_directory(dst)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(SRC, f))
    t.set_editor_property("destination_path", dst)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    AT.import_asset_tasks([t])
    n += 1
    if n % 25 == 0:
        # Save incrementally: an unsaved crash would otherwise lose the chunk.
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
        unreal.SystemLibrary.collect_garbage()
        print(f"[wpnimp] {n} (skipped {skipped}) ...")
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
print(f"[wpnimp] import phase: {n} imported, {skipped} skipped")
# report the StaticMesh asset path of the first import for wiring the C++
for f in sorted(os.listdir(SRC))[:1]:
    name = os.path.splitext(f)[0]
    for a in EAL.list_assets(f"{DST_ROOT}/{name}", recursive=True):
        o = EAL.load_asset(a)
        if o and o.get_class().get_name() == "StaticMesh":
            print(f"[wpnimp] StaticMesh asset path example: {a}")
print(f"[wpnimp] imported {n} static weapons -> {DST_ROOT}")
