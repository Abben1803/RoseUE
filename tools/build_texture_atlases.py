#!/usr/bin/env python3
"""
build_texture_atlases.py — Pack the catalog textures into 2048px RGBA atlas
pages (per category), from tools/_tmp/texture_manifest.json.

- Dedupes by pixel-content hash (many ZSC paths reference identical images).
- Shelf-packs with an 8px gutter; each texture's border is dilated into the
  gutter so mip bleeding stays the same color.
- "tiled" textures (mesh UVs outside [0,1]) and anything wider/taller than the
  page stay SOLO — imported standalone, still driven by the master material.

Output:
  SourceAssets/ATLAS/T_Atlas_<category>_<n>.png
  tools/_tmp/atlas_manifest.json
    { "pages":   {page: png_abs},
      "map":     {tex_abs: {"page": name, "scale": [su,sv], "offset": [ou,ov]}},
      "solo":    {tex_abs: reason} }

Usage:  py -3.9 build_texture_atlases.py [manifest_json] [out_json]
        (defaults: _tmp/texture_manifest.json -> _tmp/atlas_manifest.json;
         pass the map manifest for a zone's map pass)
"""
import hashlib
import json
import os
import sys

from PIL import Image

_T = os.path.dirname(os.path.abspath(__file__))
MANIFEST = sys.argv[1] if len(sys.argv) > 1 else os.path.join(_T, "_tmp", "texture_manifest.json")
if not os.path.isabs(MANIFEST):
    MANIFEST = os.path.join(_T, MANIFEST)
OUT_DIR = os.path.join(os.path.normpath(os.path.join(_T, "..")),
                       "unreal-engine rose", "RoseUE", "SourceAssets", "ATLAS")
OUT_JSON = sys.argv[2] if len(sys.argv) > 2 else os.path.join(_T, "_tmp", "atlas_manifest.json")
if not os.path.isabs(OUT_JSON):
    OUT_JSON = os.path.join(_T, OUT_JSON)

PAGE = 2048
PAD = 8          # gutter on every side of every packed texture


def dilate_paste(page, im, x, y):
    """Paste im at (x, y) and replicate its edges PAD px outward."""
    w, h = im.size
    page.paste(im, (x, y))
    left = im.crop((0, 0, 1, h)).resize((PAD, h))
    right = im.crop((w - 1, 0, w, h)).resize((PAD, h))
    page.paste(left, (x - PAD, y))
    page.paste(right, (x + w, y))
    top = page.crop((x - PAD, y, x + w + PAD, y + 1)).resize((w + 2 * PAD, PAD))
    bot = page.crop((x - PAD, y + h - 1, x + w + PAD, y + h)).resize((w + 2 * PAD, PAD))
    page.paste(top, (x - PAD, y - PAD))
    page.paste(bot, (x - PAD, y + h))


class Packer:
    """Simple shelf packer over PAGE² with PAD gutters."""
    def __init__(self):
        self.x = PAD
        self.y = PAD
        self.shelf_h = 0

    def place(self, w, h):
        """Top-left position for a w×h image, or None if the page is full."""
        if self.x + w + PAD > PAGE:                 # new shelf
            self.x = PAD
            self.y += self.shelf_h + 2 * PAD
            self.shelf_h = 0
        if self.y + h + PAD > PAGE:
            return None
        pos = (self.x, self.y)
        self.x += w + 2 * PAD
        self.shelf_h = max(self.shelf_h, h)
        return pos


def main():
    with open(MANIFEST, "r", encoding="utf-8") as f:
        man = json.load(f)
    textures = man["textures"]
    os.makedirs(OUT_DIR, exist_ok=True)

    # Load + hash + group per category (sorted for deterministic packing).
    # Tiled/oversize textures become standalone "solo" PNGs but flow through
    # the same manifest map (scale 1, offset 0) so the apply script treats
    # every texture uniformly.
    by_cat = {}
    solo = {}
    solo_map = {}
    images = {}        # hash -> PIL image
    tex_hash = {}      # tex path -> hash
    solo_dir = os.path.join(OUT_DIR, "SOLO")
    os.makedirs(solo_dir, exist_ok=True)
    solo_pages = {}
    for tex, info in sorted(textures.items()):
        try:
            im = Image.open(tex).convert("RGBA")
        except Exception as e:
            solo[tex] = f"unreadable: {e}"
            continue
        oversize = im.width > PAGE - 2 * PAD or im.height > PAGE - 2 * PAD
        if info["tiled"] or oversize:
            hs = hashlib.md5(im.tobytes()).hexdigest()
            name = f"SOLO/T_Solo_{hs[:16]}"
            png = os.path.join(solo_dir, f"T_Solo_{hs[:16]}.png")
            if not os.path.isfile(png):
                im.save(png)
            solo_pages[name] = png
            solo_map[tex] = {"page": name, "scale": [1.0, 1.0], "offset": [0.0, 0.0]}
            solo[tex] = "oversize" if oversize else "tiled"
            continue
        hs = hashlib.md5(im.tobytes()).hexdigest()
        images.setdefault(hs, im)
        tex_hash[tex] = hs
        by_cat.setdefault(info["category"], {})[hs] = None

    pages = {}
    mapping = {}
    placed = {}        # (category, hash) -> (page_name, x, y, w, h)
    for cat, hashes in sorted(by_cat.items()):
        # Tallest first packs shelves tightly.
        order = sorted(hashes, key=lambda h: (-images[h].height, -images[h].width, h))
        page_no = 0
        packer = Packer()
        page_im = Image.new("RGBA", (PAGE, PAGE), (0, 0, 0, 255))
        name = f"T_Atlas_{cat}_{page_no}"
        for hs in order:
            im = images[hs]
            pos = packer.place(im.width, im.height)
            if pos is None:                          # page full -> next page
                png = os.path.join(OUT_DIR, f"{name}.png")
                page_im.save(png)
                pages[name] = png
                page_no += 1
                packer = Packer()
                page_im = Image.new("RGBA", (PAGE, PAGE), (0, 0, 0, 255))
                name = f"T_Atlas_{cat}_{page_no}"
                pos = packer.place(im.width, im.height)
            x, y = pos
            dilate_paste(page_im, im, x, y)
            placed[(cat, hs)] = (name, x, y, im.width, im.height)
        png = os.path.join(OUT_DIR, f"{name}.png")
        page_im.save(png)
        pages[name] = png

    for tex, hs in tex_hash.items():
        cat = textures[tex]["category"]
        name, x, y, w, h = placed[(cat, hs)]
        mapping[tex] = {
            "page": name,
            "scale": [w / PAGE, h / PAGE],
            "offset": [x / PAGE, y / PAGE],
        }
    mapping.update(solo_map)
    pages.update(solo_pages)

    with open(OUT_JSON, "w", encoding="utf-8") as f:
        json.dump({"pages": pages, "map": mapping, "solo": solo}, f, indent=1)

    per_cat = {}
    for (cat, _hs), (name, *_rest) in placed.items():
        per_cat.setdefault(cat, set()).add(name)
    print(f"[atlas] {len(images)} unique images from {len(tex_hash)} paths packed into "
          f"{len(pages)} pages: " + ", ".join(f"{c}={len(p)}" for c, p in sorted(per_cat.items())))
    print(f"[atlas] {len(solo)} solo textures "
          f"({sum(1 for r in solo.values() if r == 'tiled')} tiled)")
    print(f"[atlas] pages -> {OUT_DIR}")
    print(f"[atlas] manifest -> {OUT_JSON}")


if __name__ == "__main__":
    main()
