// A ROSE warp gate: trigger volume placed from the zone IFO's WarpPoint
// objects.  Faithful chain (gs_user.cpp Recv_cli_TELEPORT_REQ): gate warp_id →
// WARP.STB row → destination zone + named event position → the destination
// zone's ZON event-position block.  Same-zone warps teleport in place; cross-
// zone warps OpenLevel the destination map with the arrival point passed as
// URL options (parsed by ARoseCharacter::BeginPlay).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoseWarpPortal.generated.h"

class UBoxComponent;
class UTextRenderComponent;

UCLASS()
class ROSEUE_API ARoseWarpPortal : public AActor
{
	GENERATED_BODY()

public:
	ARoseWarpPortal();

	// Destination level asset name, e.g. "L_JD01" (empty = dead gate).
	UPROPERTY(EditAnywhere, Category = "Rose|Warp") FString DestLevel;
	// Arrival point in the destination level's UE world space (X, Y; Z is
	// ground-traced on arrival).
	UPROPERTY(EditAnywhere, Category = "Rose|Warp") float DestX = 0.f;
	UPROPERTY(EditAnywhere, Category = "Rose|Warp") float DestY = 0.f;
	// Display name shown over the gate ("Zant", "Goblin Cave B1"...).
	UPROPERTY(EditAnywhere, Category = "Rose|Warp") FString DestZoneName;
	// Trigger half-extents (cm) — from the IFO gate scale.
	UPROPERTY(EditAnywhere, Category = "Rose|Warp") FVector TriggerExtent = FVector(200.f, 200.f, 250.f);

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere) UBoxComponent* Trigger;
	UPROPERTY(VisibleAnywhere) UTextRenderComponent* Label;

	UFUNCTION()
	void OnTriggerBegin(UPrimitiveComponent* OverlappedComp, AActor* Other,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& Sweep);
};
