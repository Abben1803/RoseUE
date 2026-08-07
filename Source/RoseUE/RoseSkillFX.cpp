#include "RoseSkillFX.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	const TCHAR* kFXMaterial = TEXT("/Game/Effects/M_RoseFX.M_RoseFX");
	const TCHAR* kSphere = TEXT("/Engine/BasicShapes/Sphere.Sphere");
}

ARoseSkillFX::ARoseSkillFX()
{
	PrimaryActorTick.bCanEverTick = true;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FX"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
}

FLinearColor ARoseSkillFX::ColorForDamageType(int32 DamageType)
{
	switch (DamageType)
	{
	case 2:  return FLinearColor(0.25f, 0.55f, 1.f);    // magic — blue
	case 3:  return FLinearColor(1.f, 0.85f, 0.25f);    // unarmed — gold
	default: return FLinearColor(1.f, 0.45f, 0.15f);    // weapon — orange
	}
}

ARoseSkillFX* ARoseSkillFX::Spawn(UWorld* World, const FVector& Location, ERoseFXStyle Style,
	const FLinearColor& Color, float Scale, float Lifetime, AActor* FollowActor)
{
	if (!World)
		return nullptr;
	FActorSpawnParameters P;
	P.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARoseSkillFX* FX = World->SpawnActor<ARoseSkillFX>(
		ARoseSkillFX::StaticClass(), Location, FRotator::ZeroRotator, P);
	if (FX)
		FX->Init(Style, Color, Scale, Lifetime, FollowActor);
	return FX;
}

void ARoseSkillFX::Init(ERoseFXStyle InStyle, const FLinearColor& InColor, float InScale,
	float InLifetime, AActor* InFollow)
{
	Style = InStyle;
	Color = InColor;
	BaseScale = FMath::Max(0.1f, InScale);
	Lifetime = FMath::Max(0.1f, InLifetime);
	Follow = InFollow;
	if (InFollow)
		FollowZ = GetActorLocation().Z - InFollow->GetActorLocation().Z;

	UStaticMesh* Shape = LoadObject<UStaticMesh>(nullptr, kSphere);
	UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, kFXMaterial);
	if (!Shape || !Base)
	{
		// FX assets absent (ue5_make_fx_assets.py not run) — cosmetic no-op.
		Destroy();
		return;
	}
	Mesh->SetStaticMesh(Shape);
	MID = UMaterialInstanceDynamic::Create(Base, this);
	Mesh->SetMaterial(0, MID);
	Tick(0.f);   // apply the frame-0 scale/alpha before first render
}

void ARoseSkillFX::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!MID)
		return;

	Age += DeltaSeconds;
	const float T = FMath::Clamp(Age / Lifetime, 0.f, 1.f);
	float Alpha = 1.f - T;                    // default: fade out
	FVector Scale3(1.f);

	switch (Style)
	{
	case ERoseFXStyle::CastBurst:
		// Swell from the caster: 60 → 150 cm.
		Scale3 = FVector(FMath::Lerp(0.6f, 1.5f, T) * BaseScale);
		Alpha = (1.f - T) * 0.8f;
		break;
	case ERoseFXStyle::HitBurst:
		// Fast impact pop: 40 → 120 cm.
		Scale3 = FVector(FMath::Lerp(0.4f, 1.2f, T) * BaseScale);
		break;
	case ERoseFXStyle::BuffAura:
		// Gentle orb rising up the character; sine fade in-out.
		Scale3 = FVector(1.1f * BaseScale);
		Alpha = FMath::Sin(T * PI) * 0.6f;
		if (AActor* F = Follow.Get())
			SetActorLocation(F->GetActorLocation()
				+ FVector(0.f, 0.f, FollowZ + T * 80.f));
		break;
	case ERoseFXStyle::GroundRing:
		// Expanding flattened shockwave: 60 → 250 cm across.
		Scale3 = FVector(FMath::Lerp(0.6f, 2.5f, T) * BaseScale,
		                 FMath::Lerp(0.6f, 2.5f, T) * BaseScale, 0.12f * BaseScale);
		break;
	}

	SetActorScale3D(Scale3);
	// Emissive pop: over-drive the color; alpha runs the fade.
	MID->SetVectorParameterValue(TEXT("Color"), Color * 4.f);
	MID->SetScalarParameterValue(TEXT("Alpha"), Alpha);

	if (Age >= Lifetime)
		Destroy();
}
