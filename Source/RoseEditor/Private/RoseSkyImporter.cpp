#include "RoseSkyImporter.h"

#include "RoseEditor.h"
#include "RoseMaterialBuilder.h"
#include "RoseObjectBuilder.h"
#include "RoseObjectFormats.h"
#include "RosePathResolver.h"
#include "RoseStbSchema.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* kSkyRoot = TEXT("/Game/Rose/Sky");
	const TCHAR* kSkyMaster = TEXT("/Game/Rose/Sky/M_RoseSky.M_RoseSky");

	UPackage* MakeWritablePackage(const FString& PkgName)
	{
		UPackage* Pkg = CreatePackage(*PkgName);
		if (Pkg)
			Pkg->FullyLoad();
		return Pkg;
	}

	void SavePkg(UPackage* Pkg, UObject* Asset)
	{
		if (!Pkg || !Asset)
			return;
		Pkg->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		if (UPackage::SavePackage(Pkg, Asset,
			*FPackageName::LongPackageNameToFilename(
				Pkg->GetName(), FPackageName::GetAssetPackageExtension()),
			Args))
		{
			Pkg->SetDirtyFlag(false);
		}
		else
		{
			UE_LOG(LogRoseImport, Error, TEXT("sky: failed to save %s"), *Pkg->GetName());
		}
	}
}

bool RoseImportSky(const FRoseSkyImportOptions& Options, FRoseSkyImportResult& Result)
{
	const double TimeStart = FPlatformTime::Seconds();
	FRosePathResolver Resolver(Options.AssetRoot);

	// The master first — an instance whose parent failed to load silently binds
	// nothing, which is the all-grey failure mode this project has hit before.
	// Both environment masters, built here so a fresh clone has them without a
	// separate step.  Precipitation is procedural, so it needs no assets of its
	// own — only the material.
	RoseMaterials::EnsurePrecipMaster();
	RoseMaterials::EnsureParticleMaster();
	UMaterial* Master = RoseMaterials::EnsureSkyMaster();
	if (!Master)
	{
		UE_LOG(LogRoseImport, Error, TEXT("sky: cannot build/load %s"), kSkyMaster);
		return false;
	}

	const FString StbDir = FPaths::Combine(Options.AssetRoot, TEXT("3DDATA"), TEXT("STB"));

	FRoseSTB SkyList;
	if (!SkyList.Load(FPaths::Combine(StbDir, TEXT("LIST_SKY.STB"))))
	{
		UE_LOG(LogRoseImport, Error, TEXT("sky: cannot read LIST_SKY.STB"));
		return false;
	}
	const FRoseStbBinding SkyCol = RoseStb::Bind(SkyList, RoseStb::SkySchema());
	RoseStb::LogBindingReport(SkyCol, RoseStb::SkySchema());

	// LIST_SKY needs a FIXED fallback, and it is the one table that has earned
	// one.
	//
	// Its Korean headers do not survive the codepage in any client we have, so
	// no alias can ever match.  The schema's pinned indices do not save it
	// either: those are stored PER PROFILE, and a table nothing can be matched
	// against detects as profile 'unknown', which has no pins — so the first run
	// bound 0 of 3 fields and imported 16 rows into zero assets.
	//
	// Falling back to the layout stb.h documents (SKY_MESH col 0,
	// SKY_TEXTURE(T) col 1+T) is safe HERE specifically: 16 rows, the same
	// layout in every era we support, and each cell is a file path we then have
	// to resolve — so a wrong column cannot pass silently, it fails to find a
	// file. Header binding still wins when a client ships readable ones.
	auto SkyCell = [&SkyCol, &SkyList](int32 Row, const TCHAR* Key, int32 FallbackCol)
	{
		const FName K(Key);
		return SkyCol.Has(K) ? SkyCol.Get(Row, K) : SkyList.Get(Row, FallbackCol);
	};

	for (const TCHAR* Key : { TEXT("Mesh"), TEXT("DayTex"), TEXT("NightTex") })
	{
		if (!SkyCol.Has(FName(Key)))
		{
			UE_LOG(LogRoseImport, Log,
				TEXT("sky: '%s' unresolved by header — using the stb.h column"), Key);
		}
	}

	FRoseAssetCache Cache(kSkyRoot, Resolver, kSkyMaster);

	Result.SkyRows = SkyList.Rows;

	// Per LIST_SKY row: the dome mesh, the two textures, and one instance.
	TArray<FString> RowMaterial;      // row -> MI object path ("" when unusable)
	TArray<FString> RowMesh;          // row -> dome mesh object path
	RowMaterial.SetNum(SkyList.Rows);
	RowMesh.SetNum(SkyList.Rows);

	for (int32 Row = 0; Row < SkyList.Rows; ++Row)
	{
		const FString MeshPath = SkyCell(Row, TEXT("Mesh"), 0);
		const FString DayPath = SkyCell(Row, TEXT("DayTex"), 1);
		const FString NightPath = SkyCell(Row, TEXT("NightTex"), 2);

		// Row 0 of a ROSE STB is routinely a label row ("list_sky"), and several
		// rows are blank.  Skip rather than fail: a gap here is normal.
		if (MeshPath.IsEmpty() || DayPath.IsEmpty())
			continue;

		UStaticMesh* Dome = Cache.GetMesh(MeshPath, /*bNeedsCollision=*/false);
		if (!Dome)
		{
			UE_LOG(LogRoseImport, Warning,
				TEXT("sky row %d: dome mesh missing (%s)"), Row, *MeshPath);
			++Result.Failed;
			continue;
		}
		RowMesh[Row] = Dome->GetPathName();

		// bNeedsAlpha=false: the sky is drawn opaque (CSkyDOME turns alpha test
		// off outright), so keeping an alpha channel would only cost DXT5 where
		// DXT1 will do.
		UTexture2D* DayTex = Cache.GetTexture(DayPath, /*bNeedsAlpha=*/false);
		UTexture2D* NightTex = NightPath.IsEmpty()
			? DayTex : Cache.GetTexture(NightPath, /*bNeedsAlpha=*/false);
		if (!DayTex)
		{
			UE_LOG(LogRoseImport, Warning,
				TEXT("sky row %d: day texture missing (%s)"), Row, *DayPath);
			++Result.Failed;
			continue;
		}
		// A row with no night texture is legitimate (LIST_SKY has several where
		// both columns name the same file) — fall back to day so the blend is a
		// no-op rather than a black sky.
		if (!NightTex)
			NightTex = DayTex;

		const FString MiName = FString::Printf(TEXT("MI_RoseSky_%d"), Row);
		const FString PkgName = FString::Printf(TEXT("%s/%s"), kSkyRoot, *MiName);
		UPackage* Pkg = MakeWritablePackage(PkgName);

		UMaterialInstanceConstant* MIC =
			FindObject<UMaterialInstanceConstant>(Pkg, *MiName);
		if (!MIC)
		{
			MIC = NewObject<UMaterialInstanceConstant>(
				Pkg, FName(*MiName), RF_Public | RF_Standalone);
		}
		MIC->SetParentEditorOnly(Master);
		MIC->SetTextureParameterValueEditorOnly(
			FMaterialParameterInfo(TEXT("DayTex")), DayTex);
		MIC->SetTextureParameterValueEditorOnly(
			FMaterialParameterInfo(TEXT("NightTex")), NightTex);
		MIC->PostEditChange();

		FAssetRegistryModule::AssetCreated(MIC);
		SavePkg(Pkg, MIC);

		RowMaterial[Row] = MIC->GetPathName();
		++Result.MaterialsBuilt;
	}

	// The textures and dome meshes the cache built are still only in memory.
	if (const int32 SaveFailures = Cache.SaveCreated())
	{
		UE_LOG(LogRoseImport, Error,
			TEXT("sky: %d mesh/texture package(s) failed to save"), SaveFailures);
	}
	Result.MeshesBuilt = Cache.NumMeshes();
	Result.TexturesBuilt = Cache.NumTextures();

	// zone -> sky row, plus the zone's day-length columns.
	//
	// Written as JSON beside zone_bgm.json rather than as a DataTable: the
	// runtime already reads that file the same way, and it keeps the sky data
	// editable without a UHT round trip.
	TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();

	TArray<TSharedPtr<FJsonValue>> RowsJson;
	for (int32 Row = 0; Row < SkyList.Rows; ++Row)
	{
		if (RowMaterial[Row].IsEmpty())
			continue;
		TSharedRef<FJsonObject> R = MakeShared<FJsonObject>();
		R->SetNumberField(TEXT("row"), Row);
		R->SetStringField(TEXT("material"), RowMaterial[Row]);
		R->SetStringField(TEXT("mesh"), RowMesh[Row]);
		RowsJson.Add(MakeShared<FJsonValueObject>(R));
	}
	Root->SetArrayField(TEXT("skies"), RowsJson);

	FRoseSTB ZoneList;
	if (ZoneList.Load(FPaths::Combine(StbDir, TEXT("LIST_ZONE.STB"))))
	{
		const FRoseStbBinding ZoneCol = RoseStb::Bind(ZoneList, RoseStb::ZoneSchema());
		TArray<TSharedPtr<FJsonValue>> ZonesJson;
		for (int32 Row = 0; Row < ZoneList.Rows; ++Row)
		{
			const FString ZonPath = ZoneCol.Get(Row, TEXT("ZonPath"));
			if (ZonPath.IsEmpty())
				continue;
			// The zone key is the .zon's FOLDER, not its filename.
			//
			// RoseImportMap resolves a zone as MapsRoot/<Planet>/<Zone>, so the
			// level called EJ01 is the row whose folder is EJ01.  Two rows name
			// EJ01.zon — "Maps\ELDEON\EJ01\EJ01.zon" and the legacy
			// "Maps\Junon\H3\EJ01.zon" — and keying on the filename collides
			// them, handing Eldeon's forest zone whichever row happened to be
			// parsed last.  By folder they are correctly EJ01 and H3.
			FString Zone;
			{
				const FString Normalised = ZonPath.Replace(TEXT("\\"), TEXT("/"));
				Zone = FPaths::GetPathLeaf(FPaths::GetPath(Normalised)).ToUpper();
				if (Zone.IsEmpty())
					Zone = FPaths::GetBaseFilename(Normalised).ToUpper();
			}
			if (Zone.IsEmpty())
				continue;

			const int32 SkyRow = ZoneCol.GetInt(Row, TEXT("Sky"), 0);
			if (!RowMaterial.IsValidIndex(SkyRow) || RowMaterial[SkyRow].IsEmpty())
				continue;

			// PLANET, taken from the ZON path, not from the zone's name.
			//
			// "3DDATA\Maps\Junon\JPT01\JPT01.zon" -> JUNON.  The letter-prefix
			// convention (J = Junon, L = Luna, E = Eldeon, O = Oro) holds for
			// most zones and breaks for the ones that matter least to guess at:
			// SKTOWN, TRON and KCHURCH carry no planet letter at all.  The
			// folder is authoritative and free to read here.
			FString Planet;
			{
				TArray<FString> Parts;
				ZonPath.Replace(TEXT("\\"), TEXT("/")).ParseIntoArray(Parts, TEXT("/"), true);
				for (int32 p = 0; p + 1 < Parts.Num(); ++p)
				{
					if (Parts[p].Equals(TEXT("Maps"), ESearchCase::IgnoreCase))
					{
						Planet = Parts[p + 1].ToUpper();
						break;
					}
				}
			}

			TSharedRef<FJsonObject> Z = MakeShared<FJsonObject>();
			Z->SetStringField(TEXT("zone"), Zone);
			Z->SetStringField(TEXT("planet"), Planet);
			Z->SetNumberField(TEXT("sky"), SkyRow);
			// Day-length columns, in the client's own units.  A DayCycle of 0
			// means the zone never changes (dungeons/interiors) — the runtime
			// pins the blend instead of running a clock.
			Z->SetNumberField(TEXT("dayCycle"), ZoneCol.GetInt(Row, TEXT("DayCycle"), 0));
			Z->SetNumberField(TEXT("morning"), ZoneCol.GetInt(Row, TEXT("MorningTime"), 0));
			Z->SetNumberField(TEXT("day"), ZoneCol.GetInt(Row, TEXT("DayTime"), 0));
			Z->SetNumberField(TEXT("evening"), ZoneCol.GetInt(Row, TEXT("EveningTime"), 0));
			Z->SetNumberField(TEXT("night"), ZoneCol.GetInt(Row, TEXT("NightTime"), 0));
			// ROSE's per-zone weather type — a SEED for our own weather set, not
			// a limit on it.  0 means clear in every zone measured.
			Z->SetNumberField(TEXT("weather"), ZoneCol.GetInt(Row, TEXT("Weather"), 0));
			ZonesJson.Add(MakeShared<FJsonValueObject>(Z));
			++Result.ZonesMapped;
		}
		Root->SetArrayField(TEXT("zones"), ZonesJson);
	}
	else
	{
		UE_LOG(LogRoseImport, Warning,
			TEXT("sky: LIST_ZONE.STB unreadable — no zone mapping written"));
	}

	FString Json;
	TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(Root, Writer);

	const FString OutFile = FPaths::ProjectContentDir() / TEXT("Sky/sky.json");
	if (!FFileHelper::SaveStringToFile(Json, *OutFile))
	{
		UE_LOG(LogRoseImport, Error, TEXT("sky: cannot write %s"), *OutFile);
		return false;
	}

	Result.SecondsTotal = FPlatformTime::Seconds() - TimeStart;
	Result.bSuccess = true;

	UE_LOG(LogRoseImport, Display,
		TEXT("=== sky import OK ===  rows %d  domes %d  textures %d  materials %d  "
		     "zones %d  failed %d  %.2fs"),
		Result.SkyRows, Result.MeshesBuilt, Result.TexturesBuilt,
		Result.MaterialsBuilt, Result.ZonesMapped, Result.Failed, Result.SecondsTotal);
	UE_LOG(LogRoseImport, Display, TEXT("wrote %s"), *OutFile);
	return true;
}
