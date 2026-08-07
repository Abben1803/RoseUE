// URoseGameInstance — carries the player's state across level travel (warp
// portals + quest teleports).  UE respawns ARoseCharacter on OpenLevel, so
// without this every cross-zone warp reset stats/bag/skills/quests.
// Capture() before OpenLevel (WarpToLevel does both); ARoseCharacter::
// BeginPlay restores when a snapshot is pending.
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"
#include "RoseCharacter.h"     // FRoseItemStack
#include "RoseQuest.h"         // FRoseQuestSlot
#include "RoseGameInstance.generated.h"

UCLASS()
class ROSEUE_API URoseGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	bool bHasSnapshot = false;

	// ── pending new-character appearance (Character Select -> game world) ──
	// Distinct from the warp snapshot: set by the select screen just before it
	// OpenLevels the start town, consumed by ARoseCharacter::BeginPlay to build
	// the chosen look.  bHasSnapshot takes precedence (a returning character).
	bool bHasPendingCreation = false;
	FString PendingGender = TEXT("Female");
	// 110 was an Arua hair id; classic tops out at 78 (bald character).
	int32 PendingHair = 1;
	int32 PendingFace = 1;
	FString PendingName;

	// Set the pending look + travel to the start town (clears any stale snapshot).
	void EnterWorldAsNewCharacter(UObject* WorldCtx, const FString& InGender,
		int32 InHair, int32 InFace, const FString& InName,
		const FString& StartLevel = TEXT("L_JPT01"));

	// ── character ──
	FString Gender;
	int32 Strength = 0, Dexterity = 0, Intelligence = 0, Concentration = 0;
	int32 Charm = 0, Sense = 0, Level = 1;
	int64 Exp = 0;
	float CurrentHP = 0.f, CurrentMP = 0.f;
	int32 Zuly = 0;
	UPROPERTY() TArray<FRoseItemStack> Bag;
	UPROPERTY() TMap<FString, int32> Equipped;
	UPROPERTY() TMap<FString, int32> EquippedRefine;     // grade per equipped slot
	UPROPERTY() TMap<FString, int32> EquippedBonus;      // GEM_OP option per slot
	UPROPERTY() TMap<FString, bool>  EquippedAppraised;  // option revealed+active?

	// ── skills ──
	int32 CurrentJob = 0, SkillPoints = 0;
	UPROPERTY() TMap<int32, int32> Learned;
	UPROPERTY() TArray<int32> Hotbar;

	// ── quests ──
	UPROPERTY() TArray<FRoseQuestSlot> QuestSlots;
	UPROPERTY() TArray<int32> EpisodeVars;
	UPROPERTY() TArray<int32> JobVars;
	UPROPERTY() TArray<int32> PlanetVars;
	UPROPERTY() TArray<int32> UnionVars;
	UPROPERTY() TArray<int32> UnionPoints;
	TArray<bool> QuestSwitches;

	void Capture(class ARoseCharacter* C);
	void Restore(class ARoseCharacter* C);   // clears bHasSnapshot

	// ── backend persistence bridge (backend/README.md) ──────────────────────
	// The cross-zone snapshot IS the save format.  Capture() then ToJson()
	// produces exactly the service's CharacterState schema
	// (backend/app/schemas.py); FromJson() then Restore() is the inverse.  One
	// definition of "what a character is" serves both level travel and the
	// database — if they ever diverge, a warp would save something different
	// from a logout, which is the kind of bug that eats inventories.
	//
	// These are pure conversions on THIS object's fields, used synchronously
	// (fill → apply → done), so the single GameInstance being shared by every
	// connection on a zone server is safe.
	TSharedPtr<class FJsonObject> ToJson(const FString& Zone, const FVector& Pos) const;
	void FromJson(const TSharedPtr<class FJsonObject>& State);

	// Capture + OpenLevel in one call — every cross-zone travel goes through
	// here so state always survives.
	static void WarpToLevel(class ARoseCharacter* C, const FString& LevelName,
		const FString& Options);

	// Arm the between-zone loading screen (MoviePlayer) — call just before an
	// OpenLevel; auto-completes when the destination finishes loading.
	static void ShowLoadingScreen(const FString& LevelName);
};
