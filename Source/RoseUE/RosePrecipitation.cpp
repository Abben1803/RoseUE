#include "RosePrecipitation.h"

#include "Camera/PlayerCameraManager.h"
#include "UObject/ConstructorHelpers.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

namespace
{
	// Per-kind behaviour.  These are the numbers that decide whether it reads as
	// rain, snow or sand — everything else is shared machinery.
	struct FPrecipProfile
	{
		float FallSpeed;      // cm/s downward
		float SpeedJitter;    // +/- fraction, so drops do not move in lockstep
		FVector Wind;         // cm/s horizontal drift
		FVector2D Size;       // quad size: X across, Y along the fall
		float Softness;       // material edge falloff
		float Opacity;
		int32 Budget;         // instances at full intensity
	};

	FPrecipProfile ProfileFor(ERosePrecipKind Kind)
	{
		switch (Kind)
		{
		case ERosePrecipKind::Rain:
			// Fast and stretched: a raindrop crossing the frame in a fraction of
			// a second IS a streak, so the quad is long and thin.
			return { 1800.f, 0.15f, FVector(60.f, 0.f, 0.f),
			         FVector2D(1.6f, 26.f), 3.0f, 0.55f, 2600 };

		case ERosePrecipKind::Snow:
			// Slow, square and drifting.  The big jitter is what makes flakes
			// wander past each other instead of falling as a sheet.
			return { 110.f, 0.55f, FVector(35.f, 25.f, 0.f),
			         FVector2D(5.f, 5.f), 2.0f, 0.85f, 1800 };

		case ERosePrecipKind::Sand:
			// Nearly horizontal: the wind dwarfs the fall speed, which is what
			// separates a sandstorm from brown snow.
			return { 90.f, 0.6f, FVector(1500.f, 420.f, 0.f),
			         FVector2D(3.f, 9.f), 1.6f, 0.35f, 3000 };

		default:
			return { 0.f, 0.f, FVector::ZeroVector, FVector2D(1.f, 1.f), 2.f, 0.f, 0 };
		}
	}
}

ARosePrecipitation::ARosePrecipitation()
{
	PrimaryActorTick.bCanEverTick = true;
	// After the camera has moved, so the field is centred on where the camera
	// actually ended up this frame rather than where it was last frame.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	Mesh = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Particles"));
	SetRootComponent(Mesh);

	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);
	Mesh->bReceivesDecals = false;
	Mesh->SetCanEverAffectNavigation(false);
	// Thousands of tiny translucent quads gain nothing from occlusion queries
	// and cost a round trip each.
	// Thousands of unlit quads would each be an emissive light to Lumen.
	Mesh->SetEmissiveLightSource(false);
	Mesh->bDisableCollision = true;
	Mesh->SetMobility(EComponentMobility::Movable);

	// THE MESH. Without it every AddInstance adds an instance of nothing: the
	// component reports the right instance count, the material binds, no error
	// is logged anywhere, and the screen stays empty.  That is exactly how this
	// shipped the first time.
	//
	// The engine's unit Plane is 100x100 cm lying in local XY with its normal on
	// +Z — which is what the scale and facing below are written against.
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PlaneMesh(
		TEXT("/Engine/BasicShapes/Plane.Plane"));
	if (PlaneMesh.Succeeded())
	{
		Mesh->SetStaticMesh(PlaneMesh.Object);
	}
}

// Local X spans the quad's width and local Y its length; the plane's normal is
// local Z.  The engine plane is 100 cm, so a size in centimetres is size/100.
//
// The first version scaled (Size.X, Size.X, Size.Y), putting the streak LENGTH
// on the normal axis — which stretches a flat plane along the one axis it has
// no extent in, so rain drops stayed square however long they were meant to be.
FVector ARosePrecipitation::QuadScale(const FVector2D& Size)
{
	return FVector(Size.X * 0.01f, Size.Y * 0.01f, 1.f);
}

FVector ARosePrecipitation::RandomPointInField(const FVector& Centre) const
{
	return Centre + FVector(
		FMath::FRandRange(-FieldRadius, FieldRadius),
		FMath::FRandRange(-FieldRadius, FieldRadius),
		FMath::FRandRange(-FieldHeight * 0.5f, FieldHeight * 0.5f));
}

// Allocate the kind's FULL budget once, not the currently-visible count.
//
// Intensity blends over several seconds, so a count-driven rebuild re-allocated
// thousands of instances EVERY FRAME for the length of the blend — clearing and
// re-adding the whole buffer, and resetting every position while it did.
// Allocating once per KIND and hiding the surplus per frame (zero scale) makes
// the blend free and keeps particles moving continuously through it.
void ARosePrecipitation::Rebuild(int32 Count)
{
	Count = FMath::Clamp(Count, 0, MaxParticles);
	if (Count == AllocatedCount)
		return;

	const FPrecipProfile P = ProfileFor(Kind);

	FVector Centre = GetActorLocation();
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
		if (const APlayerCameraManager* Cam = PC->PlayerCameraManager)
			Centre = Cam->GetCameraLocation();

	Mesh->ClearInstances();
	Positions.Reset(Count);
	Velocities.Reset(Count);

	for (int32 i = 0; i < Count; ++i)
	{
		const FVector Pos = RandomPointInField(Centre);
		Positions.Add(Pos);

		// Jittered fall speed per particle.
		const float Jit = 1.f + FMath::FRandRange(-P.SpeedJitter, P.SpeedJitter);
		Velocities.Add(FVector(P.Wind.X, P.Wind.Y, -P.FallSpeed * Jit));

		// Instances are stored in COMPONENT space and the component sits at the
		// origin, so a world position is the transform directly.
		FTransform T;
		T.SetLocation(Pos);
		T.SetScale3D(QuadScale(P.Size));
		Mesh->AddInstance(T, /*bWorldSpace=*/true);
	}

	AllocatedCount = Count;
}

void ARosePrecipitation::Configure(ERosePrecipKind InKind, float InIntensity, FLinearColor Colour)
{
	InIntensity = FMath::Clamp(InIntensity, 0.f, 1.f);

	const bool bKindChanged = (InKind != Kind);
	Kind = InKind;
	Intensity = InIntensity;

	const FPrecipProfile P = ProfileFor(Kind);

	if (MID)
	{
		MID->SetVectorParameterValue(TEXT("PrecipColor"), Colour);
		MID->SetScalarParameterValue(TEXT("Softness"), P.Softness);
		MID->SetScalarParameterValue(TEXT("Opacity"), P.Opacity);
	}

	// Allocation follows the KIND (its full budget); visibility follows the
	// INTENSITY.  Light rain is genuinely fewer drops, not faded ones — fading
	// them just reads as haze.
	if (bKindChanged)
		AllocatedCount = -1;   // sizes and speeds all changed; rebuild for real

	Rebuild(Kind == ERosePrecipKind::None ? 0 : FMath::Min(P.Budget, MaxParticles));

	ActiveCount = (Kind == ERosePrecipKind::None)
		? 0 : FMath::RoundToInt(AllocatedCount * Intensity);

	SetActorHiddenInGame(ActiveCount <= 0);

	if (bKindChanged)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Rose] precip: kind %d, intensity %.2f, %d/%d instances, mesh %s"),
			(int32)Kind, Intensity, ActiveCount, AllocatedCount,
			Mesh->GetStaticMesh() ? TEXT("ok") : TEXT("MISSING"));
	}
}

void ARosePrecipitation::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (AllocatedCount <= 0 || Kind == ERosePrecipKind::None)
		return;

	// Lazily bind the material the first time we have instances.
	if (!MID)
	{
		if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(
			nullptr, TEXT("/Game/Rose/Sky/M_RoseParticle.M_RoseParticle")))
		{
			MID = UMaterialInstanceDynamic::Create(Mat, this);
			Mesh->SetMaterial(0, MID);
			const FPrecipProfile P = ProfileFor(Kind);
			MID->SetScalarParameterValue(TEXT("Softness"), P.Softness);
			MID->SetScalarParameterValue(TEXT("Opacity"), P.Opacity);
		}
	}

	FVector Centre = GetActorLocation();
	FRotator CamRot = FRotator::ZeroRotator;
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (const APlayerCameraManager* Cam = PC->PlayerCameraManager)
		{
			Centre = Cam->GetCameraLocation();
			CamRot = Cam->GetCameraRotation();
		}
	}

	const FPrecipProfile P = ProfileFor(Kind);

	// Face the camera.  All instances share one rotation — cheaper than true
	// per-instance billboarding and indistinguishable at these sizes.
	//
	// MakeFromZY, not a hand-built FRotator: the plane's NORMAL is local Z and
	// its LENGTH is local Y, so the normal must point back down the camera's
	// forward vector while local Y stays as vertical as possible.  That second
	// constraint is what keeps rain streaks upright instead of rolling with the
	// camera. A pitch-90 rotator gets the normal roughly right and says nothing
	// about the length axis, which is why streaks came out arbitrarily spun.
	const FVector ToCamera = -CamRot.Vector();
	const FQuat Facing = FRotationMatrix::MakeFromZY(ToCamera, FVector::UpVector).ToQuat();

	TArray<FTransform> Transforms;
	Transforms.SetNum(AllocatedCount);

	const FVector Scale = QuadScale(P.Size);
	const float R = FieldRadius;
	const float H = FieldHeight * 0.5f;

	for (int32 i = 0; i < AllocatedCount; ++i)
	{
		// Surplus instances collapse to zero scale rather than being removed:
		// no reallocation, and they resume exactly where they were when the
		// weather picks up again.
		if (i >= ActiveCount)
		{
			Transforms[i] = FTransform(Facing, Positions[i], FVector::ZeroVector);
			continue;
		}

		FVector& Pos = Positions[i];
		Pos += Velocities[i] * DeltaSeconds;

		// WRAP relative to the camera rather than respawning at the top.
		//
		// Wrapping on all three axes is what lets the field follow the player
		// without a visible edge: walk in any direction and the particles you
		// leave behind reappear ahead of you.  Respawning only on Z would leave
		// a hole behind a running player.
		const FVector Rel = Pos - Centre;
		if (Rel.Z < -H)      Pos.Z += H * 2.f;
		else if (Rel.Z > H)  Pos.Z -= H * 2.f;
		if (Rel.X < -R)      Pos.X += R * 2.f;
		else if (Rel.X > R)  Pos.X -= R * 2.f;
		if (Rel.Y < -R)      Pos.Y += R * 2.f;
		else if (Rel.Y > R)  Pos.Y -= R * 2.f;

		Transforms[i] = FTransform(Facing, Pos, Scale);
	}

	// One batched update beats ActiveCount individual UpdateInstanceTransform
	// calls, each of which would dirty the render state on its own.
	Mesh->BatchUpdateInstancesTransforms(0, Transforms, /*bWorldSpace=*/true,
		/*bMarkRenderStateDirty=*/true, /*bTeleport=*/true);
}
