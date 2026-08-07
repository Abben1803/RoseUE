#include "RoseGameInstance.h"
#include "RoseSkillComponent.h"
#include "RoseLoadingScreen.h"
#include "MoviePlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

void URoseGameInstance::Capture(ARoseCharacter* C)
{
	if (!C)
		return;
	Gender = C->Gender;
	Strength = C->Strength; Dexterity = C->Dexterity; Intelligence = C->Intelligence;
	Concentration = C->Concentration; Charm = C->Charm; Sense = C->Sense;
	Level = C->Level;
	Exp = C->Exp;
	CurrentHP = C->CurrentHP; CurrentMP = C->CurrentMP;
	Zuly = C->Zuly;
	Bag = C->Bag;
	Equipped = C->Equipped;
	EquippedRefine = C->EquippedRefine;
	EquippedBonus = C->EquippedBonus;
	EquippedAppraised = C->EquippedAppraised;

	if (C->Skills)
	{
		CurrentJob = C->Skills->CurrentJob;
		SkillPoints = C->Skills->SkillPoints;
		Learned = C->Skills->Learned;
		Hotbar = C->Skills->Hotbar;
	}

	if (C->Quests)
	{
		QuestSlots = C->Quests->Slots;
		EpisodeVars = C->Quests->EpisodeVars;
		JobVars = C->Quests->JobVars;
		PlanetVars = C->Quests->PlanetVars;
		UnionVars = C->Quests->UnionVars;
		UnionPoints = C->Quests->UnionPoints;
		QuestSwitches.SetNum(C->Quests->Switches.Num());
		for (int32 i = 0; i < C->Quests->Switches.Num(); ++i)
			QuestSwitches[i] = C->Quests->Switches[i];
	}

	bHasSnapshot = true;
}

void URoseGameInstance::Restore(ARoseCharacter* C)
{
	if (!C || !bHasSnapshot)
		return;
	bHasSnapshot = false;

	C->Gender = Gender;
	C->Strength = Strength; C->Dexterity = Dexterity; C->Intelligence = Intelligence;
	C->Concentration = Concentration; C->Charm = Charm; C->Sense = Sense;
	C->Level = Level;
	C->Exp = Exp;
	C->Zuly = Zuly;
	C->Bag = Bag;
	C->Equipped = Equipped;
	C->EquippedRefine = EquippedRefine;
	C->EquippedBonus = EquippedBonus;
	C->EquippedAppraised = EquippedAppraised;

	if (C->Skills)
	{
		C->Skills->CurrentJob = CurrentJob;
		C->Skills->SkillPoints = SkillPoints;
		C->Skills->Learned = Learned;
		C->Skills->Hotbar = Hotbar;
	}

	if (C->Quests)
	{
		C->Quests->Slots = QuestSlots;
		C->Quests->EpisodeVars = EpisodeVars;
		C->Quests->JobVars = JobVars;
		C->Quests->PlanetVars = PlanetVars;
		C->Quests->UnionVars = UnionVars;
		C->Quests->UnionPoints = UnionPoints;
		for (int32 i = 0; i < QuestSwitches.Num() && i < C->Quests->Switches.Num(); ++i)
			C->Quests->Switches[i] = QuestSwitches[i];
		C->Quests->QuestRevision++;
	}

	// Rebuild everything the restored state drives: merged body, movement,
	// then HP/MP (AFTER ApplyDerivedStats, which would clamp/reset them).
	C->UpdateLocoSet();
	C->RebuildMesh();
	C->ApplyDerivedStats();
	C->CurrentHP = CurrentHP;
	C->CurrentMP = CurrentMP;
	C->BagRevision++;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Backend persistence bridge — the snapshot above <-> the service's
//  CharacterState schema (backend/app/schemas.py).  Field names here are the
//  wire contract; changing one means changing both sides.
// ═══════════════════════════════════════════════════════════════════════════

namespace
{
	template <typename T>
	TSharedPtr<FJsonObject> MapToJson(const TMap<FString, T>& In)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		for (const TPair<FString, T>& P : In)
			O->SetNumberField(P.Key, static_cast<double>(P.Value));
		return O;
	}

	TSharedPtr<FJsonObject> BoolMapToJson(const TMap<FString, bool>& In)
	{
		TSharedPtr<FJsonObject> O = MakeShared<FJsonObject>();
		for (const TPair<FString, bool>& P : In)
			O->SetBoolField(P.Key, P.Value);
		return O;
	}

	TArray<TSharedPtr<FJsonValue>> IntsToJson(const TArray<int32>& In)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (int32 V : In)
			Out.Add(MakeShared<FJsonValueNumber>(V));
		return Out;
	}

	void JsonToInts(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, TArray<int32>& Out)
	{
		const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
		if (!Obj.IsValid() || !Obj->TryGetArrayField(Field, Arr))
			return;
		Out.Reset();
		for (const TSharedPtr<FJsonValue>& V : *Arr)
			Out.Add(static_cast<int32>(V->AsNumber()));
	}

	// Read an object field into a string->int map (missing field = leave empty).
	void JsonToIntMap(const TSharedPtr<FJsonObject>& Obj, const TCHAR* Field, TMap<FString, int32>& Out)
	{
		const TSharedPtr<FJsonObject>* Sub = nullptr;
		if (!Obj.IsValid() || !Obj->TryGetObjectField(Field, Sub))
			return;
		Out.Reset();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Sub)->Values)
			Out.Add(P.Key, static_cast<int32>(P.Value->AsNumber()));
	}
}

TSharedPtr<FJsonObject> URoseGameInstance::ToJson(const FString& Zone, const FVector& Pos) const
{
	TSharedPtr<FJsonObject> S = MakeShared<FJsonObject>();

	S->SetStringField(TEXT("gender"), Gender);
	S->SetNumberField(TEXT("level"), Level);
	S->SetNumberField(TEXT("exp"), static_cast<double>(Exp));
	S->SetNumberField(TEXT("hp"), CurrentHP);
	S->SetNumberField(TEXT("mp"), CurrentMP);
	S->SetNumberField(TEXT("zuly"), Zuly);

	TSharedPtr<FJsonObject> Stats = MakeShared<FJsonObject>();
	Stats->SetNumberField(TEXT("STR"), Strength);
	Stats->SetNumberField(TEXT("DEX"), Dexterity);
	Stats->SetNumberField(TEXT("INT"), Intelligence);
	Stats->SetNumberField(TEXT("CON"), Concentration);
	Stats->SetNumberField(TEXT("CHARM"), Charm);
	Stats->SetNumberField(TEXT("SENSE"), Sense);
	S->SetObjectField(TEXT("stats"), Stats);

	S->SetObjectField(TEXT("equipped"), MapToJson(Equipped));
	S->SetObjectField(TEXT("equipped_refine"), MapToJson(EquippedRefine));
	S->SetObjectField(TEXT("equipped_bonus"), MapToJson(EquippedBonus));
	S->SetObjectField(TEXT("equipped_appraised"), BoolMapToJson(EquippedAppraised));

	TArray<TSharedPtr<FJsonValue>> BagArr;
	for (const FRoseItemStack& I : Bag)
	{
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetStringField(TEXT("slot"), I.Slot);
		E->SetNumberField(TEXT("id"), I.Id);
		E->SetNumberField(TEXT("count"), I.Count);
		E->SetNumberField(TEXT("bonus"), I.Bonus);
		E->SetBoolField(TEXT("appraised"), I.bAppraised);
		E->SetNumberField(TEXT("refine"), I.Refine);
		BagArr.Add(MakeShared<FJsonValueObject>(E));
	}
	S->SetArrayField(TEXT("bag"), BagArr);

	TSharedPtr<FJsonObject> Sk = MakeShared<FJsonObject>();
	Sk->SetNumberField(TEXT("job"), CurrentJob);
	Sk->SetNumberField(TEXT("points"), SkillPoints);
	TSharedPtr<FJsonObject> LearnedObj = MakeShared<FJsonObject>();
	for (const TPair<int32, int32>& P : Learned)
		LearnedObj->SetNumberField(FString::FromInt(P.Key), P.Value);
	Sk->SetObjectField(TEXT("learned"), LearnedObj);
	Sk->SetArrayField(TEXT("hotbar"), IntsToJson(Hotbar));
	S->SetObjectField(TEXT("skills"), Sk);

	TSharedPtr<FJsonObject> Q = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> SlotArr;
	for (const FRoseQuestSlot& Slot : QuestSlots)
	{
		TSharedPtr<FJsonObject> E = MakeShared<FJsonObject>();
		E->SetNumberField(TEXT("id"), Slot.Id);
		E->SetNumberField(TEXT("expire_at"), Slot.ExpireAt);
		E->SetNumberField(TEXT("switches"), Slot.Switches);
		E->SetArrayField(TEXT("vars"), IntsToJson(Slot.Vars));
		TArray<TSharedPtr<FJsonValue>> ItemArr;
		for (const FRoseQuestItem& It : Slot.Items)
		{
			TSharedPtr<FJsonObject> IO = MakeShared<FJsonObject>();
			IO->SetNumberField(TEXT("item_sn"), It.ItemSN);
			IO->SetNumberField(TEXT("quantity"), It.Quantity);
			ItemArr.Add(MakeShared<FJsonValueObject>(IO));
		}
		E->SetArrayField(TEXT("items"), ItemArr);
		SlotArr.Add(MakeShared<FJsonValueObject>(E));
	}
	Q->SetArrayField(TEXT("slots"), SlotArr);
	Q->SetArrayField(TEXT("episode_vars"), IntsToJson(EpisodeVars));
	Q->SetArrayField(TEXT("job_vars"), IntsToJson(JobVars));
	Q->SetArrayField(TEXT("planet_vars"), IntsToJson(PlanetVars));
	Q->SetArrayField(TEXT("union_vars"), IntsToJson(UnionVars));
	Q->SetArrayField(TEXT("union_points"), IntsToJson(UnionPoints));
	// 512 global switches pack into a bool array; JSON handles it fine and it
	// stays readable in psql, which matters more here than 64 bytes of wire.
	TArray<TSharedPtr<FJsonValue>> SwitchArr;
	for (bool B : QuestSwitches)
		SwitchArr.Add(MakeShared<FJsonValueBoolean>(B));
	Q->SetArrayField(TEXT("switches"), SwitchArr);
	S->SetObjectField(TEXT("quests"), Q);

	S->SetStringField(TEXT("zone"), Zone);
	S->SetNumberField(TEXT("x"), Pos.X);
	S->SetNumberField(TEXT("y"), Pos.Y);
	S->SetNumberField(TEXT("z"), Pos.Z);

	return S;
}

void URoseGameInstance::FromJson(const TSharedPtr<FJsonObject>& S)
{
	if (!S.IsValid())
		return;

	S->TryGetStringField(TEXT("gender"), Gender);
	Level = S->HasField(TEXT("level")) ? S->GetIntegerField(TEXT("level")) : Level;
	Exp = S->HasField(TEXT("exp")) ? static_cast<int64>(S->GetNumberField(TEXT("exp"))) : Exp;
	CurrentHP = S->HasField(TEXT("hp")) ? S->GetNumberField(TEXT("hp")) : CurrentHP;
	CurrentMP = S->HasField(TEXT("mp")) ? S->GetNumberField(TEXT("mp")) : CurrentMP;
	Zuly = S->HasField(TEXT("zuly")) ? S->GetIntegerField(TEXT("zuly")) : Zuly;

	const TSharedPtr<FJsonObject>* Stats = nullptr;
	if (S->TryGetObjectField(TEXT("stats"), Stats))
	{
		(*Stats)->TryGetNumberField(TEXT("STR"), Strength);
		(*Stats)->TryGetNumberField(TEXT("DEX"), Dexterity);
		(*Stats)->TryGetNumberField(TEXT("INT"), Intelligence);
		(*Stats)->TryGetNumberField(TEXT("CON"), Concentration);
		(*Stats)->TryGetNumberField(TEXT("CHARM"), Charm);
		(*Stats)->TryGetNumberField(TEXT("SENSE"), Sense);
	}

	JsonToIntMap(S, TEXT("equipped"), Equipped);
	JsonToIntMap(S, TEXT("equipped_refine"), EquippedRefine);
	JsonToIntMap(S, TEXT("equipped_bonus"), EquippedBonus);

	const TSharedPtr<FJsonObject>* Appr = nullptr;
	if (S->TryGetObjectField(TEXT("equipped_appraised"), Appr))
	{
		EquippedAppraised.Reset();
		for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*Appr)->Values)
			EquippedAppraised.Add(P.Key, P.Value->AsBool());
	}

	const TArray<TSharedPtr<FJsonValue>>* BagArr = nullptr;
	if (S->TryGetArrayField(TEXT("bag"), BagArr))
	{
		Bag.Reset();
		for (const TSharedPtr<FJsonValue>& V : *BagArr)
		{
			const TSharedPtr<FJsonObject>* E;
			if (!V->TryGetObject(E)) continue;
			FRoseItemStack I;
			(*E)->TryGetStringField(TEXT("slot"), I.Slot);
			I.Id = (*E)->HasField(TEXT("id")) ? (*E)->GetIntegerField(TEXT("id")) : 0;
			I.Count = (*E)->HasField(TEXT("count")) ? (*E)->GetIntegerField(TEXT("count")) : 1;
			I.Bonus = (*E)->HasField(TEXT("bonus")) ? (*E)->GetIntegerField(TEXT("bonus")) : 0;
			(*E)->TryGetBoolField(TEXT("appraised"), I.bAppraised);
			I.Refine = (*E)->HasField(TEXT("refine")) ? (*E)->GetIntegerField(TEXT("refine")) : 0;
			Bag.Add(I);
		}
	}

	const TSharedPtr<FJsonObject>* Sk = nullptr;
	if (S->TryGetObjectField(TEXT("skills"), Sk))
	{
		(*Sk)->TryGetNumberField(TEXT("job"), CurrentJob);
		(*Sk)->TryGetNumberField(TEXT("points"), SkillPoints);
		const TSharedPtr<FJsonObject>* LearnedObj = nullptr;
		if ((*Sk)->TryGetObjectField(TEXT("learned"), LearnedObj))
		{
			Learned.Reset();
			for (const TPair<FString, TSharedPtr<FJsonValue>>& P : (*LearnedObj)->Values)
				Learned.Add(FCString::Atoi(*P.Key), static_cast<int32>(P.Value->AsNumber()));
		}
		JsonToInts(*Sk, TEXT("hotbar"), Hotbar);
	}

	const TSharedPtr<FJsonObject>* Q = nullptr;
	if (S->TryGetObjectField(TEXT("quests"), Q))
	{
		const TArray<TSharedPtr<FJsonValue>>* SlotArr = nullptr;
		if ((*Q)->TryGetArrayField(TEXT("slots"), SlotArr))
		{
			QuestSlots.Reset();
			for (const TSharedPtr<FJsonValue>& V : *SlotArr)
			{
				const TSharedPtr<FJsonObject>* E;
				if (!V->TryGetObject(E)) continue;
				FRoseQuestSlot Slot;
				Slot.Reset();
				(*E)->TryGetNumberField(TEXT("id"), Slot.Id);
				(*E)->TryGetNumberField(TEXT("expire_at"), Slot.ExpireAt);
				(*E)->TryGetNumberField(TEXT("switches"), Slot.Switches);
				JsonToInts(*E, TEXT("vars"), Slot.Vars);
				const TArray<TSharedPtr<FJsonValue>>* ItemArr = nullptr;
				if ((*E)->TryGetArrayField(TEXT("items"), ItemArr))
					for (const TSharedPtr<FJsonValue>& IV : *ItemArr)
					{
						const TSharedPtr<FJsonObject>* IO;
						if (!IV->TryGetObject(IO)) continue;
						FRoseQuestItem It;
						(*IO)->TryGetNumberField(TEXT("item_sn"), It.ItemSN);
						(*IO)->TryGetNumberField(TEXT("quantity"), It.Quantity);
						Slot.Items.Add(It);
					}
				QuestSlots.Add(Slot);
			}
		}
		JsonToInts(*Q, TEXT("episode_vars"), EpisodeVars);
		JsonToInts(*Q, TEXT("job_vars"), JobVars);
		JsonToInts(*Q, TEXT("planet_vars"), PlanetVars);
		JsonToInts(*Q, TEXT("union_vars"), UnionVars);
		JsonToInts(*Q, TEXT("union_points"), UnionPoints);
		const TArray<TSharedPtr<FJsonValue>>* SwitchArr = nullptr;
		if ((*Q)->TryGetArrayField(TEXT("switches"), SwitchArr))
		{
			QuestSwitches.Reset();
			for (const TSharedPtr<FJsonValue>& V : *SwitchArr)
				QuestSwitches.Add(V->AsBool());
		}
	}

	// Restore() refuses to run without this — the JSON IS the snapshot now.
	bHasSnapshot = true;
}

void URoseGameInstance::ShowLoadingScreen(const FString& LevelName)
{
	if (!IsMoviePlayerEnabled())
		return;
	FString Zone = LevelName;
	Zone.RemoveFromStart(TEXT("L_"));

	FLoadingScreenAttributes Attrs;
	Attrs.bAutoCompleteWhenLoadingCompletes = true;
	Attrs.bMoviesAreSkippable = false;
	Attrs.MinimumLoadingScreenDisplayTime = 1.5f;   // avoid a flash on fast loads
	Attrs.WidgetLoadingScreen = RoseLoadingScreen_Make(Zone);
	GetMoviePlayer()->SetupLoadingScreen(Attrs);
}

void URoseGameInstance::WarpToLevel(ARoseCharacter* C, const FString& LevelName,
	const FString& Options)
{
	if (!C)
		return;
	if (URoseGameInstance* GI = Cast<URoseGameInstance>(C->GetGameInstance()))
		GI->Capture(C);
	ShowLoadingScreen(LevelName);
	UGameplayStatics::OpenLevel(C, FName(*LevelName), true, Options);
}

void URoseGameInstance::EnterWorldAsNewCharacter(UObject* WorldCtx,
	const FString& InGender, int32 InHair, int32 InFace, const FString& InName,
	const FString& StartLevel)
{
	bHasSnapshot = false;              // a fresh character, not a returning one
	bHasPendingCreation = true;
	PendingGender = InGender;
	PendingHair = InHair;
	PendingFace = InFace;
	PendingName = InName;
	ShowLoadingScreen(StartLevel);
	UGameplayStatics::OpenLevel(WorldCtx, FName(*StartLevel), true);
}
