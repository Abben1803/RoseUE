"""
ue5_import_minimaps.py — Import the per-zone minimap PNGs (+ the shared arrow
cursor) built by tools/build_minimaps.py as UI-group textures:

  SourceAssets/UI/MINIMAPS/*.png  ->  /Game/UI/Minimaps

UI settings per texture: TEXTUREGROUP_UI, no mipmaps, UserInterface2D
compression, never stream — crisp 1:1 pixels for the Slate minimap brush.
Asset name = PNG stem = <ZONE> (e.g. JPT01) so it matches the manifest's
"texture" field "/Game/UI/Minimaps/<ZONE>", plus MINIMAP_ARROW.

Run HEADLESS with the editor CLOSED (mirrors ue5_import_ui_sprites.py):
  UnrealEditor-Cmd.exe "<RoseUE.uproject>" \
    -ExecutePythonScript="C:/rose-next-classic/tools/ue5_import_minimaps.py" \
    -unattended -nopause -nosplash -stdout -FullStdOutLogOutput -abslog="<log>"
Textures need no RHI shader work, so -nullrhi is fine.

UDIM gotcha (build_ui_sprites/ue5_import_ui_sprites lore): an asset whose name is
a *bare* 4-digit number >= 1001 gets collapsed into a UDIM texture on import.
All zone dir names are letters+digits (JPT01, EJT01, ...) so none is at risk;
this script still asserts that before importing and prefixes any offender with
"MM_" defensively.
"""
import os
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

SRC = os.path.normpath(os.path.join(
    unreal.Paths.project_dir(), "SourceAssets", "UI", "MINIMAPS"))
DST = "/Game/UI/Minimaps"


def safe_name(stem):
    """Guard the UDIM collapse: a bare 4+digit number >=1001 -> prefix MM_."""
    if stem.isdigit() and int(stem) >= 1001:
        return "MM_" + stem
    return stem


def main():
    if not os.path.isdir(SRC):
        print(f"[minimap] {SRC} missing — run tools/build_minimaps.py first")
        return

    pngs = sorted(f for f in os.listdir(SRC) if f.lower().endswith(".png"))
    print(f"[minimap] importing {len(pngs)} -> {DST}")

    # ── WIPE FIRST ───────────────────────────────────────────────────────────
    #
    # `replace_existing` does NOT replace anything under Interchange in 5.8 — an
    # asset already at the destination is left untouched, so re-importing over
    # the same zone names is a no-op on the pixels.  That is why JPT01 rendered
    # as a black disc: its texture was imported 2026-07-06 and never refreshed
    # when build_minimaps.py regenerated the PNGs on 07-20 (45 of the imported
    # minimaps were older than their source).  Deleting first is the only way to
    # be sure the bytes change.  See the same block in ue5_import_ui_sprites.py.
    #
    # list_assets only returns registered assets, so minimaps.json — which lives
    # in this same Content folder and is the manifest the game reads — is safe.
    if os.environ.get("ROSE_MM_NOWIPE"):
        print("[minimap] wipe SKIPPED (ROSE_MM_NOWIPE set)")
    else:
        old = EAL.list_assets(DST, recursive=False) if EAL.does_directory_exist(DST) else []
        for p in old:
            EAL.delete_asset(p)
        print(f"[minimap] wiped {len(old)} existing asset(s) before import")

    # Chunked import — one huge task list can silently drop files (observed with
    # the UI sprite import), so import in batches and GC between them.
    CHUNK = 100
    imported_names = []
    for c in range(0, len(pngs), CHUNK):
        tasks = []
        for f in pngs[c:c + CHUNK]:
            stem = safe_name(os.path.splitext(f)[0])
            imported_names.append(stem)
            t = unreal.AssetImportTask()
            t.set_editor_property("filename", os.path.join(SRC, f))
            t.set_editor_property("destination_path", DST)
            t.set_editor_property("destination_name", stem)
            t.set_editor_property("automated", True)
            t.set_editor_property("replace_existing", True)
            t.set_editor_property("save", False)   # save once after settings applied
            tasks.append(t)
        AT.import_asset_tasks(tasks)
        unreal.SystemLibrary.collect_garbage()

    # Report anything that failed to land so a flaky batch is visible in the log.
    missing = [n for n in imported_names if not EAL.does_asset_exist(f"{DST}/{n}")]
    if missing:
        print(f"[minimap] WARNING {len(missing)} did not import: {missing[:10]}")

    # UI texture settings (crisp, unstreamed, no mips).
    n = 0
    for path in EAL.list_assets(DST, recursive=False):
        tex = EAL.load_asset(path)
        if not isinstance(tex, unreal.Texture2D):
            continue
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("mip_gen_settings",
                                unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("compression_settings",
                                unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("never_stream", True)
        n += 1

    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
    print(f"[minimap] {n} textures set to UI group + saved -> {DST}")


if __name__ == "__main__":
    main()
