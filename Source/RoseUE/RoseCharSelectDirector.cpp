#include "RoseCharSelectDirector.h"
#include "RoseCharacterCreator.h"

#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"

ARoseCharSelectDirector::ARoseCharSelectDirector()
{
	PrimaryActorTick.bCanEverTick = false;
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

void ARoseCharSelectDirector::BeginPlay()
{
	Super::BeginPlay();
	RefreshRoster();

	// Editable create avatar (default look), parked on the pedestal, hidden until
	// Create mode.
	if (UWorld* W = GetWorld())
	{
		FTransform T(GetActorRotation(), CreatePedestal());
		CreateAvatar = W->SpawnActorDeferred<ARoseCharacterCreator>(
			ARoseCharacterCreator::StaticClass(), T);
		if (CreateAvatar)
		{
			UGameplayStatics::FinishSpawningActor(CreateAvatar, T);
			CreateAvatar->SetActorHiddenInGame(true);
#if WITH_EDITOR
			CreateAvatar->SetActorLabel(TEXT("CharSelect_CreateAvatar"));   // editor-only
#endif
		}
	}
}

// ── roster / arc ────────────────────────────────────────────────────────────
void ARoseCharSelectDirector::LoadRoster()
{
	Roster.Reset();
	if (UGameplayStatics::DoesSaveGameExist(URoseCharSlotSave::SlotName(), 0))
		if (URoseCharSlotSave* S = Cast<URoseCharSlotSave>(
				UGameplayStatics::LoadGameFromSlot(URoseCharSlotSave::SlotName(), 0)))
			Roster = S->Slots;

	// Highest level first — matches the classic client's level-sorted arc.
	Roster.Sort([](const FRoseCharSlot& A, const FRoseCharSlot& B) { return A.Level > B.Level; });
	if (Roster.Num() > MaxAvatars)
		Roster.SetNum(MaxAvatars);
}

void ARoseCharSelectDirector::ClearAvatars()
{
	for (ARoseCharacterCreator* A : Avatars)
		if (A) A->Destroy();
	Avatars.Reset();
}

void ARoseCharSelectDirector::SpawnArc()
{
	UWorld* W = GetWorld();
	if (!W) return;

	const int32 N = Roster.Num();
	for (int32 i = 0; i < N; ++i)
	{
		const FRoseCharSlot& Slot = Roster[i];
		const FTransform T(GetActorRotation(), SlotLocation(i, N));

		// Deferred so the creator's BeginPlay builds with this slot's look, not
		// the default Female/110/1.
		ARoseCharacterCreator* A = W->SpawnActorDeferred<ARoseCharacterCreator>(
			ARoseCharacterCreator::StaticClass(), T);
		if (!A) continue;
		A->Gender = Slot.Gender;
		A->HairId = Slot.Hair;
		A->FaceId = Slot.Face;
		UGameplayStatics::FinishSpawningActor(A, T);
#if WITH_EDITOR
		A->SetActorLabel(FString::Printf(TEXT("CharSelect_Avatar_%d_%s"), i, *Slot.Name));   // editor-only
#endif
		Avatars.Add(A);
	}
}

void ARoseCharSelectDirector::RefreshRoster()
{
	ClearAvatars();
	LoadRoster();
	SpawnArc();
	// Keep selection valid.
	if (Avatars.Num() == 0) SelectedIndex = INDEX_NONE;
	else SelectedIndex = FMath::Clamp(SelectedIndex, 0, Avatars.Num() - 1);
	SelectIndex(SelectedIndex);
}

// ── selection ───────────────────────────────────────────────────────────────
void ARoseCharSelectDirector::SelectIndex(int32 Index)
{
	SelectedIndex = (Avatars.IsValidIndex(Index)) ? Index : INDEX_NONE;

	// Nudge the selected avatar a touch toward the camera as a highlight.
	const int32 N = Avatars.Num();
	for (int32 i = 0; i < N; ++i)
	{
		if (!Avatars[i]) continue;
		FVector Loc = SlotLocation(i, N);
		if (i == SelectedIndex) Loc += Forward() * 40.f;
		Avatars[i]->SetActorLocation(Loc);
	}
}

bool ARoseCharSelectDirector::GetSelectedSlot(FRoseCharSlot& Out) const
{
	if (!Roster.IsValidIndex(SelectedIndex)) return false;
	Out = Roster[SelectedIndex];
	return true;
}

bool ARoseCharSelectDirector::PickAvatarAtScreen(float X, float Y, int32& OutIndex) const
{
	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return false;

	float Best = 120.f;      // px hit radius
	OutIndex = INDEX_NONE;
	for (int32 i = 0; i < Avatars.Num(); ++i)
	{
		if (!Avatars[i]) continue;
		const FVector World = Avatars[i]->GetActorLocation() + FVector(0, 0, LookHeight);
		FVector2D Screen;
		if (PC->ProjectWorldLocationToScreen(World, Screen))
		{
			const float D = FVector2D::Distance(Screen, FVector2D(X, Y));
			if (D < Best) { Best = D; OutIndex = i; }
		}
	}
	return OutIndex != INDEX_NONE;
}

// ── create mode ─────────────────────────────────────────────────────────────
void ARoseCharSelectDirector::SetCreateMode(bool bOn)
{
	bCreateMode = bOn;
	for (ARoseCharacterCreator* A : Avatars)
		if (A) A->SetActorHiddenInGame(bOn);
	if (CreateAvatar)
	{
		CreateAvatar->SetActorLocationAndRotation(CreatePedestal(), GetActorRotation());
		CreateAvatar->SetActorHiddenInGame(!bOn);
	}
}

ARoseCharacterCreator* ARoseCharSelectDirector::GetCreateAvatar() const
{
	return CreateAvatar;
}

// ── camera framing ──────────────────────────────────────────────────────────
bool ARoseCharSelectDirector::GetDesiredFraming(FVector& OutLoc, FVector& OutLookAt) const
{
	const FVector Up(0, 0, 1);
	FVector Target;
	float Dist;

	if (bCreateMode)
	{
		Target = CreatePedestal();
		Dist = CreateDist;
	}
	else if (Avatars.IsValidIndex(SelectedIndex) && Avatars[SelectedIndex])
	{
		Target = Avatars[SelectedIndex]->GetActorLocation();
		Dist = CamFocusDist;
	}
	else
	{
		Target = ArcCenter();
		Dist = CamWideDist;
	}

	OutLoc = Target + Forward() * Dist + Up * CamHeight;
	OutLookAt = Target + Up * LookHeight;
	return true;
}

// ── layout helpers ──────────────────────────────────────────────────────────
FVector ARoseCharSelectDirector::SlotLocation(int32 Index, int32 Count) const
{
	if (Count <= 0) return ArcCenter();
	const float Half = (Count - 1) * 0.5f;
	const float Lateral = (Index - Half) * SlotSpacing;
	const float T = (Half > 0.f) ? (Index - Half) / Half : 0.f;   // [-1,1]
	const float Depth = -ArcDepth * (1.f - T * T);                // middle furthest from camera
	return ArcCenter() + Right() * Lateral + Forward() * Depth;
}

FVector ARoseCharSelectDirector::CreatePedestal() const
{
	return ArcCenter() + Forward() * 40.f;
}
