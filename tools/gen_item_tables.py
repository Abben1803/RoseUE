#!/usr/bin/env python3
"""
gen_item_tables.py — Generate per-slot item DATA TABLES (CSV) from the ROSE STBs/ZSC.

Data-driven: one editable/regenerable CSV per equipment slot, one row per item;
the runtime reads them by id.  NO hardcoded per-item code.  Re-run to regenerate.

  weapons.csv  (gender-neutral) | body.csv arms.csv foot.csv cap.csv (per-gender)

Common equipment STB columns (verified): 1=MaleModel 2=FemaleModel 4=Type
8=Quality (ITEM_QUALITY, stb.h) 19/20=ReqStat1/Amt 21/22=ReqStat2/Amt
24/25=BonusStat1/Amt 27/28=BonusStat2/Amt 29=Durability 31=Defense
32=MagicResist; STL-link col varies per slot.
DisplayName + Description are left EMPTY for hand authoring.

Usage:  py -3.9 gen_item_tables.py
"""
import csv, os, sys
_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.stb import parse as parse_stb
from rose_parser.formats.zsc import parse as parse_zsc
from rose_parser.formats.stl import parse as parse_stl

SRC = r"C:/QQ-iROSE Online/extracted/3DDATA"
#SRC = r"C:/rose-next-classic/client/out/3DDATA"
# ICON RULE (2026-07-20, final): IconIdx = the row's OWN STB col 9
# (ITEM_ICON_NO), used AS-IS.  icon index -> sheet ICON<idx//169+1>.dds,
# cell idx%169 (13x13 grid of 32px cells).  STLs are ONLY name/description
# links — NEVER derive or remap icon indices through STL keys.
OUT_DIR = r"C:/rose-next-classic/unreal-engine rose/RoseUE/DataTables"
PLAYER_WPN = {211, 212, 221, 222, 223, 231, 232, 233, 241, 242, 251, 252, 253, 271}
HAND = {0: "Right", 1: "Left", 2: "Shield"}

# common requirement / bonus / durability block (same STB layout for all equipment)
# Weight = ITEM_WEIGHT (stb.h: get_int32(I, 7)), stored in tenths → display /10.
COMMON = ["ReqStat1", "ReqAmount1", "ReqStat2", "ReqAmount2",
          "BonusStat1", "BonusAmount1", "BonusStat2", "BonusAmount2",
          "Durability", "Weight", "FieldItemId"]


_FIELD_COL = {}
def field_col(stb):
    """Column holding the FIELD ITEM id — the ground-drop model, a row into
    LIST_FIELDITEM.ZSC (imported as SM_field_<id>).

    Located by HEADER NAME, not index: it sits at 10 in every table of this
    client, but the header is 'Fielditem' in most and 'Field Item' in
    LIST_USEITEM, and indices drift between eras (see RoseStbSchema).  Falls
    back to 10 only if no header matches."""
    key = id(stb)
    if key not in _FIELD_COL:
        found = 10
        for c in range(stb.num_cols()):
            if "field" in (stb.col_name(c) or "").lower().replace(" ", ""):
                found = c
                break
        _FIELD_COL[key] = found
    return _FIELD_COL[key]


def common_vals(stb, i):
    g = stb.get
    return [g(i, 19), g(i, 20), g(i, 21), g(i, 22),
            g(i, 24), g(i, 25), g(i, 27), g(i, 28), g(i, 29), g(i, 7),
            stb.get_int(i, field_col(stb))]

def load_stl(stl_file):
    """Open a LIST_<TYPE>_S.STL name table, or None if absent."""
    path = os.path.join(SRC, "STB", stl_file)
    if not os.path.isfile(path):
        print(f"[items] STL missing: {stl_file}")
        return None
    return parse_stl(path)

def name_desc(stl, key):
    if not (stl and key):
        return "", ""

    name = stl.get(key)
    desc = stl.get_desc(key)

    if not name:
        print("[STL MISS]", repr(key))

    return name, desc

def write_csv(name, header, rows):
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, name)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f); w.writerow(header)
        for r in rows:
            w.writerow(r)
    print(f"[items] {name}: {len(rows)} rows")
    return path

def gen_face_items():
    stb = parse_Stb(os.path.join(src, "STB", "LIST_FACE"))

def detect_stl_col(stb, stl, fallback):
    """Find the column whose values are STL keys, by testing which one actually
    resolves.  The index is NOT stable across client eras: classic LIST_CAP puts
    it in col 37 (labelled "Description") while the Arua tables used a different
    one, and reading the wrong column yields rows with EVERY name blank -- which
    is exactly how the quest journal ended up showing "Quest <id>"."""
    best, best_hits = fallback, 0
    probe = min(120, stb.num_rows())
    for c in range(stb.num_cols()):
        hits = 0
        for i in range(1, probe):
            v = stb.get(i, c)
            if v and stl.get(v):
                hits += 1
        if hits > best_hits:
            best, best_hits = c, hits
    return best

def gen_weapons():
    stb = parse_stb(os.path.join(SRC, "STB", "LIST_WEAPON.STB"))
    zsc = parse_zsc(os.path.join(SRC, "WEAPON", "LIST_WEAPON.ZSC"))
    tm = parse_stb(os.path.join(SRC, "STB", "TYPE_MOTION.STB"))
    stl = load_stl("LIST_WEAPON_S.STL")
    asset = "/Game/Characters/Modular/Weapons/weapon_{0}/weapon_{0}/SkeletalMeshes/weapon_{0}"
    header = (["Name", "Id", "Type", "MotionType", "MotionName", "Hand",
               "AttackPower", "AttackSpeed", "Range", "Quality", "IconIdx",
               # 5/6 = ITEM_BASE_PRICE / ITEM_PRICE_RATE (stb.h) — store pricing.
               "BasePrice", "PriceRate",
               # 40=swing-start FILE_SOUND row, 42=hit-sound TYPE (LIST_HITSOUND row;
               # the defender's material picks the column) — stb.h WEAPON_ATK_*_SOUND.
               "AtkStartSound", "AtkHitSoundType",
               # 38 = WEAPON_BULLET_EFFECT (LIST_EFFECT row: bullet/hit EFTs, speed,
               # sounds — ranged weapons), 41 = WEAPON_ATK_FIRE_SOUND (FILE_SOUND).
               "BulletEffect", "AtkFireSound"] + COMMON +
              ["Mesh", "TextureSrc", "StlKey", "DisplayName", "Description", "HitSound"])
    rows = []
    # Column 50 is the Arua layout; classic LIST_WEAPON has 49 columns with the
    # key in 48, so a fixed index yields 738 weapons with no names at all.
    wpn_stl_col = detect_stl_col(stb, stl, 50)
    for i in range(len(zsc.models)):
        obj = zsc.models[i]
        if not obj.parts or stb.get_int(i, 4) not in PLAYER_WPN:
            continue
        mt = stb.get_int(i, 34)
        mn = tm.col_name(mt) if 0 <= mt < tm.num_cols() else ""
        hand = "+".join(HAND.get(d, str(d)) for d in sorted({p.dummy_idx for p in obj.parts}))
        p0 = obj.parts[0]
        tex = zsc.materials[p0.material_id].texture_path if p0.material_id < len(zsc.materials) else ""
        key = stb.get(i, wpn_stl_col)
        nm, desc = name_desc(stl, key)
        rows.append([f"weapon_{i}", i, stb.get_int(i, 4), mt, mn, hand,
                     stb.get_int(i, 35), stb.get(i, 36), stb.get_int(i, 33),
                     stb.get_int(i, 8),
                     stb.get_int(i, 9),   # ITEM_ICON_NO, AS-IS (icon rule)
                     stb.get_int(i, 5), stb.get_int(i, 6),
                     stb.get_int(i, 40), stb.get_int(i, 42),
                     stb.get_int(i, 38), stb.get_int(i, 41)]
                    + common_vals(stb, i)
                    + [asset.format(i), tex.replace("\\", "/"), key, nm, desc, ""])
    write_csv("weapons.csv", header, rows)


def _gear_manifest_ids(slot):
    """Item ids the gear pipeline imported for a slot (gear_manifest.json) —
    rows whose STB model columns are empty in Arua but whose ZSC has parts."""
    try:
        import json
        man = json.load(open(os.path.join(os.path.dirname(os.path.abspath(__file__)),
                                          "_tmp", "gear_manifest.json")))
        s = slot.upper()
        return {int(k.split("/")[2]) for k in man["items"] if k.split("/")[1] == s}
    except Exception:
        return set()



def gen_armor(slot, stb_file, stl_col, extra):
    """extra = list of (ColumnName, stb_col) appended after Defense/MagicResist."""
    stb = parse_stb(os.path.join(SRC, "STB", stb_file))
    mesh_ids = _gear_manifest_ids(slot)
    stl = load_stl(stb_file.replace(".STB", "_S.STL"))
    af = "/Game/Characters/Modular/Female/{0}_{1}/{0}_{1}/SkeletalMeshes/{0}_{1}"
    am = "/Game/Characters/Modular/Male/{0}_{1}/{0}_{1}/SkeletalMeshes/{0}_{1}"
    header = (["Name", "Id", "Type", "Defense", "MagicResist", "IconIdx",
               "Quality", "BasePrice", "PriceRate"] + [c for c, _ in extra]
              + COMMON + ["MeshFemale", "MeshMale", "StlKey", "DisplayName", "Description"])
    use_col = detect_stl_col(stb, stl, stl_col)
    rows = []
    for i in range(stb.num_rows()):
        # a real item has a model file in either gender column (1=Male, 2=Female)
        # OR an imported gear mesh (Arua ZSC binding — model columns often empty).
        if not (stb.get(i, 1).strip() or stb.get(i, 2).strip() or i in mesh_ids):
            continue
        key = stb.get(i, use_col)
        nm, desc = name_desc(stl, key)
        
        rows.append([f"{slot}_{i}", i, stb.get_int(i, 4),
                     stb.get_int(i, 31), stb.get_int(i, 32),
                     stb.get_int(i, 9),  # ITEM_ICON_NO, AS-IS (icon rule)
                     stb.get_int(i, 8), #Quality
                     stb.get_int(i, 5),
                     stb.get_int(i, 6)]
                    + [stb.get(i, c) for _, c in extra]
                    + common_vals(stb, i)
                    + [af.format(slot, i), am.format(slot, i), key, nm, desc])
    write_csv(f"{slot}.csv", header, rows)


def load_type_names():
    """STR_ITEMTYPE.STL — the classic item-info box's "Type:" line, keyed by the
    item's ITEM_TYPE/class (col 4).  311 'Potion', 317 'Engine Fuel', ...  This
    is the client's own table (CStringManager::GetItemType), so no hardcoded map."""
    stl = load_stl("STR_ITEMTYPE.STL")
    if not stl:
        return {}
    out = {}
    for r in stl.rows:
        key = getattr(r, "key", "")
        name = stl.get(key)
        if key.isdigit() and name:
            out[int(key)] = name
    return out


# ── LIST_USEITEM effect block (verified against src/common/include/rose/io/stb.h
#    USEITEM_*: 17/18 need type+value, 19/20 add type+value, 21 script,
#    22 use effect, 23 use sound, 24 status STB row, 25/26 cooldown type+delay,
#    and against the Arua header names, which agree exactly) ──────────────────
USE_EFFECT_COLS = ["Weight", "NeedAbility", "NeedValue", "AddAbility", "AddValue",
                   "StatusId", "StatusType", "StatusPerSec",
                   "UseEffect", "UseSound", "CooldownType", "CooldownSec",
                   "ScriptIdx", "TypeName"]


def load_status_regen():
    """LIST_STATUS.STB -> {row: (STATE_TYPE, per-second value)}.

    A consumable with USEITME_STATUS_STB != 0 is an over-time potion: the server
    (classUSER::Use_pITEM, gs_user.cpp:894) hands UpdateIngPOTION the item's total
    ADD_DATA_VALUE plus the status row's STATE_APPLY_ABILITY_VALUE, which the
    table itself labels "Per 1 Second" — so duration = total / per-second.
    STATE_TYPE (col 1) is the eING_TYPE: 1 = ING_INC_HP, 2 = ING_INC_MP.
    Resolved here so the runtime needs no second table."""
    stb = parse_stb(os.path.join(SRC, "STB", "LIST_STATUS.STB"))
    out = {}
    for i in range(1, stb.num_rows()):          # row 0 is the column legend
        per = stb.get_int(i, 6)                 # STATE_APPLY_ABILITY_VALUE(i, 0)
        if per:
            out[i] = (stb.get_int(i, 1), per)
    return out


def gen_simple(slot, stb_file):
    """Non-equipment item table (USEITEM/JEMITEM/NATURAL): id + icon + name/desc.
    Name (col 0) is the raw STB name; the STL key is the LAST column (LUSE.../
    LNAT...), resolved through LIST_<TYPE>_S.STL (English preferred).  Emits every
    row that has an icon or a name so drops always resolve to something.

    The effect block (USE_EFFECT_COLS) is only meaningful for LIST_USEITEM — the
    other tables reuse those column indices for unrelated data, so they are
    written as zeros rather than read.  Every gen_simple CSV keeps the same shape
    so they can all share FRoseSimpleItemRow."""
    stb = parse_stb(os.path.join(SRC, "STB", stb_file))
    stl = load_stl(stb_file.replace(".STB", "_S.STL"))
    ncol = stb.num_cols()
    type_names = load_type_names()
    bUse = (stb_file.upper() == "LIST_USEITEM.STB")
    regen = load_status_regen() if bUse else {}
    # Subtype = ITEM_TYPE (col 4) — CEconomy::IsEssentialGoods checks it
    # (421-428 medicine / 311-312 food get the item-rate price branch).
    header = ["Name", "Id", "IconIdx", "Subtype", "Quality", "BasePrice",
              "PriceRate", "FieldItemId"] + USE_EFFECT_COLS + \
             ["StlKey", "DisplayName", "Description"]
    rows = []
    for i in range(stb.num_rows()):
        raw = stb.get(i, 0).strip()
        key = stb.get(i, ncol - 1).strip()      # STL key lives in the last column
        icon = stb.get_int(i, 9)   # ITEM_ICON_NO, AS-IS (icon rule)
        nm, desc = name_desc(stl, key)
        if not nm:
            nm = raw
        if icon <= 0 and not nm:
            continue                            # empty/placeholder row
        cls = stb.get_int(i, 4)
        if bUse:
            status = stb.get_int(i, 24)
            stype, per = regen.get(status, (0, 0))
            eff = [stb.get_int(i, 7),                              # Weight
                   stb.get_int(i, 17), stb.get_int(i, 18),         # need type/value
                   stb.get_int(i, 19), stb.get_int(i, 20),         # add  type/value
                   status, stype, per,
                   stb.get_int(i, 22), stb.get_int(i, 23),         # use effect/sound
                   stb.get_int(i, 25), stb.get_int(i, 26),         # cooldown type/sec
                   stb.get_int(i, 21)]                             # script
        else:
            eff = [stb.get_int(i, 7)] + [0] * 12
        eff.append(type_names.get(cls, ""))
        rows.append([f"{slot}_{i}", i, icon, cls,
                     stb.get_int(i, 8), stb.get_int(i, 5), stb.get_int(i, 6),
                     stb.get_int(i, field_col(stb))]
                    + eff + [key, nm, desc])
    write_csv(f"{slot}.csv", header, rows)


def main():
    gen_weapons()
    gen_armor("body", "LIST_BODY.STB", 39, [])
    gen_armor("arms", "LIST_ARMS.STB", 39, [])                      # gloves
    gen_armor("foot", "LIST_FOOT.STB", 39, [("MoveSpeed", 33)])     # boots
    gen_armor("cap",  "LIST_CAP.STB",  42, [("HairSettingF", 36), ("HairSettingM", 33)])
    gen_armor("back", "LIST_BACK.STB", 42, [("MoveSpeed", 33)])
    gen_armor("faceitem", "LIST_FACEITEM.STB", 39, [])
    #Non-equipment drops (consumables/gems/materials) — name + icon for the bag.
    gen_simple("consumable", "LIST_USEITEM.STB")   # ITEM_TYPE_USE (10)
    gen_simple("gem",        "LIST_JEMITEM.STB")   # ITEM_TYPE_ETC/GEM (11)
    gen_simple("material",   "LIST_NATURAL.STB")   # ITEM_TYPE_NATURAL (12)
    # Face items / jewelry / sub-weapons — bag name + icon (no equip model wired).
    # NOT gen_simple: face items carry Defense 31 / Magic Defense 32 /
    # Durability 29 like armour.  This call used to run AFTER gen_armor and
    # overwrite it, throwing the stats away every single run.
    # Accessories have no Defense columns, but they DO carry Base Stat1/Value1
    # (24/25) and Base Stat2/Value2 (27/28) — the bonus pair gen_armor already
    # reads.  Through gen_simple those were dropped and every ring/earring/
    # necklace arrived with no stats at all.
    gen_armor("jewel", "LIST_JEWEL.STB", 36, [])   # ring / necklace / earring
    # Sub-weapons are DEFENSIVE EQUIPMENT, not simple items.  Generated through
    # gen_simple they came out with consumable columns (AddAbility, StatusId,
    # CooldownSec) and NO Defense/MagicDefense at all — a Wooden Shield with 3
    # defence in the STB reached the game with none.  LIST_SUBWPN's layout is
    # the armour one (Defense 31, MagicResist 32, Durability 29, reqs 19-28),
    # so it belongs on that path.
    gen_armor("subwpn", "LIST_SUBWPN.STB", 39, [])   # shields + magic tools
    # ITEM_TYPE_RIDE_PART (14) — carts / castle gears / mounts. Subtype (class)
    # per datatype.h t_eItemCLASS: 511/512/513 body (cart/cg/mount), 521/522
    # engine, 531/532 leg, 541 ability, 551/552 weapon.
    gen_simple("pat", "LIST_PAT.STB")
    print("[items] done ->", OUT_DIR)


if __name__ == "__main__":
    main()
