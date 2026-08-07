#include "RoseWarpPortal.h"

#include "RoseCharacter.h"
#include "RoseGameInstance.h"
#include "Components/BoxComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Kismet/GameplayStatics.h"

ARoseWarpPortal::ARoseWarpPortal()
{
	PrimaryActorTick.bCanEverTick = true;

	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	SetRootComponent(Trigger);
	Trigger->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Trigger->SetCollisionResponseToAllChannels(ECR_Ignore);
	Trigger->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Trigger);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 260.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(34.f);
	Label->SetTextRenderColor(FColor::Cyan);
}

void ARoseWarpPortal::BeginPlay()
{
	Super::BeginPlay();
	Trigger->SetBoxExtent(TriggerExtent);

	// A dedicated server has no renderer and no fonts: the floating destination
	// label is pure decoration, and driving a UTextRenderComponent there is an
	// access violation.  Drop it (and the billboard tick that only exists for
	// it); the trigger volume — the part that actually warps players — stays.
	if (IsNetMode(NM_DedicatedServer))
	{
		if (Label)
		{
			Label->DestroyComponent();
			Label = nullptr;
		}
		SetActorTickEnabled(false);
	}
	else
	{
		Label->SetText(FText::FromString(DestZoneName.IsEmpty() ? DestLevel : DestZoneName));
	}

	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ARoseWarpPortal::OnTriggerBegin);
}

void ARoseWarpPortal::OnTriggerBegin(UPrimitiveComponent*, AActor* Other,
	UPrimitiveComponent*, int32, bool, const FHitResult&)
{
	ARoseCharacter* Player = Cast<ARoseCharacter>(Other);
	if (!Player || DestLevel.IsEmpty())
		return;
	// Spawn protection: don't chain-trigger right after arriving in a level.
	if (GetWorld()->GetTimeSeconds() - Player->CreationTime < 2.f)
		return;

	const FString Current = GetWorld()->GetMapName();   // may carry PIE prefix
	if (Current.EndsWith(DestLevel))
	{
		// Same-zone warp: move in place, ground-snapped.
		float Z = Player->GetActorLocation().Z;
		FHitResult Hit;
		if (GetWorld()->LineTraceSingleByChannel(Hit,
				FVector(DestX, DestY, Z + 5000.f), FVector(DestX, DestY, Z - 10000.f),
				ECC_Visibility))
			Z = Hit.ImpactPoint.Z;
		Player->SetActorLocation(FVector(DestX, DestY, Z + 100.f));
		return;
	}
	// Snapshot the character first — the destination level respawns it.
	URoseGameInstance::WarpToLevel(Player, DestLevel,
		FString::Printf(TEXT("WarpX=%.0f?WarpY=%.0f"), DestX, DestY));
}

void ARoseWarpPortal::Tick(float Dt)
{
	Super::Tick(Dt);
	// Billboard the label like the other overhead texts.
	if (!Label) return;   // dedicated server: no label, nothing to face
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		if (PC->PlayerCameraManager)
		{
			const FVector ToCam = PC->PlayerCameraManager->GetCameraLocation() - Label->GetComponentLocation();
			Label->SetWorldRotation((-ToCam).Rotation());
		}
}
