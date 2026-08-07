"""
ue5_import_loading.py — import the 3 classic loading-screen images
(CONTROL/LOADING/{ELDEON,JUNON,LUNAR}.DDS, converted to PNG) as UI textures
under /Game/UI/Loading, used by SRoseLoadingScreen as the between-zone backdrop.

Headless, editor CLOSED (-nullrhi is NOT enough — texture import needs RHI; run
without -nullrhi).  Re-runnable (skips existing).
"""
import os
import unreal

SRC = r"C:/rose-next-classic/unreal-engine rose/RoseUE/SourceAssets/UI/Loading"
DEST = "/Game/UI/Loading"

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

pngs = [f for f in os.listdir(SRC) if f.lower().endswith(".png")] if os.path.isdir(SRC) else []
tasks = []
for f in pngs:
    name = os.path.splitext(f)[0]
    if EAL.does_asset_exist(f"{DEST}/{name}"):
        continue
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", os.path.join(SRC, f))
    t.set_editor_property("destination_path", DEST)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    tasks.append(t)

if tasks:
    AT.import_asset_tasks(tasks)

# Full-screen UI backdrops: UI texture group, no streaming (must be resident
# while the destination level loads), no mips.
fixed = 0
for f in pngs:
    name = os.path.splitext(f)[0]
    tex = EAL.load_asset(f"{DEST}/{name}")
    if isinstance(tex, unreal.Texture2D):
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("never_stream", True)
        EAL.save_asset(f"{DEST}/{name}")
        fixed += 1

print(f"[loading] imported/verified {fixed} loading textures under {DEST}")
