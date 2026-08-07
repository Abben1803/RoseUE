"""
ue5_import_dds_textures.py — Old-plugin-style direct DDS texture import with
source-path dedup (UnrealRosePlugin BuildAssetPath/ImportTexture equivalent).

Each 3DDATA-relative DDS becomes ONE shared /Game/ROSE/Textures/<DIRS>/<NAME>
asset regardless of how many materials reference it; re-runs and duplicate
requests resolve to the existing asset instead of importing again. UE 5.x
imports .DDS natively (BCn data + existing mip chain are preserved).

Inputs (either):
  ROSE_DDS_LIST   text file, one 3DDATA-relative or absolute DDS path per line
  ROSE_ZONE       import every texture_src of mapforge/exports/<Z>.glb.materials.json

Env:
  ROSE_ASSET_ROOT client 3DDATA root for relative paths
                  (default C:\\rose-next-classic\\Arua_Extracted\\3DDATA)

Output: tools/_tmp/dds_import_report.json  {src: asset_path | "FAILED"}
Run headless, editor CLOSED. -nullrhi is fine.
"""
import os
import json
import re
import unreal

TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
DEST_ROOT = "/Game/ROSE/Textures"

EAL = unreal.EditorAssetLibrary
AT = unreal.AssetToolsHelpers.get_asset_tools()


def log(m):
    unreal.log(f"[ddsimport] {m}")


def resolve(rel):
    """Case-insensitive resolve of a 3DDATA-relative path under ROOT."""
    if os.path.isabs(rel):
        return rel if os.path.exists(rel) else None
    parts = [p for p in rel.replace("\\", "/").split("/") if p]
    if parts and parts[0].lower() == "3ddata":
        parts = parts[1:]
    cur = ROOT
    for p in parts:
        if not os.path.isdir(cur):
            return None
        m = [e for e in os.listdir(cur) if e.lower() == p.lower()]
        if not m:
            return None
        cur = os.path.join(cur, m[0])
    return cur if os.path.exists(cur) else None


def sanitize(name):
    return re.sub(r"[^A-Za-z0-9_\-]", "_", name)


def asset_dest(src_abs):
    """Mirror the 3DDATA-relative directory tree under DEST_ROOT."""
    norm = os.path.normpath(src_abs)
    rootn = os.path.normpath(ROOT)
    if norm.lower().startswith(rootn.lower()):
        rel = norm[len(rootn):].lstrip("\\/")
    else:
        rel = os.path.basename(norm)
    dirs = [sanitize(d).upper() for d in os.path.dirname(rel).replace("\\", "/").split("/") if d]
    stem = sanitize(os.path.splitext(os.path.basename(rel))[0]).upper()
    pkg_dir = "/".join([DEST_ROOT] + dirs)
    return pkg_dir, stem


PNG_CACHE = os.path.join(TOOLS, "_tmp", "ddspng")


def png_for(src_rel):
    """decode_dds_cache.py output for this source, if present."""
    parts = [p for p in src_rel.replace("\\", "/").split("/") if p]
    if parts and parts[0].lower() == "3ddata":
        parts = parts[1:]
    p = os.path.join(PNG_CACHE, *parts) + ".png"
    return p if os.path.exists(p) else None


def import_dds(src_rel):
    src = resolve(src_rel)
    if not src or not src.lower().endswith(".dds"):
        return None
    pkg_dir, stem = asset_dest(src)
    asset_path = f"{pkg_dir}/{stem}.{stem}"
    if EAL.does_asset_exist(asset_path):          # dedup: one asset per source
        return asset_path
    # UE 5.8 cannot read BCn DDS (both Interchange and TextureFactory raise
    # "DDS DXGIFormat not supported"), so import the lossless PNG produced by
    # tools/decode_dds_cache.py instead; UE re-compresses to BCn via Oodle.
    filename = png_for(src_rel) or src
    task = unreal.AssetImportTask()
    task.filename = filename
    task.destination_path = pkg_dir
    task.destination_name = stem
    task.automated = True
    task.save = True
    task.replace_existing = False
    task.factory = unreal.TextureFactory()
    AT.import_asset_tasks([task])
    if EAL.does_asset_exist(asset_path):
        tex = EAL.load_asset(asset_path)
        if isinstance(tex, unreal.Texture2D):
            EAL.set_metadata_tag(tex, "RoseSourceDDS", src_rel)
            EAL.save_asset(asset_path)
            return asset_path
    return None


srcs = []
lst = os.environ.get("ROSE_DDS_LIST", "")
zone = os.environ.get("ROSE_ZONE", "")
if lst and os.path.exists(lst):
    with open(lst) as f:
        srcs = [l.strip() for l in f if l.strip()]
elif zone:
    mj = os.path.join(TOOLS, "..", "mapforge", "exports", f"{zone}.glb.materials.json")
    with open(mj) as f:
        mats = json.load(f)["materials"]
    seen = set()
    for m in mats:
        s = m.get("texture_src")
        if s and s.lower().endswith(".dds") and s.lower() not in seen:
            seen.add(s.lower())
            srcs.append(s)
else:
    raise RuntimeError("set ROSE_DDS_LIST or ROSE_ZONE")

report = {}
ok = 0
for s in srcs:
    ap = import_dds(s)
    report[s] = ap or "FAILED"
    if ap:
        ok += 1
with open(os.path.join(TOOLS, "_tmp", "dds_import_report.json"), "w") as f:
    json.dump(report, f, indent=1)
log(f"DONE {ok}/{len(srcs)} textures -> {DEST_ROOT}")
