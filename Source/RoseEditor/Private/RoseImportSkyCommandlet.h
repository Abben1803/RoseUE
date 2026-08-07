// usage: -run=RoseImportSky [-assetroot=...]
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RoseImportSkyCommandlet.generated.h"

UCLASS()
class URoseImportSkyCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URoseImportSkyCommandlet();
	virtual int32 Main(const FString& Params) override;
};
