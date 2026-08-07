// DataTable row structs for the ROSE skill/job tables (generated CSVs in
// RoseUE/DataTables: skills.csv / jobs.csv / skill_points.csv, produced by
// tools/gen_skill_tables.py from LIST_SKILL.STB / LIST_CLASS.STB /
// LIST_SKILL_P.STB).  Property names must match the CSV headers exactly; the
// "Name" column is the row key ("skill_5" / "job_41" / "sp_12").  Column
// semantics are the SKILL_* macros in src/common/io_skill.h — see the
// generator's docstring for the verified map.  Data-driven: change skills by
// regenerating the CSV, never by editing code.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "RoseSkillTypes.generated.h"

// skills.csv — one row per LIST_SKILL.STB row (a skill LEVEL is its own row;
// BaseSkillId groups the levels of one skill line, SKILL_1LEV_INDEX).
USTRUCT(BlueprintType)
struct FRoseSkillRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Id = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString SkillName;         // internal (STB col 0)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BaseSkillId = 0;   // SKILL_1LEV_INDEX
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   SkillLevel = 0;    // SKILL_LEVEL
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   PointCost = 0;     // SKILL_NEED_LEVELUPPOINT
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Tab = 0;           // SKILL_TAB_TYPE (skill window page)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   SkillType = 0;     // SKILL_TYPE_01..20 (io_skill.h:15)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Range = 0;         // cm to target (SKILL_DISTANCE)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   TargetFilter = 0;  // enumSKILL_TAGER_FILTER
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Radius = 0;        // area scope, cm (SKILL_SCOPE)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Power = 0;         // SKILL_POWER
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Harm = 0;          // SKILL_HARM (draws aggro)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Status1 = 0;       // LIST_STATUS row
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Status2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   StatusType1 = 0;   // resolved STATE_TYPE (eING_TYPE)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   StatusType2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   StatusHarmful1 = 0; // STATE_PRIFITS_LOSSES
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   StatusHarmful2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   SuccessRatio = 0;  // SKILL_SUCCESS_RATIO
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Duration = 0;      // seconds (SKILL_DURATION)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   DamageType = 0;    // SKILL_DAMAGE_TYPE (1 wpn/2 magic/3 unarmed)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   UseAbility1 = 0;   // consumed ability (17=AT_MP, 16=AT_HP)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   UseAmount1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   UseAbility2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   UseAmount2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float   CooldownSec = 0.f; // (ticks*200-100)/1000, io_skill.cpp:105
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   CooldownGroup = 0; // SKILL_RELOAD_TYPE (shared group 1..15; 0 = per skill)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   IncAbility1 = 0;   // buff/passive ability (t_AbilityINDEX)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   IncValue1 = 0;     // flat amount
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   IncRate1 = 0;      // percent of the ability's value
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   IncAbility2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   IncValue2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   IncRate2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   SummonPet = 0;     // LIST_NPC row (type 14)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   ActionMode = 0;    // SA_STOP/SA_ATTACK/SA_RESTORE
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedWeapon1 = 0;   // required weapon Type (211 = 1H sword...)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedWeapon2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedWeapon3 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedWeapon4 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedWeapon5 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   RequiredClassSet = 0; // LIST_CLASS row (jobs.csv Id)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   RequiredUnion1 = 0;  // clan union gate (unmodeled)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedSkill1 = 0;    // prerequisite skill row id
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedSkillLevel1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedSkill2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedSkillLevel2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedSkill3 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedSkillLevel3 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedAbility1 = 0;  // AT_STR..AT_SENSE requirement
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedAbilityValue1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedAbility2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedAbilityValue2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   IconIdx = 0;       // SKILL_ICON_NO → /Game/UI/Icons
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   CastMotion = 0;    // TYPE_MOTION action row (SKILL_ANI_CASTING)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   CastMotionSpeed = 100;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   ActionMotion = 0;  // TYPE_MOTION action row (SKILL_ANI_ACTION_TYPE)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   ActionMotionSpeed = 100;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   HitCount = 0;      // SKILL_ANI_HIT_COUNT (damage x N)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BulletNo = 0;      // LIST_BULLET row (type 5/6)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   FireSound = 0;     // FILE_SOUND row (RoseSoundData.h)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   HitSound = 0;      // FILE_SOUND row
	// FILE_EFFECT.STB rows (.EFT) — carried for the future faithful EFT
	// renderer; the current RoseSkillFX layer keys colors off DamageType.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   CastEffect = 0;    // SKILL_CASTING_EFFECT slot 0
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   HitEffect = 0;     // SKILL_HIT_EFFECT
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   ZulyCost = 0;      // learn cost / 100
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString StlKey;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString DisplayName;       // LIST_SKILL_S.STL (English)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Description;
};

// jobs.csv — one row per LIST_CLASS.STB row: a named SET of job ids.  Skills
// point at a set via RequiredClassSet; the check passes when the set is empty
// or contains the character's job (CUserDATA::Check_JobCollection,
// cuserdata.cpp:854 — the server scans the first 8 entries,
// CLASS_INCLUDE_JOB_CNT, stopping at the first 0).
USTRUCT(BlueprintType)
struct FRoseJobRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Id = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString JobName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Job1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Job2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Job3 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Job4 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Job5 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Job6 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Job7 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Job8 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Job9 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Job10 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString StlKey;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString DisplayName;
};

// skill_points.csv — points granted when reaching a level (LIST_SKILL_P.STB;
// classUSER::Add_EXP grants SP_POINT(row) where SP_LEVEL(row) == new level,
// gs_user.cpp:630).
USTRUCT(BlueprintType)
struct FRoseSkillPointRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Level = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32 Points = 0;
};
