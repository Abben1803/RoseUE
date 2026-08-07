"""Force-build catalog texture platform data (nullrhi import skipped it → checkered).
Loads every catalog texture, then blocks on AssetCompilingManager until all texture
builds finish.  CPU build (no thumbnails) → no GPU TDR.

ROSE_GENDER=Female|Male (default both)."""
import unreal, os

EAL = unreal.EditorAssetLibrary
genv = os.environ.get("ROSE_GENDER", "").capitalize()
GENDERS = [genv] if genv in ("Female", "Male") else ["Female", "Male"]

loaded = 0
for g in GENDERS:
    ROOT = f"/Game/Characters/Modular/{g}"
    if not EAL.does_directory_exist(ROOT):
        continue
    for d in EAL.list_assets(ROOT, recursive=True):
        if "/Textures/" in d:
            t = EAL.load_asset(d)
            if t:
                loaded += 1
    print(f"[buildtex] {g}: requested {loaded} textures so far")

print("[buildtex] finishing compilation...")
try:
    unreal.AssetCompilingManager.get().finish_all_compilation()
    print("[buildtex] AssetCompilingManager.finish_all_compilation OK")
except Exception as e:
    print(f"[buildtex] finish_all_compilation FAILED: {e}")
print(f"[buildtex] done: {loaded} textures")
