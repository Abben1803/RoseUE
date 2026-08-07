#!/usr/bin/env python3
"""
gen_ai_tables.py — build Content/DataTables/ai_patterns.json from the ROSE AIP
scripts: FILE_AI.STB row (= the npcs table's AiType) -> .AIP -> parsed patterns.

JSON shape (consumed by URoseMonsterAIComponent):
  { "<ai_id>": { "idle_sec": n, "damaged_pct": n, "aip": "<stem>",
                 "patterns": [ [ {name, conds:[{op,...}], acts:[{op,...}]} ] x6 ] } }

Also prints the skill-usage summary (which AIs carry AIACT_24 = use-skill, and
which npc ids reference them via npcs.csv AiType) — the in-game test-mob list.

Offline: py -3.9 tools/gen_ai_tables.py
"""
import csv
import json
import os
import sys

_TOOLS = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _TOOLS)
from rose_parser.formats.stb import parse as parse_stb
from rose_parser.formats.aip import parse as parse_aip

UE = os.path.normpath(os.path.join(_TOOLS, "..", "unreal-engine rose", "RoseUE"))

# FILE_AI.STB must come from Arua, because npcs.csv's AiType is an Arua row
# index (Arua 1209 rows vs classic 802; only 470 rows agree).  Reading the
# classic table would bind monsters to a different monster's AI.
ROSE_3DDATA = os.environ.get("ROSE_ASSET_ROOT",
                             r"C:\QQ-iROSE Online\extracted\3DDATA")

# The .AIP payloads are CLASSIC-ONLY AND UNRECOVERABLE.  Monster AI runs
# server-side, so the Arua client VFS never carried it: a name-hash probe of
# C:\Arua\data.idx (algorithm from rose-file-readers/src/aruavfs.rs) returns
# ZERO hits for all 759 AIP paths named by Arua's own FILE_AI.STB, all 430
# classic filenames, and 9 path-shape variants, while control paths resolve in
# the same pass.  Do not re-attempt extraction.
#
# Therefore AIPs are resolved by BASENAME, not by row index — the FILE_AI table
# and the AIP files come from different clients.  First match wins:
# C:\Arua\extracted\3DDATA\AI is from a newer client (619 AIPs) and on all 10
# files that differ from the classic set it is strictly richer (more events,
# never fewer), so it is searched first.
AIP_DIRS = [os.environ.get("ROSE_AIP_DIR", r"C:\Arua\extracted\3DDATA\AI"),
            os.path.join(UE, "SourceAssets", "3DDATA", "AI"),
            os.path.normpath(os.path.join(_TOOLS, "..", "client", "3ddata", "AI"))]

ROOTS = [ROSE_3DDATA,
         os.path.join(UE, "SourceAssets", "3DDATA"),
         os.path.normpath(os.path.join(_TOOLS, "..", "client", "3ddata"))]
OUT = os.path.join(UE, "Content", "DataTables", "ai_patterns.json")
NPCS_CSV = os.path.join(UE, "DataTables", "npcs.csv")


def find_file(rel):
    for root in ROOTS:
        p = os.path.join(root, *rel.replace("\\", "/").split("/"))
        if os.path.isfile(p):
            return p
    return None


def _aip_index():
    """{UPPERCASE basename: abs path} across AIP_DIRS, earlier dirs winning."""
    idx = {}
    for d in AIP_DIRS:
        if not os.path.isdir(d):
            continue
        for fn in os.listdir(d):
            if fn.upper().endswith(".AIP"):
                idx.setdefault(fn.upper(), os.path.join(d, fn))
    return idx


AIP_INDEX = _aip_index()


def find_aip(rel):
    """Resolve an AIP by basename (row indexes are not portable across eras)."""
    return AIP_INDEX.get(os.path.basename(rel.replace("\\", "/")).upper())


stb_path = find_file("STB/FILE_AI.STB")
if not stb_path:
    raise SystemExit("FILE_AI.STB not found")
stb = parse_stb(stb_path)

out = {}
skill_ais = {}          # ai_id -> set of skill ids
parsed = failed = missing = 0
for row in range(stb.num_rows()):
    rel = (stb.get(row, 0) or "").strip()
    if not rel or rel == ".":
        continue
    aip_path = find_aip(rel)
    if not aip_path:
        missing += 1
        continue
    try:
        aip = parse_aip(aip_path)
    except Exception as e:
        failed += 1
        print(f"[ai] FAIL row {row} {rel}: {e}")
        continue
    parsed += 1

    pats = []
    for pat in aip.patterns:
        evs = []
        for ev in pat.events:
            evs.append({
                "name": ev.name,
                "conds": [dict(op=r.op, **r.fields) for r in ev.conds],
                "acts": [dict(op=r.op, **r.fields) for r in ev.acts],
            })
            for r in ev.acts:
                if r.op == 25 and "skill" in r.fields:
                    skill_ais.setdefault(row, set()).add(r.fields["skill"])
        pats.append(evs)
    out[str(row)] = {
        "idle_sec": aip.idle_sec, "damaged_pct": aip.damaged_pct,
        "aip": os.path.splitext(os.path.basename(aip_path))[0],
        "patterns": pats,
    }

os.makedirs(os.path.dirname(OUT), exist_ok=True)
json.dump(out, open(OUT, "w"), separators=(",", ":"))
print(f"[ai] {parsed} AIPs parsed ({failed} failed, {missing} missing) "
      f"-> {OUT} ({os.path.getsize(OUT)//1024} KB)")

# ── skill summary + test-mob list ────────────────────────────────────────────
print(f"[ai] {len(skill_ais)} AIs contain USE-SKILL (AIACT_24)")
npc_by_ai = {}
if os.path.exists(NPCS_CSV):
    for r in csv.DictReader(open(NPCS_CSV, encoding="utf-8-sig")):
        try:
            npc_by_ai.setdefault(int(r.get("AiType") or 0), []).append(
                (int(r["Id"]), r.get("DisplayName", "")))
        except (ValueError, KeyError):
            pass
shown = 0
for ai_id in sorted(skill_ais):
    npcs = npc_by_ai.get(ai_id, [])
    if not npcs:
        continue
    shown += 1
    if shown <= 15:
        ex = ", ".join(f"{i}:{n}" for i, n in npcs[:3])
        print(f"[ai]   ai {ai_id} ({out[str(ai_id)]['aip']}) skills={sorted(skill_ais[ai_id])} "
              f"npcs[{len(npcs)}]: {ex}")
print(f"[ai] {shown} skill-AIs are referenced by npcs.csv")
