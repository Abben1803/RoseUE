#include "RoseImportTablesCommandlet.h"

#include "RoseEditor.h"
#include "RoseTableImporter.h"

URoseImportTablesCommandlet::URoseImportTablesCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URoseImportTablesCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FRoseTableImportOptions Options;
	Options.AssetRoot = ParamsMap.Contains(TEXT("assetroot"))
		? ParamsMap[TEXT("assetroot")]
		: TEXT("C:/QQ-iROSE Online/extracted");

	if (const FString* Only = ParamsMap.Find(TEXT("only")))
	{
		TArray<FString> Kinds;
		Only->ParseIntoArray(Kinds, TEXT(","), true);
		auto Has = [&Kinds](const TCHAR* K)
		{
			return Kinds.ContainsByPredicate(
				[K](const FString& S) { return S.Equals(K, ESearchCase::IgnoreCase); });
		};
		Options.bSkills = Has(TEXT("skills"));
	}

	UE_LOG(LogRoseImport, Log, TEXT("=== table import ==="));
	UE_LOG(LogRoseImport, Log, TEXT("asset root: %s"), *Options.AssetRoot);

	FRoseTableImportResult Result;
	if (!RoseImportTables(Options, Result))
	{
		UE_LOG(LogRoseImport, Error, TEXT("=== table import FAILED ==="));
		return 1;
	}

	UE_LOG(LogRoseImport, Log, TEXT("=== table import OK ==="));
	for (const FRoseTablePackResult& Pack : Result.Packs)
	{
		UE_LOG(LogRoseImport, Log, TEXT("  %-14s rows %5d / %5d  %s"),
			*Pack.Name, Pack.RowsOut, Pack.RowsIn,
			Pack.bSaved ? TEXT("saved") : TEXT("SAVE FAILED"));
	}
	if (Result.MissingNames > 0)
	{
		UE_LOG(LogRoseImport, Warning,
			TEXT("  %d rows had an STL key that resolved to no string"), Result.MissingNames);
	}
	UE_LOG(LogRoseImport, Log, TEXT("  TOTAL     %.2fs"), Result.SecondsTotal);
	return 0;
}
