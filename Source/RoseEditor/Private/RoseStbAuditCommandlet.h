#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RoseStbAuditCommandlet.generated.h"

// Report how each schema binds against a client's STB tables, WITHOUT importing
// anything.  Run this first when pointing the importer at a client that has not
// been used before: it names the detected profile and lists every field that
// resolved by header, by pinned index, or not at all.
//
// usage: -run=RoseStbAudit -assetroot="C:\SomeClient"
UCLASS()
class URoseStbAuditCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URoseStbAuditCommandlet();
	virtual int32 Main(const FString& Params) override;
};
