#!/usr/bin/env python3
"""
gen_pat_tables.py — Generate the PAT (vehicle) runtime tables from LIST_PAT.STB.

ROSE has THREE distinct PAT features, and the table says which is which:
LIST_PAT.STB col 72 "PAT Class (1:Cart, 2:CG)" — 1 = cart, 2 = castle gear,
3 = mount.  Carts and castle gears are assembled from five parts
(t_eRidePART BODY/ENGINE/LEG/ABIL/ARMS) and burn fuel; mounts are standalone
single-model items with no parts and no fuel.

Two tables are written to DataTables/ (import as FRosePatPartRow / FRosePatMotionRow):

  pat_parts.csv   one row per LIST_PAT item — family, slot, stats, motion bases.
  pat_motion.csv  the resolved TYPE_MOTION grid: (row,col) -> animation name.

Motion resolution (verified against src/client/cobjcart.cpp + cobjavt.h):
    vehicle anim = TYPE_MOTION[ BODY.PatMotion + CART_ANI_x ][ ARMS.PatMotion ]
    rider  anim  = TYPE_MOTION[ BODY.AvatarMotion + PETMODE_AVATAR_ANI_x ][ 0 ]
i.e. the ROW comes from the BODY part and the COLUMN from the ARMS part
("Cart 의 경우는 무기에 따른 모션이 아니라 ARMS 테이블의 모션 타입에 의존한다").
TYPE_MOTION cells hold a FILE_MOTION index; FILE_MOTION col 0 is the ZMO path.
The animation NAME is the ZMO stem, which is what build_pat_anims.py names the
glTF tracks — so the runtime only ever needs the name.

Usage:  py -3.9 tools/gen_pat_tables.py
"""
import csv
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _TOOLS)

from rose_parser.formats.stb import parse as parse_stb

ARUA = r"C:\QQ-iROSE Online\extracted\3DDATA"
OUT_DIR = os.path.join(os.path.dirname(_TOOLS), "unreal-engine rose", "RoseUE", "DataTables")

# LIST_PAT.STB data columns (header[c+1] labels the data column c)
C_NAME, C_MODEL, C_ITEMTYPE = 0, 1, 4
C_PART_TYPE = 16          # 21 cart / 31 castle gear / mount+event families
C_MAX_FUEL, C_FUEL_RATE, C_MOVE_SPEED = 31, 32, 33
C_ATK_RANGE, C_ATK_POWER, C_ATK_SPEED = 35, 36, 37
C_PAT_MOTION, C_AVATAR_MOTION = 40, 41
C_STOP_SOUND, C_MOVE_SOUND = 48, 50
C_HP_GAUGE = 67
C_PAT_CLASS = 72          # 1 cart, 2 castle gear, 3 mount
C_HIDE_PLAYER, C_SCALING = 75, 76

PAT_CART, PAT_CG, PAT_MOUNT = 1, 2, 3

# t_eRidePART, from the item type's tens digit (51x body, 52x engine, ...)
SLOT_BODY, SLOT_ENGINE, SLOT_LEG, SLOT_ABIL, SLOT_ARMS = range(5)

# enumCART_ANI / enumPETMODE_AVATAR_ANI (src/client/cobjcart.h) — same order
ACTIONS = ["STOP1", "MOVE", "ATTACK01", "ATTACK02", "ATTACK03",
           "DIE", "SPECIAL01", "SPECIAL02"]


def slot_for_type(itemtype):
    """t_eRidePART slot for a LIST_PAT Itemtype, or -1 if it is not a part."""
    if itemtype <= 0:
        return -1
    tens = (itemtype // 10) % 10
    return tens - 1 if 1 <= tens <= 5 else -1


def main():
    stb = parse_stb(os.path.join(ARUA, "STB", "LIST_PAT.STB"))
    tm = parse_stb(os.path.join(ARUA, "STB", "TYPE_MOTION.STB"))
    fm = parse_stb(os.path.join(ARUA, "STB", "FILE_MOTION.STB"))
    nrows, ncols = stb.num_rows(), stb.num_cols()
    print(f"[pat] LIST_PAT {nrows}x{ncols}  TYPE_MOTION {tm.num_rows()}x{tm.num_cols()}"
          f"  FILE_MOTION {fm.num_rows()}x{fm.num_cols()}")

    def g(i, c):
        return stb.get(i, c).strip() if c < ncols else ""

    def gi(i, c, default=0):
        v = g(i, c)
        try:
            return int(v)
        except ValueError:
            return default

    # ── pat_parts.csv ─────────────────────────────────────────────────────────
    parts_path = os.path.join(OUT_DIR, "pat_parts.csv")
    motion_bases = set()
    counts = {PAT_CART: 0, PAT_CG: 0, PAT_MOUNT: 0, 0: 0}
    with open(parts_path, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Name", "Id", "PatClass", "PartType", "PartSlot", "ItemType",
                    "MaxFuel", "FuelRate", "MoveSpeed",
                    "AtkRange", "AtkPower", "AtkSpeed",
                    "PatMotion", "AvatarMotion", "Scale", "HidePlayer",
                    "HpGauge", "StopSound", "MoveSound", "HasModel"])
        for i in range(1, nrows):
            itemtype = gi(i, C_ITEMTYPE)
            if not itemtype:
                continue
            # PAT class: Arua has an explicit "PAT Class" column (72); CLASSIC
            # does not — col 72 there is "User Switch" and reading it classified
            # every cart and castle gear as 0.  Classic encodes the family in
            # col 16 "Type parts" instead: 21 = cart, 31 = castle gear.  Mounts
            # are Arua-only, but carts and castle gear are very much in classic.
            pat_class = gi(i, C_PAT_CLASS)
            if pat_class not in (PAT_CART, PAT_CG, PAT_MOUNT):
                fam = gi(i, C_PART_TYPE) // 10
                pat_class = (PAT_CART if fam == 2 else
                             PAT_CG   if fam == 3 else 0)
            slot = slot_for_type(itemtype)
            # A mount is standalone: force it into the BODY slot so the equip
            # code has a single well-defined slot to hang it on.
            if pat_class == PAT_MOUNT:
                slot = SLOT_BODY
            counts[pat_class if pat_class in counts else 0] += 1

            pat_motion = gi(i, C_PAT_MOTION, -1)
            avatar_motion = gi(i, C_AVATAR_MOTION, -1)
            # Only BODY rows carry a motion ROW base; ARMS rows carry a COLUMN.
            if slot == SLOT_BODY and pat_motion > 0:
                motion_bases.add(pat_motion)
            if slot == SLOT_BODY and avatar_motion > 0:
                motion_bases.add(avatar_motion)

            # Scaling is a percentage (100 == 1.0); absent means 100.
            scale = gi(i, C_SCALING, 0) or 100
            w.writerow([f"pat_{i}", i, pat_class, gi(i, C_PART_TYPE), slot, itemtype,
                        gi(i, C_MAX_FUEL), gi(i, C_FUEL_RATE), gi(i, C_MOVE_SPEED),
                        gi(i, C_ATK_RANGE), gi(i, C_ATK_POWER), gi(i, C_ATK_SPEED),
                        pat_motion, avatar_motion, scale, gi(i, C_HIDE_PLAYER),
                        gi(i, C_HP_GAUGE), gi(i, C_STOP_SOUND), gi(i, C_MOVE_SOUND),
                        1 if g(i, C_MODEL) else 0])
    print(f"[pat] {parts_path}: cart={counts[PAT_CART]} cg={counts[PAT_CG]} "
          f"mount={counts[PAT_MOUNT]} other={counts[0]}")

    # ── pat_motion.csv ────────────────────────────────────────────────────────
    # Every (base + action, column) cell any BODY row can reach, resolved to the
    # ZMO stem.  Cells that fall outside TYPE_MOTION or hold 0 are omitted; the
    # runtime falls back to the STOP1 cell exactly like CObjCART::Get_MOTION,
    # which retries action 0 when FILE_MOTION returns 0.
    motion_path = os.path.join(OUT_DIR, "pat_motion.csv")
    n_cells = n_missing = 0
    seen = set()
    with open(motion_path, "w", newline="", encoding="utf-8") as fh:
        w = csv.writer(fh)
        w.writerow(["Name", "MotionRow", "MotionCol", "Action", "AnimName", "ZmoPath"])
        for base in sorted(motion_bases):
            for act_idx, act in enumerate(ACTIONS):
                row = base + act_idx
                if row >= tm.num_rows():
                    continue
                for col in range(tm.num_cols()):
                    raw = tm.get(row, col).strip()
                    if not raw.isdigit():
                        continue
                    fidx = int(raw)
                    if fidx <= 0 or fidx >= fm.num_rows():
                        continue
                    zmo = fm.get(fidx, 0).strip()
                    if not zmo:
                        continue
                    stem = os.path.splitext(os.path.basename(zmo.replace("\\", "/")))[0]
                    key = (row, col)
                    if key in seen:
                        continue
                    seen.add(key)
                    full = os.path.join(ARUA, zmo.replace("3DDATA\\", "").replace(
                        "3Ddata\\", "").replace("\\", os.sep))
                    if not os.path.exists(full):
                        n_missing += 1
                        continue
                    w.writerow([f"m_{row}_{col}", row, col, act, stem, zmo])
                    n_cells += 1
    print(f"[pat] {motion_path}: {n_cells} cells from {len(motion_bases)} motion bases"
          f" ({n_missing} skipped — ZMO absent on disk)")


if __name__ == "__main__":
    main()
