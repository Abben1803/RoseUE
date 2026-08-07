#!/usr/bin/env python3
"""
gen_texture_manifest.py — Map every imported mesh asset's material slots to
their SOURCE textures, for the atlas + master-material pipeline.

Slot order replicates the builders exactly (they define the imported section
order): wardrobe = rose_avatar.resolve_item part order with the face 3-section
blink split; static weapons = dummy-0 part first, then off-hand parts;
monsters = CHR body-part -> PART_NPC.ZSC part order.

Also parses every referenced ZMS's UV channel 0: a texture whose ANY user mesh
has UVs outside [0,1] is marked "tiled" — it must stay a standalone texture
(atlas sub-rects break wrapping).

Output: tools/_tmp/texture_manifest.json
  { "meshes": [ {asset, class, category, slots:[{tex, mode, two_sided}]} ],
    "textures": { tex_abs: {"tiled": bool, "category": str} } }

Usage:  py -3.9 gen_texture_manifest.py
"""
import json
import os
import sys

_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)

from rose_parser.formats.zms import parse as parse_zms
from rose_parser.formats.zsc import parse as parse_zsc
from rose_avatar import resolve_item
from build_monsters import resolve_model
from rose_parser.formats.chr_ import parse as parse_chr
from build_weapons import weapon_ids

SRC = os.path.join(os.path.normpath(os.path.join(_T, "..")),
                   "unreal-engine rose", "RoseUE", "SourceAssets")
AVATAR = os.path.join(SRC, "3DDATA", "AVATAR")
NPC_DIR = os.path.join(SRC, "3DDATA", "NPC")
OUT = os.path.join(_T, "_tmp", "texture_manifest.json")

WARDROBE_SLOTS = ("body", "arms", "foot", "cap", "back", "hair", "face")
# Final render-state rules (established during the wardrobe/monster sagas).
MODE = {"hair": "MASK"}          # everything else OPAQUE; all two-sided.

_zms_cache = {}


def zms_info(path):
    """(uv_ok, n_mat_groups) for a ZMS — cached."""
    if path not in _zms_cache:
        try:
            z = parse_zms(path)
            ok = True
            for v in z.vertices:
                if not v.uvs:
                    continue
                u, w = v.uvs[0]
                if not (-0.01 <= u <= 1.01 and -0.01 <= w <= 1.01):
                    ok = False
                    break
            _zms_cache[path] = (ok, len(z.mat_ids) if z.mat_ids else 0)
        except Exception:
            _zms_cache[path] = (True, 0)
    return _zms_cache[path]


def add_mesh(meshes, textures, asset, cls, category, part_slots):
    slots = []
    for p in part_slots:
        tex = p.get("texture")
        if tex and not os.path.isfile(tex):
            tex = None
        mode = p.get("mode", "OPAQUE")
        slots.append({"tex": tex, "mode": mode, "two_sided": True})
        if tex:
            uv_ok, _ = zms_info(p["path"])
            t = textures.setdefault(tex, {"tiled": False, "category": category})
            if not uv_ok:
                t["tiled"] = True
    if slots:
        meshes.append({"asset": asset, "class": cls,
                       "category": category, "slots": slots})


def wardrobe(meshes, textures):
    for gender, gname in (("F", "Female"), ("M", "Male")):
        category = gname.lower()
        root = f"/Game/Characters/Modular/{gname}"
        # base = BODY 1 (body-only, defines the skeleton)
        parts = resolve_item(AVATAR, SRC, "BODY", 1, gender)
        add_mesh(meshes, textures, f"{root}/base/base/SkeletalMeshes/base",
                 "SkeletalMesh", category, parts)
        for slot in WARDROBE_SLOTS:
            from build_wardrobe import SLOTS as WSLOTS
            zsc = parse_zsc(os.path.join(AVATAR, WSLOTS[slot][1][0 if gender == "F" else 1]))
            ids = [i for i, o in enumerate(zsc.models) if getattr(o, "parts", None)]
            for iid in ids:
                parts = resolve_item(AVATAR, SRC, slot.upper(), iid, gender)
                if not parts:
                    continue
                out_parts = []
                for p in parts:
                    p = dict(p, mode=MODE.get(slot, "OPAQUE"))
                    tex = p.get("texture")
                    # replicate the face blink split: >=3 material groups with a
                    # texture -> 3 sections (closed/main/open), same texture.
                    if p.get("blink") and tex and os.path.isfile(tex):
                        _, ngroups = zms_info(p["path"])
                        if ngroups >= 3:
                            out_parts += [p, p, p]
                            continue
                    out_parts.append(p)
                name = f"{slot}_{iid}"
                add_mesh(meshes, textures,
                         f"{root}/{name}/{name}/SkeletalMeshes/{name}",
                         "SkeletalMesh", category, out_parts)


def weapons(meshes, textures):
    root = "/Game/Characters/Modular/WeaponsStatic"
    for wid in weapon_ids(SRC):
        parts = resolve_item(AVATAR, SRC, "WEAPON", wid, "F")
        if not parts:
            continue
        if len(parts) <= 1:
            groups = [(f"weapon_{wid}", parts)]
        else:
            parts = sorted(parts, key=lambda p: p.get("pin_dummy") or 0)
            groups = [(f"weapon_{wid}", [parts[0]]),
                      (f"weapon_{wid}_off", parts[1:])]
        for name, ps in groups:
            add_mesh(meshes, textures,
                     f"{root}/{name}/{name}/StaticMeshes/{name}",
                     "StaticMesh", "weapons", ps)


def monsters(meshes, textures):
    chr_ = parse_chr(os.path.join(NPC_DIR, "LIST_NPC.CHR"))
    zsc = parse_zsc(os.path.join(NPC_DIR, "PART_NPC.ZSC"))
    for idx in range(len(chr_.models)):
        r = resolve_model(chr_, zsc, idx)
        if r is None:
            continue
        _, parts, _ = r
        name = f"npc_{idx}"
        add_mesh(meshes, textures,
                 f"/Game/Monsters/{name}/{name}/SkeletalMeshes/{name}",
                 "SkeletalMesh", "monsters", parts)


def main():
    meshes, textures = [], {}
    wardrobe(meshes, textures)
    weapons(meshes, textures)
    monsters(meshes, textures)
    os.makedirs(os.path.dirname(OUT), exist_ok=True)
    with open(OUT, "w", encoding="utf-8") as f:
        json.dump({"meshes": meshes, "textures": textures}, f, indent=1)
    tiled = sum(1 for t in textures.values() if t["tiled"])
    by_cat = {}
    for t in textures.values():
        by_cat[t["category"]] = by_cat.get(t["category"], 0) + 1
    print(f"[manifest] {len(meshes)} meshes, {len(textures)} unique texture paths "
          f"({tiled} tiled/solo) by category {by_cat} -> {OUT}")


if __name__ == "__main__":
    main()
