#!/usr/bin/env python3
"""
gen_npc_table.py — Generate the NPC/monster DATA TABLE (npcs.csv) from LIST_NPC.STB.

Column map is the authoritative one from src/common/include/rose/io/stb.h
(NPC_WALK_SPEED=2 ... NPC_ATK_RANGE=26, NPC_TYPE=27, NPC_STRING_ID=40).
DisplayName resolves through LIST_NPC_S.STL (English preferred), falling back
to the raw STB name column.  Only rows with a valid LIST_NPC.CHR model are
emitted — those are the spawnable characters.

Usage:  py -3.9 gen_npc_table.py
"""
import csv, os, sys
_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.stb import parse as parse_stb
from rose_parser.formats.stl import parse as parse_stl
from rose_parser.formats.chr_ import parse as parse_chr

# Asset source: Arua (CLAUDE.md "Asset source: Arua, always").  Was the
# deprecated SourceAssets/3DDATA classic tree; ZSC object ids and STB row
# ids do not line up across eras, so a row resolved to different content
# than the Arua-era DataTables this feeds.  Output roots are unchanged.
SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
OUT_DIR = r"C:/rose-next-classic/unreal-engine rose/RoseUE/DataTables"
MESH = "/Game/Monsters/npc_{0}/npc_{0}/SkeletalMeshes/npc_{0}"


def main():
    stb = parse_stb(os.path.join(SRC, "STB", "LIST_NPC.STB"))
    chr_ = parse_chr(os.path.join(SRC, "NPC", "LIST_NPC.CHR"))
    stl_path = os.path.join(SRC, "NPC", "LIST_NPC_S.STL")
    if not os.path.isfile(stl_path):
        stl_path = os.path.join(SRC, "STB", "LIST_NPC_S.STL")
    stl = parse_stl(stl_path) if os.path.isfile(stl_path) else None
    if stl is None:
        print("[npcs] LIST_NPC_S.STL not found — DisplayName falls back to STB col 0")

    # Drop columns (stb.h): 18 DROP_TYPE (item_drop.stb row), 19 DROP_MONEY
    # (money-drop chance 1-100), 20 DROP_ITEM (drop rate/quality).
    header = ["Name", "Id", "DisplayName", "WalkSpeed", "RunSpeed", "Scale",
              "Level", "HP", "Attack", "Hit", "Defense", "Resist", "Avoid",
              "AttackSpeed", "IsMagicDamage", "AiType", "GiveExp", "DropType",
              "RWeapon", "LWeapon",
              "DropMoney", "DropItem", "CanTarget", "AttackRange", "NpcType",
              "HitMaterial", "AttackSound", "DieSound",
              # store tabs (NPC_SELL_TAB, cols 21-24 → LIST_SELL.STB rows) +
              # on-death quest trigger (NPC_DESC col 41 — CObjMOB::Do_DeadEvent
              # fires QF_doQuestTrigger with it).
              "SellTab0", "SellTab1", "SellTab2", "SellTab3", "QuestTrigger",
              "Mesh", "StlKey"]
    rows = []
    for i in range(min(stb.num_rows(), len(chr_.models))):
        if not chr_.models[i].is_valid or not chr_.models[i].body_part_indices:
            continue
        g = stb.get_int
        key = stb.get(i, 40)
        name = (stl.get(key) if (stl and key) else "") or stb.get(i, 0)
        rows.append([f"npc_{i}", i, name, g(i, 2), g(i, 3), g(i, 4),
                     g(i, 7), g(i, 8), g(i, 9), g(i, 10), g(i, 11), g(i, 12),
                     g(i, 13), g(i, 14), g(i, 15), g(i, 16), g(i, 17), g(i, 18),
                     g(i, 5), g(i, 6),
                     g(i, 19), g(i, 20), g(i, 25), g(i, 26), g(i, 27),
                     # sounds: 28=hit material type (LIST_HITSOUND column),
                     # 31=attack (swing start), 35=die — FILE_SOUND rows.
                     g(i, 28), g(i, 31), g(i, 35),
                     g(i, 21), g(i, 22), g(i, 23), g(i, 24), stb.get(i, 41),
                     MESH.format(i), key])

    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, "npcs.csv")
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f); w.writerow(header)
        for r in rows:
            w.writerow(r)
    print(f"[npcs] npcs.csv: {len(rows)} rows -> {path}")


if __name__ == "__main__":
    main()
