"""
decode_dds_cache.py — Offline companion for ue5_import_dds_textures.py.

UE 5.8 cannot ingest BCn-compressed DDS at all ("DDS DXGIFormat not
supported: BC1_UNORM" from both Interchange and the legacy TextureFactory), so
"direct DDS" import is impossible on this engine version. This decodes each
DDS to a lossless PNG cache (Pillow reads ROSE's BC1/BC2/BC3 fine) that the
UE-side importer picks up; UE recompresses to BCn via Oodle on import. The
old plugin's real win — ONE shared texture asset per source path — is kept.

Usage: py -3.9 tools/decode_dds_cache.py <list-file> [asset-root]
Cache: tools/_tmp/ddspng/<3DDATA-relative path>.png
"""
import os
import sys

from PIL import Image

TOOLS = os.path.dirname(os.path.abspath(__file__))
CACHE = os.path.join(TOOLS, "_tmp", "ddspng")

LIST = sys.argv[1]
ROOT = sys.argv[2] if len(sys.argv) > 2 else r"C:\QQ-iROSE Online\extracted\3DDATA"


def resolve(rel):
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


def cache_rel(rel):
    parts = [p for p in rel.replace("\\", "/").split("/") if p]
    if parts and parts[0].lower() == "3ddata":
        parts = parts[1:]
    return "/".join(parts)


n_ok = n_fail = n_cached = 0
with open(LIST) as f:
    for line in f:
        rel = line.strip()
        if not rel:
            continue
        out = os.path.join(CACHE, cache_rel(rel) + ".png")
        if os.path.exists(out):
            n_cached += 1
            continue
        src = resolve(rel)
        if not src:
            print("MISSING", rel)
            n_fail += 1
            continue
        try:
            im = Image.open(src)
            im.load()
            has_alpha = im.mode in ("RGBA", "LA") or "A" in im.getbands()
            im = im.convert("RGBA" if has_alpha else "RGB")
            os.makedirs(os.path.dirname(out), exist_ok=True)
            im.save(out, "PNG")
            n_ok += 1
        except Exception as e:
            print("FAIL", rel, e)
            n_fail += 1
print(f"decoded {n_ok}, cached {n_cached}, failed {n_fail} -> {CACHE}")
