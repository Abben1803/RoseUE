// usage: -run=RoseImportWater -zone=LP05|ALL [-assetroot=...]
//
// Adds UE Water-plugin bodies to levels that are ALREADY imported, without
// touching their terrain, objects, NPCs or lighting.  Use this instead of a
// full -run=RoseImportMap when only the water is missing: a re-import rebuilds
// everything and risks regressing a level that is otherwise correct.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RoseImportWaterCommandlet.generated.h"

UCLASS()
class URoseImportWaterCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URoseImportWaterCommandlet();
	virtual int32 Main(const FString& Params) override;
};
