// ROSE ZSC constant-animation (SWITCH_CNST_ANI) runtime for map scenes.
// Drives building parts (windmill blades, clock hands, cranes, port vessels)
// with their original ZMO node animations, faithful to CFixedPART::LoadVisible
// (src/client/io_model.cpp): the ZMO's POSITION/ROTATION/SCALE channels replace
// the part's local transform under its ZSC parent each frame.
//
// Data: Content/MapAnims/<ZONE>.json written by tools/gen_zsc_part_manifest.py.
// Target actors carry a "RoseAnim_<gltfNodeIndex>" tag (set at import fix-up).
// All math is done in ROSE space (Z-up, cm) and converted with the same
// mirror the map import used: UE = diag(1,-1,1) * M * diag(1,-1,1).

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoseMapAnimManager.generated.h"

USTRUCT()
struct FRoseZmoClip
{
	GENERATED_BODY()

	float Fps = 30.f;
	int32 Frames = 0;
	// Empty arrays mean "channel absent — keep the static value".
	TArray<FVector3d> Pos;      // ROSE local position per frame (cm)
	TArray<FVector4d> Rot;      // ROSE local quat per frame (w,x,y,z)
	TArray<double> Scale;       // uniform scale per frame
};

struct FRoseAnimBinding
{
	TWeakObjectPtr<AActor> Actor;
	int32 ClipIndex = INDEX_NONE;
	FMatrix ParentRose = FMatrix::Identity;   // ZSC parent world (ROSE space)
	FVector3d StaticPos = FVector3d::ZeroVector;
	FVector4d StaticRot = FVector4d(1, 0, 0, 0);   // (w,x,y,z)
	FVector3d StaticScl = FVector3d(1, 1, 1);
};

UCLASS()
class ROSEUE_API ARoseMapAnimManager : public AActor
{
	GENERATED_BODY()

public:
	ARoseMapAnimManager();

	// Zone key (e.g. "JPT01") — set when the manager is placed into a level.
	UPROPERTY(EditAnywhere, Category = "Rose")
	FString ZoneKey;

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	bool LoadZoneJson();

	TArray<FRoseZmoClip> Clips;
	TMap<FString, int32> ClipIndexByPath;
	TArray<FRoseAnimBinding> Bindings;
	double PlayTime = 0.0;
};
