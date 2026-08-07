// usage: -run=RoseImportNpc [-assetroot=...] [-npc=1709] [-max=N] [-skipexisting]
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RoseImportNpcCommandlet.generated.h"

UCLASS()
class URoseImportNpcCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URoseImportNpcCommandlet();
	virtual int32 Main(const FString& Params) override;
};
