#include "RoseCart.h"

#include "Animation/AnimSequence.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "SkeletalMergingLibrary.h"

namespace
{
	// pat_parts.csv / pat_motion.csv row-name conventions (gen_pat_tables.py).
	FName PartRowName(int32 Id) { return FName(*FString::Printf(TEXT("pat_%d"), Id)); }
	FName MotionRowName(int32 Row, int32 Col) { return FName(*FString::Printf(TEXT("m_%d_%d"), Row, Col)); }

	// An imported GLB names each AnimSequence "<glb stem><TRACK NAME upper>",
	// so a track "car_move_01" in base_21.glb lands as "base_21CAR_MOVE_01".
	FString AnimAssetPath(const FString& SkeletonPath, const FString& Stem, const FString& AnimName)
	{
		const FString Asset = Stem + AnimName.ToUpper();
		return FString::Printf(TEXT("%s/%s.%s"), *SkeletonPath, *Asset, *Asset);
	}

	// Interchange names each imported track "<glb-stem><TRACK>" (baseCAR_SIT_M1,
	// base_21CAR_STOP_01).  The paths we get end in ".../<stem>/<stem>/
	// SkeletalMeshes", so the stem is the PARENT directory's name —
	// GetCleanFilename(SkeletonPath) returns "SkeletalMeshes", which built
	// "SkeletalMeshesCAR_SIT_M1" and made every anim lookup fail silently.
	FString AnimStemFromPath(const FString& SkeletonPath)
	{
		return FPaths::GetCleanFilename(FPaths::GetPath(SkeletonPath));
	}
}

ARoseCart::ARoseCart()
{
	PrimaryActorTick.bCanEverTick = false;
	Mesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("CartMesh"));
	SetRootComponent(Mesh);
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetAnimationMode(EAnimationMode::AnimationSingleNode);
}

bool ARoseCart::Build(const TArray<int32>& PartIds, UDataTable* PartTable, UDataTable* InMotionTable)
{
	MotionTable = InMotionTable;
	if (!PartTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseCart] pat_parts table missing — run gen_pat_tables.py and import it"));
		return false;
	}

	// The BODY part decides the family, the skeleton and both motion rows.
	const FRosePatPartRow* Body = PartIds.IsValidIndex(RIDE_PART_BODY) && PartIds[RIDE_PART_BODY] >= 0
		? PartTable->FindRow<FRosePatPartRow>(PartRowName(PartIds[RIDE_PART_BODY]), TEXT("RoseCart"), false)
		: nullptr;
	if (!Body)
	{
		UE_LOG(LogTemp, Log, TEXT("[RoseCart] no BODY part equipped"));
		return false;
	}

	PatClass = static_cast<ERosePatClass>(Body->PatClass);
	// Mounts ride the cart skeleton (build_pat_parts.py builds them onto it).
	Pet = (PatClass == ERosePatClass::CastleGear) ? 31 : 21;
	bHideRider = Body->HidePlayer != 0;
	ModelScale = (Body->Scale > 0 ? Body->Scale : 100) / 100.f;
	VehicleRow = Body->PatMotion > 0 ? Body->PatMotion : PAT_FALLBACK_VEHICLE_ROW;
	RiderRow = Body->AvatarMotion > 0 ? Body->AvatarMotion : PAT_FALLBACK_RIDER_ROW;

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *FString::Printf(
		TEXT("/Game/Pat/base_%d/base_%d/SkeletalMeshes/base_%d_Skeleton.base_%d_Skeleton"),
		Pet, Pet, Pet, Pet));
	if (!Skeleton)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseCart] base_%d skeleton missing — run ue5_import_pat.py"), Pet);
		return false;
	}

	// A mount is standalone: only its BODY item contributes geometry, and it
	// has neither fuel nor an ARMS part to pick a motion column with.
	const bool bAssembled = (PatClass == ERosePatClass::Cart || PatClass == ERosePatClass::CastleGear);

	TArray<TObjectPtr<USkeletalMesh>> Parts;
	for (int32 Slot = 0; Slot < PartIds.Num(); ++Slot)
	{
		const int32 Id = PartIds[Slot];
		if (Id < 0 || (!bAssembled && Slot != RIDE_PART_BODY))
			continue;

		if (const FRosePatPartRow* Row = PartTable->FindRow<FRosePatPartRow>(
				PartRowName(Id), TEXT("RoseCart"), false))
		{
			// Fuel comes from the ENGINE part, the motion column from ARMS.
			if (Slot == RIDE_PART_ENGINE)
			{
				MaxFuel = Row->MaxFuel;
				FuelRate = Row->FuelRate;
			}
			if (Slot == RIDE_PART_ARMS && Row->PatMotion >= 0)
				MotionCol = Row->PatMotion;
			// Cart/CG speeds are percentages that stack; a mount's is absolute.
			if (Row->MoveSpeed > 0)
				MoveSpeed = bAssembled ? FMath::Max(MoveSpeed, Row->MoveSpeed) : Row->MoveSpeed;
		}

		USkeletalMesh* M = LoadObject<USkeletalMesh>(nullptr, *FString::Printf(
			TEXT("/Game/Pat/pat_%d/pat_%d/SkeletalMeshes/pat_%d.pat_%d"), Id, Id, Id, Id));
		if (M)
			Parts.Add(M);
		else
			UE_LOG(LogTemp, Warning, TEXT("[RoseCart] pat_%d mesh missing"), Id);
	}
	if (Parts.Num() == 0)
		return false;

	FSkeletalMeshMergeParams Params;
	Params.MeshesToMerge = Parts;
	Params.Skeleton = Skeleton;
	USkeletalMesh* Merged = USkeletalMergingLibrary::MergeMeshes(Params);
	if (!Merged)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseCart] merge failed (%d parts)"), Parts.Num());
		return false;
	}

	Mesh->SetSkeletalMeshAsset(Merged);
	// Carts and castle gears render 1.2x in the client; mounts use their own
	// LIST_PAT "Scaling" instead.
	const float Extra = bAssembled ? 1.2f : 1.f;
	Mesh->SetRelativeScale3D(FVector(ModelScale * Extra));

	// Seat height by family: a castle gear is a walker and seats the pilot far
	// higher than a cart does. Scaled with the model so big mounts still fit.
	const float SeatZ = (PatClass == ERosePatClass::CastleGear) ? 190.f : 55.f;
	SeatOffset = FVector(0.f, 0.f, SeatZ * ModelScale * Extra);

	PlayPatAnim(ERosePatAnim::Stop1);
	return true;
}

UAnimSequence* ARoseCart::ResolveAnim(int32 RowBase, int32 Col, ERosePatAnim Action,
                                      int32 FallbackRowBase, const FString& SkeletonPath) const
{
	if (!MotionTable)
		return nullptr;

	const FString Stem = AnimStemFromPath(SkeletonPath);
	const int32 ActionIdx = static_cast<int32>(Action);

	// CObjCART::Get_MOTION retries action 0 when a cell is empty; we also fall
	// back from an odd ARMS column to column 0, and finally to the cart rows
	// for the mounts whose own rows fall outside TYPE_MOTION entirely.
	const int32 Rows[] = { RowBase, FallbackRowBase };
	for (const int32 Base : Rows)
	{
		if (Base <= 0)
			continue;
		const int32 Actions[] = { ActionIdx, 0 };
		for (const int32 A : Actions)
		{
			const int32 Cols[] = { Col, 0 };
			for (const int32 C : Cols)
			{
				const FRosePatMotionRow* Cell = MotionTable->FindRow<FRosePatMotionRow>(
					MotionRowName(Base + A, C), TEXT("RoseCart"), false);
				if (!Cell || Cell->AnimName.IsEmpty())
					continue;
				if (UAnimSequence* Anim = LoadObject<UAnimSequence>(
						nullptr, *AnimAssetPath(SkeletonPath, Stem, Cell->AnimName)))
					return Anim;
			}
		}
	}
	return nullptr;
}

void ARoseCart::PlayPatAnim(ERosePatAnim Action)
{
	const FString SkeletonPath = FString::Printf(
		TEXT("/Game/Pat/base_%d/base_%d/SkeletalMeshes"), Pet, Pet);
	if (UAnimSequence* Anim = ResolveAnim(VehicleRow, MotionCol, Action,
	                                      PAT_FALLBACK_VEHICLE_ROW, SkeletonPath))
	{
		// Stop and Move loop; attacks and death play once.
		const bool bLoop = (Action == ERosePatAnim::Stop1 || Action == ERosePatAnim::Move);
		Mesh->PlayAnimation(Anim, bLoop);
	}
}

UAnimSequence* ARoseCart::ResolveRiderAnim(ERosePatAnim Action, const FString& RiderSkeletonPath) const
{
	// The rider's clips live on the avatar skeleton, indexed by the vehicle's
	// AVATAR MOTION row (column 0 — the rider has no ARMS column).
	return ResolveAnim(RiderRow, 0, Action, PAT_FALLBACK_RIDER_ROW, RiderSkeletonPath);
}

