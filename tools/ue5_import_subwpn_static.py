"""Import static sub-weapons (SUBWPNSTATIC/*.glb) as Static Meshes under
/Game/Characters/Modular/SubWpnStatic/subwpn_<id> — the left-hand mirror of
WeaponsStatic (ARoseCharacter::LoadSubWpnStatic).  Gender-neutral shared folder;
source GLBs from the Female build.  Mirrors ue5_import_back_static.py.

ROSE_ONLY / ROSE_SKIP_EXISTING / ROSE_LIMIT as usual.  Editor CLOSED.
"""
import unreal, os

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
PROJ = unreal.Paths.project_dir()
SRC = os.path.normpath(os.path.join(PROJ, "SourceAssets", "GLTF", "AVATAR", "MODULAR",
                                    "FEMALE", "SUBWPNSTATIC"))
DST_ROOT = "/Game/Characters/Modular/SubWpnStatic"
ONLY = [x for x in os.environ.get("ROSE_ONLY", "").split(",") if x.strip()]
SKIP_EXISTING = bool(os.environ.get("ROSE_SKIP_EXISTING", "").strip())
LIMIT = int(os.environ.get("ROSE_LIMIT", "0") or 0)

n = skipped = 0
for f in sorted(os.listdir(SRC)):
    if not f.lower().endswith(".glb"):
        continue
    name = os.path.splitext(f)[0]
    if ONLY and name not in ONLY:
        continue
    dst = f"{DST_ROOT}/{name}"
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
        unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
        unreal.SystemLibrary.collect_garbage()
        print(f"[subimp] {n} (skipped {skipped}) ...")
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
print(f"[subimp] import phase: {n} imported, {skipped} skipped -> {DST_ROOT}")
