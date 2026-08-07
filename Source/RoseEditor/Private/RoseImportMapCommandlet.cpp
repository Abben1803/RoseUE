#include "RoseImportMapCommandlet.h"

#include "RoseEditor.h"
#include "RoseMapImporter.h"

#include "HAL/FileManager.h"
#include "Misc/Paths.h"

namespace
{
	// Every folder under MAPS/<planet>/ that has a matching .ZON.
	TArray<FString> FindAllZones(const FString& AssetRoot)
	{
		TArray<FString> Out;
		const FString MapsRoot = FPaths::Combine(AssetRoot, TEXT("3DDATA"), TEXT("MAPS"));

		TArray<FString> Planets;
		IFileManager::Get().FindFiles(Planets, *(MapsRoot / TEXT("*")), false, true);
		for (const FString& Planet : Planets)
		{
			TArray<FString> ZoneDirs;
			IFileManager::Get().FindFiles(ZoneDirs, *(MapsRoot / Planet / TEXT("*")), false, true);
			for (const FString& Zone : ZoneDirs)
			{
				const FString Zon = MapsRoot / Planet / Zone / (Zone + TEXT(".ZON"));
				if (IFileManager::Get().FileExists(*Zon))
					Out.Add(Zone.ToUpper());
			}
		}
		Out.Sort();
		return Out;
	}
}

URoseImportMapCommandlet::URoseImportMapCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URoseImportMapCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens;
	TArray<FString> Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	FRoseMapImportOptions Options;

	FString ZoneArg;
	if (const FString* Zone = ParamsMap.Find(TEXT("zone")))
	{
		ZoneArg = *Zone;
	}
	else
	{
		UE_LOG(LogRoseImport, Error,
			TEXT("usage: -run=RoseImportMap -zone=JPT01|ALL [-assetroot=...] [-cleanlegacy]"));
		return 1;
	}

	// Arua is THE asset source (CLAUDE.md).  The classic tree is deprecated and
	// mixing eras is a silent data-corruption bug, not a style preference.
	Options.AssetRoot = ParamsMap.Contains(TEXT("assetroot"))
		? ParamsMap[TEXT("assetroot")]
		: TEXT("C:/QQ-iROSE Online/extracted");

	if (const FString* Suffix = ParamsMap.Find(TEXT("suffix")))
		Options.LevelSuffix = *Suffix;
	if (const FString* Gutter = ParamsMap.Find(TEXT("gutter")))
		Options.AtlasGutter = FCString::Atoi(**Gutter);
	if (Switches.Contains(TEXT("nolighting")))
		Options.bAddLighting = false;
	if (const FString* YawOff = ParamsMap.Find(TEXT("npcyaw")))
		Options.NpcYawOffset = FCString::Atof(**YawOff);
	Options.bDeleteLegacyAssets = Switches.Contains(TEXT("cleanlegacy"));

	// Baked ground lightmaps are ON by default now that they are Arua-native
	// (recovered from the VFS by name-hash — see FRoseMapImportOptions).
	if (Switches.Contains(TEXT("nolightmaps")))
		Options.bImportLightmaps = false;

	TArray<FString> ZonesToDo;
	if (ZoneArg.Equals(TEXT("ALL"), ESearchCase::IgnoreCase))
		ZonesToDo = FindAllZones(Options.AssetRoot);
	else
		ZonesToDo.Add(ZoneArg.ToUpper());

	UE_LOG(LogRoseImport, Log, TEXT("=== map import: %d zone(s) ==="), ZonesToDo.Num());
	UE_LOG(LogRoseImport, Log, TEXT("asset root: %s"), *Options.AssetRoot);
	if (Options.bDeleteLegacyAssets)
		UE_LOG(LogRoseImport, Log, TEXT("legacy /Scene assets will be retired after each success"));

	const bool bSingle = ZonesToDo.Num() == 1;
	int32 Failures = 0;
	FRoseMapImportResult Result;

	for (const FString& Zone : ZonesToDo)
	{
		Options.Zone = Zone;
		FRoseMapImportResult ZoneResult;
		if (!RoseImportMap(Options, ZoneResult))
		{
			UE_LOG(LogRoseImport, Error, TEXT("  %s FAILED"), *Zone);
			++Failures;
			continue;
		}
		if (bSingle)
		{
			Result = ZoneResult;   // keep it for the detailed breakdown below
		}
		else
		{
			UE_LOG(LogRoseImport, Log,
				TEXT("  %-10s ok  chunks %3d  objects %5d  npcs %3d  spawners %3d  "
				     "retired %4d  %.1fs"),
				*Zone, ZoneResult.ChunksLoaded, ZoneResult.ObjectActors,
				ZoneResult.NpcActors, ZoneResult.SpawnerActors,
				ZoneResult.LegacyAssetsDeleted, ZoneResult.SecondsTotal);
		}
	}

	if (!bSingle)
	{
		UE_LOG(LogRoseImport, Log, TEXT("=== %d/%d zones imported ==="),
			ZonesToDo.Num() - Failures, ZonesToDo.Num());
		return Failures > 0 ? 1 : 0;
	}

	if (Failures > 0)
	{
		UE_LOG(LogRoseImport, Error, TEXT("=== import FAILED for %s ==="), *Options.Zone);
		return 1;
	}

	UE_LOG(LogRoseImport, Log, TEXT("=== import OK: %s ==="), *Result.LevelPackage);
	UE_LOG(LogRoseImport, Log, TEXT("  chunks       %d"), Result.ChunksLoaded);
	UE_LOG(LogRoseImport, Log, TEXT("  meshes       %d"), Result.MeshesBuilt);
	UE_LOG(LogRoseImport, Log, TEXT("  actors       %d"), Result.ActorsPlaced);
	UE_LOG(LogRoseImport, Log, TEXT("  atlas        %d tiles, %dpx"), Result.AtlasTiles, Result.AtlasSize);
	if (Options.bImportLightmaps)
	{
		UE_LOG(LogRoseImport, Log, TEXT("  lightmaps    %d applied, %d missing"),
			Result.LightmapsImported, Result.LightmapsMissing);
	}
	UE_LOG(LogRoseImport, Log, TEXT("  object parts %d  (animated %d)"), Result.ObjectActors, Result.AnimatedParts);
	UE_LOG(LogRoseImport, Log, TEXT("  unique       %d meshes / %d textures / %d materials"),
		Result.UniqueMeshes, Result.UniqueTextures, Result.UniqueMaterials);
	UE_LOG(LogRoseImport, Log, TEXT("  missing      %d"), Result.MissingAssets);
	UE_LOG(LogRoseImport, Log, TEXT("  lamps        %d"), Result.Lamps);
	UE_LOG(LogRoseImport, Log, TEXT("  npcs         %d"), Result.NpcActors);
	UE_LOG(LogRoseImport, Log, TEXT("  spawners     %d"), Result.SpawnerActors);
	UE_LOG(LogRoseImport, Log, TEXT("  portals      %d"), Result.PortalActors);
	UE_LOG(LogRoseImport, Log, TEXT("  vertices     %lld"), Result.TotalVertices);
	UE_LOG(LogRoseImport, Log, TEXT("  triangles    %lld"), Result.TotalTriangles);
	UE_LOG(LogRoseImport, Log, TEXT("  parse        %.2fs"), Result.SecondsParse);
	UE_LOG(LogRoseImport, Log, TEXT("  atlas build  %.2fs"), Result.SecondsAtlas);
	UE_LOG(LogRoseImport, Log, TEXT("  meshes       %.2fs"), Result.SecondsMeshes);
	UE_LOG(LogRoseImport, Log, TEXT("  objects      %.2fs"), Result.SecondsObjects);
	UE_LOG(LogRoseImport, Log, TEXT("  level save   %.2fs"), Result.SecondsSave);
	UE_LOG(LogRoseImport, Log, TEXT("  retired      %d legacy assets"), Result.LegacyAssetsDeleted);
	UE_LOG(LogRoseImport, Log, TEXT("  TOTAL        %.2fs"), Result.SecondsTotal);

	return 0;
}
