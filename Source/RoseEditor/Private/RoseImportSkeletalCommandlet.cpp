#include "RoseImportSkeletalCommandlet.h"

#include "RoseEditor.h"
#include "RoseSkeletalImporter.h"

URoseImportSkeletalCommandlet::URoseImportSkeletalCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URoseImportSkeletalCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FRoseSkeletalImportOptions Options;
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
		Options.bBody = Has(TEXT("body"));
		Options.bArms = Has(TEXT("arms"));
		Options.bFoot = Has(TEXT("foot"));
		Options.bCap = Has(TEXT("cap"));
		Options.bFace = Has(TEXT("face"));
		Options.bHair = Has(TEXT("hair"));
		Options.bFaceItem = Has(TEXT("faceitem"));
		// Without this, -only=body would still grind through all 543 ZMOs.
		Options.bAnimations = Has(TEXT("anims"));
	}
	if (const FString* Gender = ParamsMap.Find(TEXT("gender")))
	{
		Options.bFemale = Gender->Equals(TEXT("F"), ESearchCase::IgnoreCase);
		Options.bMale = Gender->Equals(TEXT("M"), ESearchCase::IgnoreCase);
	}
	if (const FString* Max = ParamsMap.Find(TEXT("max")))
		Options.MaxItemsPerPack = FCString::Atoi(**Max);
	Options.bSkipExisting = Switches.Contains(TEXT("skipexisting"));

	// -item=<id> — build ONE object id.  Implies no animations, since a single
	// mesh never needs the 1,074-clip motion pass.
	if (const FString* Item = ParamsMap.Find(TEXT("item")))
	{
		Options.OnlyItemId = FCString::Atoi(**Item);
		Options.bAnimations = false;
	}

	UE_LOG(LogRoseImport, Log, TEXT("=== skeletal import ==="));
	UE_LOG(LogRoseImport, Log, TEXT("asset root: %s"), *Options.AssetRoot);

	FRoseSkeletalImportResult Result;
	if (!RoseImportSkeletal(Options, Result))
	{
		UE_LOG(LogRoseImport, Error, TEXT("=== skeletal import FAILED ==="));
		return 1;
	}

	UE_LOG(LogRoseImport, Log, TEXT("=== skeletal import OK ==="));
	UE_LOG(LogRoseImport, Log, TEXT("  skeletons %d  (F %d bones / M %d bones)"),
		Result.SkeletonsBuilt, Result.BonesFemale, Result.BonesMale);
	for (const FRoseSkeletalPackResult& Pack : Result.Packs)
	{
		UE_LOG(LogRoseImport, Log,
			TEXT("  %-12s objects %5d  built %5d  empty %5d  skipped %4d  failed %4d"),
			*Pack.Kind, Pack.ObjectsInPack, Pack.Built, Pack.Empty,
			Pack.Skipped, Pack.Failed);
	}
	UE_LOG(LogRoseImport, Log, TEXT("  anims     built %d, skipped %d (vertex-morph)"),
		Result.AnimationsBuilt, Result.AnimationsSkippedMorph);
	UE_LOG(LogRoseImport, Log, TEXT("  textures  %d"), Result.UniqueTextures);
	UE_LOG(LogRoseImport, Log, TEXT("  materials %d"), Result.UniqueMaterials);
	UE_LOG(LogRoseImport, Log, TEXT("  missing   %d"), Result.MissingAssets);
	UE_LOG(LogRoseImport, Log, TEXT("  TOTAL     %.2fs"), Result.SecondsTotal);

	return 0;
}
