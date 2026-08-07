"""
ue5_import_pat.py — Import the PAT (cart / castle gear) part GLBs.

  SourceAssets/GLTF/PAT/base_21.glb  -> /Game/Pat/base_21   (cart skeleton)
  SourceAssets/GLTF/PAT/base_31.glb  -> /Game/Pat/base_31   (castle gear)
  SourceAssets/GLTF/PAT/pat_<id>.glb -> /Game/Pat/pat_<id>

Every part skeleton is marked compatible with its base skeleton (class
511/521/531/541/551 -> base_21, 512/522/532/552 -> base_31) so
USkeletalMergingLibrary can merge equipped parts onto one skeleton at runtime
(same recipe as the modular avatar). Interchange materials are KEPT
(embedded textures — the reliable path; ROSE_NORESKIN lesson).

Env: ROSE_ONLY="pat_1,pat_2" to limit.
     ROSE_PAT_PET   "21" or "31" — import only that skeleton family. Mixing
                    families in one process hit an Interchange assertion
                    (index 30 into array of size 29: the 29-bone castle-gear
                    skeleton state bleeding into a 56-bone cart import).
     ROSE_PAT_START / ROSE_PAT_COUNT — chunk window over the sorted part list
                    (run chunks in separate processes; driver: import_pat.ps1).
Bases are imported only when missing, otherwise loaded.
Run headless WITH RHI, editor CLOSED.
"""
import os
import sys
import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, TOOLS)
from rose_parser.formats.stb import parse as parse_stb   # noqa: E402

PROJECT_DIR = unreal.Paths.project_dir()
GLTF_DIR = os.path.normpath(os.path.join(PROJECT_DIR, "SourceAssets", "GLTF", "PAT"))
GAME_ROOT = "/Game/Pat"
ARUA_STB = r"C:\QQ-iROSE Online\extracted\3DDATA\STB\LIST_PAT.STB"

AT = unreal.AssetToolsHelpers.get_asset_tools()
EAL = unreal.EditorAssetLibrary

ONLY = {x.strip() for x in os.environ.get("ROSE_ONLY", "").split(",") if x.strip()}
PET_FILTER = int(os.environ.get("ROSE_PAT_PET", "0") or 0)
CHUNK_START = int(os.environ.get("ROSE_PAT_START", "0") or 0)
CHUNK_COUNT = int(os.environ.get("ROSE_PAT_COUNT", "0") or 0)

# Family comes from LIST_PAT col 72 "PAT Class (1:Cart, 2:CG)"; 3 = mount.
# Mounts share the cart skeleton — see build_pat_parts.py.
C_PAT_CLASS = 72
PET_FOR_CLASS = {1: 21, 2: 31, 3: 21}


def log(m):
    unreal.log(f"[patimport] {m}")


def import_glb(glb, dst):
    if EAL.does_directory_exist(dst):
        EAL.delete_directory(dst)
    t = unreal.AssetImportTask()
    t.set_editor_property("filename", glb)
    t.set_editor_property("destination_path", dst)
    t.set_editor_property("automated", True)
    t.set_editor_property("replace_existing", True)
    AT.import_asset_tasks([t])
    unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
    sk = None
    for a in EAL.list_assets(dst, recursive=True):
        o = EAL.load_asset(a)
        if o and o.get_class().get_name() == "Skeleton":
            sk = o
    return sk


stb = parse_stb(ARUA_STB)


def base_for(item_id):
    return PET_FOR_CLASS.get(stb.get_int(item_id, C_PAT_CLASS))


# 1 — bases (import only if missing; a chunked run reuses the existing asset)
base_sk = {}
for pet in (21, 31):
    if PET_FILTER and pet != PET_FILTER:
        continue
    existing = f"{GAME_ROOT}/base_{pet}"
    sk = None
    for a in (EAL.list_assets(existing, recursive=True)
              if EAL.does_directory_exist(existing) else []):
        o = EAL.load_asset(a)
        if o and o.get_class().get_name() == "Skeleton":
            sk = o
    if not sk:
        glb = os.path.join(GLTF_DIR, f"base_{pet}.glb")
        if not os.path.exists(glb):
            log(f"missing {glb}")
            continue
        sk = import_glb(glb, existing)
    base_sk[pet] = sk
    log(f"base_{pet}: skeleton {'OK' if sk else 'MISSING'}")

# 2 — parts (numeric order; optional pet filter + chunk window)
part_list = []
for fn in os.listdir(GLTF_DIR):
    if not (fn.startswith("pat_") and fn.endswith(".glb")):
        continue
    stem = fn[:-4]
    if ONLY and stem not in ONLY:
        continue
    item_id = int(stem.split("_")[1])
    pet = base_for(item_id)
    if pet not in base_sk or not base_sk[pet]:
        continue
    part_list.append((item_id, stem, pet))
part_list.sort()
if CHUNK_COUNT > 0:
    part_list = part_list[CHUNK_START:CHUNK_START + CHUNK_COUNT]
log(f"importing {len(part_list)} parts (pet={PET_FILTER or 'all'} "
    f"chunk={CHUNK_START}+{CHUNK_COUNT or 'all'})")

n = fail = 0
for item_id, stem, pet in part_list:
    dst = f"{GAME_ROOT}/{stem}"
    # re-runnable: skip parts that already imported (crash-recovery chunks)
    if EAL.does_asset_exist(f"{dst}/{stem}/SkeletalMeshes/{stem}.{stem}"):
        continue
    sk = import_glb(os.path.join(GLTF_DIR, f"{stem}.glb"), dst)
    if sk:
        base_sk[pet].add_compatible_skeleton(sk)
        sk.add_compatible_skeleton(base_sk[pet])
        EAL.save_asset(sk.get_path_name().split(".")[0])
        n += 1
    else:
        fail += 1
    if n % 50 == 0 and n:
        log(f"parts: {n} ...")

for pet, sk in base_sk.items():
    if sk:
        EAL.save_asset(sk.get_path_name().split(".")[0])
unreal.EditorLoadingAndSavingUtils.save_dirty_packages(False, True)
log(f"DONE: {n} parts imported, {fail} failed")
