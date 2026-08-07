#!/usr/bin/env python3
"""
rose_avatar.py — STB-driven avatar item resolver.

The binding (verified): LIST_<type>.STB row ID maps directly to
LIST_<gender><type>.ZSC.objects[ID].  Each ZSC object lists the mesh+material
parts for that item, and the material's texture_path is the correct texture.

resolve_item(part_type, item_id, gender) -> list of part dicts:
    {path, pin, texture, alpha, two_side}
ready to feed straight into rose_combine_anims.convert().
"""
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from rose_parser.formats.zsc import parse as parse_zsc
from rose_parser.formats.zms import parse as parse_zms

HEAD_BONE = 4   # FEMALE/MALE.ZMD: 4 = b1_head

# src/common/shared/datatype.h — the client's dummy indices.  Only the ones the
# avatar slots need are mirrored here.
DUMMY_IDX_MOUSE = 4   # p_04, parented to bone 4 (head) — the FACE_ITEM anchor
DUMMY_IDX_CAP = 6     # p_06, top of the head

# part_type -> ZSC file per gender + attachment bone for non-skinned parts.
# BODY/ARMS/FOOT are skinned (attach=None → uses skin weights).
# FACE/HAIR/CAP are rigid, authored in head-local space → pinned to the head bone.
PART_TYPES = {
    "BODY": {"zsc": {"F": "LIST_WBODY.ZSC", "M": "LIST_MBODY.ZSC"}, "attach": None},
    "FACE": {"zsc": {"F": "LIST_WFACE.ZSC", "M": "LIST_MFACE.ZSC"}, "attach": HEAD_BONE},
    "HAIR": {"zsc": {"F": "LIST_WHAIR.ZSC", "M": "LIST_MHAIR.ZSC"}, "attach": HEAD_BONE},
    # Cap is rigid and authored in dummy-local space; it attaches to the head
    # bone (so it follows the head) but is positioned at head dummy 6
    # (DUMMY_IDX_CAP), which sits on top of the head — not the head bone (neck).
    "CAP":  {"zsc": {"F": "LIST_WCAP.ZSC",  "M": "LIST_MCAP.ZSC"},  "attach": HEAD_BONE, "dummy": DUMMY_IDX_CAP},
    # Face items (masks / goggles / glasses).  Authority is the original client,
    # src/client/io_basic.cpp:64 —
    #     m_pMD_CharPARTS[0][BODY_PART_FACE_ITEM]->Load(
    #         "3Ddata\\avatar\\LIST_FACEIEM.ZSC", -1, DUMMY_IDX_MOUSE);
    #     m_pMD_CharPARTS[1][BODY_PART_FACE_ITEM] = m_pMD_CharPARTS[0][...];
    # i.e. bone = -1 (NOT bone-linked) and dummy = DUMMY_IDX_MOUSE (4), and the
    # pack is loaded ONCE and shared by both genders — one un-gendered ZSC, but
    # the GLBs are still built per gender because dummy 4 (p_04) sits at a
    # different head-local offset in FEMALE.ZMD vs MALE.ZMD.
    # Note the filename really is LIST_FACEI**E**M.ZSC — the typo is ROSE's.
    # Like cap, the dummy is parented to bone 4 (head), so the mesh is rigid-
    # skinned to the head bone and positioned at the dummy; attach is only the
    # fallback bone if the dummy is missing.  The ZSC parts carry dummy_idx=-1
    # (2 of 346 redundantly say 4), so the slot config is what names the dummy —
    # exactly the cap/back pattern, NOT the weapon use_part_xform pattern.
    "FACEITEM": {"zsc": {"F": "LIST_FACEIEM.ZSC", "M": "LIST_FACEIEM.ZSC"},
                 "attach": HEAD_BONE, "dummy": DUMMY_IDX_MOUSE},
    "ARMS": {"zsc": {"F": "LIST_WARMS.ZSC", "M": "LIST_MARMS.ZSC"}, "attach": None},
    "FOOT": {"zsc": {"F": "LIST_WFOOT.ZSC", "M": "LIST_MFOOT.ZSC"}, "attach": None},
    # Back items (capes/knapsacks/wings) are rigid, authored in dummy-local space
    # and attach to the back dummy (DUMMY_IDX_BACK = 3, parented to the chest) —
    # it sits behind and slightly above the character.  attach is the fallback
    # bone; the converter derives the real transform from dummy 3's parent.
    "BACK": {"zsc": {"F": "LIST_BACK.ZSC",  "M": "LIST_BACK.ZSC"},  "attach": 2, "dummy": 3},
    # Weapons & sub-weapons (shields/arrows) are rigid meshes that attach to a
    # HAND dummy named by the ZSC part itself (dummy_idx: 0=R_HAND, 1=L_HAND,
    # 2=L_SHIELD).  Not gendered → one shared ZSC under ../WEAPON.  Unlike
    # cap/back, the ZSC part carries a real grip rotation that orients the mesh
    # in the hand, so these slots opt in to honouring the per-part transform
    # (use_part_xform) and read their dummy from the part (dummy=None below).
    "WEAPON": {"zsc": {"F": "../WEAPON/LIST_WEAPON.ZSC", "M": "../WEAPON/LIST_WEAPON.ZSC"},
               "attach": None, "dummy": None, "use_part_xform": True},
    "SUBWPN": {"zsc": {"F": "../WEAPON/LIST_SUBWPN.ZSC", "M": "../WEAPON/LIST_SUBWPN.ZSC"},
               "attach": None, "dummy": None, "use_part_xform": True},
}


def _ident_rot(q) -> bool:
    """A ZSC part quaternion (w,x,y,z) that represents no rotation.  ROSE leaves
    many parts as the zero quaternion (which _quat_to_mat3 maps to identity) or
    as (±1,0,0,0)."""
    if all(abs(v) < 1e-4 for v in q):
        return True
    return abs(abs(q[0]) - 1.0) < 1e-4 and all(abs(v) < 1e-4 for v in q[1:])

_zsc_cache = {}
_zms_skin_cache = {}


def _resolve_path(src_assets: str, rel: str) -> str:
    rel = rel.replace("\\", os.sep).replace("/", os.sep)
    return os.path.normpath(os.path.join(src_assets, rel))


def _zsc(avatar_dir: str, name: str):
    p = os.path.join(avatar_dir, name)
    if p not in _zsc_cache:
        _zsc_cache[p] = parse_zsc(p)
    return _zsc_cache[p]


def _is_skinned(mesh_abs: str) -> bool:
    if mesh_abs not in _zms_skin_cache:
        try:
            _zms_skin_cache[mesh_abs] = parse_zms(mesh_abs).has_skin()
        except Exception:
            _zms_skin_cache[mesh_abs] = False
    return _zms_skin_cache[mesh_abs]


def resolve_item(avatar_dir, src_assets, part_type, item_id, gender):
    """Return the list of part dicts for a given item (one per ZSC object part)."""
    cfg = PART_TYPES.get(part_type)
    if cfg is None:
        raise ValueError(f"unknown part type {part_type}")
    zsc = _zsc(avatar_dir, cfg["zsc"][gender])
    if item_id < 0 or item_id >= len(zsc.models):
        print(f"  [resolve] {part_type} id {item_id} out of range (max {len(zsc.models)-1})")
        return []

    obj = zsc.models[item_id]
    parts = []
    for zp in obj.parts:
        if zp.mesh_id >= len(zsc.mesh_files):
            continue
        mesh_abs = _resolve_path(src_assets, zsc.mesh_files[zp.mesh_id])
        if not os.path.isfile(mesh_abs):
            print(f"  [resolve] missing mesh: {mesh_abs}")
            continue
        mat = zsc.materials[zp.material_id] if zp.material_id < len(zsc.materials) else None
        tex_abs = _resolve_path(src_assets, mat.texture_path) if mat and mat.texture_path else None
        skinned = _is_skinned(mesh_abs)
        pin = None if skinned else cfg["attach"]
        # Dummy to pin a rigid part to.  Cap/back fix it in the slot config;
        # weapons name their own dummy via the ZSC part's dummy_idx.
        pin_dummy = cfg.get("dummy")
        if pin_dummy is None and cfg.get("use_part_xform") and zp.dummy_idx >= 0:
            pin_dummy = zp.dummy_idx
        # Per-part local transform (weapon grip).  Only honoured for opt-in slots
        # (weapons) so cap/back/hair/face keep their proven behaviour.  Guard the
        # handful of degenerate zero-scale ZSC parts (treat as unit scale).
        pin_xform = None
        if not skinned and cfg.get("use_part_xform"):
            sc = tuple(zp.scale)
            degenerate = any(abs(v) < 1e-6 for v in sc)
            if not (zp.position == (0.0, 0.0, 0.0) and _ident_rot(zp.rotation)
                    and (sc == (1.0, 1.0, 1.0) or degenerate)):
                pin_xform = {"pos": zp.position, "rot": zp.rotation,
                             "scale": (1.0, 1.0, 1.0) if degenerate else sc}
        parts.append({
            "path": mesh_abs,
            "pin": pin,
            # Dummy index for rigid parts authored in dummy-local space (cap).
            "pin_dummy": None if skinned else pin_dummy,
            # Optional ZSC per-part local transform applied after the dummy.
            "pin_xform": pin_xform,
            "texture": tex_abs,
            "alpha": bool(mat.is_alpha or mat.alpha_test) if mat else False,
            # TRUE cutout signal (zz_material: MASK only when is_alpha AND
            # alpha_test).  alpha_test alone is 1 on nearly every ROSE material
            # even solid ones — there the DDS alpha is SPECULAR, not a mask — so
            # the combined "alpha" above over-selects.  Use this for MASK mode.
            "mask": bool(mat.is_alpha and mat.alpha_test) if mat else False,
            "two_side": bool(mat.is_2side) if mat else False,
            # Faces carry eye overlays (closed/open material groups) → split into
            # toggleable sections for blinking.
            "blink": part_type == "FACE",
        })
    return parts


def resolve_avatar(avatar_dir, src_assets, config, gender):
    """config = {part_type: item_id, ...}.  Returns the combined parts list."""
    parts = []
    for part_type, item_id in config.items():
        if item_id is None:
            continue
        parts.extend(resolve_item(avatar_dir, src_assets, part_type.upper(), item_id, gender))
    return parts
