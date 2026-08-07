#include "RoseImportNpcCommandlet.h"

#include "RoseEditor.h"
#include "RoseNpcImporter.h"

URoseImportNpcCommandlet::URoseImportNpcCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URoseImportNpcCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FRoseNpcImportOptions Options;
	const FString* Root = ParamsMap.Find(TEXT("assetroot"));
	Options.AssetRoot = Root && !Root->IsEmpty()
		? *Root
		: TEXT("C:/QQ-iROSE Online/extracted");

	if (const FString* S = ParamsMap.Find(TEXT("npc")))
		Options.OnlyNpcId = FCString::Atoi(**S);
	if (const FString* S = ParamsMap.Find(TEXT("max")))
		Options.MaxNpcs = FCString::Atoi(**S);
	Options.bSkipExisting = Switches.Contains(TEXT("skipexisting"));

	UE_LOG(LogRoseImport, Display, TEXT("=== npc import ==="));
	UE_LOG(LogRoseImport, Display, TEXT("asset root: %s"), *Options.AssetRoot);

	FRoseNpcImportResult Result;
	if (!RoseImportNpcs(Options, Result))
	{
		UE_LOG(LogRoseImport, Error, TEXT("=== npc import FAILED ==="));
		return 1;
	}
	return 0;
}
