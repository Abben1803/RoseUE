// ARoseCharSelectCamPath — an editable spline the Character Select camera flies
// along (ROSE's select screen sweeps a camera through the town).  Place one in
// L_CharacterSelect and shape the spline through the scene; the camera pawn
// finds it and cruises it on a loop.  If none is present the pawn falls back to
// a slow orbit around the preview character.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoseCharSelectCamPath.generated.h"

class USplineComponent;

UCLASS()
class ROSEUE_API ARoseCharSelectCamPath : public AActor
{
	GENERATED_BODY()

public:
	ARoseCharSelectCamPath();

	UPROPERTY(VisibleAnywhere, Category = "Rose") USplineComponent* Spline;

	// Seconds to traverse the whole loop.
	UPROPERTY(EditAnywhere, Category = "Rose", meta = (ClampMin = "5.0")) float LoopSeconds = 40.f;

	// If set, the camera keeps looking at this world point; otherwise it looks
	// along the spline tangent (a true fly-through).
	UPROPERTY(EditAnywhere, Category = "Rose") bool bLookAtTarget = true;
	UPROPERTY(EditAnywhere, Category = "Rose") FVector LookAtWorld = FVector::ZeroVector;
};
