#!/usr/bin/env python3
"""
build_gear_meshes.py — Phase 3: build one GLB per UNIQUE gear/back mesh.

Each GLB = the avatar skeleton + that ZMS as a single skinned/rigid section, NO
texture (the atlas material is assigned in UE).  Deduping by mesh is valid: body/
arms/foot are skinned to the skeleton; cap/back are rigid but attach to a fixed
dummy (cap=6, back=3) with no per-item transform.

Reads tools/_tmp/gear_manifest.json; writes
    SourceAssets/GLTF/AVATAR/GEAR/mesh_<id>.glb   (id = manifest mesh index)

py -3.9 (needs the parser stack).  Env: ROSE_ASSET_ROOT (Arua 3DDATA),
ROSE_LIMIT (smoke-test a few), resumable (skips existing GLBs).
"""
import json
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _TOOLS)
from rose_avatar import PART_TYPES, _is_skinned
from rose_combine_anims import convert

ROOT = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
LIMIT = int(os.environ.get("ROSE_LIMIT", "0"))
MANIFEST = os.path.join(_TOOLS, "_tmp", "gear_manifest.json")
OUT = os.path.normpath(os.path.join(
    _TOOLS, "..", "unreal-engine rose", "RoseUE", "SourceAssets", "GLTF", "AVATAR", "GEAR"))
os.makedirs(OUT, exist_ok=True)

# skeleton per gender (Arua avatar skeletons)
ZMD = {"F": os.path.join(ROOT, "AVATAR", "FEMALE.ZMD"),
       "M": os.path.join(ROOT, "AVATAR", "MALE.ZMD"),
       "N": os.path.join(ROOT, "AVATAR", "FEMALE.ZMD")}  # back = neutral, female rig

man = json.load(open(MANIFEST))
meshes = man["meshes"]

# mesh_id -> (slot, gender) from the first item that uses it
mesh_meta = {}
for key, parts in man["items"].items():
    gender, slot, _ = key.split("/")
    for p in parts:
        mesh_meta.setdefault(p["mesh"], (slot, gender))

# case-insensitive ZMS index under the asset root
zms_index = {}
for dp, _, files in os.walk(ROOT):
    for f in files:
        if f.lower().endswith(".zms"):
            zms_index.setdefault(f.lower(), os.path.join(dp, f))


def resolve(relpath):
    return zms_index.get(os.path.basename(relpath.replace("\\", "/")).lower())


built = skipped = missing = failed = 0
for mid, rel in enumerate(meshes):
    out_glb = os.path.join(OUT, f"mesh_{mid}.glb")
    if os.path.exists(out_glb):
        skipped += 1
        continue
    abs_zms = resolve(rel)
    if not abs_zms:
        missing += 1
        continue
    slot, gender = mesh_meta.get(mid, ("BODY", "F"))
    cfg = PART_TYPES.get(slot, PART_TYPES["BODY"])
    skinned = _is_skinned(abs_zms)
    part = {
        "path": abs_zms,
        "pin": None if skinned else cfg.get("attach"),
        "pin_dummy": None if skinned else cfg.get("dummy"),
        "pin_xform": None, "texture": None, "alpha": False,
        "two_side": False, "blink": False,
    }
    try:
        convert(ZMD[gender], [part], [], out_glb)
        built += 1
    except Exception as e:
        failed += 1
        print(f"[gearmesh] FAIL mesh_{mid} ({os.path.basename(abs_zms)}): {e}")
    if LIMIT and built >= LIMIT:
        break
    if built and built % 200 == 0:
        print(f"[gearmesh] ... built {built}")

print(f"[gearmesh] DONE: built={built} skipped={skipped} missing={missing} failed={failed} "
      f"-> {OUT}")
