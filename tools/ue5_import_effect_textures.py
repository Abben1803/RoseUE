"""Import the converted particle-effect textures (tools/build_effects.py →
SourceAssets/GLTF/EFFECTS/*.png) as UTexture2D assets under
/Game/Effects/Particles/<name>.  Editor must be CLOSED.  Re-runnable
(replace_existing)."""
import unreal, os

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
PROJ = unreal.Paths.project_dir()
SRC = os.path.join(PROJ, "SourceAssets", "GLTF", "EFFECTS")
DST = "/Game/Effects/Particles"

pngs = sorted(f for f in os.listdir(SRC) if f.lower().endswith(".png"))
tasks = []
for f in pngs:
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(SRC, f))
    t.set_editor_property("destination_path", DST)
    t.set_editor_property("destination_name", os.path.splitext(f)[0])
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    tasks.append(t)

AT.import_asset_tasks(tasks)

ok = sum(1 for f in pngs if EAL.does_asset_exist(f"{DST}/{os.path.splitext(f)[0]}"))
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
print(f"[fxtex] imported {ok}/{len(pngs)} particle textures -> {DST}")
