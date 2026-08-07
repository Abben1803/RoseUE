// ROSE job (class) + skill runtime for ARoseCharacter — data-driven from the
// skills/jobs/skill_points DataTables (tools/gen_skill_tables.py), faithful to
// the server logic in src/sho_gameserver/src/common/cuserdata.cpp (learn/level
// checks), src/sho_gameserver/src/cobjchar.cpp (Skill_START dispatch,
// Skill_ApplyIngSTATUS) and src/common/calculation.cpp (skill damage / adjust
// value — transcribed in RoseStatFormulas.h).
//
// Scope (single-player dev build): ACTIVE damage skills (types 3/4/5 melee,
// 6 bolt, 7/17 area) hit monsters/dummies through the same reach query as the
// basic attack; SELF buffs (types 8/10/12) and friendly target buffs (9/11/13,
// applied to self — there is no other friendly target yet) become timed
// ING-status bonuses feeding ComputeDerivedStats; PASSIVES (type 15) feed it
// permanently once learned.  Hostile status effects on monsters, summons (14),
// warps (18) and window/emote types are out of scope and fail with a message.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RoseSkillTypes.h"
#include "RoseSkillComponent.generated.h"

class UDataTable;
class ARoseCharacter;

// A running buff/debuff on the owner: one applied ING status (eING_TYPE,
// datatype.h:703).  Value is the FLAT amount computed once at cast time by
// Get_SkillAdjustVALUE — the server stores the resolved value too
// (CObjCHAR::Skill_ApplyIngSTATUS → UpdateIngSTATUS, cobjchar.cpp:1127-1139).
USTRUCT()
struct FRoseActiveBuff
{
	GENERATED_BODY()

	UPROPERTY() int32 StatusId = 0;     // LIST_STATUS row
	UPROPERTY() int32 StatusType = 0;   // resolved STATE_TYPE (eING_TYPE)
	UPROPERTY() int32 Value = 0;        // flat amount (sign per INC/DEC type)
	UPROPERTY() float EndTime = 0.f;    // world seconds (0 = no expiry)
	UPROPERTY() int32 SourceSkill = 0;  // skill row id (for HUD/dedupe)
	// The looping on-character visual (LIST_STATUS STATE_STEP_EFFECT) — killed
	// when the buff expires or is replaced.  Not reflected: runtime-only.
	TWeakObjectPtr<class ARoseEffect> FxActor;
};

UCLASS(ClassGroup = (Rose), meta = (BlueprintSpawnableComponent))
class ROSEUE_API URoseSkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// LIST_STATUS visual for a status id (STATE_STEP_EFFECT → FILE_EFFECT row;
	// 0 = none).  OutBad: 0 good / 1 bad / 2 other.  Shared by the player's buff
	// visuals and the monster-debuff visuals (status_effects.json).
	static int32 GetStatusFx(int32 StatusId, int32* OutBad = nullptr);

	URoseSkillComponent();

	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;

	// ── Job (class).  ROSE job = the AT_CLASS ability (datatype.h:531): 0
	// Visitor, 111 Soldier, 121 Knight, 122 Champion, 211 Muse, 221 Mage,
	// 222 Cleric, 311 Hawker, 411 Dealer, ... (CLASS_* defines, datatype.h:695
	// region).  The live server changes it through quest rewards; SetJob is the
	// dev path.
	UPROPERTY(EditAnywhere, Category = "Rose|Job") int32 CurrentJob = 0;
	// Dev default so skills can be learned immediately; the faithful source is
	// the per-level LIST_SKILL_P grants (see OnLevelUp).
	UPROPERTY(EditAnywhere, Category = "Rose|Job") int32 SkillPoints = 100;

	// skills.csv / jobs.csv / skill_points.csv DataTables; auto-loaded by path
	// in BeginPlay when unset (same pattern as the item tables).
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* SkillTable = nullptr;
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* JobTable = nullptr;
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* SkillPointTable = nullptr;

	// Learned skills: skill line (BaseSkillId) -> the CURRENTLY learned row id
	// of that line (each level is its own row — CUserDATA stores it the same
	// way and matches lines via SKILL_1LEV_INDEX, cuserdata.cpp:1209-1223).
	UPROPERTY() TMap<int32, int32> Learned;

	// Hotbar: 12 slots (keys 1-9, 0, -, =) holding learned ACTIVE skill row
	// ids; -1 empty.
	// TWO bars of 12: bar 1 on 1..= and bar 2 on Alt+1..Alt+=.  Slots are one
	// flat array, bar N owning [N*SlotsPerBar, (N+1)*SlotsPerBar).
	static constexpr int32 SlotsPerBar = 12;
	static constexpr int32 HotbarBars  = 2;
	static constexpr int32 HotbarSlots = SlotsPerBar * HotbarBars;

	/** Key label for a slot: "3" on bar 1, "Alt+3" on bar 2. */
	static FString HotbarKeyLabel(int32 Slot);
	/** 1-based bar a slot belongs to. */
	static int32 HotbarBarOf(int32 Slot) { return Slot / SlotsPerBar + 1; }
	UPROPERTY() TArray<int32> Hotbar;

	// Bumped whenever the learned set changes (learn / level-up) so UI lists
	// know to rebuild without polling every field.
	UPROPERTY() int32 SkillRevision = 0;

	const FRoseSkillRow* GetSkillRow(int32 SkillId) const;
	const FRoseJobRow*   GetJobRow(int32 ClassSetId) const;
	FString SkillLabel(int32 SkillId) const;   // "Double Attack Lv2 (id 322)"

	// ── Learning (faithful) ────────────────────────────────────────────────
	// CUserDATA::Check_JobCollection (cuserdata.cpp:854): pass when the class
	// set's job list is empty, else when it contains CurrentJob (first 8
	// entries — CLASS_INCLUDE_JOB_CNT, stb.h:614 — stopping at the first 0).
	bool JobInClassSet(int32 ClassSetId) const;
	// Skill_FindLearnedLevel (cuserdata.cpp:1197): learned level of a line, -1 none.
	int32 LearnedLevelOfLine(int32 BaseSkillId) const;
	int32 LearnedIdOfLine(int32 BaseSkillId) const;
	// The next learnable row of a line (its base row when unlearned, else the
	// level above the learned one) — **-1 when the line is maxed**.  Row ids are
	// consecutive ACROSS lines, so a bare base+level off the end of a line lands
	// on the NEXT skill's base row; every "level this line up" path must use
	// this instead of computing the id itself.
	int32 NextLevelIdOfLine(int32 BaseSkillId) const;
	bool IsLearned(int32 SkillId) const;
	// Skill_LearnCondition (cuserdata.cpp:1289) for level-1 rows /
	// Skill_LevelUpCondition (cuserdata.cpp:1325) for higher rows:
	// points → job → prerequisite skills → required abilities, in that order.
	bool CanLearn(int32 SkillId, FString& OutReason) const;
	// Learn/level the skill: deducts PointCost (Skill_LEARN, cuserdata.cpp:1554)
	// and re-derives stats (passives take effect immediately).
	bool LearnSkill(int32 SkillId, bool bIgnoreChecks = false);
	void SetJob(int32 JobId);   // dev path; drops no skills (server keeps them too)
	/** Give the free BASIC skills (Sit / Pick Up / Jump / Drive Cart / Castle
	 *  Gear / Ride Request) — SkillLevel 0, cost 0, everyone has them. */
	void GrantBasicSkills();

	// ── Casting ────────────────────────────────────────────────────────────
	void SetHotbarSlot(int32 Slot, int32 SkillId);
	void CastHotbar(int32 Slot);
	// Full use-condition chain (Skill_ActionCondition, cuserdata.cpp:1373:
	// consumables + required weapon) + cooldown, then plays the motion and
	// applies the effect at the swing's contact point.
	bool CastSkill(int32 SkillId);

	// ── Stat feed (read by ARoseCharacter::ComputeDerivedStats) ────────────
	// Sum of learned PASSIVE skills' flat IncValue for an ability id, and the
	// summed IncRate percent (Skill_LEARN passive branch, cuserdata.cpp:1560+).
	int32 PassiveFlat(int32 Ability) const;

	// Capacity a summonable mob consumes (LIST_NPC col 21 / SellTab0).
	int32 SummonCapacityCost(int32 NpcId) const;
	int32 PassiveRate(int32 Ability) const;
	// Net running-buff bonus for an ING "increase" type: +Value for IncType
	// buffs, -Value for the paired IncType+1 "decrease" type (eING_TYPE pairs,
	// datatype.h:716-731).
	int32 BuffNet(int32 IncType) const;
	// Weapon-class passive attack power / speed (GetPassiveSkillAttackPower /
	// ...Speed, cuserdata.cpp:1449-1542): flat + CurValue*rate%, selected by the
	// equipped weapon's STB Type (0 = bare hands).
	int32 PassiveAttackPowerBonus(int32 CurAttackPower, int32 WeaponType) const;
	int32 PassiveAttackSpeedBonus(int32 CurAttackSpeed, int32 WeaponType) const;

	// Skill points granted on reaching Level (LIST_SKILL_P / gs_user.cpp:630);
	// call from a future level-up flow.
	void OnLevelUp(int32 NewLevel);

	// Seconds of cooldown left on a skill (0 = ready) + its full duration —
	// for the quickbar's cooldown sweep (RoseUIHud.cpp).
	float GetCooldownRemaining(int32 SkillId) const;

	UPROPERTY() TArray<FRoseActiveBuff> Buffs;

	FString DescribeState() const;   // multi-line debug dump (job/points/skills/buffs)

private:
	ARoseCharacter* OwnerChar() const;

	// Cooldowns: end-time (world seconds) per reuse key.  Group > 0 shares one
	// timer across the group (SKILL_RELOAD_TYPE, io_skill.h:111-113); group 0
	// cools per skill line.
	TMap<int32, float> CooldownEnd;
	int32 CooldownKey(const FRoseSkillRow& Row) const;

	// Pending effect at the swing's contact point (like the monster's
	// AttackHitTimer): applied at 45% of the motion.
	int32 PendingSkill = -1;
	float PendingTimer = -1.f;

	// Cast range (cm, centre-to-centre) a targeted offensive skill demands:
	// bolt/area use the row's Range/Radius, melee types fall back to weapon reach.
	float SkillCastRange(const FRoseSkillRow& Row) const;

	void ApplySkillEffect(const FRoseSkillRow& Row);
	void ApplyDamageSkill(const FRoseSkillRow& Row);   // types 3/4/5/6/7/17/19
	void ApplySelfStatus(const FRoseSkillRow& Row);    // types 8/9/10/11/12/13 (self)
	bool CheckUseCosts(const FRoseSkillRow& Row, FString& OutReason) const;
	void ConsumeUseCosts(const FRoseSkillRow& Row);
	bool CheckNeedWeapon(const FRoseSkillRow& Row) const;
	void Msg(const FString& Text, const FColor& Color = FColor::Cyan) const;
};
