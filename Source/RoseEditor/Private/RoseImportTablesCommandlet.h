#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RoseImportTablesCommandlet.generated.h"

// usage: -run=RoseImportTables [-assetroot=...] [-only=skills]
UCLASS()
class URoseImportTablesCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URoseImportTablesCommandlet();
	virtual int32 Main(const FString& Params) override;
};
