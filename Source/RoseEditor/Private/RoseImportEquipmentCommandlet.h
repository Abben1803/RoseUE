// Equipment import in one launch.
//
//   UnrealEditor-Cmd.exe "<RoseUE.uproject>" -run=RoseImportEquipment
//       [-assetroot=...] [-only=weapon,subwpn,back,pat] [-max=N] [-skipexisting]
//
// Replaces build_weapons_static.py + the TDR-chunked Interchange imports, which
// cost ~27 editor launches for weapons alone (50 assets per launch, ~8-10 min a
// launch — see the bulk-import rules in docs/UE5_WORKFLOW.md).
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RoseImportEquipmentCommandlet.generated.h"

UCLASS()
class URoseImportEquipmentCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URoseImportEquipmentCommandlet();

	virtual int32 Main(const FString& Params) override;
};
