"""
ue5_promote_fix.py — finish the JPT01V2 -> JPT01 promotion: world assets can't
be rename_asset'd, so load the old level and SAVE it as the new package
(references resolve through the moved assets' redirectors and serialize to the
final /Game/Maps/JPT01/Scene paths), then delete the old folder.
Headless, editor CLOSED (-nullrhi fine).
"""
import unreal

EAL = unreal.EditorAssetLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
UES = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem)

OLD = "/Game/Maps/JPT01V2/L_JPT01V2"
NEW = "/Game/Maps/JPT01/L_JPT01"

if EAL.does_asset_exist(NEW):
    print("[fix] new level already exists")
elif not EAL.does_asset_exist(OLD):
    raise RuntimeError("old level missing too — nothing to do")
else:
    if not LES.load_level(OLD):
        raise RuntimeError("could not load old level")
    world = UES.get_editor_world()
    ok = unreal.EditorLoadingAndSavingUtils.save_map(world, NEW)
    print(f"[fix] save_map as {NEW}: {ok}")
    if not ok:
        raise RuntimeError("save_map failed")

# sanity: the new level loads
if not LES.load_level(NEW):
    raise RuntimeError("new level does not load")
print("[fix] new level loads OK")
LES.save_current_level()

# drop the old folder (old umap + redirectors from the asset moves)
if EAL.does_directory_exist("/Game/Maps/JPT01V2"):
    n = len(EAL.list_assets("/Game/Maps/JPT01V2", recursive=True))
    ok = EAL.delete_directory("/Game/Maps/JPT01V2")
    print(f"[fix] deleted /Game/Maps/JPT01V2 ({n} assets/redirectors): {ok}")

# verify nothing under JPT01 still references the old folder
bad = 0
for p in EAL.list_assets("/Game/Maps/JPT01", recursive=True, include_folder=False):
    for d in EAL.find_package_referencers_for_asset(str(p).split(".")[0], False):
        if "JPT01V2" in str(d):
            bad += 1
print(f"[fix] stale JPT01V2 referencers: {bad}")
print("[fix] done")
