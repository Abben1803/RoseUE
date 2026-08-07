#!/usr/bin/env python3
"""
build_modular.py — Build modular avatar/equipment part GLBs for the equip system.

Produces, under SourceAssets/GLTF/AVATAR/MODULAR/<gender>/:
    base.glb              BODY (default) + ALL animations  -> the leader/driver
    body_<id>.glb         body armor (torso+legs)          -> swaps the body slot
    arms_<id>.glb         gauntlets/hands
    foot_<id>.glb         boots
    cap_<id>.glb          helmets/headgear
    hair_<id>.glb         hair  (appearance)
    face_<id>.glb         face  (appearance)

Each carries the full skeleton (compatible with base) so it imports as a skeletal
mesh and follows the body via Leader Pose.  An equipped item = (slot, id).

Usage:
    python build_modular.py --gender F
    python build_modular.py --gender F --body 1,2,3,4 --arms 1,2,3 --cap 1,2,3
"""
import argparse
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from rose_avatar import resolve_item
from rose_combine_anims import convert
from build_combined_anims import _max_bone, AVATAR_BONE_COUNT

# Equippable / appearance slots -> the STB part-type used to resolve them.
# "weapon" resolves via LIST_WEAPON.ZSC and pins to the right-hand dummy.
# "faceitem" = masks/goggles/glasses (LIST_FACEIEM.ZSC, pinned to the MOUTH
# dummy — see rose_avatar.PART_TYPES for the client citation).
SLOTS = ["body", "arms", "foot", "cap", "hair", "back", "face", "faceitem", "weapon"]

# Asset source.  Arua is THE source for this project (CLAUDE.md "Asset source:
# Arua, always"): the DataTables and the gear pipeline are all Arua-era, and ZSC
# object ids do NOT line up across eras — a classic-built mesh under an Arua row
# id is a *different item*.  Same env var + default as build_gear_manifest.py,
# and it points AT the 3DDATA directory.
ROSE_3DDATA = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")


# Weapon locomotion/combat prefixes.  Several weapon types resolve (via
# FILE_MOTION) to the _M1 ZMO even for a Female character (no _F1 exists, e.g.
# bow/gun/crossbow/cannon/dual idles), so the base must carry the opposite
# gender's files for these prefixes too — otherwise those weapons fall back to
# the bare idle.  Emotes are NOT cross-included (avoid bloating the base).
_WEAPON_ANIM_PREFIXES = (
    "EMPTY_", "ONEHAND_", "ONETOOL_", "TWOSWD_", "TWOSPEAR_", "TWOHAND_",
    "BOW_", "BOWGUN_", "GUN_", "CANNON_", "KARTAR_", "MAGIC_",
)


def _skill_motion_stems():
    """Uppercased ZMO stems referenced by any SKILL action/casting row (via
    TYPE_MOTION -> FILE_MOTION).  Skills are authored as _M1 motions and shared
    across genders, so the FEMALE base must cross-include them too — otherwise
    every skill cast falls back to the basic swing (see gen_skill_motion.py).
    Best-effort: returns empty if the STBs/CSV aren't where expected."""
    try:
        from rose_parser.formats.stb import parse as _p
        tm = _p(os.path.join(ROSE_3DDATA, "STB", "TYPE_MOTION.STB"))
        fm = _p(os.path.join(ROSE_3DDATA, "STB", "FILE_MOTION.STB"))
        import csv as _csv
        rows = list(_csv.DictReader(open(os.path.join(
            _TOOLS, "..", "unreal-engine rose", "RoseUE", "DataTables", "skills.csv"),
            encoding="utf-8-sig")))
        used = set()
        for r in rows:
            for c in ("ActionMotion", "CastMotion"):
                try:
                    a = int(r[c])
                except (TypeError, ValueError, KeyError):
                    continue
                if 0 < a < tm.num_rows():
                    used.add(a)
        stems = set()
        for a in used:
            for mt in range(tm.num_cols()):
                try:
                    mi = int(tm.get(a, mt))
                except (TypeError, ValueError):
                    continue
                cell = (fm.get(mi, 1) or fm.get(mi, 0) or "").strip()
                if cell and cell != ".":
                    stems.add(os.path.splitext(os.path.basename(
                        cell.replace("\\", "/")))[0].upper())
        return stems
    except Exception as e:
        print(f"[_anims] skill-motion stems unavailable ({e}); "
              f"cross-gender skill anims skipped")
        return set()


def _anims(motion_dir, gender):
    suffix = "_F1" if gender == "F" else "_M1"
    other = "_M1" if gender == "F" else "_F1"
    skill_stems = _skill_motion_stems() if gender == "F" else set()
    g, n = [], []
    for zmo in sorted(os.listdir(motion_dir)):
        if not zmo.upper().endswith(".ZMO"):
            continue
        p = os.path.join(motion_dir, zmo)
        if _max_bone(p) >= AVATAR_BONE_COUNT:
            continue
        stem = os.path.splitext(zmo)[0]
        up = stem.upper()
        if up.endswith(suffix):
            g.append((stem, p))
        elif up.endswith(other):
            # opposite-gender file: keep weapon loco/combat sets + skill motions
            if (any(up.startswith(pre) for pre in _WEAPON_ANIM_PREFIXES)
                    or up in skill_stems):
                g.append((stem, p))
        else:
            n.append((stem, p))   # gender-neutral (no _F1/_M1 suffix)
    return g + n


def zsc_object_ids(avatar_dir, part_type, gender):
    """Every ZSC object index that actually has parts, for a slot.  Used by
    `--<slot> all`: the STB row set and the ZSC object set do not agree (for
    FACEITEM, 261 named STB rows vs 338 real ZSC objects, 83 of them unnamed),
    and it is the ZSC that decides whether a mesh exists to build."""
    from rose_avatar import PART_TYPES, _zsc
    zsc = _zsc(avatar_dir, PART_TYPES[part_type]["zsc"][gender])
    return [i for i, m in enumerate(zsc.models) if m.parts]


def run(gender, slot_ids, build_base=True):
    project_root = os.path.normpath(os.path.join(_TOOLS, ".."))
    ue = os.path.join(project_root, "unreal-engine rose", "RoseUE")
    # `src` is the PARENT of 3DDATA: ZSC mesh/texture paths are stored as
    # "3DDATA\AVATAR\..." and rose_avatar._resolve_path joins them onto it.
    # Only the GLB *output* still lives in the UE project's SourceAssets tree.
    src = os.path.dirname(ROSE_3DDATA)
    A = os.path.join(ROSE_3DDATA, "AVATAR")
    motion = os.path.join(ROSE_3DDATA, "MOTION", "AVATAR")
    out = os.path.join(ue, "SourceAssets", "GLTF", "AVATAR", "MODULAR",
                       "FEMALE" if gender == "F" else "MALE")
    os.makedirs(out, exist_ok=True)
    zmd = os.path.join(A, "FEMALE.ZMD" if gender == "F" else "MALE.ZMD")

    # base = default body (id 1) + animations: the animated leader/driver.
    # Skippable (--no-base) so a single-slot rebuild doesn't churn the shared
    # skeleton/anim asset that every other part is made compatible with.
    if build_base:
        base = resolve_item(A, src, "BODY", 1, gender)
        convert(zmd, base, _anims(motion, gender), os.path.join(out, "base.glb"))
        print(f"[base] body#1 + anims")

    built = 0
    for slot in SLOTS:
        for iid in slot_ids.get(slot, []):
            parts = resolve_item(A, src, slot.upper(), iid, gender)
            if not parts:
                continue
            convert(zmd, parts, [], os.path.join(out, f"{slot}_{iid}.glb"))
            built += 1

    print(f"[done] {'base + ' if build_base else ''}{built} items -> {out}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gender", choices=["F", "M"], default="F")
    ap.add_argument("--body", default="1,2,3,4")
    ap.add_argument("--arms", default="1,2,3")
    ap.add_argument("--foot", default="1,2,3")
    ap.add_argument("--cap",  default="1,2,3")
    ap.add_argument("--hair", default="110,115,120,125,130")
    ap.add_argument("--face", default="1,2,3,4")
    ap.add_argument("--weapon", default="1,2,3,4,5")
    # Face items have no small "starter set" worth curating — build the whole
    # pack (338 real Arua objects) by default.
    ap.add_argument("--faceitem", default="all")
    ap.add_argument("--only", default="",
                    help="comma-separated slot names to build (default: all of SLOTS)")
    ap.add_argument("--no-base", action="store_true",
                    help="skip rebuilding base.glb (the shared skeleton + anims)")
    a = ap.parse_args()

    A = os.path.join(ROSE_3DDATA, "AVATAR")

    def ids(s, slot):
        s = (s or "").strip()
        if s.lower() == "all":
            return zsc_object_ids(A, slot.upper(), a.gender)
        return [int(x) for x in s.split(",") if x.strip()]

    want = {s.strip() for s in a.only.split(",") if s.strip()}
    sel = {}
    for slot in ("body", "arms", "foot", "cap", "hair", "face", "faceitem", "weapon"):
        sel[slot] = ids(getattr(a, slot), slot) if (not want or slot in want) else []

    run(a.gender, sel, build_base=not a.no_base)


if __name__ == "__main__":
    main()
