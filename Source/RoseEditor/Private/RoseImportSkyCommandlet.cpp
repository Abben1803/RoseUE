#include "RoseImportSkyCommandlet.h"

#include "RoseEditor.h"
#include "RoseSkyImporter.h"

URoseImportSkyCommandlet::URoseImportSkyCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URoseImportSkyCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FRoseSkyImportOptions Options;
	const FString* Root = ParamsMap.Find(TEXT("assetroot"));
	Options.AssetRoot = Root && !Root->IsEmpty()
		? *Root
		: TEXT("C:/QQ-iROSE Online/extracted");

	UE_LOG(LogRoseImport, Display, TEXT("=== sky import ==="));
	UE_LOG(LogRoseImport, Display, TEXT("asset root: %s"), *Options.AssetRoot);

	FRoseSkyImportResult Result;
	if (!RoseImportSky(Options, Result))
	{
		UE_LOG(LogRoseImport, Error, TEXT("=== sky import FAILED ==="));
		return 1;
	}
	return 0;
}
