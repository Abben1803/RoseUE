// ARoseGroundItem — a dropped item lying on the ground (ROSE loot is picked up
// MANUALLY, not auto-looted).  A monster's death rolls the drop table and spawns
// one of these at the corpse; the player walks over and presses the pickup key
// (ARoseCharacter::PickUpNearest) to collect it into the bag / zuly.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoseGroundItem.generated.h"

class UStaticMeshComponent;
class UTextRenderComponent;

UCLASS()
class ROSEUE_API ARoseGroundItem : public AActor
{
	GENERATED_BODY()

public:
	ARoseGroundItem();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ── Payload ──────────────────────────────────────────────────────────────
	// Money drop: bIsMoney=true, Zuly set.  Item drop: Slot/ItemId/Count(+ rolled
	// Bonus/bAppraised) set.  DisplayName is the resolved label for the world text.
	//
	// The whole payload replicates: the drop is rolled on the server (only it has
	// the authoritative RNG and the killer's Charm), and every client needs the
	// values to draw the world label.  One shared OnRep redraws it once.
	UPROPERTY(ReplicatedUsing = OnRep_Payload) bool    bIsMoney = false;
	UPROPERTY(ReplicatedUsing = OnRep_Payload) int32   Zuly = 0;
	UPROPERTY(ReplicatedUsing = OnRep_Payload) FString Slot;
	UPROPERTY(ReplicatedUsing = OnRep_Payload) int32   ItemId = 0;
	UPROPERTY(ReplicatedUsing = OnRep_Payload) int32   Count = 1;
	UPROPERTY(ReplicatedUsing = OnRep_Payload) int32   Bonus = 0;
	UPROPERTY(ReplicatedUsing = OnRep_Payload) bool    bAppraised = true;
	UPROPERTY(ReplicatedUsing = OnRep_Payload) FString DisplayName;
	// LIST_FIELDITEM row — the item's real ground model.  Replicated with the
	// rest of the payload because the client draws the mesh and cannot look it
	// up: the item DataTables live on ARoseCharacter, which a loose drop actor
	// has no handle to.  Resolved once by the server from the row it already
	// read to roll the drop.
	UPROPERTY(ReplicatedUsing = OnRep_Payload) int32   FieldModel = 0;
	UFUNCTION() void OnRep_Payload() { RefreshVisual(); }

	void InitMoney(int32 InZuly);
	void InitItem(const FString& InSlot, int32 InId, int32 InCount, int32 InBonus,
	              bool bInAppraised, const FString& InName, int32 InFieldModel);

	// Refresh the world label + colour from the payload.
	void RefreshVisual();

protected:
	/** Swap the placeholder cube for SM_field_<FieldModel> when one exists. */
	void ApplyDropMesh();

public:

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere) UStaticMeshComponent* Mesh;
	UPROPERTY(VisibleAnywhere) UTextRenderComponent* Label;

	// Despawn timer (ROSE loot lingers, then vanishes).
	UPROPERTY(EditAnywhere) float Lifetime = 120.f;
	float Age = 0.f;
	float BobPhase = 0.f;
	float BaseZ = 0.f;
};
