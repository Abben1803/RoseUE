"""ue5_check_textures.py — reproduce the "invalid payload" save failure headlessly.

The editor reports, for many imported textures:

    Attempting to save bulkdata <guid> with an invalid payload to package
    '/Game/Rose/Equipment/Textures/T_...'.

That is a complaint about RE-saving a package the commandlet wrote.  This loads
each texture and force-saves it, which is exactly what the editor does when you
hit Save All — so it reproduces the failure without a human clicking anything.

  UnrealEditor-Cmd.exe <proj> -ExecutePythonScript=tools/ue5_check_textures.py \
      -unattended -nopause -nosplash -stdout -FullStdOutLogOutput -nullrhi

Env:
  ROSE_TEX_PATH   asset path to sweep  (default /Game/Rose/Equipment/Textures)
  ROSE_TEX_MAX    stop after N assets  (default 200; 0 = all)
"""
import os
import unreal

PATH = os.environ.get("ROSE_TEX_PATH", "/Game/Rose/Equipment/Textures")
MAX = int(os.environ.get("ROSE_TEX_MAX", "200"))

AR = unreal.AssetRegistryHelpers.get_asset_registry()
EAL = unreal.EditorAssetLibrary


def log(m):
    unreal.log(f"[texcheck] {m}")


assets = AR.get_assets_by_path(PATH, recursive=True)
log(f"{len(assets)} assets under {PATH}")
if MAX:
    assets = assets[:MAX]

loaded = 0
bad_source = 0
save_ok = 0
save_fail = 0
failures = []

for a in assets:
    pkg = str(a.package_name)
    obj = EAL.load_asset(pkg)
    if not isinstance(obj, unreal.Texture2D):
        continue
    loaded += 1

    # ImportedSize comes from FTextureSource (the bulkdata payload's dimensions),
    # NOT from platform data — so 0x0 means the source payload is gone.
    isz = obj.get_editor_property("imported_size")
    if isz.x <= 0 or isz.y <= 0:
        bad_source += 1

    # Force a save even though nothing is dirty: this is the exact path that
    # emits "invalid payload".  Nothing about the asset is modified first.
    if EAL.save_asset(pkg, only_if_is_dirty=False):
        save_ok += 1
    else:
        save_fail += 1
        if len(failures) < 10:
            failures.append(f"{pkg}  imported={isz.x}x{isz.y}")

log("---- result ----")
log(f"  textures loaded        : {loaded}")
log(f"  zero ImportedSize      : {bad_source}")
log(f"  re-saved OK            : {save_ok}")
log(f"  re-save FAILED         : {save_fail}")
for f in failures:
    log(f"    {f}")

print("[texcheck] DONE")
