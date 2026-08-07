#!/usr/bin/env python3
"""
build_gear_atlases.py — Phase 2: pack the gear textures into 2048 atlases.

Reads tools/_tmp/gear_manifest.json, loads each unique texture from the Arua
packs, resizes to 128x128, and pastes it into its atlas cell (row-major, 16x16
per 2048 page).  Writes the atlas PNGs to
    SourceAssets/GLTF/AVATAR/GEAR_ATLAS/gear_atlas_<page>.png
which the UE import + gear master material sample via the per-item UVTransform
(the cell rect is already recorded in the manifest's "atlas" map).

ALPHA DILATION (the black-fringe fix): ROSE authors its cutout textures with
*pure black* RGB under the transparent texels (measured: 86% of all transparent
texels across the 32 pages).  Black RGB next to visible colour bleeds into the
visible edge at three separate stages — the LANCZOS downscale here, the GPU's
bilinear filtering, and BC3 block compression + mipmaps — which is what produced
the black halos/blocks around wings and every other alpha-cut item.  Fixing the
material blend mode cannot help: the black arrives through *filtering*, not
through coverage.

So before the resize we dilate (a.k.a. edge-pad / colour-bleed) each texture:
transparent texels take the colour of their nearest opaque neighbour while
keeping alpha=0.  Resizing is also done PREMULTIPLIED and then un-premultiplied,
so the downscale itself can never average black into the edge.

Offline (py -3.9 for Pillow + numpy).  Env: ROSE_ASSET_ROOT (default Arua 3DDATA).
"""
import json
import os

import numpy as np
from PIL import Image

_TOOLS = os.path.dirname(os.path.abspath(__file__))
ROOT = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
MANIFEST = os.path.join(_TOOLS, "_tmp", "gear_manifest.json")
OUT = os.path.normpath(os.path.join(
    _TOOLS, "..", "unreal-engine rose", "RoseUE", "SourceAssets",
    "GLTF", "AVATAR", "GEAR_ATLAS"))
ATLAS_PX, CELL_PX, PER_AXIS, PER_ATLAS = 2048, 128, 16, 256

os.makedirs(OUT, exist_ok=True)
man = json.load(open(MANIFEST))
textures = man["textures"]
n_atlas = (len(textures) + PER_ATLAS - 1) // PER_ATLAS
print(f"[atlas] {len(textures)} textures -> {n_atlas} atlas pages")

# build a case-insensitive index of every DDS under the asset root once
index = {}
for dp, _, files in os.walk(ROOT):
    for f in files:
        if f.lower().endswith(".dds"):
            index.setdefault(f.lower(), os.path.join(dp, f))


def find(texpath):
    base = os.path.basename(texpath.replace("\\", "/")).lower()
    return index.get(base)


def dilate_rgb(im, passes=32):
    """Edge-pad: flood each transparent texel with its nearest opaque colour,
    leaving alpha untouched.  One pass grows the known region by one texel, so
    `passes` bounds the bleed radius (32 covers a 128px cell's mip chain)."""
    a = np.array(im, dtype=np.uint8)
    rgb = a[..., :3].astype(np.int16)
    known = a[..., 3] > 0
    if known.all() or not known.any():
        return im
    for _ in range(passes):
        if known.all():
            break
        # Sum the colour of known neighbours (4-way) and average them.
        acc = np.zeros_like(rgb)
        cnt = np.zeros(known.shape, dtype=np.int16)
        for dy, dx in ((-1, 0), (1, 0), (0, -1), (0, 1)):
            k = np.roll(known, (dy, dx), (0, 1))
            c = np.roll(rgb, (dy, dx), (0, 1))
            # np.roll wraps; blank the wrapped edge so colour can't cross borders.
            if dy:
                (k[0:1] if dy > 0 else k[-1:])[...] = False
            if dx:
                (k[:, 0:1] if dx > 0 else k[:, -1:])[...] = False
            acc += c * k[..., None]
            cnt += k
        fill = (cnt > 0) & ~known
        if not fill.any():
            break                      # nothing else reachable
        safe = np.maximum(cnt, 1)[..., None]
        rgb[fill] = (acc // safe)[fill]
        known |= fill
    a[..., :3] = rgb.astype(np.uint8)
    return Image.fromarray(a, "RGBA")


def resize_premul(im, size):
    """Downscale with premultiplied alpha so the filter cannot average the
    (black) colour of transparent texels into the visible edge."""
    a = np.array(im, dtype=np.float32)
    al = a[..., 3:4] / 255.0
    a[..., :3] *= al                                   # -> premultiplied
    sm = np.array(Image.fromarray(a.astype(np.uint8), "RGBA")
                  .resize(size, Image.LANCZOS), dtype=np.float32)
    al2 = np.maximum(sm[..., 3:4] / 255.0, 1e-4)
    sm[..., :3] = np.clip(sm[..., :3] / al2, 0, 255)    # -> straight again
    return Image.fromarray(sm.astype(np.uint8), "RGBA")


pages = {}
missing = 0
for tid, texpath in enumerate(textures):
    page, cell = divmod(tid, PER_ATLAS)
    cy, cx = divmod(cell, PER_AXIS)
    if page not in pages:
        pages[page] = Image.new("RGBA", (ATLAS_PX, ATLAS_PX), (0, 0, 0, 0))
    src = find(texpath)
    if not src:
        missing += 1
        continue
    try:
        im = Image.open(src).convert("RGBA")
        # Dilate BEFORE the resize — otherwise the downscale has already mixed
        # black into the edge and no later padding can recover the colour.
        im = dilate_rgb(im)
        if im.size != (CELL_PX, CELL_PX):
            im = resize_premul(im, (CELL_PX, CELL_PX))
            im = dilate_rgb(im)      # re-pad: the resize softens the alpha edge
        pages[page].paste(im, (cx * CELL_PX, cy * CELL_PX))
    except Exception as e:
        missing += 1
    if tid % 1000 == 0:
        print(f"[atlas] ... {tid}/{len(textures)}")

for page, img in sorted(pages.items()):
    fp = os.path.join(OUT, f"gear_atlas_{page:02d}.png")
    img.save(fp)
print(f"[atlas] DONE: {len(pages)} atlases -> {OUT}  ({missing} textures unresolved)")
