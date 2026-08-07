#include "RoseMonsterAI.h"

#include "RoseCharacter.h"
#include "RoseMonster.h"

#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

// ── shared table ─────────────────────────────────────────────────────────────
static TMap<int32, FAipData> GAipTable;
static bool GAipLoaded = false;

static void LoadAipTable()
{
	if (GAipLoaded) return;
	GAipLoaded = true;

	const FString Path = FPaths::ProjectContentDir() / TEXT("DataTables/ai_patterns.json");
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseAI] ai_patterns.json not found at %s"), *Path);
		return;
	}
	TSharedPtr<FJsonObject> Root;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
	if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseAI] ai_patterns.json parse failed"));
		return;
	}

	auto ParseRec = [](const TSharedPtr<FJsonObject>& O) -> FAipRec
	{
		FAipRec Rec;
		for (const TPair<FString, TSharedPtr<FJsonValue>>& It : O->Values)
		{
			if (It.Key == TEXT("op"))
				Rec.Op = (int32)It.Value->AsNumber();
			else if (It.Value->Type == EJson::Number)
				Rec.F.Add(FName(*It.Key), (int32)It.Value->AsNumber());
			else if (It.Value->Type == EJson::String)
				Rec.Text = It.Value->AsString();
		}
		return Rec;
	};

	for (const TPair<FString, TSharedPtr<FJsonValue>>& It : Root->Values)
	{
		const TSharedPtr<FJsonObject> O = It.Value->AsObject();
		if (!O.IsValid()) continue;
		FAipData D;
		D.IdleSec = FMath::Max(1, (int32)O->GetNumberField(TEXT("idle_sec")));
		D.DamagedPct = (int32)O->GetNumberField(TEXT("damaged_pct"));
		const TArray<TSharedPtr<FJsonValue>>* Pats = nullptr;
		if (O->TryGetArrayField(TEXT("patterns"), Pats))
		{
			for (int32 p = 0; p < Pats->Num() && p < 6; ++p)
			{
				const TArray<TSharedPtr<FJsonValue>>* Evs = nullptr;
				if (!(*Pats)[p]->TryGetArray(Evs)) continue;
				for (const TSharedPtr<FJsonValue>& EV : *Evs)
				{
					const TSharedPtr<FJsonObject> EO = EV->AsObject();
					if (!EO.IsValid()) continue;
					FAipEvent Ev;
					Ev.Name = EO->GetStringField(TEXT("name"));
					const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
					if (EO->TryGetArrayField(TEXT("conds"), Arr))
						for (const TSharedPtr<FJsonValue>& V : *Arr)
							if (V->AsObject().IsValid()) Ev.Conds.Add(ParseRec(V->AsObject()));
					if (EO->TryGetArrayField(TEXT("acts"), Arr))
						for (const TSharedPtr<FJsonValue>& V : *Arr)
							if (V->AsObject().IsValid()) Ev.Acts.Add(ParseRec(V->AsObject()));
					D.Patterns[p].Add(MoveTemp(Ev));
				}
			}
		}
		GAipTable.Add(FCString::Atoi(*It.Key), MoveTemp(D));
	}
	UE_LOG(LogTemp, Log, TEXT("[RoseAI] loaded %d AI scripts"), GAipTable.Num());
}

const FAipData* URoseMonsterAIComponent::FindAi(int32 Id)
{
	LoadAipTable();
	return GAipTable.Find(Id);
}

// ── setup / triggers ─────────────────────────────────────────────────────────
bool URoseMonsterAIComponent::Setup(int32 InAiId)
{
	AiId = InAiId;
	Data = FindAi(InAiId);
	return Data != nullptr;
}

ARoseMonster* URoseMonsterAIComponent::Mob() const
{
	return Cast<ARoseMonster>(GetOwner());
}

ARoseCharacter* URoseMonsterAIComponent::Player() const
{
	return Cast<ARoseCharacter>(UGameplayStatics::GetPlayerCharacter(GetWorld(), 0));
}

void URoseMonsterAIComponent::FireTrigger(EAipTrigger T)
{
	if (!Data) return;
	ARoseMonster* M = Mob();
	if (!M) return;
	const TArray<FAipEvent>& Events = Data->Patterns[(int32)T];
	for (const FAipEvent& Ev : Events)
	{
		FindActor = nullptr;
		if (EvalConds(Ev))
		{
			RunActs(Ev);
			return;                       // first matching event wins
		}
	}
}

void URoseMonsterAIComponent::TickIdle(float Dt)
{
	if (!Data) return;
	StopAccum += Dt;
	if (StopAccum >= (float)Data->IdleSec)
	{
		StopAccum = 0.f;
		FireTrigger(EAipTrigger::Stop);
	}
}

void URoseMonsterAIComponent::TickChase(float Dt)
{
	if (!Data) return;
	ChaseAccum += Dt;
	if (ChaseAccum >= 3.f)                // classic fires per attack-move step
	{
		ChaseAccum = 0.f;
		FireTrigger(EAipTrigger::AttackMove);
	}
}

void URoseMonsterAIComponent::OnDamaged()
{
	if (!Data) return;
	// Classic: when already fighting, DAMAGED only runs iSecondOfAttackMove % of
	// the time (cai_file.cpp AI_WhenDAMAGED).
	if (Mob() && Mob()->IsAggroed() && FMath::RandRange(0, 99) >= Data->DamagedPct)
		return;
	FireTrigger(EAipTrigger::Damaged);
}

// ── evaluation ───────────────────────────────────────────────────────────────
static bool CheckOp(int32 A, int32 Op, int32 B)
{
	switch (Op)
	{
	case 0: return A == B;
	case 1: return A >  B;
	case 2: return A >= B;
	case 3: return A <  B;
	case 4: return A <= B;
	case 10: return A != B;
	default: return false;
	}
}

static void LogOnce(const TCHAR* Kind, int32 Op)
{
	static TSet<uint32> Seen;
	const uint32 Key = (Kind[0] == 'c' ? 0x10000u : 0x20000u) | (uint32)Op;
	if (!Seen.Contains(Key))
	{
		Seen.Add(Key);
		UE_LOG(LogTemp, Log, TEXT("[RoseAI] unsupported %s op %d (skipped)"), Kind, Op);
	}
}

bool URoseMonsterAIComponent::EvalConds(const FAipEvent& Ev)
{
	for (const FAipRec& C : Ev.Conds)
		if (!EvalCond(C))
			return false;
	return true;                           // no conditions = always true
}

bool URoseMonsterAIComponent::EvalCond(const FAipRec& R)
{
	ARoseMonster* M = Mob();
	ARoseCharacter* P = Player();
	const float ToPlayer = P ? FVector::Dist2D(M->GetActorLocation(), P->GetActorLocation())
	                         : TNumericLimits<float>::Max();

	switch (R.Op)
	{
	case 1:   // fight state: not_fight 0 = non-combat, 1 = engaged-waiting
		return (R.F.FindRef("not_fight") == 0) != M->IsAggroed();

	case 3:   // nearby char count of type within dist (+select)
	{
		const int32 Dist = R.F.FindRef("dist");
		const int32 Need = FMath::Max(1, (int32)R.F.FindRef("count"));
		if (R.F.FindRef("allied") == 0)   // enemies = the player (v1 single-player)
		{
			if (P && P->CurrentHP > 0.f && ToPlayer <= Dist)
			{
				FindActor = P;
				return 1 >= Need;
			}
			return false;
		}
		// allies = other monsters in range
		int32 N = 0;
		AActor* Nearest = nullptr;
		float Best = TNumericLimits<float>::Max();
		for (TActorIterator<ARoseMonster> It(GetWorld()); It; ++It)
		{
			if (*It == M || It->IsDead()) continue;
			const float D = FVector::Dist2D(M->GetActorLocation(), It->GetActorLocation());
			if (D <= Dist) { ++N; if (D < Best) { Best = D; Nearest = *It; } }
		}
		if (Nearest) FindActor = Nearest;
		return N >= Need;
	}

	case 5:   // distance to target
	{
		if (!P || !M->IsAggroed()) return false;
		const int32 Dist = R.F.FindRef("dist");
		return R.F.FindRef("less") ? ToPlayer <= Dist : ToPlayer >= Dist;
	}

	case 7:   // self HP%
	{
		const int32 Pct = FMath::RoundToInt(100.f * M->GetHP() / FMath::Max(1.f, M->GetMaxHP()));
		const int32 Ref = R.F.FindRef("hp_pct");
		return R.F.FindRef("less") ? Pct <= Ref : Pct >= Ref;
	}

	case 8:   // random %
		return FMath::RandRange(0, 99) < R.F.FindRef("pct");

	case 9:   // nearest char in range (+select)
	{
		const int32 Dist = R.F.FindRef("dist");
		if (R.F.FindRef("allied") == 0)
		{
			if (P && P->CurrentHP > 0.f && ToPlayer <= Dist) { FindActor = P; return true; }
			return false;
		}
		for (TActorIterator<ARoseMonster> It(GetWorld()); It; ++It)
		{
			if (*It == M || It->IsDead()) continue;
			if (FVector::Dist2D(M->GetActorLocation(), It->GetActorLocation()) <= Dist)
			{ FindActor = *It; return true; }
		}
		return false;
	}

	case 10:  // has attack target
		return M->IsAggroed() && P && P->CurrentHP > 0.f;

	case 21:  // own ability check (31 = level; others unsupported)
	{
		const int32 Ab = R.F.FindRef("ab");
		if (Ab == 31)
			return CheckOp(M->Level, R.F.FindRef("cmp"), R.F.FindRef("value"));
		LogOnce(TEXT("cond-ability"), Ab);
		return false;
	}

	default:
		LogOnce(TEXT("cond"), R.Op);
		return false;
	}
}

void URoseMonsterAIComponent::RunActs(const FAipEvent& Ev)
{
	for (const FAipRec& A : Ev.Acts)
		RunAct(A);
}

void URoseMonsterAIComponent::RunAct(const FAipRec& R)
{
	ARoseMonster* M = Mob();
	ARoseCharacter* P = Player();

	switch (R.Op)
	{
	case 1:   // stop
		M->AIStop();
		break;

	case 4:   // random move around current pos
		M->AIWander(false, (float)FMath::Max(200, (int32)R.F.FindRef("dist")), R.F.FindRef("run") != 0);
		break;
	case 5:   // random move around born pos
		M->AIWander(true, (float)FMath::Max(200, (int32)R.F.FindRef("dist")), R.F.FindRef("run") != 0);
		break;

	case 6:   // move to the condition-found char → engage
	case 7:   // attack best-ability enemy in range   (v1: the player)
	case 13:  // target nearest
	case 14:  // target condition-found char
	case 16:  // target whoever hit me
	case 20:  // target nearest avatar
		if (P && P->CurrentHP > 0.f)
			M->AIAggroPlayer();
		break;

	case 9:   // flee from target
	case 17:  // run away persistent
		M->AIFlee((float)FMath::Max(500, (int32)R.F.FindRef("dist")));
		break;

	case 12:  // call N allies within dist to gang my target
	case 15:  // call same-type allies
	case 19:  // call specific monster id
	{
		if (!P || P->CurrentHP <= 0.f) break;
		const int32 Dist = FMath::Max(500, (int32)R.F.FindRef("dist"));
		const int32 Want = R.Op == 19 ? (int32)R.F.FindRef("count")
		                 : R.Op == 12 ? (int32)R.F.FindRef("count") : 99;
		const int32 OnlyId = R.Op == 19 ? (int32)R.F.FindRef("monster")
		                   : R.Op == 15 ? M->NpcId : -1;
		int32 Called = 0;
		for (TActorIterator<ARoseMonster> It(GetWorld()); It && Called < FMath::Max(1, Want); ++It)
		{
			if (*It == M || It->IsDead() || It->IsAggroed()) continue;
			if (OnlyId >= 0 && It->NpcId != OnlyId) continue;
			if (FVector::Dist2D(M->GetActorLocation(), It->GetActorLocation()) > Dist) continue;
			It->AIAggroPlayer();
			++Called;
		}
		break;
	}

	case 25:  // USE SKILL (AIACT_24) — the whole point
	{
		const int32 Target = R.F.FindRef("target");   // 0 found / 1 attack target / 2 self
		AActor* At = (Target == 0 && FindActor.IsValid()) ? FindActor.Get()
		           : (Target == 2) ? (AActor*)M : (AActor*)P;
		M->AICastSkill(R.F.FindRef("skill"), R.F.FindRef("motion"), At, Target == 2);
		break;
	}

	default:
		LogOnce(TEXT("act"), R.Op);
		break;
	}
}
