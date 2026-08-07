// ARoseCharacterCreator — the character-preview mannequin used on the Character
// Select screen.  Body is the animated leader; hair/face/arms/feet follow its
// pose.  Parts load by id from /Game/Characters/Modular/<Gender>/<slot>_<id>/…
// (the same asset layout the in-game ARoseCharacter uses), so the preview looks
// like the real character.  The select UI drives it via SetGender/Hair/Face.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoseCharacterCreator.generated.h"

class USkeletalMeshComponent;
class USkeletalMesh;

UCLASS()
class ROSEUE_API ARoseCharacterCreator : public AActor
{
	GENERATED_BODY()

public:
	ARoseCharacterCreator();

	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, Category = "Rose") USkeletalMeshComponent* Body;
	UPROPERTY(VisibleAnywhere, Category = "Rose") USkeletalMeshComponent* Hair;
	UPROPERTY(VisibleAnywhere, Category = "Rose") USkeletalMeshComponent* Face;
	UPROPERTY(VisibleAnywhere, Category = "Rose") USkeletalMeshComponent* Feet;
	UPROPERTY(VisibleAnywhere, Category = "Rose") USkeletalMeshComponent* Arms;

	// "Female" or "Male".
	UPROPERTY(EditAnywhere, Category = "Rose") FString Gender = TEXT("Female");
	// 110 was an Arua hair id; the classic tree stops at 78.
	UPROPERTY(EditAnywhere, Category = "Rose") int32 HairId = 1;
	UPROPERTY(EditAnywhere, Category = "Rose") int32 FaceId = 1;

	// Driven by the Character Select UI.
	UFUNCTION(BlueprintCallable, Category = "Rose") void SetGender(const FString& InGender);
	UFUNCTION(BlueprintCallable, Category = "Rose") void SetHairId(int32 Id);
	UFUNCTION(BlueprintCallable, Category = "Rose") void SetFaceId(int32 Id);
	UFUNCTION(BlueprintCallable, Category = "Rose") void NextHair();
	UFUNCTION(BlueprintCallable, Category = "Rose") void PrevHair();
	UFUNCTION(BlueprintCallable, Category = "Rose") void NextFace();
	UFUNCTION(BlueprintCallable, Category = "Rose") void PrevFace();

	int32 GetHairId() const { return HairId; }
	int32 GetFaceId() const { return FaceId; }
	const FString& GetGender() const { return Gender; }

protected:
	// Available base-hair ids and face ids for the current gender (sorted).
	TArray<int32> HairIds;
	TArray<int32> FaceIds;

	USkeletalMesh* LoadPart(const FString& Slot, int32 Id) const;
	void ScanOptions();       // fill HairIds/FaceIds from the modular folder
	void RebuildAll();        // (re)load body + all parts for the current gender
	void ApplyHair();
	void ApplyFace();
	void PlayIdle();
};
