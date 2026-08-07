"""
ue5_import_bgm.py — import the ROSE background music (and any other sound folder
that has never been imported) as USoundWave assets.

  client/sound/<folder>/*.ogg|*.wav  ->  /Game/Sounds/<folder>/<stem>

ue5_import_sounds.py only covers the COMBAT wavs listed in sound_manifest.json,
so bgm/ and terrain/ had no importer at all — which is why the game was silent
between zones.

Env: ROSE_SOUND_ONLY="bgm" to limit to one folder.
Run headless, editor CLOSED.
"""
import os

import unreal

SOUND_ROOT = r"C:\QQ-iROSE Online\Sound"
# Lowercase here is the GAME path; the on-disk folder is resolved case-
# insensitively below, because QQ-iROSE spells them "BGM"/"Terrain" where the
# classic tree used lowercase.
FOLDERS = ("bgm", "terrain", "interface", "item", "avata", "attack", "mob", "skill")
ONLY = os.environ.get("ROSE_SOUND_ONLY", "").strip()

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

total_done = total_skip = total_fail = 0
for folder in FOLDERS:
    if ONLY and folder != ONLY:
        continue
    src = os.path.join(SOUND_ROOT, folder)
    if not os.path.isdir(src):
        # Resolve the real casing ("BGM" vs "bgm") before giving up.
        for d in (os.listdir(SOUND_ROOT) if os.path.isdir(SOUND_ROOT) else []):
            if d.lower() == folder:
                src = os.path.join(SOUND_ROOT, d)
                break
    if not os.path.isdir(src):
        print(f"[bgm] {folder} missing under {SOUND_ROOT} — skipped")
        continue
    dst_dir = f"/Game/Sounds/{folder}"

    files = [f for f in sorted(os.listdir(src))
             if os.path.splitext(f)[1].lower() in (".ogg", ".wav")]
    done = skip = fail = 0
    corrupt = []
    tasks = []
    for f in files:
        stem = os.path.splitext(f)[0]
        asset = f"{dst_dir}/{stem}"
        if EAL.does_asset_exist(asset):
            skip += 1
            continue

        # Some shipped "wavs" are HTML error pages saved under a .wav name —
        # 14 geb_* attack sounds plus gebboss_death, ~570 bytes each, all
        # starting "<!DOCTYPE HTML".  They are broken downloads in the client,
        # not an import problem, and reporting them as generic failures makes a
        # clean run look broken.  Name them for what they are and move on.
        try:
            with open(os.path.join(src, f), "rb") as fh:
                head = fh.read(5)
        except OSError:
            head = b""
        if head[:1] == b"<":
            corrupt.append(f)
            continue
        t = unreal.AssetImportTask()
        t.set_editor_property("filename", os.path.join(src, f))
        t.set_editor_property("destination_path", dst_dir)
        t.set_editor_property("destination_name", stem)
        t.set_editor_property("automated", True)
        t.set_editor_property("replace_existing", True)
        t.set_editor_property("save", True)
        tasks.append((asset, t))

    if tasks:
        AT.import_asset_tasks([t for _, t in tasks])
        for asset, _ in tasks:
            if EAL.does_asset_exist(asset):
                done += 1
            else:
                fail += 1
                unreal.log_warning(f"[bgm] FAILED: {asset}")

    print(f"[bgm] {folder:10} imported {done:4}  already had {skip:4}  failed {fail}"
          + (f"  CORRUPT(not audio) {len(corrupt)}" if corrupt else ""))
    total_done += done
    total_skip += skip
    total_fail += fail

print(f"[bgm] TOTAL imported {total_done}, existing {total_skip}, failed {total_fail}")
