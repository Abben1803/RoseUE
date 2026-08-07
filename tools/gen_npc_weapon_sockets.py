#!/usr/bin/env python3
"""
gen_npc_weapon_sockets.py — hand-attach sockets for monster-held weapons.

Classic NPC models attach LIST_NPC col 5 (R weapon) / col 6 (L weapon) to the
CHR skeleton's hand dummies (dummy 0 = R hand, 1 = L hand — same convention as
avatars, cmodelchar.cpp SetPartMODEL(BODY_PART_WEAPON_R, NPC_R_WEAPON)).

The imported monster GLBs carry only the plain bones (named bone00..boneNN by
ZMD index) — no dummy nodes — so the runtime attaches the weapon static mesh to
the dummy's PARENT BONE with the dummy's local offset.  This tool emits that
mapping for every npc that has a weapon:

  Content/DataTables/npc_weapon_sockets.json
    { "<npc_id>": { "rweapon": id, "lweapon": id,
                    "bone": "bone03", "pos": [x,y,z]      # UE cm, bone-local
                    , "lbone": "...", "lpos": [...] } }   # when L dummy exists

Offline: py -3.9 tools/gen_npc_weapon_sockets.py
"""
import json
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _TOOLS)
from rose_parser.formats.stb import parse as parse_stb
from rose_parser.formats.chr_ import parse as parse_chr
from rose_parser.formats.zmd import parse as parse_zmd
from rose_to_gltf import _apply_basis

UE = os.path.normpath(os.path.join(_TOOLS, "..", "unreal-engine rose", "RoseUE"))
# Asset source: Arua (CLAUDE.md "Asset source: Arua, always").  Was the
# deprecated SourceAssets/3DDATA classic tree; ZSC object ids and STB row
# ids do not line up across eras, so a row resolved to different content
# than the Arua-era DataTables this feeds.  Output roots are unchanged.
SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
OUT = os.path.join(UE, "Content", "DataTables", "npc_weapon_sockets.json")

stb = parse_stb(os.path.join(SRC, "STB", "LIST_NPC.STB"))
chr_ = parse_chr(os.path.join(SRC, "NPC", "LIST_NPC.CHR"))

zmd_cache = {}
def skel(idx):
    if idx not in zmd_cache:
        rel = chr_.skeleton_files[idx].replace("\\", "/")
        parts = [p for p in rel.split("/") if p]
        if parts and parts[0].upper() == "3DDATA":
            parts = parts[1:]
        p = os.path.join(SRC, *parts)
        zmd_cache[idx] = parse_zmd(p) if os.path.isfile(p) else None
    return zmd_cache[idx]


def dummy_socket(z, di):
    if not z or di >= len(z.dummies):
        return None
    d = z.dummies[di]
    # ROSE cm -> basis-swapped cm == UE bone-local cm (GLB path: x0.01 to metres,
    # basis swap, UE import x100 — net just the swap).
    px, py, pz = _apply_basis(*d.translation)
    # Imported skeletons keep the ZMD's REAL bone names (b1_rhand ...);
    # "bone%02d" is only the fallback for unnamed bones.
    bone = z.bones[d.parent_id].name if (0 <= d.parent_id < len(z.bones)
                                         and z.bones[d.parent_id].name) else f"bone{d.parent_id:02d}"
    return {"bone": bone, "pos": [round(px, 2), round(py, 2), round(pz, 2)]}


out = {}
n_r = n_l = 0
for i in range(min(stb.num_rows(), len(chr_.models))):
    m = chr_.models[i]
    if not m.is_valid:
        continue
    rw = stb.get_int(i, 5)
    lw = stb.get_int(i, 6)
    if rw <= 0 and lw <= 0:
        continue
    z = skel(m.skel_index)
    entry = {}
    if rw > 0:
        s = dummy_socket(z, 0)
        if s:
            entry.update({"rweapon": rw, "bone": s["bone"], "pos": s["pos"]})
            n_r += 1
    if lw > 0:
        s = dummy_socket(z, 1)
        if s:
            entry.update({"lweapon": lw, "lbone": s["bone"], "lpos": s["pos"]})
            n_l += 1
    if entry:
        out[str(i)] = entry

os.makedirs(os.path.dirname(OUT), exist_ok=True)
json.dump(out, open(OUT, "w"), separators=(",", ":"))
print(f"[wsock] {len(out)} armed npcs ({n_r} right, {n_l} left) -> {OUT}")
for k in list(out)[:6]:
    print(f"[wsock]   npc {k}: {out[k]}")
