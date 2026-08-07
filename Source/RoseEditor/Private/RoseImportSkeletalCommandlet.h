// Avatars + armour in one launch.
//
//   UnrealEditor-Cmd.exe "<RoseUE.uproject>" -run=RoseImportSkeletal
//       [-assetroot=...] [-max=N] [-skipexisting] [-only=body,arms,foot,cap,face,hair,faceitem]
//       [-gender=F|M]
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RoseImportSkeletalCommandlet.generated.h"

UCLASS()
class URoseImportSkeletalCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URoseImportSkeletalCommandlet();

	virtual int32 Main(const FString& Params) override;
};
