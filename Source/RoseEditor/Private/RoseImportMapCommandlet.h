// One-launch zone import.
//
//   UnrealEditor-Cmd.exe "<RoseUE.uproject>" -run=RoseImportMap -zone=JPT01
//       [-assetroot="C:/QQ-iROSE Online/extracted"]
//       [-suffix=_Native] [-gutter=8] [-nolighting]
//
// Replaces steps 1-2 of tools/import_zone.ps1 (mapforge export + Interchange
// scene import) with a single in-process pass.  QQ-iROSE is the default asset
// root per CLAUDE.md (switched 2026-08-06); assets and the STB tables that index
// them must come from the SAME client, or a row id silently resolves to a
// different item's mesh.
#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "RoseImportMapCommandlet.generated.h"

UCLASS()
class URoseImportMapCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	URoseImportMapCommandlet();

	virtual int32 Main(const FString& Params) override;
};
