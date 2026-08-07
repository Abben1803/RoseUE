// A simple training dummy you can hit with the equipped weapon: HP, a hit reaction
// (squash + floating damage), and auto-respawn.  Placeable, and ARoseCharacter also
// spawns a few in front of the player for quick testing.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoseTargetDummy.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS()
class ROSEUE_API ARoseTargetDummy : public AActor
{
	GENERATED_BODY()

public:
	ARoseTargetDummy();
	virtual void Tick(float DeltaSeconds) override;

	// Take a hit: subtract Damage, react; at 0 HP it "dies" and respawns after a delay.
	void ApplyHit(float Damage, const FVector& FromDir, bool bCritical = false);
	// The attack was dodged (ROSE Get_SuccessRATE <= 0) — show MISS, no damage.
	void ShowMiss();

	UPROPERTY(EditAnywhere, Category = "Rose|Dummy") float MaxHP = 100.f;
	UPROPERTY(EditAnywhere, Category = "Rose|Dummy") float RespawnDelay = 3.f;

	// Defender stats for the ROSE combat roll (calculation.cpp): the attacker's
	// hit is checked against Avoid (dodge), damage against Defense.
	UPROPERTY(EditAnywhere, Category = "Rose|Dummy") int32 Level = 3;
	UPROPERTY(EditAnywhere, Category = "Rose|Dummy") int32 Avoid = 25;
	UPROPERTY(EditAnywhere, Category = "Rose|Dummy") int32 Defense = 40;

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere) USceneComponent* Root;
	UPROPERTY(VisibleAnywhere) UStaticMeshComponent* Body;
	UPROPERTY(VisibleAnywhere) UTextRenderComponent* HPText;

	float HP = 100.f;
	bool  bDead = false;
	float RespawnTimer = 0.f;
	float SquashTimer = 0.f;     // hit-reaction punch
	float FlashTimer = 0.f;      // HP-text red flash
	const FVector BaseScale = FVector(0.7f, 0.7f, 1.8f);

	void Refresh();              // update HP text + colour
};
