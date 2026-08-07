#include "RoseNpc.h"
#include "Components/TextRenderComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/CapsuleComponent.h"

ARoseNpc::ARoseNpc()
{
	// Town NPCs are scenery-stable: no AI wander, no gravity fuss.
	bAggressive = false;
	AggroRange = 0.f;
	WanderRadius = 0.f;

	NameLabel = CreateDefaultSubobject<UTextRenderComponent>(TEXT("NameLabel"));
	NameLabel->SetupAttachment(RootComponent);
	NameLabel->SetHorizontalAlignment(EHTA_Center);
	NameLabel->SetWorldSize(28.f);
	NameLabel->SetTextRenderColor(FColor(160, 255, 160));

	// NOTE: the capsule deliberately keeps BLOCKING the Pawn channel.
	//
	// ARoseCharacter::OnLeftClick picks with GetHitResultUnderCursor(ECC_Pawn),
	// so an NPC that merely overlaps Pawn cannot be clicked at all — that is a
	// trace channel, not just physics.  Making them overlap here is what broke
	// talking to NPCs entirely.
	//
	// "Do not body-block the player" is handled on the PLAYER instead
	// (ARoseCharacter::RefreshNpcMoveIgnores), which adds each NPC to the
	// capsule's move-ignore list: the trace still hits them, the movement does
	// not.  That separates the two concerns the collision response conflates.
}

void ARoseNpc::BeginPlay()
{
	Super::BeginPlay();   // ARoseMonster::BeginPlay — Initialize() + EnterIdle
	if (!bInitialized)
		return;           // Super already destroyed us (model not imported)

	// The floating name is decoration only — a dedicated server has no renderer
	// or fonts and crashes driving a UTextRenderComponent.
	if (IsNetMode(NM_DedicatedServer))
	{
		if (NameLabel)
		{
			NameLabel->DestroyComponent();
			NameLabel = nullptr;
		}
		return;
	}

	NameLabel->SetText(FText::FromString(GetDisplayName()));
	// Float the label just above the capsule.
	NameLabel->SetRelativeLocation(FVector(0.f, 0.f,
		GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 40.f));
}

void ARoseNpc::Tick(float DeltaSeconds)
{
	// TickAI is overridden to nothing — the idle anim from EnterIdle just
	// loops; here the label billboards toward the camera.
	Super::Tick(DeltaSeconds);
	if (!bInitialized || !NameLabel) return;

	if (APlayerCameraManager* Cam = GetWorld()->GetFirstPlayerController()
		? GetWorld()->GetFirstPlayerController()->PlayerCameraManager : nullptr)
	{
		const FVector ToCam = Cam->GetCameraLocation() - NameLabel->GetComponentLocation();
		NameLabel->SetWorldRotation(FRotator(0.f, ToCam.Rotation().Yaw, 0.f));
	}
}
