#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RoseAuditMergeCommandlet.generated.h"

// Merge the native character parts exactly as ARoseCharacter does and report
// what comes out.
//
// The parts all measure correct in isolation (bounds, materials, textures,
// skeleton), so a character that renders wrong must be breaking at the MERGE —
// and that step is invisible from the assets on disk.  This runs it headlessly
// and prints the merged bounds, sections, materials and bone count, so the
// failure can be seen without launching the game.
//
// usage: -run=RoseAuditMerge [-gender=F] [-body=1 -arms=1 -foot=1 -hair=1 -face=1]
UCLASS()
class URoseAuditMergeCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URoseAuditMergeCommandlet();
	virtual int32 Main(const FString& Params) override;
};
