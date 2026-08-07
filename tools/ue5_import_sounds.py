"""
ue5_import_sounds.py — Import the ROSE combat WAVs listed in
tools/_tmp/sound_manifest.json (written by gen_sound_data.py) as USoundWave
assets at the exact paths RoseSoundData.h references.

Run headless, editor CLOSED.
"""
import json
import os
import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
MANIFEST = os.path.join(TOOLS, "_tmp", "sound_manifest.json")

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

with open(MANIFEST, "r", encoding="utf-8") as f:
    manifest = json.load(f)

done = skipped = failed = 0
for wav, asset in sorted(manifest.items()):
    if EAL.does_asset_exist(asset):
        skipped += 1
        continue
    dst_dir, name = asset.rsplit("/", 1)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", wav)
    t.set_editor_property("destination_path", dst_dir)
    t.set_editor_property("destination_name", name)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    t.set_editor_property("save", True)
    AT.import_asset_tasks([t])
    if EAL.does_asset_exist(asset):
        done += 1
    else:
        failed += 1
        unreal.log_warning(f"[sounds] FAILED: {wav} -> {asset}")

unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
print(f"[sounds] imported {done}, skipped {skipped} existing, failed {failed}")
