"""
extract_mi_textures.py — offline (no UE): scan each moved JPT01 scene MI's
.uasset binary for the texture it imported (the old-path import string
survives on disk) and write the MI->texture mapping JSON that
ue5_repair_jpt01_textures.py applies in-editor.

Usage: py extract_mi_textures.py
Out:   tools/_tmp/jpt01_mi_textures.json  { "M0_T030_02": "JPT01V2_texture_12", ... }
"""
import json
import os
import re

MATS = r"C:\rose-next-classic\unreal-engine rose\RoseUE\Content\Maps\JPT01\Scene\JPT01V2\Materials"
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), "_tmp", "jpt01_mi_textures.json")

pat = re.compile(rb"JPT01V2_texture_(\d+)")
mapping = {}
missing = []
for f in os.listdir(MATS):
    if not f.endswith(".uasset"):
        continue
    name = f[:-len(".uasset")]
    with open(os.path.join(MATS, f), "rb") as fh:
        data = fh.read()
    nums = sorted({int(m.group(1)) for m in pat.finditer(data)})
    if not nums:
        missing.append(name)
        continue
    if len(nums) > 1:
        print(f"[extract] {name}: multiple textures {nums} (using first)")
    mapping[name] = f"JPT01V2_texture_{nums[0]}"

os.makedirs(os.path.dirname(OUT), exist_ok=True)
with open(OUT, "w", encoding="utf-8") as fh:
    json.dump(mapping, fh, indent=1)
print(f"[extract] {len(mapping)} MIs mapped, {len(missing)} without texture "
      f"(color/water: {missing[:6]}{'...' if len(missing) > 6 else ''})")
print(f"[extract] -> {OUT}")
