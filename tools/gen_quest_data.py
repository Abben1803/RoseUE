#!/usr/bin/env python3
"""
gen_quest_data.py — Export the QSD quest-trigger database + the quest journal
table for UE.

1. LIST_QUESTDATA.STB → parse each QSD → Content/Quests/quests.json:
     { "triggers": { "<name>": {
         "checkNext": 0|1,
         "next": "<name of the following trigger>"   (only when checkNext),
         "conditions": [ {type-tagged dicts}, ... ],
         "rewards":    [ ... ]
       }, ... } }
   Trigger names are the QF_checkQuestCondition/doQuestTrigger keys.
   Reward type 12 (message) gets its English text embedded from
   ULNGTB_QST.LTB (strID-1 row, like CQuestTRIGGER::Load does with the STB).

2. LIST_QUEST.STB + LIST_QUEST_S.STL → DataTables/quests.csv
   (journal names/descriptions; STB col 4 = STL key, col 1 time limit,
    col 2 owner type, col 3 icon — io_quest.h QUEST_* macros).

Usage:  py -3.9 gen_quest_data.py
"""
import csv
import json
import os
import sys

_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.qsd import parse as parse_qsd
from rose_parser.formats.stb import parse as parse_stb
from rose_parser.formats.stl import parse as parse_stl
from rose_parser.formats.ltb import parse as parse_ltb

# Asset source: Arua (CLAUDE.md "Asset source: Arua, always").  Was the
# deprecated SourceAssets/3DDATA classic tree; ZSC object ids and STB row
# ids do not line up across eras, so a row resolved to different content
# than the Arua-era DataTables this feeds.  Output roots are unchanged.
SRC = os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA")
OUT_QUESTS = r"C:/rose-next-classic/unreal-engine rose/RoseUE/Content/Quests"
OUT_TABLES = r"C:/rose-next-classic/unreal-engine rose/RoseUE/DataTables"

LANG_EN = 2


def find_file(d, name):
    """Case-insensitive lookup inside a directory."""
    low = name.lower()
    for f in os.listdir(d):
        if f.lower() == low:
            return os.path.join(d, f)
    return None


def main():
    qdir = os.path.join(SRC, "QUESTDATA")

    # quest-language table (reward-12 messages)
    ltb = None
    ltb_path = find_file(qdir, "ULNGTB_QST.LTB") or find_file(qdir, "LNGTB_QST.LTB")
    if ltb_path:
        ltb = parse_ltb(ltb_path)
        print(f"[quests] {os.path.basename(ltb_path)}: {ltb.row_count} rows")

    lst = parse_stb(os.path.join(SRC, "STB", "LIST_QUESTDATA.STB"))
    triggers = {}
    files = dupes = 0
    for i in range(lst.num_rows()):
        fn = lst.get(i, 0)
        if not fn:
            continue
        path = find_file(qdir, os.path.basename(fn.replace("\\", "/")))
        if not path:
            print(f"[quests] MISSING {fn}")
            continue
        qsd = parse_qsd(path)
        files += 1
        prev = None
        for trg in qsd.triggers:
            if trg.name in triggers:
                # first-load wins, like LoadQuestTrigger's hash-collision skip
                dupes += 1
                prev = None
                continue
            for rw in trg.rewards:
                if rw["type"] == 12 and ltb:
                    rw["msg"] = ltb.get(rw["strID"] - 1, LANG_EN)
            entry = {
                "checkNext": trg.check_next,
                "conditions": trg.conditions,
                "rewards": trg.rewards,
            }
            triggers[trg.name] = entry
            # chain: a checkNext trigger falls through to the NEXT one in file
            # order when its own conditions fail (CQuestDATA::LoadQuestTrigger)
            if prev is not None and prev["checkNext"]:
                prev["next"] = trg.name
            prev = entry

    os.makedirs(OUT_QUESTS, exist_ok=True)
    out = os.path.join(OUT_QUESTS, "quests.json")
    with open(out, "w", encoding="utf-8") as fp:
        json.dump({"triggers": triggers}, fp, ensure_ascii=False, indent=1)
    print(f"[quests] {files} QSD files, {len(triggers)} triggers "
          f"({dupes} duplicate names skipped) -> {out}")

    # ---- journal table ----
    stb = parse_stb(os.path.join(SRC, "STB", "LIST_QUEST.STB"))
    stl = parse_stl(os.path.join(SRC, "STB", "LIST_QUEST_S.STL"))
    os.makedirs(OUT_TABLES, exist_ok=True)
    path = os.path.join(OUT_TABLES, "quests.csv")
    rows = 0
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["Name", "Id", "DisplayName", "Description", "StartMsg",
                    "EndMsg", "TimeLimit", "OwnerType", "Icon", "StlKey"])
        # The STL key column is NOT a fixed index across client eras: this STB
        # has 13 columns with the key in the LAST one ("STL"), while col 4 is
        # "Block Abandon".  Reading col 4 produced 259 rows whose DisplayName
        # and Description were all empty, so the journal showed "Quest <id>".
        # Bind by header, and fall back to the last column.
        key_col = next((c for c in range(stb.num_cols())
                        if (stb.col_name(c) or "").strip().upper() == "STL"),
                       stb.num_cols() - 1)
        for i in range(stb.num_rows()):
            key = stb.get(i, key_col)
            if not key:
                continue
            w.writerow([f"quest_{i}", i,
                        stl.get(key), stl.get_desc(key),
                        stl.get_start_msg(key), stl.get_end_msg(key),
                        stb.get_int(i, 1), stb.get_int(i, 2),
                        stb.get_int(i, 3), key])
            rows += 1
    print(f"[quests] quests.csv: {rows} rows -> {path}")


if __name__ == "__main__":
    main()
