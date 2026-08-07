#include "RoseImportEquipmentCommandlet.h"

#include "RoseEditor.h"
#include "RoseEquipmentImporter.h"

URoseImportEquipmentCommandlet::URoseImportEquipmentCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URoseImportEquipmentCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FRoseEquipImportOptions Options;
	Options.AssetRoot = ParamsMap.Contains(TEXT("assetroot"))
		? ParamsMap[TEXT("assetroot")]
		: TEXT("C:/QQ-iROSE Online/extracted");

	if (const FString* Only = ParamsMap.Find(TEXT("only")))
	{
		TArray<FString> Kinds;
		Only->ParseIntoArray(Kinds, TEXT(","), true);
		Options.bWeapons = Kinds.ContainsByPredicate([](const FString& S) { return S.Equals(TEXT("weapon"), ESearchCase::IgnoreCase); });
		Options.bSubWeapons = Kinds.ContainsByPredicate([](const FString& S) { return S.Equals(TEXT("subwpn"), ESearchCase::IgnoreCase); });
		Options.bBack = Kinds.ContainsByPredicate([](const FString& S) { return S.Equals(TEXT("back"), ESearchCase::IgnoreCase); });
		Options.bPat = Kinds.ContainsByPredicate([](const FString& S) { return S.Equals(TEXT("pat"), ESearchCase::IgnoreCase); });
	}
	if (const FString* Max = ParamsMap.Find(TEXT("max")))
		Options.MaxItemsPerPack = FCString::Atoi(**Max);
	Options.bSkipExisting = Switches.Contains(TEXT("skipexisting"));

	UE_LOG(LogRoseImport, Log, TEXT("=== native equipment import ==="));
	UE_LOG(LogRoseImport, Log, TEXT("asset root: %s"), *Options.AssetRoot);

	FRoseEquipImportResult Result;
	if (!RoseImportEquipment(Options, Result))
	{
		UE_LOG(LogRoseImport, Error, TEXT("=== equipment import FAILED ==="));
		return 1;
	}

	UE_LOG(LogRoseImport, Log, TEXT("=== equipment import OK ==="));
	for (const FRoseEquipPackResult& Pack : Result.Packs)
	{
		UE_LOG(LogRoseImport, Log,
			TEXT("  %-8s objects %5d  built %5d  empty %5d  skipped %5d  failed %3d  tris %lld"),
			*Pack.Kind, Pack.ObjectsInPack, Pack.Built, Pack.Empty,
			Pack.Skipped, Pack.Failed, Pack.Triangles);
	}
	UE_LOG(LogRoseImport, Log, TEXT("  textures  %d"), Result.UniqueTextures);
	UE_LOG(LogRoseImport, Log, TEXT("  materials %d"), Result.UniqueMaterials);
	UE_LOG(LogRoseImport, Log, TEXT("  missing   %d"), Result.MissingAssets);
	UE_LOG(LogRoseImport, Log, TEXT("  winding agreement %.1f%%  (low = inside-out)"),
		Result.NormalAgreement * 100.0);
	UE_LOG(LogRoseImport, Log, TEXT("  TOTAL     %.2fs"), Result.SecondsTotal);

	return 0;
}
