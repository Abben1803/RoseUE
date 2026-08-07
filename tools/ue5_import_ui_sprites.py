"""
ue5_import_ui_sprites.py — Import the sliced Arua UI PNGs as UI-group textures:

  SourceAssets/UI/Sprites    -> /Game/UI/Sprites     (window-skin sprites, by GID)
  SourceAssets/UI/Icons      -> /Game/UI/Icons       (item icons, icon_<IconIdx>)
  SourceAssets/UI/SkillIcons -> /Game/UI/SkillIcons  (skill icons, skillicon_<no>)
  SourceAssets/UI/SlotGhosts -> /Game/UI/SlotGhosts  (empty-equip-slot silhouettes)

UI settings per texture: TEXTUREGROUP_UI, no mipmaps, UserInterface2D
compression, never stream — crisp 1:1 pixels for Slate brushes.

Env: ROSE_UI_ONLY="Icons" (or "Sprites") to import just one folder;
     ROSE_UI_MATCH="_BOX" to import only files whose name contains that.
Run headless (editor CLOSED); textures need no RHI shader work, -nullrhi is fine.
"""
import os
import unreal

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary
# ROSE_UI_ONLY="Icons" or "Icons,SkillIcons" — comma-separated, because an
# editor launch costs ~8 minutes and the icon folders are usually rebuilt together.
ONLY = [s.strip() for s in os.environ.get("ROSE_UI_ONLY", "").split(",") if s.strip()]
# ROSE_UI_MATCH="_BOX" imports only the files whose name contains it — a full
# 1171-sprite pass costs an editor cycle for what is usually a handful of new
# frames.
MATCH = os.environ.get("ROSE_UI_MATCH", "").strip()

for sub in ("Sprites", "Icons", "SkillIcons", "SlotGhosts"):
    if ONLY and sub not in ONLY:
        continue
    src = os.path.normpath(os.path.join(unreal.Paths.project_dir(), "SourceAssets", "UI", sub))
    dst = f"/Game/UI/{sub}"
    if not os.path.isdir(src):
        print(f"[uispr] {src} missing — skipped")
        continue
    pngs = sorted(f for f in os.listdir(src) if f.lower().endswith(".png"))
    if MATCH:
        pngs = [f for f in pngs if MATCH.lower() in f.lower()]
    print(f"[uispr] importing {len(pngs)} -> {dst}")

    # ── WIPE FIRST ───────────────────────────────────────────────────────────
    #
    # `replace_existing` does NOT replace anything under Interchange in 5.8 —
    # an asset that already exists at the destination is silently left alone,
    # so a re-import over the same names is a no-op on the pixels.  Setting the
    # texture group afterwards still dirties and re-saves every package, which
    # makes the .uasset mtimes look freshly written; that is what hid an entire
    # era of wrong icon art (the tables had moved to classic, the textures were
    # still Arua).  Deleting first is the only way to be sure the bytes change.
    #
    # Skipped when MATCH is set — a filtered run legitimately touches a subset.
    if MATCH:
        print(f"[uispr] {sub}: wipe SKIPPED (ROSE_UI_MATCH is set)")
    elif os.environ.get("ROSE_UI_NOWIPE"):
        print(f"[uispr] {sub}: wipe SKIPPED (ROSE_UI_NOWIPE set)")
    else:
        old = EAL.list_assets(dst, recursive=False) if EAL.does_directory_exist(dst) else []
        for p in old:
            EAL.delete_asset(p)
        print(f"[uispr] {sub}: wiped {len(old)} existing asset(s) before import")

    # Import in chunks — one huge task list silently drops files (observed:
    # 259 of 1052 icons missing after a single 1052-task call).
    CHUNK = 200
    for c in range(0, len(pngs), CHUNK):
        tasks = []
        for f in pngs[c:c + CHUNK]:
            t = unreal.AssetImportTask()
            t.set_editor_property("filename", os.path.join(src, f))
            t.set_editor_property("destination_path", dst)
            t.set_editor_property("automated", True)
            t.set_editor_property("replace_existing", True)
            t.set_editor_property("save", False)   # save once after settings are applied
            tasks.append(t)
        AT.import_asset_tasks(tasks)
        unreal.SystemLibrary.collect_garbage()
    # report anything still missing so a flaky batch is visible in the log
    missing = [f for f in pngs
               if not EAL.does_asset_exist(f"{dst}/{os.path.splitext(f)[0]}")]
    if missing:
        print(f"[uispr] WARNING {len(missing)} files did not import: {missing[:10]}")

    n = 0
    for path in EAL.list_assets(dst, recursive=False):
        tex = EAL.load_asset(path)
        if not isinstance(tex, unreal.Texture2D):
            continue
        tex.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_UI)
        tex.set_editor_property("mip_gen_settings", unreal.TextureMipGenSettings.TMGS_NO_MIPMAPS)
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_EDITOR_ICON)
        tex.set_editor_property("never_stream", True)
        n += 1
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
    print(f"[uispr] {sub}: {n} textures set to UI group + saved")

    # ── ORPHAN SWEEP ─────────────────────────────────────────────────────────
    #
    # Delete assets in the destination that this run did NOT produce.
    #
    # Importing OVER a folder only overwrites names that still exist in the
    # source.  Anything the previous era had and this one does not just stays,
    # silently serving the wrong art — an icon id that existed in Arua but not
    # in classic keeps its Arua picture, and nothing in the log says so.  That
    # is the same era-mixing failure the asset migration keeps hitting, so the
    # importer sweeps rather than trusting overwrite.
    #
    # Skipped when MATCH is set: a filtered run legitimately produces only a
    # subset, and sweeping would delete everything else.
    if MATCH:
        print(f"[uispr] {sub}: orphan sweep SKIPPED (ROSE_UI_MATCH is set)")
    else:
        expected = {os.path.splitext(f)[0] for f in pngs}
        orphans = [p for p in EAL.list_assets(dst, recursive=False)
                   if p.split("/")[-1].split(".")[0] not in expected]
        for p in orphans:
            EAL.delete_asset(p)
        print(f"[uispr] {sub}: swept {len(orphans)} orphaned assets "
              f"({len(expected)} expected)")
