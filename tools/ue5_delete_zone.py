"""ue5_delete_zone.py — delete /Game/Maps/<ROSE_ZONE> (level + scene assets)
ahead of a re-import.  Headless, editor CLOSED (-nullrhi fine)."""
import os
import unreal

ZONE = os.environ.get("ROSE_ZONE", "").upper()
if not ZONE:
    raise RuntimeError("ROSE_ZONE not set")
EAL = unreal.EditorAssetLibrary
d = f"/Game/Maps/{ZONE}"
if not EAL.does_directory_exist(d):
    print(f"[delzone] {d} absent")
else:
    n = len(EAL.list_assets(d, recursive=True))
    ok = EAL.delete_directory(d)
    left = len(EAL.list_assets(d, recursive=True)) if EAL.does_directory_exist(d) else 0
    print(f"[delzone] {d}: {n} assets, delete={ok}, left={left}")
print("[delzone] done")
