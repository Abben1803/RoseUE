"""ue5_reimport_pat_bases.py — re-import base_21 / base_31 WITH their animations.

The PAT bases were imported 2026-07-19 17:41 from early GLBs, but
build_pat_parts.py rebuilt the GLBs with the full animation sets at 22:59 the
same day (base_21: 49 tracks — cart+mount; base_31: 26 — castle gear) and they
were never re-imported.  Result: base_21 had ZERO AnimSequences and base_31 one,
so ARoseCart::PlayPatAnim had nothing to play — vehicles rode around in bind
pose.  This wipes /Game/Pat/base_<pet> and re-imports the current GLBs;
Interchange yields one AnimSequence per track named base_<pet><TRACK>.

Part skeletons reference the base by SOFT PATH (compatible-skeleton lists), and
the re-import recreates the skeleton at the same path, so parts stay compatible.

Headless, editor CLOSED, WITH RHI.
"""
import os
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
PROJ = unreal.Paths.project_dir()
GLTF = os.path.normpath(os.path.join(PROJ, "SourceAssets", "GLTF", "PAT"))


def log(m):
    unreal.log(f"[patbase] {m}")


for pet in (21, 31):
    glb = os.path.join(GLTF, f"base_{pet}.glb")
    dst = f"/Game/Pat/base_{pet}"
    if not os.path.exists(glb):
        log(f"missing {glb} — skipped")
        continue
    if EAL.does_directory_exist(dst):
        EAL.delete_directory(dst)
        log(f"wiped {dst}")
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", glb)
    t.set_editor_property("destination_path", dst)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    AT.import_asset_tasks([t])
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)

    anims = [a for a in EAL.list_assets(dst, recursive=True)
             if (lambda o: o and o.get_class().get_name() == "AnimSequence")(EAL.load_asset(a))]
    log(f"base_{pet}: {len(anims)} AnimSequences imported")
    for a in anims[:6]:
        log(f"   {a.split('.')[-1]}")

log("DONE")
