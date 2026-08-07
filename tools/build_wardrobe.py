#!/usr/bin/env python3
"""
build_wardrobe.py — Build the FULL equipment catalog GLBs from the STBs/ZSCs.

For each gender and slot, enumerates every valid item (ZSC object that has parts),
resolves it via the STB→ZSC binding, and writes <slot>_<id>.glb into
SourceAssets/GLTF/AVATAR/MODULAR/<gender>/.  Also writes catalog_<gender>.json
mapping <slot>_<id> -> localized name (from LIST_<slot>_S.STL via the STB
"STL Link" column) for a later inventory UI.

Run with:  py -3.9 build_wardrobe.py --gender F        (or M, or both)
           py -3.9 build_wardrobe.py --gender both --slots body,arms,foot,cap
"""
import argparse, os, sys, json, time

_TOOLS = os.path.dirname(os.path.abspath(__file__))
if _TOOLS not in sys.path:
    sys.path.insert(0, _TOOLS)

from rose_parser.formats.zsc import parse as parse_zsc
from rose_parser.formats.stb import parse as parse_stb
from rose_parser.formats.stl import parse as parse_stl
from rose_avatar import resolve_item
from rose_combine_anims import convert
from build_modular import _anims

# slot -> (STB base name, (female ZSC, male ZSC))
SLOTS = {
    "body": ("LIST_BODY", ("LIST_WBODY.ZSC", "LIST_MBODY.ZSC")),
    "arms": ("LIST_ARMS", ("LIST_WARMS.ZSC", "LIST_MARMS.ZSC")),
    "foot": ("LIST_FOOT", ("LIST_WFOOT.ZSC", "LIST_MFOOT.ZSC")),
    "cap":  ("LIST_CAP",  ("LIST_WCAP.ZSC",  "LIST_MCAP.ZSC")),
    "back": ("LIST_BACK", ("LIST_BACK.ZSC",  "LIST_BACK.ZSC")),
    "hair": ("LIST_HAIR", ("LIST_WHAIR.ZSC", "LIST_MHAIR.ZSC")),
    "face": ("LIST_FACE", ("LIST_WFACE.ZSC", "LIST_MFACE.ZSC")),
}


def _stl_names(stb_dir, stb_base):
    """Return {item_id: name} from the STB 'STL Link' col + LIST_<slot>_S.STL."""
    names = {}
    try:
        stb = parse_stb(os.path.join(stb_dir, stb_base + ".STB"))
        stl_path = os.path.join(stb_dir, stb_base + "_S.STL")
        stl = parse_stl(stl_path) if os.path.isfile(stl_path) else None
        # headers includes the 'ID' column at 0, but get() is 0-based over DATA
        # columns (ID excluded) → data col = header index - 1.
        link_col = (stb.headers.index("STL Link") - 1) if "STL Link" in stb.headers else None
        for r in range(stb.num_rows()):
            try:
                rid = int(stb.get_row_id(r))
            except (ValueError, TypeError):
                continue
            key = stb.get(r, link_col) if link_col is not None else ""
            nm = stl.get(key) if (stl and key) else ""
            if nm:
                names[rid] = nm
    except Exception as e:
        print(f"   [stl] {stb_base}: {e}")
    return names


def run(gender, slots):
    root = os.path.normpath(os.path.join(_TOOLS, ".."))
    src = os.path.join(root, "unreal-engine rose", "RoseUE", "SourceAssets")
    # Asset SOURCE is Arua (CLAUDE.md); only the GLB OUTPUT stays in SourceAssets.
    # The equip STBs differ enormously across eras (LIST_BODY 8261 rows in Arua
    # vs 904 in classic, LIST_CAP 7003 vs 977), and the wardrobe catalog keys on
    # row id — so classic-sourced meshes were labelled with Arua-era names.
    rose_3ddata = os.environ.get("ROSE_ASSET_ROOT",
                                 r"C:\QQ-iROSE Online\extracted\3DDATA")
    A = os.path.join(rose_3ddata, "AVATAR")
    STB = os.path.join(rose_3ddata, "STB")
    motion = os.path.join(rose_3ddata, "MOTION", "AVATAR")
    gdir = "FEMALE" if gender == "F" else "MALE"
    out = os.path.join(src, "GLTF", "AVATAR", "MODULAR", gdir)
    os.makedirs(out, exist_ok=True)
    zmd = os.path.join(A, "FEMALE.ZMD" if gender == "F" else "MALE.ZMD")
    zi = 0 if gender == "F" else 1

    # base = body #1 + all animations (defines shared skeleton + anim source)
    if not os.path.isfile(os.path.join(out, "base.glb")):
        base = resolve_item(A, os.path.dirname(rose_3ddata), "BODY", 1, gender)
        convert(zmd, base, _anims(motion, gender), os.path.join(out, "base.glb"))
        print(f"[{gender}] base built")

    catalog = {}
    for slot in slots:
        stb_base, zscs = SLOTS[slot]
        zsc = parse_zsc(os.path.join(A, zscs[zi]))
        ids = [i for i, o in enumerate(zsc.models) if getattr(o, "parts", None)]
        names = _stl_names(STB, stb_base)
        built = skipped = 0
        t0 = time.time()
        for iid in ids:
            dst = os.path.join(out, f"{slot}_{iid}.glb")
            if os.path.isfile(dst):
                skipped += 1
                if iid in names: catalog[f"{slot}_{iid}"] = names[iid]
                continue
            try:
                parts = resolve_item(A, os.path.dirname(rose_3ddata),
                                     slot.upper(), iid, gender)
                if not parts:
                    continue
                convert(zmd, parts, [], dst)
                built += 1
                if iid in names: catalog[f"{slot}_{iid}"] = names[iid]
            except Exception as e:
                print(f"   [{gender}] {slot}_{iid}: FAIL {e}")
        print(f"[{gender}] {slot}: {built} built, {skipped} existed "
              f"({len(ids)} ids, {time.time()-t0:.0f}s)")

    with open(os.path.join(out, f"catalog_{gdir}.json"), "w", encoding="utf-8") as f:
        json.dump(catalog, f, ensure_ascii=False, indent=1)
    print(f"[{gender}] catalog: {len(catalog)} named items -> {out}")


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--gender", choices=["F", "M", "both"], default="both")
    ap.add_argument("--slots", default=",".join(SLOTS))
    a = ap.parse_args()
    slots = [s.strip() for s in a.slots.split(",") if s.strip() in SLOTS]
    genders = ["F", "M"] if a.gender == "both" else [a.gender]
    for g in genders:
        run(g, slots)


if __name__ == "__main__":
    main()
