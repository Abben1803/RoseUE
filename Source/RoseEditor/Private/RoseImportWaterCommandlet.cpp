#include "RoseImportWaterCommandlet.h"

#include "RoseEditor.h"
#include "RoseMapImporter.h"
#include "RoseObjectFormats.h"
#include "RoseWaterBuilder.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "GameFramework/Actor.h"
#include "HAL/FileManager.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	// Every actor this tool creates carries this tag, so a re-run can remove
	// exactly what it made and nothing else.  Both the bodies and the zone get
	// it (see RoseWaterBuilder).
	const TCHAR* kWaterTag = TEXT("RoseWater");

	FString FindZoneDir(const FString& AssetRoot, const FString& Zone)
	{
		const FString MapsRoot = FPaths::Combine(AssetRoot, TEXT("3DDATA"), TEXT("MAPS"));
		TArray<FString> Planets;
		IFileManager::Get().FindFiles(Planets, *(MapsRoot / TEXT("*")), false, true);
		for (const FString& Planet : Planets)
		{
			const FString Candidate = MapsRoot / Planet / Zone;
			if (IFileManager::Get().DirectoryExists(*Candidate))
				return Candidate;
		}
		return FString();
	}

	// Every zone folder that exists on disk, so -zone=ALL needs no table.
	TArray<FString> FindAllZoneNames(const FString& AssetRoot)
	{
		TArray<FString> Zones;
		const FString MapsRoot = FPaths::Combine(AssetRoot, TEXT("3DDATA"), TEXT("MAPS"));
		TArray<FString> Planets;
		IFileManager::Get().FindFiles(Planets, *(MapsRoot / TEXT("*")), false, true);
		for (const FString& Planet : Planets)
		{
			TArray<FString> Dirs;
			IFileManager::Get().FindFiles(Dirs, *(MapsRoot / Planet / TEXT("*")), false, true);
			for (const FString& D : Dirs)
				Zones.AddUnique(D.ToUpper());
		}
		Zones.Sort();
		return Zones;
	}

	// Collect the zone's ocean patches out of every chunk IFO.  A zone's water
	// is described per-chunk but is one continuous surface, so they are gathered
	// and handed to the builder in one go — exactly as RoseImportMap does it.
	TArray<FRoseOceanBlock> GatherOcean(const FString& ZoneDir)
	{
		TArray<FRoseOceanBlock> Patches;
		TArray<FString> IfoFiles;
		IFileManager::Get().FindFiles(IfoFiles, *(ZoneDir / TEXT("*.IFO")), true, false);
		for (const FString& IfoFile : IfoFiles)
		{
			FRoseIFO Ifo;
			if (!Ifo.Load(ZoneDir / IfoFile))
				continue;
			Patches.Append(Ifo.Ocean);
		}
		return Patches;
	}
}

URoseImportWaterCommandlet::URoseImportWaterCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URoseImportWaterCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	const FString* ZoneArgPtr = ParamsMap.Find(TEXT("zone"));
	if (!ZoneArgPtr || ZoneArgPtr->IsEmpty())
	{
		UE_LOG(LogRoseImport, Error,
			TEXT("usage: -run=RoseImportWater -zone=LP05|ALL [-assetroot=...]"));
		return 1;
	}
	const FString ZoneArg = *ZoneArgPtr;

	const FString* Root = ParamsMap.Find(TEXT("assetroot"));
	const FString AssetRoot = Root && !Root->IsEmpty()
		? *Root : TEXT("C:/QQ-iROSE Online/extracted");

	// -expand=N grows every patch by N cm; see FRoseMapImportOptions.
	float PatchExpand = FRoseMapImportOptions().WaterPatchExpand;
	if (const FString* E = ParamsMap.Find(TEXT("expand")))
		PatchExpand = FCString::Atof(**E);

	TArray<FString> Zones;
	if (ZoneArg.Equals(TEXT("ALL"), ESearchCase::IgnoreCase))
		Zones = FindAllZoneNames(AssetRoot);
	else
		Zones.Add(ZoneArg.ToUpper());

	UE_LOG(LogRoseImport, Display, TEXT("=== water import: %d zone(s) ==="), Zones.Num());
	UE_LOG(LogRoseImport, Display, TEXT("asset root: %s"), *AssetRoot);

	int32 ZonesDone = 0, ZonesSkipped = 0, ZonesFailed = 0, BodiesTotal = 0;

	for (const FString& Zone : Zones)
	{
		const FString ZoneDir = FindZoneDir(AssetRoot, Zone);
		if (ZoneDir.IsEmpty())
			continue;

		const TArray<FRoseOceanBlock> Patches = GatherOcean(ZoneDir);
		if (Patches.Num() == 0)
			continue;   // a dry zone is the common case, not a failure

		// The level has to exist already — this tool adds to an imported map,
		// it does not create one.
		//
		// The asset is L_<ZONE>, not <ZONE>: RoseImportMap prefixes the level
		// name (Content/Maps/JPT01/L_JPT01.umap).
		const FString LevelPkgName =
			FString::Printf(TEXT("/Game/Maps/%s/L_%s"), *Zone, *Zone);
		if (!FPackageName::DoesPackageExist(LevelPkgName))
		{
			UE_LOG(LogRoseImport, Warning,
				TEXT("  %-10s SKIP  no level at %s — run -run=RoseImportMap first"),
				*Zone, *LevelPkgName);
			++ZonesSkipped;
			continue;
		}

		UPackage* LevelPkg = LoadPackage(nullptr, *LevelPkgName, LOAD_None);
		UWorld* World = LevelPkg ? UWorld::FindWorldInPackage(LevelPkg) : nullptr;
		if (!World)
		{
			UE_LOG(LogRoseImport, Error, TEXT("  %-10s FAILED  no world in %s"),
				*Zone, *LevelPkgName);
			++ZonesFailed;
			continue;
		}

		// The whole reason water was missing: a level serialised as Inactive has
		// no UWaterSubsystem, and RoseSpawnWater refuses to run without one.
		if (World->WorldType != EWorldType::Editor)
			World->WorldType = EWorldType::Editor;

		// Whether WE initialised this world decides whether we must tear it down.
		// Initialising brings up the world subsystem collection — the water one
		// we are after, but also WorldPartitionSubsystem and other TICKABLE
		// subsystems.  A tickable subsystem that reaches destruction still
		// initialised takes the process down on shutdown:
		//   "Tickable subsystem WorldPartitionSubsystem ... was destroyed while
		//    still initialized! Check for missing Super::Deinitialize call"
		// which is an exit code 3 AFTER the level has already saved — the work
		// is fine, the process is not.
		const bool bWeInitialised = !World->IsInitialized();
		if (bWeInitialised)
		{
			World->InitWorld(UWorld::InitializationValues()
				.CreatePhysicsScene(false)
				.ShouldSimulatePhysics(false)
				.EnableTraceCollision(true)
				.CreateNavigation(false)
				.CreateAISystem(false));
		}

		// Tear the world down at every exit from this iteration, not just the
		// happy one — a zone that fails to save must not leave an initialised
		// world behind either.
		ON_SCOPE_EXIT
		{
			if (bWeInitialised && World && World->IsInitialized())
				World->CleanupWorld();
		};

		// Idempotent: drop whatever a previous water run left before adding, so
		// running this twice does not stack two zones and 89 duplicate bodies on
		// top of each other.
		{
			TArray<AActor*> Old;
			for (AActor* Actor : World->PersistentLevel->Actors)
			{
				if (Actor && Actor->Tags.Contains(FName(kWaterTag)))
					Old.Add(Actor);
			}
			for (AActor* Actor : Old)
				World->DestroyActor(Actor);
			if (Old.Num() > 0)
			{
				UE_LOG(LogRoseImport, Log,
					TEXT("  %-10s cleared %d actor(s) from a previous water run"),
					*Zone, Old.Num());
			}
		}

		FRoseMapImportOptions Options;
		Options.AssetRoot = AssetRoot;
		Options.Zone = Zone;
		Options.WaterPatchExpand = PatchExpand;

		FRoseMapImportResult ZoneResult;
		RoseSpawnWater(World, Patches, Options, ZoneResult);

		if (ZoneResult.WaterBodies == 0)
		{
			UE_LOG(LogRoseImport, Error,
				TEXT("  %-10s FAILED  %d ocean patches produced NO water bodies"),
				*Zone, Patches.Num());
			++ZonesFailed;
			continue;
		}

		World->MarkPackageDirty();
		FSavePackageArgs SaveArgs;
		SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
		const FString Filename = FPackageName::LongPackageNameToFilename(
			LevelPkgName, FPackageName::GetMapPackageExtension());
		if (!UPackage::SavePackage(LevelPkg, World, *Filename, SaveArgs))
		{
			UE_LOG(LogRoseImport, Error, TEXT("  %-10s FAILED  could not save %s"),
				*Zone, *Filename);
			++ZonesFailed;
			continue;
		}
		LevelPkg->SetDirtyFlag(false);

		UE_LOG(LogRoseImport, Display, TEXT("  %-10s ok  %3d patches -> %3d water bodies"),
			*Zone, Patches.Num(), ZoneResult.WaterBodies);
		BodiesTotal += ZoneResult.WaterBodies;
		++ZonesDone;
	}

	UE_LOG(LogRoseImport, Display,
		TEXT("=== water import: %d zone(s) watered, %d bodies, %d skipped, %d failed ==="),
		ZonesDone, BodiesTotal, ZonesSkipped, ZonesFailed);

	return ZonesFailed > 0 ? 1 : 0;
}
