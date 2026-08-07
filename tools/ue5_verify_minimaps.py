"""Verify every minimaps.json entry against the imported texture.

The widget sizes its brush from the MANIFEST (px_w/px_h) but samples the
TEXTURE, so a disagreement between the two draws the map at the wrong scale and
the arrow lands in the wrong place.  Checks both existence and dimensions.
"""
import json
import os

import unreal

EAL = unreal.EditorAssetLibrary
MANIFEST = os.path.join(unreal.Paths.project_content_dir(), "UI", "Minimaps", "minimaps.json")

with open(MANIFEST, encoding="utf-8-sig") as f:
    entries = json.load(f)

missing, mismatch, ok = [], [], 0
for zone, e in sorted(entries.items()):
    path = e.get("texture") or ""
    if not path or not EAL.does_asset_exist(path):
        missing.append(zone)
        continue
    tex = EAL.load_asset(path)
    w = tex.blueprint_get_size_x()
    h = tex.blueprint_get_size_y()
    mw, mh = int(e.get("px_w") or 0), int(e.get("px_h") or 0)
    if (w, h) != (mw, mh):
        mismatch.append((zone, w, h, mw, mh))
    else:
        ok += 1

print(f"[mm] manifest zones {len(entries)} | texture OK {ok} | "
      f"missing {len(missing)} | size mismatch {len(mismatch)}")
if missing:
    print(f"[mm] MISSING textures: {missing}")
for z, w, h, mw, mh in mismatch[:15]:
    print(f"[mm] size mismatch {z}: texture {w}x{h} vs manifest {mw}x{mh}")

# The zone in the screenshot, called out explicitly.
jpt = entries.get("JPT01")
if jpt and EAL.does_asset_exist(jpt["texture"]):
    t = EAL.load_asset(jpt["texture"])
    print(f"[mm] JPT01 '{jpt.get('zone_name')}' -> {t.blueprint_get_size_x()}x"
          f"{t.blueprint_get_size_y()}, manifest {jpt.get('px_w')}x{jpt.get('px_h')}, "
          f"start=({jpt.get('start_x')},{jpt.get('start_y')})")
