// Real precipitation: thousands of instances falling through the world.
//
// The first attempt was a camera-locked cylinder with a procedural material.
// That cannot work, and the reason is worth keeping: every pixel of a shell is
// the same distance from the eye, so nothing parallaxes as you move or turn.
// The brain reads "texture on glass", which is exactly the drifting-fog look.
//
// This spawns actual instances in a box around the camera and moves them in
// WORLD space.  They pass the camera at their own depths, go behind buildings,
// and streak past when you run — because they really are out there.
//
// Instanced, one draw call, no Niagara asset and no artist round trip: the mesh
// is the engine's unit plane and the material is built by RoseImportSky.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RosePrecipitation.generated.h"

class UInstancedStaticMeshComponent;
class UMaterialInstanceDynamic;

UENUM(BlueprintType)
enum class ERosePrecipKind : uint8
{
	None,
	Rain,
	Snow,
	Sand
};

UCLASS()
class ROSEUE_API ARosePrecipitation : public AActor
{
	GENERATED_BODY()

public:
	ARosePrecipitation();

	virtual void Tick(float DeltaSeconds) override;

	// Intensity 0..1 scales the number of live instances, so light rain is
	// genuinely fewer drops rather than the same drops faded out.
	UFUNCTION(BlueprintCallable, Category = "Rose|Weather")
	void Configure(ERosePrecipKind Kind, float Intensity, FLinearColor Colour);

	// Radius of the box the instances live in, centred on the camera.
	UPROPERTY(EditAnywhere, Category = "Rose|Weather")
	float FieldRadius = 2200.f;

	UPROPERTY(EditAnywhere, Category = "Rose|Weather")
	float FieldHeight = 1600.f;

	// The most instances any weather will use. Intensity scales down from here.
	UPROPERTY(EditAnywhere, Category = "Rose|Weather")
	int32 MaxParticles = 3000;

private:
	UPROPERTY(VisibleAnywhere, Category = "Rose|Weather")
	TObjectPtr<UInstancedStaticMeshComponent> Mesh;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> MID;

	ERosePrecipKind Kind = ERosePrecipKind::None;
	float Intensity = 0.f;
	int32 ActiveCount = 0;      // currently VISIBLE, follows intensity
	int32 AllocatedCount = 0;   // instances that exist, follows kind

	// Per-particle velocity, so drops do not all fall at exactly one speed —
	// uniform motion is the other half of what makes fake rain look fake.
	TArray<FVector> Velocities;
	TArray<FVector> Positions;

	// Quad size in centimetres -> component scale.  Shared by spawn and tick so
	// the two can never disagree about which axis is the length.
	static FVector QuadScale(const FVector2D& Size);

	void Rebuild(int32 Count);
	FVector RandomPointInField(const FVector& Centre) const;
};
