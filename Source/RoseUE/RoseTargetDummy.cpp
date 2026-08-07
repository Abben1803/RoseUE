#include "RoseTargetDummy.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"
#include "DrawDebugHelpers.h"
#include "GameFramework/PlayerController.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"

ARoseTargetDummy::ARoseTargetDummy()
{
	PrimaryActorTick.bCanEverTick = true;

	Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	Body->SetupAttachment(Root);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cyl(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (Cyl.Succeeded()) Body->SetStaticMesh(Cyl.Object);
	Body->SetRelativeScale3D(BaseScale);
	// Cylinder pivot is centred; lift it so the base sits on the ground.
	Body->SetRelativeLocation(FVector(0.f, 0.f, BaseScale.Z * 50.f));
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionResponseToAllChannels(ECR_Block);

	HPText = CreateDefaultSubobject<UTextRenderComponent>(TEXT("HPText"));
	HPText->SetupAttachment(Root);
	HPText->SetRelativeLocation(FVector(0.f, 0.f, 210.f));
	HPText->SetHorizontalAlignment(EHTA_Center);
	HPText->SetWorldSize(36.f);
	HPText->SetTextRenderColor(FColor::Green);
}

void ARoseTargetDummy::BeginPlay()
{
	Super::BeginPlay();
	HP = MaxHP;
	bDead = false;
	Body->SetRelativeScale3D(BaseScale);
	Refresh();
}

void ARoseTargetDummy::Refresh()
{
	HPText->SetText(FText::FromString(bDead
		? TEXT("KO")
		: FString::Printf(TEXT("%d / %d"), FMath::CeilToInt(HP), FMath::CeilToInt(MaxHP))));
	HPText->SetTextRenderColor(bDead ? FColor::Silver : (FlashTimer > 0.f ? FColor::Red : FColor::Green));
}

void ARoseTargetDummy::ShowMiss()
{
	if (bDead) return;
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 160.f),
		TEXT("MISS"), nullptr, FColor::Silver, 0.8f, true);
}

void ARoseTargetDummy::ApplyHit(float Damage, const FVector& /*FromDir*/, bool bCritical)
{
	if (bDead) return;
	HP = FMath::Max(0.f, HP - Damage);
	SquashTimer = 0.15f;
	FlashTimer = 0.15f;
	DrawDebugString(GetWorld(), GetActorLocation() + FVector(0, 0, 160.f),
		FString::Printf(TEXT("-%d%s"), FMath::CeilToInt(Damage), bCritical ? TEXT("!!") : TEXT("")),
		nullptr, bCritical ? FColor::Orange : FColor::Yellow, 0.8f, true);
	if (HP <= 0.f)
	{
		bDead = true;
		RespawnTimer = RespawnDelay;
		Body->SetVisibility(false);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Refresh();
}

void ARoseTargetDummy::Tick(float Dt)
{
	Super::Tick(Dt);

	// Hit punch: squash wider+shorter, recover over the timer.
	if (SquashTimer > 0.f)
	{
		SquashTimer = FMath::Max(0.f, SquashTimer - Dt);
		const float a = SquashTimer / 0.15f;   // 1 → 0
		Body->SetRelativeScale3D(BaseScale * FVector(1.f + 0.25f * a, 1.f + 0.25f * a, 1.f - 0.30f * a));
	}
	if (FlashTimer > 0.f)
	{
		FlashTimer = FMath::Max(0.f, FlashTimer - Dt);
		if (FlashTimer == 0.f) Refresh();
	}

	// Respawn after death.
	if (bDead)
	{
		RespawnTimer -= Dt;
		if (RespawnTimer <= 0.f)
		{
			bDead = false;
			HP = MaxHP;
			Body->SetVisibility(true);
			Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Body->SetRelativeScale3D(BaseScale);
			Refresh();
		}
	}

	// Billboard the HP text toward the player camera so it's always readable.
	if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
	{
		if (PC->PlayerCameraManager)
		{
			const FVector ToCam = PC->PlayerCameraManager->GetCameraLocation() - HPText->GetComponentLocation();
			HPText->SetWorldRotation((-ToCam).Rotation());   // text +X reads from the camera side
		}
	}
}
