"""
ue5_repair_jpt01.py — repair the promoted JPT01 scene after the redirector
mishap: every scene MI lost its texture (the old /Game/Maps/JPT01V2 redirectors
are gone, so the MIs' texture imports resolve to null -> white map).

The MI->texture association survives in each package's ON-DISK dependency list
(asset registry), which still names the old texture package.  For each MI:
  old dep  /Game/Maps/JPT01V2/Scene/JPT01V2/Textures/JPT01V2_texture_N
  -> set its texture parameter(s) to /Game/Maps/JPT01/Scene/JPT01V2/Textures/JPT01V2_texture_N
Also moves the one straggler texture (JPT01V2_texture_99) to the new folder
first, then deletes the old /Game/Maps/JPT01V2 folder once nothing needs it.

Headless, editor CLOSED (-nullrhi fine).  Re-runnable (fixed MIs no longer
have old-path deps and are skipped).
"""
import re
import unreal

EAL = unreal.EditorAssetLibrary
MEL = unreal.MaterialEditingLibrary
AR = unreal.AssetRegistryHelpers.get_asset_registry()

MAT_DIR = "/Game/Maps/JPT01/Scene/JPT01V2/Materials"
NEW_TEX_DIR = "/Game/Maps/JPT01/Scene/JPT01V2/Textures"
OLD_PREFIX = "/Game/Maps/JPT01V2/"
DEP_PAT = re.compile(r"/Game/Maps/JPT01V2/.*/(JPT01V2_texture_\d+)$")

# 0 ── the straggler texture still at the old path ────────────────────────────
straggler_old = "/Game/Maps/JPT01V2/Scene/JPT01V2/Textures/JPT01V2_texture_99"
straggler_new = f"{NEW_TEX_DIR}/JPT01V2_texture_99"
if EAL.does_asset_exist(straggler_old) and not EAL.does_asset_exist(straggler_new):
    ok = EAL.rename_asset(straggler_old, straggler_new)
    print(f"[repair] moved straggler texture_99: {ok}")

opts = unreal.AssetRegistryDependencyOptions()

fixed = notex = already = failed = 0
for path in EAL.list_assets(MAT_DIR, recursive=False, include_folder=False):
    clean = str(path).split(".")[0]
    pkg = clean  # package name == object path sans .name for top-level assets
    deps = AR.get_dependencies(unreal.Name(pkg), opts) or []
    tex_name = None
    stale = False
    for d in deps:
        ds = str(d)
        m = DEP_PAT.match(ds)
        if m:
            tex_name = m.group(1)
        if ds.startswith(OLD_PREFIX):
            stale = True
    if not stale:
        already += 1
        continue
    mi = EAL.load_asset(clean)
    if not isinstance(mi, unreal.MaterialInstanceConstant):
        continue
    if tex_name is None:
        notex += 1          # stale dep but no texture pattern (shouldn't happen)
        continue
    tex = EAL.load_asset(f"{NEW_TEX_DIR}/{tex_name}")
    if not tex:
        print(f"[repair] {clean}: texture {tex_name} MISSING in new folder")
        failed += 1
        continue
    # set every texture parameter this MI overrides; if it records none,
    # fall back to the parent's texture parameter names.
    names = [tp.get_editor_property("parameter_info").get_editor_property("name")
             for tp in mi.get_editor_property("texture_parameter_values")]
    if not names:
        parent = mi.get_editor_property("parent")
        base = parent
        while isinstance(base, unreal.MaterialInstanceConstant):
            base = base.get_editor_property("parent")
        names = list(MEL.get_texture_parameter_names(base)) if isinstance(base, unreal.Material) else []
    if not names:
        print(f"[repair] {clean}: no texture parameter names found")
        failed += 1
        continue
    for n in names:
        MEL.set_material_instance_texture_parameter_value(mi, n, tex)
    MEL.update_material_instance(mi)
    EAL.save_asset(clean)
    if fixed < 3:
        print(f"[repair] {clean.rsplit('/',1)[-1]} -> {tex_name} (params {list(map(str,names))})")
    fixed += 1

print(f"[repair] MIs: {fixed} repaired, {already} already clean, "
      f"{notex} stale-no-texture, {failed} failed")

# ── final cleanup: only when nothing references the old folder any more ─────
left = EAL.list_assets("/Game/Maps/JPT01V2", recursive=True, include_folder=False) \
    if EAL.does_directory_exist("/Game/Maps/JPT01V2") else []
if left:
    ok = EAL.delete_directory("/Game/Maps/JPT01V2")
    print(f"[repair] old folder: {len(left)} assets left, delete: {ok}")
else:
    print("[repair] old folder already gone/empty")
print("[repair] done")
