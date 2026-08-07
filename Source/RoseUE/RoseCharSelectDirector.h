// ARoseCharSelectDirector — orchestrates the ROSE-style Character Select scene.
// Placed once in L_CharacterSelect (by ue5_build_charselect.py) at the platform
// stand point.  On BeginPlay it loads the saved roster (URoseCharSlotSave),
// sorts by level, and spawns up to 5 ARoseCharacterCreator avatars on a shallow
// arc facing the camera — mirroring the classic client's c_AvatarPositions
// layout (cgamedatacreateavatar.cpp).  It tracks the selected avatar, answers
// screen-space picks, and computes the camera framing that ARoseCharSelectPawn
// eases toward (wide arc view → focus on the selected/creating avatar).
//
// The Slate UI (RoseCharSelectUI.cpp) drives it: select a row, Create, Delete,
// Start.  Create mode hides the arc and shows a single editable avatar on a
// front pedestal.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoseCharSlotSave.h"
#include "RoseCharSelectDirector.generated.h"

class ARoseCharacterCreator;

UCLASS()
class ROSEUE_API ARoseCharSelectDirector : public AActor
{
	GENERATED_BODY()

public:
	ARoseCharSelectDirector();

	virtual void BeginPlay() override;

	// ── arc + camera tuning (editable in-level, no rebuild needed) ──
	UPROPERTY(EditAnywhere, Category = "Rose|Arc") int32 MaxAvatars = 5;
	UPROPERTY(EditAnywhere, Category = "Rose|Arc") float SlotSpacing = 175.f;  // cm, lateral
	UPROPERTY(EditAnywhere, Category = "Rose|Arc") float ArcDepth = 110.f;     // cm, middle pull-back
	UPROPERTY(EditAnywhere, Category = "Rose|Cam") float CamHeight = 165.f;
	UPROPERTY(EditAnywhere, Category = "Rose|Cam") float CamWideDist = 640.f;
	UPROPERTY(EditAnywhere, Category = "Rose|Cam") float CamFocusDist = 300.f;
	UPROPERTY(EditAnywhere, Category = "Rose|Cam") float LookHeight = 120.f;   // chest
	UPROPERTY(EditAnywhere, Category = "Rose|Cam") float CreateDist = 260.f;

	// ── roster / avatars ──
	void RefreshRoster();                       // reload save + respawn the arc
	int32 NumAvatars() const { return Avatars.Num(); }
	const TArray<FRoseCharSlot>& GetRoster() const { return Roster; }

	// ── selection ──
	void SelectIndex(int32 Index);              // highlight + focus camera
	int32 GetSelectedIndex() const { return SelectedIndex; }
	bool GetSelectedSlot(FRoseCharSlot& Out) const;

	// map a viewport click to an avatar (screen-space nearest); returns false if none near
	bool PickAvatarAtScreen(float X, float Y, int32& OutIndex) const;

	// ── create mode ──
	void SetCreateMode(bool bOn);
	bool IsCreateMode() const { return bCreateMode; }
	ARoseCharacterCreator* GetCreateAvatar() const;

	// camera target the pawn eases toward; false if nothing to frame yet
	bool GetDesiredFraming(FVector& OutLoc, FVector& OutLookAt) const;

private:
	UPROPERTY(Transient) TArray<ARoseCharacterCreator*> Avatars;
	UPROPERTY(Transient) ARoseCharacterCreator* CreateAvatar = nullptr;

	TArray<FRoseCharSlot> Roster;
	int32 SelectedIndex = INDEX_NONE;
	bool bCreateMode = false;

	void LoadRoster();
	void SpawnArc();
	void ClearAvatars();

	FVector ArcCenter() const { return GetActorLocation(); }
	FVector Forward() const { return GetActorForwardVector(); }   // avatars face this way (toward cam)
	FVector Right() const { return GetActorRightVector(); }
	FVector SlotLocation(int32 Index, int32 Count) const;
	FVector CreatePedestal() const;
};
