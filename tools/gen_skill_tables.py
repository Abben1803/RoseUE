#!/usr/bin/env python3
"""
gen_skill_tables.py — Generate the skill/job DATA TABLES (CSV) from the ROSE STBs.

Data-driven: the UE runtime (URoseSkillComponent) reads these by id; NO hardcoded
per-skill code.  Re-run to regenerate, then re-run tools/ue5_import_datatables.py.

Outputs (into RoseUE/DataTables/):
  skills.csv        one row per named LIST_SKILL.STB row  (FRoseSkillRow)
  jobs.csv          one row per non-empty LIST_CLASS.STB row (FRoseJobRow)
  skill_points.csv  level -> skill points granted (LIST_SKILL_P.STB, FRoseSkillPointRow)

Column map — VERIFIED against src/common/io_skill.h (SKILL_* macros; server and
client share the file) and spot-checked against live data (Double Attack 321,
Leap Attack 341, Divine Force 391, Taunt 411, Meditation 821, Bow Mastery 1401):
    0  Name (internal)                 SKILL_NAME (server col 0)
    1  BaseSkillId                     SKILL_1LEV_INDEX     (skill-line level-1 row)
    2  SkillLevel                      SKILL_LEVEL
    3  PointCost                       SKILL_NEED_LEVELUPPOINT
    4  Tab                             SKILL_TAB_TYPE
    5  SkillType                       SKILL_TYPE           (SKILL_TYPE_01..20, io_skill.h:15)
    6  Range (cm)                      SKILL_DISTANCE
    7  TargetFilter                    SKILL_CLASS_FILTER   (enumSKILL_TAGER_FILTER)
    8  Radius (cm)                     SKILL_SCOPE
    9  Power                           SKILL_POWER
   10  Harm                            SKILL_HARM           (hostility on use)
   11+C Status1/2                      SKILL_STATE_STB      (LIST_STATUS row)
   13  SuccessRatio                    SKILL_SUCCESS_RATIO
   14  Duration (sec)                  SKILL_DURATION
   15  DamageType                      SKILL_DAMAGE_TYPE    (1 wpn/2 magic/3 unarmed/0 basic)
   16+2T UseAbility/UseAmount          SKILL_USE_PROPERTY/VALUE (17=AT_MP, 16=AT_HP)
   20  CooldownTicks                   SKILL_RELOAD_TIME    (ms = t*200-100, io_skill.cpp:105)
   21+3T IncAbility/IncValue/IncRate   SKILL_INCREASE_ABILITY[_VALUE]/CHANGE_ABILITY_RATE
   27  CooldownGroup                   SKILL_RELOAD_TYPE    (shared-reuse group 0..15)
   28  SummonPet                       SKILL_SUMMON_PET
   29  ActionMode                      SKILL_ACTION_MODE    (SA_STOP/SA_ATTACK/SA_RESTORE)
   30..34 NeedWeapon1-5                SKILL_NEED_WEAPON    (weapon STB Type, e.g. 211)
   35  RequiredClassSet                SKILL_AVAILBLE_CLASS_SET (LIST_CLASS.STB row)
   36  RequiredUnion1                  SKILL_AVAILBLE_UNION (first of 3)
   39+2T NeedSkillN/LevelN             SKILL_NEED_SKILL_INDEX/SKILL_NEDD_SKILL_LEVEL
   45+2T NeedAbilityN/ValueN           SKILL_NEED_ABILITY_TYPE/VALUE (AT_STR..AT_SENSE)
   51  IconIdx                         SKILL_ICON_NO
   52/53 CastMotion/Speed              SKILL_ANI_CASTING[_SPEED] (TYPE_MOTION action row)
   68/69 ActionMotion/Speed            SKILL_ANI_ACTION_TYPE/SPEED
   70  HitCount                        SKILL_ANI_HIT_COUNT  (damage multiplier per swing)
   71  BulletNo / 73 FireSound         SKILL_BULLET_NO / SKILL_BULLET_FIRE_SOUND
   56  CastEffect                      SKILL_CASTING_EFFECT slot 0 (FILE_EFFECT.STB row)
   74  HitEffect                       SKILL_HIT_EFFECT     (FILE_EFFECT.STB row)
   76  HitSound                        SKILL_HIT_SOUND      (FILE_SOUND row)
   85  ZulyCost (x100)                 SKILL_LEVELUP_NEED_ZULY
   86  StlKey                          LIST_SKILL_S.STL link (this classic STB; the
                                       later server STB moved desc to col 87)

StatusType1/2 are resolved here from LIST_STATUS.STB col 1 (STATE_TYPE, stb.h:561)
so the runtime maps buffs straight to the eING_TYPE enum (datatype.h:703) without
shipping the status table.  StatusHarmful1/2 = STATE_PRIFITS_LOSSES (stb.h:563).

LIST_CLASS.STB (stb.h:610-616): col 0 = name, cols 1..10 = included job ids
(CLASS_INCLUDE_JOB — the server checks the first 8, CLASS_INCLUDE_JOB_CNT),
col 11 = STL key.  A skill's RequiredClassSet points at one of these rows;
CUserDATA::Check_JobCollection (cuserdata.cpp:854) passes when the row's job
list is empty or contains the player's job.

LIST_SKILL_P.STB: col 0 = level, col 1 = points granted at that level
(SP_LEVEL/SP_POINT, def_stb.h:6; used by classUSER::Add_EXP, gs_user.cpp:630).

Usage:  py -3.9 gen_skill_tables.py
"""
import csv, os, sys
_T = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, _T)
from rose_parser.formats.stb import parse as parse_stb
from rose_parser.formats.stl import parse as parse_stl

# Asset source: Arua (CLAUDE.md "Asset source: Arua, always").  Was the
# deprecated SourceAssets/3DDATA classic tree; ZSC object ids and STB row
# ids do not line up across eras, so a row resolved to different content
# than the Arua-era DataTables this feeds.  Output roots are unchanged.
SRC = os.path.join(os.environ.get("ROSE_ASSET_ROOT", r"C:\QQ-iROSE Online\extracted\3DDATA"), "STB")
OUT_DIR = r"C:/rose-next-classic/unreal-engine rose/RoseUE/DataTables"



def detect_stl_col(stb, stl, fallback):
    """Locate the STL-key column by testing which one resolves.  Fixed indices
    do not survive a client change: classic LIST_SKILL has 90 columns with the
    key in 89, not the 86 the Arua layout used — which produced 2475 skills with
    no names at all."""
    best, best_hits = fallback, 0
    for c in range(stb.num_cols()):
        hits = sum(1 for i in range(1, min(200, stb.num_rows()))
                   if stb.get(i, c) and stl.get(stb.get(i, c)))
        if hits > best_hits:
            best, best_hits = c, hits
    return best

def clean(s):
    """CSV/DataTable-safe single-line text."""
    return (s or "").replace("\r", " ").replace("\n", " ").strip()


def write_csv(name, header, rows):
    os.makedirs(OUT_DIR, exist_ok=True)
    path = os.path.join(OUT_DIR, name)
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(header)
        for r in rows:
            w.writerow(r)
    print(f"[skills] {name}: {len(rows)} rows")
    return path


def gen_skills():
    stb = parse_stb(os.path.join(SRC, "LIST_SKILL.STB"))
    status = parse_stb(os.path.join(SRC, "LIST_STATUS.STB"))
    stl = parse_stl(os.path.join(SRC, "LIST_SKILL_S.STL"))
    skill_stl_col = detect_stl_col(stb, stl, 86)

    def state_type(sid):
        return status.get_int(sid, 1) if 0 < sid < status.num_rows() else 0

    def state_harmful(sid):
        return status.get_int(sid, 3) if 0 < sid < status.num_rows() else 0

    header = ["Name", "Id", "SkillName", "BaseSkillId", "SkillLevel", "PointCost",
              "Tab", "SkillType", "Range", "TargetFilter", "Radius", "Power", "Harm",
              "Status1", "Status2", "StatusType1", "StatusType2",
              "StatusHarmful1", "StatusHarmful2",
              "SuccessRatio", "Duration", "DamageType",
              "UseAbility1", "UseAmount1", "UseAbility2", "UseAmount2",
              "CooldownSec", "CooldownGroup",
              "IncAbility1", "IncValue1", "IncRate1",
              "IncAbility2", "IncValue2", "IncRate2",
              "SummonPet", "ActionMode",
              "NeedWeapon1", "NeedWeapon2", "NeedWeapon3", "NeedWeapon4", "NeedWeapon5",
              "RequiredClassSet", "RequiredUnion1",
              "NeedSkill1", "NeedSkillLevel1", "NeedSkill2", "NeedSkillLevel2",
              "NeedSkill3", "NeedSkillLevel3",
              "NeedAbility1", "NeedAbilityValue1", "NeedAbility2", "NeedAbilityValue2",
              "IconIdx", "CastMotion", "CastMotionSpeed",
              "ActionMotion", "ActionMotionSpeed", "HitCount",
              "BulletNo", "FireSound", "HitSound", "ZulyCost",
              "CastEffect", "HitEffect",
              "StlKey", "DisplayName", "Description"]
    rows = []
    for i in range(1, stb.num_rows()):
        if not stb.get(i, 0).strip():
            continue  # unnamed filler row — the game skips skill 0 / blanks too
        g = stb.get_int
        st1, st2 = g(i, 11), g(i, 12)
        # skill reuse delay: ms = ticks*200 - 100 (CSkillLIST::LoadSkillTable,
        # io_skill.cpp:105); 0 ticks = no cooldown.
        ticks = g(i, 20)
        cooldown_sec = round(max(0, ticks * 200 - 100) / 1000.0, 2) if ticks > 0 else 0.0
        key = stb.get(i, skill_stl_col)
        rows.append([f"skill_{i}", i, clean(stb.get(i, 0)), g(i, 1), g(i, 2), g(i, 3),
                     g(i, 4), g(i, 5), g(i, 6), g(i, 7), g(i, 8), g(i, 9), g(i, 10),
                     st1, st2, state_type(st1), state_type(st2),
                     state_harmful(st1), state_harmful(st2),
                     g(i, 13), g(i, 14), g(i, 15),
                     g(i, 16), g(i, 17), g(i, 18), g(i, 19),
                     cooldown_sec, g(i, 27),
                     g(i, 21), g(i, 22), g(i, 23),
                     g(i, 24), g(i, 25), g(i, 26),
                     g(i, 28), g(i, 29),
                     g(i, 30), g(i, 31), g(i, 32), g(i, 33), g(i, 34),
                     g(i, 35), g(i, 36),
                     g(i, 39), g(i, 40), g(i, 41), g(i, 42), g(i, 43), g(i, 44),
                     g(i, 45), g(i, 46), g(i, 47), g(i, 48),
                     g(i, 51), g(i, 52), g(i, 53),
                     g(i, 68), g(i, 69), g(i, 70),
                     g(i, 71), g(i, 73), g(i, 76), g(i, 85),
                     g(i, 56), g(i, 74),
                     key, clean(stl.get(key)), clean(stl.get_desc(key))])
    write_csv("skills.csv", header, rows)


def gen_jobs():
    stb = parse_stb(os.path.join(SRC, "LIST_CLASS.STB"))
    stl = parse_stl(os.path.join(SRC, "LIST_CLASS_S.STL"))
    class_stl_col = detect_stl_col(stb, stl, 11)
    header = (["Name", "Id", "JobName"]
              + [f"Job{n}" for n in range(1, 11)]
              + ["StlKey", "DisplayName"])
    rows = []
    for i in range(stb.num_rows()):
        row_vals = [stb.get(i, c) for c in range(stb.num_cols())]
        if not any(v.strip() for v in row_vals):
            continue
        key = stb.get(i, class_stl_col)
        rows.append([f"job_{i}", i, clean(stb.get(i, 0))]
                    + [stb.get_int(i, c) for c in range(1, 11)]
                    + [key, clean(stl.get(key))])
    write_csv("jobs.csv", header, rows)


def gen_skill_points():
    stb = parse_stb(os.path.join(SRC, "LIST_SKILL_P.STB"))
    header = ["Name", "Level", "Points"]
    rows = []
    for i in range(stb.num_rows()):
        lvl = stb.get_int(i, 0)
        if lvl <= 0:
            continue
        rows.append([f"sp_{lvl}", lvl, stb.get_int(i, 1)])
    write_csv("skill_points.csv", header, rows)


def main():
    gen_skills()
    gen_jobs()
    gen_skill_points()


if __name__ == "__main__":
    main()
