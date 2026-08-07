// ARoseCharSelectPawn — the Character Select camera.  Eases toward the framing
// the ARoseCharSelectDirector requests (wide arc view → focus on the selected /
// creating avatar), giving the ROSE intro-pan → focus feel without ZMO camera
// motions.  Left-clicks not consumed by the Slate UI pick an avatar in the arc
// (double-click enters the world as that character).  Falls back to a slow orbit
// if no director is present.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "RoseCharSelectPawn.generated.h"

class UCameraComponent;
class ARoseCharSelectDirector;

UCLASS()
class ROSEUE_API ARoseCharSelectPawn : public APawn
{
	GENERATED_BODY()

public:
	ARoseCharSelectPawn();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void SetupPlayerInputComponent(UInputComponent* Input) override;

	UPROPERTY(VisibleAnywhere, Category = "Rose") UCameraComponent* Camera;

	// Interp responsiveness (higher = snappier).
	UPROPERTY(EditAnywhere, Category = "Rose") float MoveInterpSpeed = 3.0f;
	UPROPERTY(EditAnywhere, Category = "Rose") float LookInterpSpeed = 4.0f;

	// Orbit fallback (used when no director is present).
	UPROPERTY(EditAnywhere, Category = "Rose") FVector FocusPoint = FVector::ZeroVector;
	UPROPERTY(EditAnywhere, Category = "Rose") float OrbitRadius = 500.f;
	UPROPERTY(EditAnywhere, Category = "Rose") float OrbitHeight = 220.f;
	UPROPERTY(EditAnywhere, Category = "Rose") float OrbitDegPerSec = 8.f;

private:
	TWeakObjectPtr<ARoseCharSelectDirector> Director;
	FVector LookTarget = FVector::ZeroVector;
	float Elapsed = 0.f;
	double LastClickTime = 0.0;
	int32 LastClickIndex = -1;

	void OnClick();
	void EnterSelected();
	void Orbit(float Dt);
};
