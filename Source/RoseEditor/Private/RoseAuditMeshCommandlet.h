#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RoseAuditMeshCommandlet.generated.h"

// Print what an imported skeletal mesh actually references: every LOD section,
// its material, that material's blend mode and bound BaseColor texture, and
// whether the mesh carries vertex colours.  Read-only — imports nothing.
//
// Answers "why does this part render wrong" with data instead of inference.
//
// usage: -run=RoseAuditMesh -asset=/Game/Rose/Characters/F/BODY/SK_F_BODY_1
UCLASS()
class URoseAuditMeshCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URoseAuditMeshCommandlet();
	virtual int32 Main(const FString& Params) override;
};
