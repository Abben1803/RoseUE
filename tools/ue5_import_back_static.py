"""Import the raw static-mesh BACK items (BACKSTATIC/*.glb) as Static Meshes
under /Game/Characters/Modular/BackStatic/back_<id>.  These socket onto the
character's back dummy bone (ARoseCharacter::UpdateBack), so no skeleton/skin/
compat is needed.  Keeps Interchange's textured materials — each back item
embeds its OWN texture, so shared-mesh items (all Astarot wings = mesh_5175)
render distinctly.  Mirrors ue5_import_weapons_static.py.

Back items are gender-neutral, so the destination is a SHARED folder.  Source
GLBs come from the Female BACKSTATIC build (build_back_static.py --gender F).

ROSE_ONLY="back_5,..." to limit.  Editor must be CLOSED.
Resumable chunks (embeds textures -> RHI + shader compile -> TDR risk):
  ROSE_SKIP_EXISTING=1   skip already-imported items
  ROSE_LIMIT=<n>         stop after n imports this session (~25 safe, 50 ok)
"""
import unreal, os

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()
PROJ = unreal.Paths.project_dir()
SRC = os.path.normpath(os.path.join(PROJ, "SourceAssets", "GLTF", "AVATAR", "MODULAR",
                                    "FEMALE", "BACKSTATIC"))
DST_ROOT = "/Game/Characters/Modular/BackStatic"
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
        print(f"[backimp] {n} (skipped {skipped}) ...")
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
print(f"[backimp] import phase: {n} imported, {skipped} skipped")
for f in sorted(os.listdir(SRC))[:1]:
    name = os.path.splitext(f)[0]
    for a in EAL.list_assets(f"{DST_ROOT}/{name}", recursive=True):
        o = EAL.load_asset(a)
        if o and o.get_class().get_name() == "StaticMesh":
            print(f"[backimp] StaticMesh asset path example: {a}")
print(f"[backimp] imported {n} static back items -> {DST_ROOT}")
