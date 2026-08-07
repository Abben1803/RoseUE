// ROSE PAT vehicle (cart / castle gear / mount) — visual actor.
//
// Faithful to CObjCART (src/client/cobjcart.cpp): a cart is ONE model on the
// PAT skeleton whose render parts come from the equipped LIST_PAT items
// (t_eRidePART BODY/ENGINE/LEG/ABIL/ARMS). Here the equipped parts merge into
// a single skeletal mesh via USkeletalMergingLibrary onto the imported base
// skeleton (/Game/Pat/base_<21|31>) — the same recipe as RoseCharacter's
// modular avatar. A mount is the degenerate case: one BODY item, no other
// parts, and it rides the cart skeleton.
//
// The vehicle owns its own animation state. CObjCART resolves a clip as
//     TYPE_MOTION[ BODY.PatMotion + action ][ ARMS.PatMotion ]
// — ROW from the body, COLUMN from the arms ("Cart 의 경우는 무기에 따른
// 모션이 아니라 ARMS 테이블의 모션 타입에 의존한다"). pat_motion.csv is that
// grid pre-resolved to animation names.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RosePatTypes.h"
#include "RoseCart.generated.h"

class UAnimSequence;

UCLASS()
class ROSEUE_API ARoseCart : public AActor
{
	GENERATED_BODY()

public:
	ARoseCart();

	// Merge the equipped PAT items (pat_parts.csv ids, -1 = empty) into the
	// vehicle mesh and latch its motion rows. PartIds is MAX_RIDING_PART long;
	// a mount only fills RIDE_PART_BODY. False if nothing merged.
	bool Build(const TArray<int32>& PartIds, UDataTable* PartTable, UDataTable* MotionTable);

	// Play the vehicle's own clip for an action (CObjCART::GetANI_*).
	void PlayPatAnim(ERosePatAnim Action);

	// Animation the RIDER should play for the same action — the avatar motion
	// row, which is a different row base on the same grid.
	UAnimSequence* ResolveRiderAnim(ERosePatAnim Action, const FString& RiderSkeletonPath) const;

	ERosePatClass GetPatClass() const { return PatClass; }
	// Morph-style mounts (LIST_PAT "Hide Player") replace the avatar entirely.
	bool ShouldHideRider() const { return bHideRider; }
	// Cart/castle gear burn fuel; mounts never do.
	bool UsesFuel() const { return PatClass == ERosePatClass::Cart || PatClass == ERosePatClass::CastleGear; }
	float GetMaxFuel() const { return MaxFuel; }
	float GetFuelRate() const { return FuelRate; }
	// Percent of walk speed for cart/CG, absolute cm/s for mounts (see MoveSpeed).
	int32 GetMoveSpeed() const { return MoveSpeed; }
	float GetModelScale() const { return ModelScale; }

	// Where the rider sits, relative to the vehicle root.
	//
	// CObjCART links the avatar to a dummy on the cart body (dummy 0
	// "p_cart01" for the driver, dummy 10 for a second passenger). The PAT
	// skeletons DO carry those dummies, but rose_combine_anims emits bones
	// only, so there is no socket to attach to yet — until the converter
	// exports dummies, the seat is this offset, tuned per family.
	// TODO: emit ZMD dummies as sockets and attach to dummy 0 instead.
	FVector GetSeatOffset() const { return SeatOffset; }
    FRotator GetSeatRotation() const { return SeatRotation; }

	// Seat Offset
	UPROPERTY(EditAnywhere, Category = "Rose|Pat") FVector SeatOffset = FVector(0.f, 0.f, 88.0f);
	// Seat Rotation
    UPROPERTY(EditAnywhere, Category = "Rose|Pat") FRotator SeatRotation = FRotator(0.f, 90.f, 0.0f);

	USkeletalMeshComponent* GetMesh() const { return Mesh; }

private:
	// Resolve one cell of the motion grid, with CObjCART::Get_MOTION's
	// fallbacks: missing column -> column 0, missing action -> action 0.
	UAnimSequence* ResolveAnim(int32 RowBase, int32 Col, ERosePatAnim Action,
	                           int32 FallbackRowBase, const FString& SkeletonPath) const;

	UPROPERTY() USkeletalMeshComponent* Mesh;
	UPROPERTY() UDataTable* MotionTable = nullptr;

	ERosePatClass PatClass = ERosePatClass::None;
	int32 Pet = 21;              // base skeleton family (21 cart, 31 castle gear)
	int32 VehicleRow = PAT_FALLBACK_VEHICLE_ROW;   // from BODY.PatMotion
	int32 RiderRow = PAT_FALLBACK_RIDER_ROW;       // from BODY.AvatarMotion
	int32 MotionCol = 0;                            // from ARMS.PatMotion
	int32 MaxFuel = 0;
	int32 FuelRate = 0;
	int32 MoveSpeed = 0;
	float ModelScale = 1.f;
	bool bHideRider = false;
};
