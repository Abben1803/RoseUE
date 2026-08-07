#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RoseAuditLevelCommandlet.generated.h"

// Report what a level ACTUALLY contains: the exposure volume and its clamps, the
// sun/sky, water bodies and their zone, lamps, and the terrain material's
// shading model and lightmap strength.  Read-only.
//
// usage: -run=RoseAuditLevel -level=/Game/Maps/JPT01/L_JPT01
UCLASS()
class URoseAuditLevelCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URoseAuditLevelCommandlet();
	virtual int32 Main(const FString& Params) override;
};
