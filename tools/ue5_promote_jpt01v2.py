"""
ue5_promote_jpt01v2.py — JPT01V2 (modern-client Zant) becomes THE JPT01:

  1. delete the classic /Game/Maps/JPT01 (level + scene assets)
  2. rename directory /Game/Maps/JPT01V2 -> /Game/Maps/JPT01
  3. rename the level asset L_JPT01V2 -> L_JPT01
  4. load + resave everything so references serialize to the new paths
  5. try to remove the leftover /Game/Maps/JPT01V2 redirectors

Portal retargeting (dest_level "L_JPT01V2" -> "L_JPT01") is a separate pass:
tools/ue5_retarget_portals.py with ROSE_FROM=L_JPT01V2 ROSE_TO=L_JPT01.
DefaultEngine.ini GameDefaultMap is updated by hand alongside this.

Run headless, editor CLOSED (-nullrhi fine).  NOT re-runnable (the source
folder is gone afterwards) — it no-ops with a message instead.
"""
import unreal

EAL = unreal.EditorAssetLibrary
LES = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)

OLD_DIR = "/Game/Maps/JPT01"
V2_DIR = "/Game/Maps/JPT01V2"

if not EAL.does_directory_exist(V2_DIR):
    print("[promote] /Game/Maps/JPT01V2 missing — already promoted? aborting")
    raise SystemExit

# 1 ── delete classic JPT01 ---------------------------------------------------
if EAL.does_directory_exist(OLD_DIR):
    n = len(EAL.list_assets(OLD_DIR, recursive=True))
    ok = EAL.delete_directory(OLD_DIR)
    print(f"[promote] deleted classic {OLD_DIR} ({n} assets): {ok}")
else:
    print(f"[promote] classic {OLD_DIR} already absent")

# 2+3 ── move every V2 asset into place (rename_directory refuses non-empty
# trees on some builds — per-asset renames are equivalent and reliable).
# The level asset changes NAME too: L_JPT01V2 -> L_JPT01.
new_level = f"{OLD_DIR}/L_JPT01"
moved = failed = 0
for path in EAL.list_assets(V2_DIR, recursive=True, include_folder=False):
    clean = str(path).split(".")[0]
    dest = clean.replace(V2_DIR + "/", OLD_DIR + "/")
    if clean.rsplit("/", 1)[-1] == "L_JPT01V2":
        dest = new_level
    if EAL.rename_asset(clean, dest):
        moved += 1
    else:
        failed += 1
        print(f"[promote] FAILED: {clean} -> {dest}")
print(f"[promote] moved {moved} assets, {failed} failed")
if failed:
    raise RuntimeError("asset renames failed")

# 4 ── resave so all references point at the new paths ------------------------
if LES.load_level(new_level):
    LES.save_current_level()
    print("[promote] level loaded + saved at new path")
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(True, True)

# 5 ── leftover redirectors ---------------------------------------------------
left = EAL.list_assets(V2_DIR, recursive=True) if EAL.does_directory_exist(V2_DIR) else []
if left:
    ok = EAL.delete_directory(V2_DIR)
    print(f"[promote] old dir had {len(left)} redirectors, delete: {ok}")
else:
    print("[promote] old dir clean")

print("[promote] done")
