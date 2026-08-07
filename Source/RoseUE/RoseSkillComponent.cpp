// ROSE job + skill runtime — see RoseSkillComponent.h for scope and the
// server-source citations. Everything reads the skills/jobs DataTables; no
// per-skill code.
#include "RoseSkillComponent.h"

#include "RoseBullet.h"
#include "RoseCharacter.h"
#include "RoseNpcTypes.h"
#include "RoseEffect.h"
#include "RoseMonster.h"
#include "RoseSkillFX.h"
#include "RoseTargetDummy.h"
#include "RoseStatFormulas.h"
#include "RoseSoundData.h"
#include "RoseItemTypes.h"
#include "Components/CapsuleComponent.h"
#include "Engine/DataTable.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "DrawDebugHelpers.h"

// eING_TYPE values we act on (datatype.h:703).
static constexpr int32 ING_INC_HP = 1, ING_INC_MP = 2;
static constexpr int32 ING_CHECK_START = 6, ING_CHECK_END = 28;

// ── status visuals (status_effects.json: LIST_STATUS fx/bad/symbol) ──────────
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

int32 URoseSkillComponent::GetStatusFx(int32 StatusId, int32* OutBad)
{
	struct FStatusFx { int32 Fx = 0; int32 Bad = 0; };
	static TMap<int32, FStatusFx> Table;
	static bool bLoaded = false;
	if (!bLoaded)
	{
		bLoaded = true;
		FString Raw;
		if (FFileHelper::LoadFileToString(Raw,
				*(FPaths::ProjectContentDir() / TEXT("DataTables/status_effects.json"))))
		{
			TSharedPtr<FJsonObject> Root;
			TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
			if (FJsonSerializer::Deserialize(R, Root) && Root.IsValid())
				for (const TPair<FString, TSharedPtr<FJsonValue>>& It : Root->Values)
					if (const TSharedPtr<FJsonObject> O = It.Value->AsObject())
						Table.Add(FCString::Atoi(*It.Key),
							{ (int32)O->GetNumberField(TEXT("fx")),
							  (int32)O->GetNumberField(TEXT("bad")) });
			UE_LOG(LogTemp, Log, TEXT("[Rose] %d status visuals loaded"), Table.Num());
		}
	}
	if (const FStatusFx* S = Table.Find(StatusId))
	{
		if (OutBad) *OutBad = S->Bad;
		return S->Fx;
	}
	return 0;
}

URoseSkillComponent::URoseSkillComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void URoseSkillComponent::BeginPlay()
{
	Super::BeginPlay();

	if (!SkillTable)
		SkillTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/skills.skills"));
	if (!JobTable)
		JobTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/jobs.jobs"));
	if (!SkillPointTable)
		SkillPointTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/skill_points.skill_points"));

	// 12 slots = one quickbar (keys 1-9, 0, -, =).
	if (Hotbar.Num() < HotbarSlots)
		Hotbar.Init(-1, HotbarSlots);

	GrantBasicSkills();
}

// ROSE's BASIC skills — everyone has them from creation.
//
// They are stored at **SkillLevel 0** (not 1) with PointCost 0 and class 0, so
// they are free and unconditional; the client simply starts you with them.  A
// character had none of these because nothing ever granted a starting set, which
// is why sitting, picking up and riding were unreachable no matter what the tree
// showed.
//
// Ids are from LIST_SKILL and are stable: this is the same fixed set the client
// hardcodes.  Anything a player EARNS (skill books, quest rewards) goes through
// LearnSkill normally and does not belong here.
void URoseSkillComponent::GrantBasicSkills()
{
	// Only the authority owns the learned set; clients receive it.
	AActor* Owner = GetOwner();
	if (Owner && !Owner->HasAuthority())
		return;

	static const int32 kBasicSkills[] = {
		11,   // Sit
		12,   // Pick Up
		13,   // Jump
		17,   // Drive Cart
		24,   // Drive Castle Gear
		25,   // Ride Request
	};

	int32 Granted = 0;
	for (int32 Id : kBasicSkills)
	{
		const FRoseSkillRow* Row = GetSkillRow(Id);
		if (!Row)
			continue;                       // absent in this client's table
		if (LearnedLevelOfLine(Row->BaseSkillId) >= 0)
			continue;                       // already have it (loaded save)
		// bIgnoreChecks: these cost nothing and gate on nothing, and running the
		// full condition chain here would depend on job/stat state that is not
		// set up yet at BeginPlay.
		if (LearnSkill(Id, /*bIgnoreChecks*/ true))
			++Granted;
	}

	if (Granted > 0)
		UE_LOG(LogTemp, Log, TEXT("[Rose] granted %d basic skill(s)"), Granted);
}

ARoseCharacter* URoseSkillComponent::OwnerChar() const
{
	return Cast<ARoseCharacter>(GetOwner());
}

const FRoseSkillRow* URoseSkillComponent::GetSkillRow(int32 SkillId) const
{
	if (!SkillTable || SkillId <= 0) return nullptr;
	return SkillTable->FindRow<FRoseSkillRow>(
		FName(*FString::Printf(TEXT("skill_%d"), SkillId)), TEXT("skill"), false);
}

const FRoseJobRow* URoseSkillComponent::GetJobRow(int32 ClassSetId) const
{
	if (!JobTable || ClassSetId < 0) return nullptr;
	return JobTable->FindRow<FRoseJobRow>(
		FName(*FString::Printf(TEXT("job_%d"), ClassSetId)), TEXT("job"), false);
}

FString URoseSkillComponent::SkillLabel(int32 SkillId) const
{
	const FRoseSkillRow* R = GetSkillRow(SkillId);
	if (!R) return FString::Printf(TEXT("skill %d"), SkillId);
	const FString N = R->DisplayName.IsEmpty() ? R->SkillName : R->DisplayName;
	return FString::Printf(TEXT("%s Lv%d (id %d)"), *N, FMath::Max(1, R->SkillLevel), SkillId);
}

void URoseSkillComponent::Msg(const FString& Text, const FColor& Color) const
{
	if (GEngine)
		GEngine->AddOnScreenDebugMessage(-1, 3.f, Color, Text);
	UE_LOG(LogTemp, Log, TEXT("[Rose][skill] %s"), *Text);
}

// ── Learning ─────────────────────────────────────────────────────────────────

// CUserDATA::Check_JobCollection (cuserdata.cpp:854): empty first slot = pass;
// scan CLASS_INCLUDE_JOB_CNT (8) entries, stop at the first 0 (= fail), pass on
// a match with the current job.
bool URoseSkillComponent::JobInClassSet(int32 ClassSetId) const
{
	const FRoseJobRow* Row = GetJobRow(ClassSetId);
	if (!Row) return true;   // no such set = ungated (matches empty-set pass)
	const int32 Jobs[8] = { Row->Job1, Row->Job2, Row->Job3, Row->Job4,
	                        Row->Job5, Row->Job6, Row->Job7, Row->Job8 };
	if (Jobs[0] == 0) return true;
	for (int32 i = 0; i < 8; ++i)
	{
		if (Jobs[i] == 0) return false;
		if (Jobs[i] == CurrentJob) return true;
	}
	return false;
}

int32 URoseSkillComponent::LearnedIdOfLine(int32 BaseSkillId) const
{
	const int32* Found = Learned.Find(BaseSkillId);
	return Found ? *Found : -1;
}

int32 URoseSkillComponent::LearnedLevelOfLine(int32 BaseSkillId) const
{
	const FRoseSkillRow* R = GetSkillRow(LearnedIdOfLine(BaseSkillId));
	return R ? R->SkillLevel : -1;   // Skill_FindLearnedLevel returns -1 too
}

int32 URoseSkillComponent::NextLevelIdOfLine(int32 BaseSkillId) const
{
	const int32 Have = LearnedLevelOfLine(BaseSkillId);          // -1 = unlearned
	const int32 NextId = BaseSkillId + FMath::Max(0, Have);      // base+0 / base+N
	const FRoseSkillRow* Next = GetSkillRow(NextId);
	// The row after a line's last level is the NEXT line's base — same-line check.
	return (Next && Next->BaseSkillId == BaseSkillId) ? NextId : -1;
}

bool URoseSkillComponent::IsLearned(int32 SkillId) const
{
	const FRoseSkillRow* R = GetSkillRow(SkillId);
	return R && LearnedIdOfLine(R->BaseSkillId) == SkillId;
}

// Skill_LearnCondition / Skill_LevelUpCondition (cuserdata.cpp:1289 / 1325).
bool URoseSkillComponent::CanLearn(int32 SkillId, FString& OutReason) const
{
	const FRoseSkillRow* Row = GetSkillRow(SkillId);
	if (!Row) { OutReason = TEXT("no such skill"); return false; }

	const int32 HaveLevel = LearnedLevelOfLine(Row->BaseSkillId);
	if (Row->SkillLevel > 1)
	{
		// Level-up path: must own exactly the previous level of the same line
		// (SKILL_1LEV_INDEX equal && level+1, cuserdata.cpp:1337-1341).
		if (HaveLevel != Row->SkillLevel - 1)
		{
			OutReason = FString::Printf(TEXT("needs %s at Lv%d first"),
				*SkillLabel(Row->BaseSkillId), Row->SkillLevel - 1);
			return false;
		}
	}
	else if (HaveLevel >= 0)
	{
		OutReason = TEXT("already learned");   // cuserdata.cpp:1295
		return false;
	}

	// 1. skill points (cuserdata.cpp:1299)
	if (SkillPoints < Row->PointCost)
	{
		OutReason = FString::Printf(TEXT("needs %d skill points (have %d)"),
			Row->PointCost, SkillPoints);
		return false;
	}

	// 2. job (Skill_CheckJOB, cuserdata.cpp:1227; union gates unmodeled — the
	// classic skills we target leave them 0)
	if (!JobInClassSet(Row->RequiredClassSet))
	{
		const FRoseJobRow* Set = GetJobRow(Row->RequiredClassSet);
		OutReason = FString::Printf(TEXT("requires class %s (current job %d)"),
			Set ? *Set->JobName : TEXT("?"), CurrentJob);
		return false;
	}

	// 3. prerequisite skills (Skill_CheckLearnedSKILL, cuserdata.cpp:1251:
	// stop at the first 0 entry; compare owned line level)
	const int32 Needs[3][2] = { { Row->NeedSkill1, Row->NeedSkillLevel1 },
	                            { Row->NeedSkill2, Row->NeedSkillLevel2 },
	                            { Row->NeedSkill3, Row->NeedSkillLevel3 } };
	for (int32 i = 0; i < 3; ++i)
	{
		if (Needs[i][0] == 0) break;
		const FRoseSkillRow* Need = GetSkillRow(Needs[i][0]);
		const int32 Have = Need ? LearnedLevelOfLine(Need->BaseSkillId) : -1;
		if (Have < Needs[i][1])
		{
			OutReason = FString::Printf(TEXT("requires %s Lv%d"),
				*SkillLabel(Needs[i][0]), Needs[i][1]);
			return false;
		}
	}

	// 4. required abilities (Skill_CheckNeedABILITY, cuserdata.cpp:1269)
	ARoseCharacter* Char = OwnerChar();
	const int32 Abil[2][2] = { { Row->NeedAbility1, Row->NeedAbilityValue1 },
	                           { Row->NeedAbility2, Row->NeedAbilityValue2 } };
	for (int32 i = 0; i < 2 && Char; ++i)
	{
		if (Abil[i][0] == 0) break;
		if (Char->GetAbilityValue(Abil[i][0]) < Abil[i][1])
		{
			OutReason = FString::Printf(TEXT("requires ability %d >= %d"),
				Abil[i][0], Abil[i][1]);
			return false;
		}
	}

	return true;
}

bool URoseSkillComponent::LearnSkill(int32 SkillId, bool bIgnoreChecks)
{
	const FRoseSkillRow* Row = GetSkillRow(SkillId);
	if (!Row) { Msg(FString::Printf(TEXT("learn: no skill %d"), SkillId), FColor::Red); return false; }

	FString Reason;
	if (!bIgnoreChecks && !CanLearn(SkillId, Reason))
	{
		Msg(FString::Printf(TEXT("cannot learn %s: %s"), *SkillLabel(SkillId), *Reason), FColor::Red);
		return false;
	}

	// Skill_LEARN (cuserdata.cpp:1548): deduct points, store the new level's row
	// in the line's slot; passives re-derive stats immediately.
	SkillPoints = FMath::Max(0, SkillPoints - Row->PointCost);
	Learned.Add(Row->BaseSkillId, SkillId);
	++SkillRevision;   // UI lists rebuild on change (learn/level-up)

	// Keep a line that is ALREADY on the bar pointing at its new level — but do
	// NOT place a newly learned skill on the bar.
	//
	// The bar is the player's layout to arrange; auto-filling the first free slot
	// took that decision away and scattered skills into binds nobody chose.  The
	// upgrade below is different: it is not placing anything, it is stopping a
	// slot the player DID choose from still casting the old, weaker level.
	if (Row->SkillType != 15)
	{
		for (int32 i = 0; i < Hotbar.Num(); ++i)
			if (Hotbar[i] > 0)
				if (const FRoseSkillRow* H = GetSkillRow(Hotbar[i]))
					if (H->BaseSkillId == Row->BaseSkillId)
						Hotbar[i] = SkillId;
	}

	if (ARoseCharacter* Char = OwnerChar())
		Char->ApplyDerivedStats();

	Msg(FString::Printf(TEXT("learned %s  (%d pts left)"), *SkillLabel(SkillId), SkillPoints),
		FColor::Green);
	return true;
}

void URoseSkillComponent::SetJob(int32 JobId)
{
	CurrentJob = JobId;
	Msg(FString::Printf(TEXT("job set to %d"), JobId), FColor::Yellow);
}

void URoseSkillComponent::OnLevelUp(int32 NewLevel)
{
	// LIST_SKILL_P grant for the reached level (gs_user.cpp:630-637).
	if (!SkillPointTable) return;
	if (const FRoseSkillPointRow* Row = SkillPointTable->FindRow<FRoseSkillPointRow>(
			FName(*FString::Printf(TEXT("sp_%d"), NewLevel)), TEXT("sp"), false))
	{
		SkillPoints += Row->Points;
		Msg(FString::Printf(TEXT("+%d skill points (level %d)"), Row->Points, NewLevel));
	}
}

// ── Casting ──────────────────────────────────────────────────────────────────

// The 12 keys ROSE puts under a bar, in order.
FString URoseSkillComponent::HotbarKeyLabel(int32 Slot)
{
	static const TCHAR* kKeys[SlotsPerBar] = {
		TEXT("1"), TEXT("2"), TEXT("3"), TEXT("4"), TEXT("5"), TEXT("6"),
		TEXT("7"), TEXT("8"), TEXT("9"), TEXT("0"), TEXT("-"), TEXT("=") };
	if (Slot < 0 || Slot >= HotbarSlots)
		return FString();
	const FString Key = kKeys[Slot % SlotsPerBar];
	return HotbarBarOf(Slot) == 1 ? Key : FString(TEXT("Alt+")) + Key;
}

void URoseSkillComponent::SetHotbarSlot(int32 Slot, int32 SkillId)
{
	if (Hotbar.Num() < HotbarSlots) Hotbar.Init(-1, HotbarSlots);
	if (Slot < 0 || Slot >= Hotbar.Num()) return;
	Hotbar[Slot] = SkillId;
	Msg(FString::Printf(TEXT("hotbar %d = %s"), Slot + 1,
		SkillId > 0 ? *SkillLabel(SkillId) : TEXT("(empty)")));
}

void URoseSkillComponent::CastHotbar(int32 Slot)
{
	if (Slot < 0 || Slot >= Hotbar.Num() || Hotbar[Slot] <= 0)
	{
		Msg(FString::Printf(TEXT("hotbar %d empty (RoseHotbar <slot> <skillid> to assign)"), Slot + 1));
		return;
	}
	CastSkill(Hotbar[Slot]);
}

float URoseSkillComponent::GetCooldownRemaining(int32 SkillId) const
{
	const FRoseSkillRow* Row = GetSkillRow(SkillId);
	if (!Row || !GetWorld())
		return 0.f;
	if (const float* End = CooldownEnd.Find(CooldownKey(*Row)))
		return FMath::Max(0.f, *End - GetWorld()->GetTimeSeconds());
	return 0.f;
}

int32 URoseSkillComponent::CooldownKey(const FRoseSkillRow& Row) const
{
	// Group cooldowns are shared across the group (SKILL_RELOAD_TYPE 1..15,
	// io_skill.h:111-113); group 0 skills cool per skill line.
	return Row.CooldownGroup > 0 ? Row.CooldownGroup : 0x10000 + Row.BaseSkillId;
}

// Skill_ActionCondition part 1 (cuserdata.cpp:1396): every listed consumable
// must be affordable.  We model AT_HP(16)/AT_MP(17); others pass (unmodeled).
bool URoseSkillComponent::CheckUseCosts(const FRoseSkillRow& Row, FString& OutReason) const
{
	ARoseCharacter* Char = OwnerChar();
	if (!Char) return false;
	const int32 Use[2][2] = { { Row.UseAbility1, Row.UseAmount1 },
	                          { Row.UseAbility2, Row.UseAmount2 } };
	for (int32 i = 0; i < 2; ++i)
	{
		if (Use[i][0] == 0) break;
		if (Use[i][0] == 17 && Char->CurrentMP < Use[i][1])
		{ OutReason = FString::Printf(TEXT("needs %d MP"), Use[i][1]); return false; }
		if (Use[i][0] == 16 && Char->CurrentHP <= Use[i][1])
		{ OutReason = FString::Printf(TEXT("needs %d HP"), Use[i][1]); return false; }
	}
	return true;
}

// Skill_UseAbilityValue (cuserdata.cpp:1091): subtract the costs.  (The MP-save
// passive discount, GetCur_RateUseMP, is unmodeled — full cost.)
void URoseSkillComponent::ConsumeUseCosts(const FRoseSkillRow& Row)
{
	ARoseCharacter* Char = OwnerChar();
	if (!Char) return;
	const int32 Use[2][2] = { { Row.UseAbility1, Row.UseAmount1 },
	                          { Row.UseAbility2, Row.UseAmount2 } };
	for (int32 i = 0; i < 2; ++i)
	{
		if (Use[i][0] == 0) break;
		if (Use[i][0] == 17) Char->CurrentMP = FMath::Max(0.f, Char->CurrentMP - Use[i][1]);
		if (Use[i][0] == 16) Char->CurrentHP = FMath::Max(1.f, Char->CurrentHP - Use[i][1]);
	}
}

// Skill_ActionCondition part 2 (cuserdata.cpp:1426): first 0 in slot 0 = no
// weapon needed; otherwise the equipped weapon's STB Type must match a slot
// (a 0 after slot 0 ends the list = fail).
bool URoseSkillComponent::CheckNeedWeapon(const FRoseSkillRow& Row) const
{
	ARoseCharacter* Char = OwnerChar();
	const int32 WType = Char ? Char->GetEquippedWeaponType() : 0;
	const int32 Need[5] = { Row.NeedWeapon1, Row.NeedWeapon2, Row.NeedWeapon3,
	                        Row.NeedWeapon4, Row.NeedWeapon5 };
	for (int32 i = 0; i < 5; ++i)
	{
		if (Need[i] == 0)
			return i == 0;   // slot 0 empty = unrestricted; later 0 = end of list
		if (WType == Need[i])
			return true;
	}
	return false;
}

bool URoseSkillComponent::CastSkill(int32 SkillId)
{
	const FRoseSkillRow* Row = GetSkillRow(SkillId);
	ARoseCharacter* Char = OwnerChar();
	if (!Row || !Char) return false;

	if (!IsLearned(SkillId))
	{ Msg(FString::Printf(TEXT("%s is not learned"), *SkillLabel(SkillId)), FColor::Red); return false; }

	// Only the action types this runtime implements (Skill_START dispatch,
	// cobjchar.cpp:1365).
	static const TSet<int32> Castable = { 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 17, 19 };
	if (Row->SkillType == 15)
	{ Msg(TEXT("passive skills are always on")); return false; }
	if (!Castable.Contains(Row->SkillType))
	{ Msg(FString::Printf(TEXT("skill type %d not supported yet"), Row->SkillType)); return false; }

	// SUMMON capacity (skill.cpp:1189).  Checked BEFORE cooldown and cost so a
	// refused summon does not burn either — the client refuses the same way.
	if (Row->SkillType == 14)
	{
		if (Row->SummonPet <= 0)
		{ Msg(FString::Printf(TEXT("%s has no summon mob"), *SkillLabel(SkillId)), FColor::Red); return false; }

		const int32 Cost = SummonCapacityCost(Row->SummonPet);
		const int32 Used = Char->GetSummonUsedCapacity();
		const int32 Max  = Char->GetSummonMaxCapacity();
		if (Cost + Used > Max)
		{
			// STR_CANT_SUMMON_NPC
			Msg(FString::Printf(TEXT("Not enough summon capacity (%d needed, %d/%d used)"),
				Cost, Used, Max), FColor::Red);
			return false;
		}
	}

	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (const float* End = CooldownEnd.Find(CooldownKey(*Row)))
		if (*End > Now)
		{
			Msg(FString::Printf(TEXT("%s on cooldown (%.1fs)"), *SkillLabel(SkillId), *End - Now));
			return false;
		}

	FString Reason;
	if (!CheckUseCosts(*Row, Reason))
	{ Msg(FString::Printf(TEXT("%s: %s"), *SkillLabel(SkillId), *Reason), FColor::Red); return false; }
	if (!CheckNeedWeapon(*Row))
	{ Msg(FString::Printf(TEXT("%s: wrong weapon equipped"), *SkillLabel(SkillId)), FColor::Red); return false; }
	if (PendingSkill > 0)
		return false;   // a cast is already in flight

	// Targeted offensive skills (melee 3/4/5/19, bolt 6, area-at-target 7) need
	// the click-target inside the skill's range and the caster facing it — ROSE
	// runs into range first, then casts.  Type 17 hits around SELF (no target);
	// buffs (8-13) are self/friendly and skip this entirely.  Out of range costs
	// nothing: the approach re-issues this cast once close enough.
	const bool bTargeted = (Row->SkillType == 3 || Row->SkillType == 4 || Row->SkillType == 5
		|| Row->SkillType == 6 || Row->SkillType == 7 || Row->SkillType == 19);
	if (bTargeted)
	{
		ARoseMonster* Tgt = Char->GetTargetMonster();
		if (!Tgt || Tgt->IsDead())
		{ Msg(FString::Printf(TEXT("%s: no target — click a monster first"), *SkillLabel(SkillId)), FColor::Red); return false; }
		const float Range = SkillCastRange(*Row);
		const float Dist = FVector::Dist2D(Char->GetActorLocation(), Tgt->GetActorLocation());
		if (Dist > Range)
		{
			Char->RequestSkillApproach(SkillId, Range - 20.f);   // stop inside the edge
			return false;
		}
		// Face the target for the cast (and for the frontal damage query).
		const FVector To = Tgt->GetActorLocation() - Char->GetActorLocation();
		Char->SetActorRotation(FRotator(0.f, To.GetSafeNormal2D().Rotation().Yaw, 0.f));
	}

	// Motion: the skill's own action/casting anim (ActionMotionSpeed is percent,
	// io_skill.h:169); PlaySkillMotion falls back to the basic swing if the
	// skill's ZMO isn't imported for this gender/weapon.
	const float Dur = Char->PlaySkillMotion(
		Row->ActionMotion, Row->CastMotion,
		Row->ActionMotionSpeed > 0 ? (float)Row->ActionMotionSpeed : 100.f);

	ConsumeUseCosts(*Row);
	CooldownEnd.Add(CooldownKey(*Row), Now + Row->CooldownSec);

	if (Row->FireSound > 0)
		RosePlaySoundId(GetWorld(), Row->FireSound, Char->GetActorLocation());

	// Cast effect: the FAITHFUL converted .EFT (SKILL_CASTING_EFFECT slot 0)
	// riding the caster; generic flash only when the effect isn't converted.
	if (!ARoseEffect::SpawnById(GetWorld(), Row->CastEffect,
			Char->GetActorLocation(), Char))
	{
		const bool bBuffish = (Row->SkillType >= 8 && Row->SkillType <= 13);
		ARoseSkillFX::Spawn(GetWorld(), Char->GetActorLocation() + FVector(0, 0, 60.f),
			ERoseFXStyle::CastBurst,
			bBuffish ? ARoseSkillFX::BuffColor()
			         : ARoseSkillFX::ColorForDamageType(Row->DamageType),
			1.f, 0.5f);
	}

	// Apply at the contact point (~45% of the motion, like the monster swing);
	// no anim = apply immediately.
	if (Dur > 0.f)
	{
		PendingSkill = SkillId;
		PendingTimer = Dur * 0.45f;
	}
	else
		ApplySkillEffect(*Row);

	return true;
}

void URoseSkillComponent::TickComponent(float DeltaTime, ELevelTick TickType,
	FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (PendingTimer >= 0.f)
	{
		PendingTimer -= DeltaTime;
		if (PendingTimer < 0.f)
		{
			const int32 Id = PendingSkill;
			PendingSkill = -1;
			if (const FRoseSkillRow* Row = GetSkillRow(Id))
				ApplySkillEffect(*Row);
		}
	}

	// Expire buffs; a change re-derives the owner's stats.
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	bool bExpired = false;
	for (int32 i = Buffs.Num() - 1; i >= 0; --i)
		if (Buffs[i].EndTime > 0.f && Buffs[i].EndTime <= Now)
		{
			Msg(FString::Printf(TEXT("buff expired (%s)"), *SkillLabel(Buffs[i].SourceSkill)));
			if (Buffs[i].FxActor.IsValid())
				Buffs[i].FxActor->Destroy();
			Buffs.RemoveAt(i);
			bExpired = true;
		}
	if (bExpired)
		if (ARoseCharacter* Char = OwnerChar())
			Char->ApplyDerivedStats();
}

float URoseSkillComponent::SkillCastRange(const FRoseSkillRow& Row) const
{
	ARoseCharacter* Char = OwnerChar();
	switch (Row.SkillType)
	{
	case 6:   // bolt — the row's SKILL_DISTANCE
		return FMath::Max(400.f, (float)Row.Range);
	case 7:   // area at target — cast range is the row's distance too
		return FMath::Max(400.f, (float)FMath::Max(Row.Range, Row.Radius));
	case 17:  // around self — no approach needed
		return FMath::Max(400.f, (float)FMath::Max(Row.Range, Row.Radius));
	default:  // melee types (3/4/5/19): authored Range, else the weapon's reach
		return Row.Range > 0 ? FMath::Max(300.f, (float)Row.Range) + 60.f
		                     : (Char ? Char->GetWeaponReach() : 360.f);
	}
}

// Summon capacity cost for a mob: LIST_NPC col 21, which our row exposes as
// SellTab0 because that column is DUAL PURPOSE (NPC_NEED_SUMMON_CNT and
// NPC_SELL_TAB0 are the same index — see stb.h:294).
int32 URoseSkillComponent::SummonCapacityCost(int32 NpcId) const
{
	if (NpcId <= 0)
		return 0;
	UDataTable* T = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/npcs.npcs"));
	if (!T)
		return 0;
	const FRoseNpcRow* Row = T->FindRow<FRoseNpcRow>(
		*FString::Printf(TEXT("npc_%d"), NpcId), TEXT("SummonCost"));
	return Row ? FMath::Max(0, Row->SellTab0) : 0;
}

void URoseSkillComponent::ApplySkillEffect(const FRoseSkillRow& Row)
{
	switch (Row.SkillType)   // Skill_START (cobjchar.cpp:1365)
	{
	case 3: case 4: case 5: case 6: case 7: case 17: case 19:
		ApplyDamageSkill(Row);
		// type 19 also buffs the caster on success (SKILL_TYPE_19 comment,
		// io_skill.h:35) — apply the self side too.
		if (Row.SkillType == 19)
			ApplySelfStatus(Row);
		break;
	case 8: case 10: case 12:
		ApplySelfStatus(Row);
		break;
	case 14:
		// SKILL_ACTION_SUMMON_PET — the mob id is LIST_SKILL col 28.  Capacity
		// was already checked at cast time; TrySummon re-checks on the server,
		// which is the authority.
		if (ARoseCharacter* C = OwnerChar())
			C->TrySummon(Row.SummonPet);
		break;
	case 9: case 11: case 13:
		// Target-bound: with no party/friendly targets yet, friendly buffs land
		// on self; hostile ones (Harm/harmful status) are not applied to mobs.
		if (Row.Harm == 0 && Row.StatusHarmful1 == 0)
			ApplySelfStatus(Row);
		else
			Msg(FString::Printf(TEXT("%s: hostile status effects on monsters not modeled yet"),
				*SkillLabel(Row.Id)));
		break;
	default:
		break;
	}
}

// Types 3/4/5/19 (melee, Skill_START_03_04_05, cobjchar.cpp:1252), 6 (bolt,
// cobjchar.cpp:1383) and 7/17 (area, Skill_DamageToAROUND, cobjchar.cpp:1202).
// Damage = CCal::Get_SkillDAMAGE (RoseStats::SkillDamage), hit count from the
// skill row (the server passes the motion's total attack frames).
void URoseSkillComponent::ApplyDamageSkill(const FRoseSkillRow& Row)
{
	ARoseCharacter* Char = OwnerChar();
	UWorld* World = GetWorld();
	if (!Char || !World) return;

	RoseStats::FSkillAttacker A;
	A.Level = Char->Level;
	A.AttackPower = Char->GetAttackPowerStat();
	A.HitRate = Char->GetHitStat();
	A.Int = Char->Intelligence;
	A.Sense = Char->Sense;
	A.Critical = Char->GetCritStat();

	const bool bBolt = (Row.SkillType == 6);
	const bool bAreaTarget = (Row.SkillType == 7);
	const bool bAreaSelf = (Row.SkillType == 17);
	const float Reach = SkillCastRange(Row);

	const FVector Origin = Char->GetActorLocation();
	const FVector Fwd = Char->GetActorForwardVector();
	const int32 HitCnt = FMath::Max(1, Row.HitCount);

	// Faithful victim selection: with a click-target, melee/bolt strike ONLY
	// the target and type 7 explodes AROUND the target (Skill_DamageToAROUND);
	// type 17 always sweeps around self.  A free cast with no target keeps the
	// old frontal-cone behaviour (bolt = nearest in front).
	ARoseMonster* Tgt = Char->GetTargetMonster();
	if (Tgt && Tgt->IsDead())
		Tgt = nullptr;

	TArray<ARoseMonster*> Victims;
	if (bAreaTarget || bAreaSelf)
	{
		const FVector Center = (bAreaTarget && Tgt) ? Tgt->GetActorLocation() : Origin;
		const float Radius = FMath::Max(250.f, (float)FMath::Max(Row.Radius, bAreaSelf ? Row.Range : 0));
		for (TActorIterator<ARoseMonster> It(World); It; ++It)
			if (*It && !(*It)->IsDead()
				&& FVector::Dist2D((*It)->GetActorLocation(), Center) <= Radius)
				Victims.Add(*It);
		// Area blast at the center: faithful hit .EFT when converted, else the
		// generic ground shockwave.
		if (!ARoseEffect::SpawnById(World, Row.HitEffect, Center))
			ARoseSkillFX::Spawn(World, Center + FVector(0, 0, 20.f), ERoseFXStyle::GroundRing,
				ARoseSkillFX::ColorForDamageType(Row.DamageType), Radius / 250.f, 0.7f);
	}
	else if (Tgt)
	{
		// Single-target; slack for a mob mid-step (CastSkill verified the range).
		if (FVector::Dist2D(Tgt->GetActorLocation(), Origin) <= Reach * 1.2f)
			Victims.Add(Tgt);
	}
	else
	{
		ARoseMonster* Nearest = nullptr;
		float NearestDist = MAX_FLT;
		for (TActorIterator<ARoseMonster> It(World); It; ++It)
		{
			ARoseMonster* M = *It;
			if (!M || M->IsDead()) continue;
			const FVector To = M->GetActorLocation() - Origin;
			const float Dist = To.Size2D();
			if (Dist > Reach || FVector::DotProduct(Fwd, To.GetSafeNormal2D()) < 0.25f)
				continue;
			if (bBolt) { if (Dist < NearestDist) { NearestDist = Dist; Nearest = M; } }
			else Victims.Add(M);
		}
		if (bBolt && Nearest)
			Victims.Add(Nearest);
	}

	// Bolt skills with an authored bullet (SKILL_BULLET_NO → LIST_EFFECT row)
	// fire a homing projectile like the client — the damage and the hit effect
	// land when it arrives, not instantly.
	if (bBolt && Row.BulletNo > 0 && Victims.Num() > 0)
	{
		ARoseMonster* M = Victims[0];
		RoseStats::FSkillDefender D;
		D.Level = M->Level; D.Defense = M->Defense; D.Resist = M->Resist; D.Avoid = M->Avoid;
		const int32 Dmg = RoseStats::SkillDamage(Row.Power, Row.DamageType, A, D, HitCnt);
		ARoseBullet::Fire(World, Char->GetActorLocation() + FVector(0, 0, 90.f),
			M, Row.BulletNo, Dmg > 0 ? Dmg : -1, false);
		return;
	}

	int32 Hits = 0, Misses = 0, LastDmg = 0;
	for (ARoseMonster* M : Victims)
	{
		RoseStats::FSkillDefender D;
		D.Level = M->Level; D.Defense = M->Defense; D.Resist = M->Resist; D.Avoid = M->Avoid;
		const int32 Dmg = RoseStats::SkillDamage(Row.Power, Row.DamageType, A, D, HitCnt);
		if (Dmg <= 0) { M->ShowMiss(); ++Misses; continue; }
		M->ApplyHit((float)Dmg, Fwd, false);
		// Faithful hit .EFT (SKILL_HIT_EFFECT) on the victim; generic pop fallback.
		if (!ARoseEffect::SpawnById(World, Row.HitEffect, M->GetActorLocation(), M))
			ARoseSkillFX::Spawn(World, M->GetActorLocation() + FVector(0, 0, 50.f),
				ERoseFXStyle::HitBurst, ARoseSkillFX::ColorForDamageType(Row.DamageType), 1.f, 0.4f);
		if (Row.HitSound > 0)
			RosePlaySoundId(World, Row.HitSound, M->GetActorLocation());
		LastDmg = Dmg; ++Hits;
	}

	// Target dummies take the same roll (no Resist stat — 0).
	if (!bBolt)
		for (TActorIterator<ARoseTargetDummy> It(World); It; ++It)
		{
			ARoseTargetDummy* T = *It;
			if (!T) continue;
			const FVector To = T->GetActorLocation() - Origin;
			if (To.Size2D() > Reach
				|| FVector::DotProduct(Fwd, To.GetSafeNormal2D()) < (bAreaSelf ? -1.f : 0.25f))
				continue;
			RoseStats::FSkillDefender D;
			D.Level = T->Level; D.Defense = T->Defense; D.Resist = 0; D.Avoid = T->Avoid;
			const int32 Dmg = RoseStats::SkillDamage(Row.Power, Row.DamageType, A, D, HitCnt);
			if (Dmg <= 0) { T->ShowMiss(); ++Misses; continue; }
			T->ApplyHit((float)Dmg, Fwd, false);
			LastDmg = Dmg; ++Hits;
		}

	Msg(FString::Printf(TEXT("%s: hit %d  missed %d  dmg %d"),
		*SkillLabel(Row.Id), Hits, Misses, LastDmg),
		Hits > 0 ? FColor::Yellow : FColor::Silver);
}

// Self-status application — the self path of CObjCHAR::Skill_ApplyIngSTATUS
// (cobjchar.cpp:1064): per (Status, IncAbility) pair, a zero status applies the
// ability directly (HP heal scales via Get_SkillAdjustVALUE with the caster's
// INT, cobjchar.cpp:1094; MP adds the flat value, :1100); a non-zero status
// becomes a timed ING buff whose amount is Get_SkillAdjustVALUE.
void URoseSkillComponent::ApplySelfStatus(const FRoseSkillRow& Row)
{
	ARoseCharacter* Char = OwnerChar();
	if (!Char) return;
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;

	const int32 Pair[2][5] = {
		{ Row.Status1, Row.StatusType1, Row.IncAbility1, Row.IncValue1, Row.IncRate1 },
		{ Row.Status2, Row.StatusType2, Row.IncAbility2, Row.IncValue2, Row.IncRate2 } };

	bool bChanged = false;
	for (int32 k = 0; k < 2; ++k)
	{
		const int32 StatusId = Pair[k][0], SType = Pair[k][1];
		const int32 IncA = Pair[k][2], IncV = Pair[k][3], IncR = Pair[k][4];

		if (StatusId == 0)
		{
			if (IncV == 0 && IncR == 0) continue;
			if (IncA == 16)      // AT_HP heal (cobjchar.cpp:1094)
			{
				const int32 Adj = RoseStats::SkillAdjustValue(
					(int32)Char->CurrentHP, IncR, IncV, Char->Intelligence);
				Char->CurrentHP = FMath::Min((float)Char->GetMaxHPStat(), Char->CurrentHP + Adj);
				DrawDebugString(GetWorld(), Char->GetActorLocation() + FVector(0, 0, 130.f),
					FString::Printf(TEXT("+%d"), Adj), nullptr, FColor::Green, 0.9f, true);
			}
			else if (IncA == 17) // AT_MP restore — flat value (cobjchar.cpp:1100)
				Char->CurrentMP = FMath::Min((float)Char->GetMaxMPStat(), Char->CurrentMP + IncV);
			continue;
		}

		// Instant-proc statuses (ING_INC_HP/MP, datatype.h:707) heal instead of
		// lingering.
		const int32 Adj = RoseStats::SkillAdjustValue(
			Char->GetAbilityValue(IncA) == MAX_int32 ? 0 : Char->GetAbilityValue(IncA),
			IncR, IncV, Char->Intelligence);
		if (SType == ING_INC_HP)
		{
			Char->CurrentHP = FMath::Min((float)Char->GetMaxHPStat(), Char->CurrentHP + Adj);
			bChanged = true;
			continue;
		}
		if (SType == ING_INC_MP)
		{
			Char->CurrentMP = FMath::Min((float)Char->GetMaxMPStat(), Char->CurrentMP + Adj);
			continue;
		}
		if (SType < ING_CHECK_START || SType > ING_CHECK_END)
			continue;   // stun/sleep/etc. states not modeled on the player

		// Re-applying the same status replaces the running instance (the server
		// refuses weaker ones, IsEnableApplay — replacement is the common case).
		for (int32 i = Buffs.Num() - 1; i >= 0; --i)
			if (Buffs[i].StatusId == StatusId)
			{
				if (Buffs[i].FxActor.IsValid())
					Buffs[i].FxActor->Destroy();
				Buffs.RemoveAt(i);
			}
		FRoseActiveBuff B;
		B.StatusId = StatusId;
		B.StatusType = SType;
		B.Value = Adj;
		B.EndTime = Row.Duration > 0 ? Now + Row.Duration : 0.f;
		B.SourceSkill = Row.Id;
		// The classic on-character status visual (LIST_STATUS STATE_STEP_EFFECT)
		// loops on the character while the buff runs.
		if (const int32 Fx = GetStatusFx(StatusId))
		{
			// Anchor at the FEET: ROSE status effects are authored around the
			// model origin (feet), not the capsule centre (chest).
			const FVector Feet = Char->GetActorLocation()
				- FVector(0, 0, Char->GetCapsuleComponent()->GetScaledCapsuleHalfHeight());
			B.FxActor = ARoseEffect::SpawnById(GetWorld(), Fx, Feet,
				Char, Row.Duration > 0 ? (float)Row.Duration : 8.f, /*bForceLink*/ true);
		}
		Buffs.Add(B);
		bChanged = true;
		Msg(FString::Printf(TEXT("%s: +%d (status %d, %ds)"),
			*SkillLabel(Row.Id), Adj, StatusId, Row.Duration), FColor::Green);
	}

	if (bChanged)
	{
		Char->ApplyDerivedStats();
		// The faithful buff visual is the skill's CastEffect (already spawned
		// by CastSkill); only add the generic rising aura when it's missing.
		if (!RoseFX_GetDef(Row.CastEffect))
			ARoseSkillFX::Spawn(GetWorld(), Char->GetActorLocation() + FVector(0, 0, 40.f),
				ERoseFXStyle::BuffAura, ARoseSkillFX::BuffColor(), 1.f, 0.9f, Char);
	}
}

// ── Stat feed ────────────────────────────────────────────────────────────────

int32 URoseSkillComponent::PassiveFlat(int32 Ability) const
{
	int32 Sum = 0;
	for (const TPair<int32, int32>& P : Learned)
		if (const FRoseSkillRow* R = GetSkillRow(P.Value))
			if (R->SkillType == 15)
			{
				if (R->IncAbility1 == Ability) Sum += R->IncValue1;
				if (R->IncAbility2 == Ability) Sum += R->IncValue2;
			}
	return Sum;
}

int32 URoseSkillComponent::PassiveRate(int32 Ability) const
{
	int32 Sum = 0;
	for (const TPair<int32, int32>& P : Learned)
		if (const FRoseSkillRow* R = GetSkillRow(P.Value))
			if (R->SkillType == 15)
			{
				if (R->IncAbility1 == Ability) Sum += R->IncRate1;
				if (R->IncAbility2 == Ability) Sum += R->IncRate2;
			}
	return Sum;
}

int32 URoseSkillComponent::BuffNet(int32 IncType) const
{
	int32 Sum = 0;
	for (const FRoseActiveBuff& B : Buffs)
	{
		if (B.StatusType == IncType)     Sum += B.Value;   // ING_INC_*
		if (B.StatusType == IncType + 1) Sum -= B.Value;   // paired ING_DEC_*
	}
	return Sum;
}

// GetPassiveSkillAttackPower (cuserdata.cpp:1491): the weapon's STB Type picks
// the AT_PSV_ATK_POW_* ability; bonus = flat + curAP * rate%.
int32 URoseSkillComponent::PassiveAttackPowerBonus(int32 CurAttackPower, int32 WeaponType) const
{
	int32 A = 0;
	switch (WeaponType)
	{
	case 0:                     A = 41; break;  // AT_PSV_ATK_POW_NO_WEAPON
	case 211: case 212:         A = 42; break;  // 1H sword/blunt
	case 221: case 222: case 223: A = 43; break; // 2H sword/spear/axe
	case 231:                   A = 44; break;  // bow
	case 232: case 233: case 253: A = 45; break; // gun/launcher/dual gun
	case 241: case 242:         A = 46; break;  // staff/wand
	case 251: case 252:         A = 48; break;  // katar/dual
	case 271:                   A = 47; break;  // crossbow
	default: return 0;
	}
	return PassiveFlat(A) + (int32)(CurAttackPower * PassiveRate(A) / 100.f);
}

// GetPassiveSkillAttackSpeed (cuserdata.cpp:1449): bow/gun/dual only.
int32 URoseSkillComponent::PassiveAttackSpeedBonus(int32 CurAttackSpeed, int32 WeaponType) const
{
	int32 A = 0;
	switch (WeaponType)
	{
	case 231:           A = 49; break;           // AT_PSV_ATK_SPD_BOW
	case 232: case 233: A = 50; break;           // AT_PSV_ATK_SPD_GUN
	case 251: case 252: A = 51; break;           // AT_PSV_ATK_SPD_PAIR
	default: return 0;
	}
	return PassiveFlat(A) + (int32)(CurAttackSpeed * PassiveRate(A) / 100.f);
}

FString URoseSkillComponent::DescribeState() const
{
	FString S = FString::Printf(TEXT("job %d   skill points %d\n"), CurrentJob, SkillPoints);
	for (const TPair<int32, int32>& P : Learned)
		if (const FRoseSkillRow* R = GetSkillRow(P.Value))
			S += FString::Printf(TEXT("  %s%s\n"), *SkillLabel(P.Value),
				R->SkillType == 15 ? TEXT("  [passive]") : TEXT(""));
	for (int32 i = 0; i < Hotbar.Num(); ++i)
		S += FString::Printf(TEXT("  [%d] %s\n"), i + 1,
			Hotbar[i] > 0 ? *SkillLabel(Hotbar[i]) : TEXT("-"));
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	for (const FRoseActiveBuff& B : Buffs)
		S += FString::Printf(TEXT("  buff: type %d %+d (%.0fs left)\n"),
			B.StatusType, B.Value, B.EndTime > 0.f ? B.EndTime - Now : 0.f);
	return S;
}
