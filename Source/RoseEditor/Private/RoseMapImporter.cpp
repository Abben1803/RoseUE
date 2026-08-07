#include "RoseMapImporter.h"

#include "RoseBinaryReader.h"
#include "RoseDds.h"
#include "RoseEditor.h"
#include "RoseMaterialBuilder.h"
#include "RoseObjectBuilder.h"
#include "RoseObjectFormats.h"
#include "RosePathResolver.h"
#include "RoseStbSchema.h"
#include "RoseWaterBuilder.h"
#include "RoseTerrainFormats.h"

// Gameplay actors the importer places (RoseUE module).
#include "RoseMonsterSpawner.h"
#include "RoseNpc.h"
#include "RoseWarpPortal.h"
#include "GameFramework/PlayerStart.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "ObjectTools.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Brush.h"
#include "Engine/DirectionalLight.h"
#include "Engine/PointLight.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/Level.h"
#include "GameFramework/WorldSettings.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/Texture2D.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	// ── ROSE terrain constants (src/client/io_terrain.cpp) ──────────────────
	// A chunk file (e.g. 30_30.HIM) covers 16 patches x 4 grids = 64 quads per
	// axis, 65 vertices, across 16000 world units.
	constexpr int32 kPatchesPerChunk = 16;
	constexpr int32 kGridsPerPatch = 4;
	constexpr int32 kQuadsPerChunk = kPatchesPerChunk * kGridsPerPatch;   // 64
	constexpr float kTileWorldSize = 16000.f;
	constexpr float kGridStep = kTileWorldSize / kQuadsPerChunk;          // 250

	const TCHAR* kTerrainMaster = TEXT("/Game/Atlas/M_RoseTerrain.M_RoseTerrain");

	struct FChunk
	{
		int32 FileX = 0;
		int32 FileY = 0;
		int32 WorldTileY = 0;    // file Y is flipped about the zone centre
		FString Stem;
		FRoseHIM HIM;
		FRoseTIL TIL;
	};

	// ── ROSE -> UE space ───────────────────────────────────────────────────
	// The two frames differ by a single mirror in Y (ROSE world Y grows north,
	// UE's grows south).  A transform M in ROSE space becomes F·M·F in UE
	// space; because F is its own inverse this is a homomorphism, so parts can
	// be composed in ROSE space and converted once at the end.
	//
	// Mesh VERTICES are pre-mirrored when the UStaticMesh is built
	// (RoseObjectBuilder), which is what keeps instance scales positive.
	const FMatrix kMirrorY(
		FPlane(1, 0, 0, 0), FPlane(0, -1, 0, 0), FPlane(0, 0, 1, 0), FPlane(0, 0, 0, 1));

	// Compose a ROSE transform into UE's ROW-vector matrix layout.
	//
	// mapforge builds a COLUMN-vector matrix and scales its columns; UE reads
	// row-vectors, so this is the transpose and the scale lands on rows.
	// Getting that backwards transposes every rotation in the zone.
	FMatrix RoseCompose(const FVector3f& Pos, const FQuat4f& Q, const FVector3f& Scale)
	{
		float X = Q.X, Y = Q.Y, Z = Q.Z, W = Q.W;
		const float N = FMath::Sqrt(X * X + Y * Y + Z * Z + W * W);
		if (N > KINDA_SMALL_NUMBER) { X /= N; Y /= N; Z /= N; W /= N; }

		// Column-vector rotation (mapforge quat_matrix).
		const double R00 = 1 - 2 * (Y * Y + Z * Z), R01 = 2 * (X * Y - Z * W), R02 = 2 * (X * Z + Y * W);
		const double R10 = 2 * (X * Y + Z * W), R11 = 1 - 2 * (X * X + Z * Z), R12 = 2 * (Y * Z - X * W);
		const double R20 = 2 * (X * Z - Y * W), R21 = 2 * (Y * Z + X * W), R22 = 1 - 2 * (X * X + Y * Y);

		FMatrix M = FMatrix::Identity;
		M.M[0][0] = R00 * Scale.X; M.M[0][1] = R10 * Scale.X; M.M[0][2] = R20 * Scale.X;
		M.M[1][0] = R01 * Scale.Y; M.M[1][1] = R11 * Scale.Y; M.M[1][2] = R21 * Scale.Y;
		M.M[2][0] = R02 * Scale.Z; M.M[2][1] = R12 * Scale.Z; M.M[2][2] = R22 * Scale.Z;
		M.M[3][0] = Pos.X; M.M[3][1] = Pos.Y; M.M[3][2] = Pos.Z;
		return M;
	}

	FTransform RoseToUnrealTransform(const FMatrix& RoseMatrix)
	{
		return FTransform(kMirrorY * RoseMatrix * kMirrorY);
	}

	// Loaded tile texture + where it landed in the atlas.
	struct FAtlasEntry
	{
		FVector2f UVMin = FVector2f::ZeroVector;
		FVector2f UVScale = FVector2f(1.f, 1.f);
	};

	FString NormaliseRosePath(const FString& In)
	{
		FString Out = In;
		Out.ReplaceInline(TEXT("\\"), TEXT("/"));
		return Out;
	}

	// FullyLoad belongs HERE, at creation — never just before SavePackage.
	// Late, it pulls the on-disk exports in on top of the object already built
	// and the bulkdata fails validation ("invalid payload"), fatally.
	UPackage* MakePackage(const FString& PackageName)
	{
		UPackage* Pkg = CreatePackage(*PackageName);
		if (Pkg)
			Pkg->FullyLoad();
		return Pkg;
	}

	bool SavePackageToDisk(UPackage* Package, UObject* Asset, const FString& Extension)
	{
		Package->MarkPackageDirty();
		const FString Filename = FPackageName::LongPackageNameToFilename(Package->GetName(), Extension);

		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		Args.Error = GWarn;

		if (!UPackage::SavePackage(Package, Asset, *Filename, Args))
		{
			UE_LOG(LogRoseImport, Error, TEXT("failed to save %s"), *Package->GetName());
			return false;
		}

		// Leave it clean.  A package left dirty gets picked up by the editor's
		// autosave, which re-serialises texture source bulkdata that has already
		// been written out — the "invalid payload" save-failed dialog.
		Package->SetDirtyFlag(false);
		return true;
	}

	// Bilinear resample so tiles of differing sizes still pack into a uniform
	// grid.  ROSE tiles are 256x256 almost everywhere, so this is a fallback.
	void ResampleTo(const FRoseImage& Src, int32 DstW, int32 DstH, FRoseImage& Dst)
	{
		Dst.Width = DstW;
		Dst.Height = DstH;
		Dst.Pixels.SetNumUninitialized(DstW * DstH * 4);

		for (int32 Y = 0; Y < DstH; ++Y)
		{
			const float SY = (Y + 0.5f) * Src.Height / DstH - 0.5f;
			const int32 Y0 = FMath::Clamp((int32)FMath::FloorToInt(SY), 0, Src.Height - 1);
			const int32 Y1 = FMath::Clamp(Y0 + 1, 0, Src.Height - 1);
			const float FY = FMath::Clamp(SY - Y0, 0.f, 1.f);

			for (int32 X = 0; X < DstW; ++X)
			{
				const float SX = (X + 0.5f) * Src.Width / DstW - 0.5f;
				const int32 X0 = FMath::Clamp((int32)FMath::FloorToInt(SX), 0, Src.Width - 1);
				const int32 X1 = FMath::Clamp(X0 + 1, 0, Src.Width - 1);
				const float FX = FMath::Clamp(SX - X0, 0.f, 1.f);

				const FLinearColor C00(Src.GetPixel(X0, Y0));
				const FLinearColor C10(Src.GetPixel(X1, Y0));
				const FLinearColor C01(Src.GetPixel(X0, Y1));
				const FLinearColor C11(Src.GetPixel(X1, Y1));
				const FLinearColor Top = FMath::Lerp(C00, C10, FX);
				const FLinearColor Bot = FMath::Lerp(C01, C11, FX);
				Dst.SetPixel(X, Y, FMath::Lerp(Top, Bot, FY).ToFColor(false));
			}
		}
	}
}

bool RoseImportMap(const FRoseMapImportOptions& Options, FRoseMapImportResult& Result)
{
	const double TimeStart = FPlatformTime::Seconds();

	// ── 1. locate the zone folder ──────────────────────────────────────────
	// Scanning beats parsing LIST_ZONE.STB here: the folder name IS the zone
	// name in every shipped client, and it keeps STB out of this stage.
	const FString MapsRoot = FPaths::Combine(Options.AssetRoot, TEXT("3DDATA"), TEXT("MAPS"));
	FString ZoneDir;
	{
		TArray<FString> Planets;
		IFileManager::Get().FindFiles(Planets, *(MapsRoot / TEXT("*")), false, true);
		for (const FString& Planet : Planets)
		{
			const FString Candidate = MapsRoot / Planet / Options.Zone;
			if (IFileManager::Get().DirectoryExists(*Candidate))
			{
				ZoneDir = Candidate;
				break;
			}
		}
	}
	if (ZoneDir.IsEmpty())
	{
		UE_LOG(LogRoseImport, Error, TEXT("zone '%s' not found under %s"), *Options.Zone, *MapsRoot);
		return false;
	}
	UE_LOG(LogRoseImport, Log, TEXT("zone dir: %s"), *ZoneDir);

	// ── 2. ZON ─────────────────────────────────────────────────────────────
	FRoseZON Zon;
	if (!Zon.Load(ZoneDir / (Options.Zone + TEXT(".ZON"))))
	{
		UE_LOG(LogRoseImport, Error, TEXT("could not read %s.ZON"), *Options.Zone);
		return false;
	}

	// ── 3. chunks ──────────────────────────────────────────────────────────
	const double TimeParseStart = FPlatformTime::Seconds();
	TArray<FString> HimFiles;
	IFileManager::Get().FindFiles(HimFiles, *(ZoneDir / TEXT("*.HIM")), true, false);

	TArray<FChunk> Chunks;
	Chunks.Reserve(HimFiles.Num());
	for (const FString& HimFile : HimFiles)
	{
		const FString Stem = FPaths::GetBaseFilename(HimFile);
		FString XStr, YStr;
		if (!Stem.Split(TEXT("_"), &XStr, &YStr))
			continue;

		FChunk Chunk;
		Chunk.Stem = Stem;
		Chunk.FileX = FCString::Atoi(*XStr);
		Chunk.FileY = FCString::Atoi(*YStr);
		// The map-file Y index is flipped relative to world space: object world
		// positions for file (X,Y) land on world tile 2*centre_y - Y.  Objects
		// use the engine's +centre convention with no flip, so terrain must be
		// placed at the flipped Y to register with them.  X is not flipped.
		Chunk.WorldTileY = 2 * Zon.CenterY - Chunk.FileY;

		if (!Chunk.HIM.Load(ZoneDir / (Stem + TEXT(".HIM"))))
			continue;
		if (!Chunk.TIL.Load(ZoneDir / (Stem + TEXT(".TIL"))))
			continue;

		Chunks.Add(MoveTemp(Chunk));
	}

	if (Chunks.Num() == 0)
	{
		UE_LOG(LogRoseImport, Error, TEXT("no readable chunks in %s"), *ZoneDir);
		return false;
	}
	Result.ChunksLoaded = Chunks.Num();
	Result.SecondsParse = FPlatformTime::Seconds() - TimeParseStart;

	// Global heightfield lookup, so vertex normals can be computed from the
	// HEIGHTFIELD (central differences) instead of from per-patch geometry.
	// Patches duplicate their boundary vertices, so geometric normals crease at
	// every patch and chunk seam; sampling across chunks removes both.
	TMap<FIntPoint, const FRoseHIM*> HeightLookup;
	for (const FChunk& C : Chunks)
		HeightLookup.Add(FIntPoint(C.FileX, C.WorldTileY), &C.HIM);

	auto SampleHeight = [&HeightLookup](int32 GlobalCol, int32 GlobalRow) -> float
	{
		// Vertex 64 of a chunk is vertex 0 of the next, so plain division and
		// modulo land on the right chunk with no special casing.
		const int32 ChunkX = FMath::FloorToInt((float)GlobalCol / kQuadsPerChunk);
		const int32 ChunkY = FMath::FloorToInt((float)GlobalRow / kQuadsPerChunk);
		const int32 LocalCol = GlobalCol - ChunkX * kQuadsPerChunk;
		const int32 LocalRow = GlobalRow - ChunkY * kQuadsPerChunk;
		if (const FRoseHIM* const* Found = HeightLookup.Find(FIntPoint(ChunkX, ChunkY)))
			return (*Found)->At(LocalRow, LocalCol);
		return 0.f;   // outside the zone: flat, only reached at the outer border
	};

	// ── 4. atlas ───────────────────────────────────────────────────────────
	const double TimeAtlasStart = FPlatformTime::Seconds();

	// Every texture index any patch actually references.
	TSet<int32> UsedTextures;
	for (const FChunk& C : Chunks)
	{
		for (int32 Row = 0; Row < C.TIL.Height; ++Row)
		{
			for (int32 Col = 0; Col < C.TIL.Width; ++Col)
			{
				const int32 TileNo = C.TIL.At(Row, Col);
				if (!Zon.Tiles.IsValidIndex(TileNo))
					continue;
				const FRoseZonTile& Tile = Zon.Tiles[TileNo];
				if (Zon.TileTextures.IsValidIndex(Tile.BottomIndex()))
					UsedTextures.Add(Tile.BottomIndex());
				if (Zon.TileTextures.IsValidIndex(Tile.TopIndex()))
					UsedTextures.Add(Tile.TopIndex());
			}
		}
	}

	TArray<int32> TextureOrder = UsedTextures.Array();
	TextureOrder.Sort();

	FRosePathResolver Resolver(Options.AssetRoot);

	TArray<FRoseImage> TileImages;
	TArray<int32> LoadedIndices;
	int32 CellContent = 0;
	for (int32 TexIdx : TextureOrder)
	{
		// ZON tile paths carry a leading "3DData\" and disagree with the
		// extracted files on case — the resolver handles both.
		const FString FullPath = Resolver.Resolve(Zon.TileTextures[TexIdx]);
		FRoseImage Img;
		if (FullPath.IsEmpty() || !RoseLoadDDS(FullPath, Img) || !Img.IsValid())
		{
			UE_LOG(LogRoseImport, Warning, TEXT("tile texture %d unreadable: %s"),
				TexIdx, *Zon.TileTextures[TexIdx]);
			continue;
		}
		CellContent = FMath::Max(CellContent, FMath::Max(Img.Width, Img.Height));
		TileImages.Add(MoveTemp(Img));
		LoadedIndices.Add(TexIdx);
	}

	if (TileImages.Num() == 0)
	{
		UE_LOG(LogRoseImport, Error, TEXT("no tile textures could be loaded"));
		return false;
	}

	const int32 Gutter = FMath::Max(0, Options.AtlasGutter);
	const int32 CellSize = CellContent + Gutter * 2;
	const int32 Columns = FMath::CeilToInt(FMath::Sqrt((float)TileImages.Num()));
	const int32 Rows = FMath::DivideAndRoundUp(TileImages.Num(), Columns);

	// Power of two keeps the mip chain clean.
	const int32 AtlasW = FMath::RoundUpToPowerOfTwo(Columns * CellSize);
	const int32 AtlasH = FMath::RoundUpToPowerOfTwo(Rows * CellSize);

	FRoseImage Atlas;
	Atlas.Width = AtlasW;
	Atlas.Height = AtlasH;
	Atlas.Pixels.SetNumZeroed(AtlasW * AtlasH * 4);

	TMap<int32, FAtlasEntry> AtlasRects;
	for (int32 i = 0; i < TileImages.Num(); ++i)
	{
		const int32 CellX = (i % Columns) * CellSize;
		const int32 CellY = (i / Columns) * CellSize;

		FRoseImage Scaled;
		const FRoseImage* Src = &TileImages[i];
		if (TileImages[i].Width != CellContent || TileImages[i].Height != CellContent)
		{
			ResampleTo(TileImages[i], CellContent, CellContent, Scaled);
			Src = &Scaled;
		}

		// Copy the tile, then replicate its edge pixels outward into the gutter.
		// Without that, mip generation averages across neighbouring cells — the
		// exact cause of the black edges on the gear atlas.
		for (int32 Y = -Gutter; Y < CellContent + Gutter; ++Y)
		{
			const int32 SY = FMath::Clamp(Y, 0, CellContent - 1);
			const int32 DY = CellY + Gutter + Y;
			if (DY < 0 || DY >= AtlasH) continue;
			for (int32 X = -Gutter; X < CellContent + Gutter; ++X)
			{
				const int32 SX = FMath::Clamp(X, 0, CellContent - 1);
				const int32 DX = CellX + Gutter + X;
				if (DX < 0 || DX >= AtlasW) continue;
				Atlas.SetPixel(DX, DY, Src->GetPixel(SX, SY));
			}
		}

		FAtlasEntry Entry;
		Entry.UVMin = FVector2f((CellX + Gutter) / (float)AtlasW, (CellY + Gutter) / (float)AtlasH);
		Entry.UVScale = FVector2f(CellContent / (float)AtlasW, CellContent / (float)AtlasH);
		AtlasRects.Add(LoadedIndices[i], Entry);
	}

	Result.AtlasTiles = TileImages.Num();
	Result.AtlasSize = FMath::Max(AtlasW, AtlasH);
	Result.SecondsAtlas = FPlatformTime::Seconds() - TimeAtlasStart;

	// The terrain blend lives ENTIRELY in the atlas alpha.
	//
	// M_RoseTerrain reproduces the ROSE pixel shader (shader/terrain.psh):
	//     lrp r0.rgb, t1.a, t1, r0     // rgb = lerp(bottom, top, top.a)
	// so if alpha comes out flat, the lerp collapses to "always the top tile"
	// and every patch renders as one texture with a hard edge at its border —
	// the patchwork ground.  ROSE's transition tiles (T026_01..05 against
	// T025_01, rotated per patch) carry ~45% intermediate alpha, so a healthy
	// atlas MUST show a wide mid band here.  Report it rather than assume:
	// a silent flat-alpha atlas is indistinguishable from a material bug.
	{
		int64 Zero = 0, Full = 0, Mid = 0;
		uint8 MinA = 255, MaxA = 0;
		for (int32 i = 3; i < Atlas.Pixels.Num(); i += 4)
		{
			const uint8 A = Atlas.Pixels[i];
			if (A == 0) ++Zero; else if (A == 255) ++Full; else ++Mid;
			MinA = FMath::Min(MinA, A);
			MaxA = FMath::Max(MaxA, A);
		}
		const double Total = FMath::Max<int64>(1, Zero + Full + Mid);
		UE_LOG(LogRoseImport, Log,
			TEXT("atlas alpha: 0=%.1f%%  255=%.1f%%  mid=%.1f%%  (min %d max %d)"),
			Zero / Total * 100.0, Full / Total * 100.0, Mid / Total * 100.0, MinA, MaxA);
		// A zone with a SINGLE tile texture has nothing to blend between, so flat
		// alpha is the correct answer there, not a fault.  KCHURCH is the case:
		// "1 tile textures, 1 tiles" — an interior with one ground material.
		// Reporting that as an error every run trains the eye to ignore the
		// message, and it is what makes an otherwise-clean 53/53 import exit 1.
		if (Mid == 0 && TileImages.Num() > 1)
		{
			UE_LOG(LogRoseImport, Error,
				TEXT("atlas alpha has NO intermediate values — the terrain blend "
					 "cannot work; the tile DDS alpha was lost during decode"));
		}
		else if (Mid == 0)
		{
			UE_LOG(LogRoseImport, Log,
				TEXT("atlas alpha is flat, but this zone has %d tile texture(s) — "
					 "nothing to blend, so this is expected"), TileImages.Num());
		}
	}

	// ── 5. atlas texture asset ─────────────────────────────────────────────
	// Assets live directly under the zone folder — this is the base import
	// path, not a parallel "native" one.
	const FString NativeDir = FString::Printf(TEXT("/Game/Maps/%s"), *Options.Zone);
	const FString AtlasPkgName = FString::Printf(TEXT("%s/Terrain/T_%s_TerrainAtlas"), *NativeDir, *Options.Zone);

	UPackage* AtlasPkg = MakePackage(AtlasPkgName);
	const FString AtlasAssetName = FPackageName::GetShortName(AtlasPkgName);
	// Re-use on re-import rather than stacking a second object under the same
	// name in the package (see FindOrCreateAsset in RoseObjectBuilder.cpp).
	UTexture2D* AtlasTex = FindObject<UTexture2D>(AtlasPkg, *AtlasAssetName);
	if (!AtlasTex)
		AtlasTex = NewObject<UTexture2D>(AtlasPkg, *AtlasAssetName, RF_Public | RF_Standalone);
	AtlasTex->Source.Init(AtlasW, AtlasH, 1, 1, TSF_BGRA8, Atlas.Pixels.GetData());
	// BC7, not TC_Default.
	//
	// This atlas's ALPHA is the terrain blend mask — the material does
	// lerp(bottom, top, top.a).  TC_Default lets UE pick the compressed format,
	// and if its heuristics decide the alpha is not needed it chooses DXT1/BC1,
	// which has no alpha channel at all.  The lerp then collapses to "always the
	// top tile": every patch renders as one flat texture with a hard border,
	// while the source data, the atlas and the material all still measure
	// perfectly correct.  BC7 always carries alpha, so the question cannot
	// arise.
	AtlasTex->CompressionSettings = TC_BC7;
	AtlasTex->CompressionNoAlpha = false;
	AtlasTex->SRGB = true;
	AtlasTex->CompressionSettings = TC_Default;
	AtlasTex->MipGenSettings = TMGS_FromTextureGroup;
	AtlasTex->LODGroup = TEXTUREGROUP_World;
	// The tile UVs never leave their cell, so clamping is correct and stops any
	// residual bleed from wrapping to the far edge of the atlas.
	AtlasTex->AddressX = TA_Clamp;
	AtlasTex->AddressY = TA_Clamp;
	// The whole terrain blend rides on this texture's ALPHA (the material does
	// lerp(bottom, top, top.a)).  TC_Default lets UE choose the compressed
	// format, and if it decides the alpha is not needed it picks DXT1/BC1 —
	// which has NO alpha channel at all.  The lerp then collapses to "always the
	// top tile" and every patch renders as one flat texture with a hard border,
	// while every input still measures perfectly.  Report the format actually
	// chosen rather than assuming.
	AtlasTex->PostEditChange();
	FAssetRegistryModule::AssetCreated(AtlasTex);
	SavePackageToDisk(AtlasPkg, AtlasTex, FPackageName::GetAssetPackageExtension());

	// ── 6. material instance ───────────────────────────────────────────────
	// M_RoseTerrain is the engine's terrain pass in one opaque material:
	//   rgb = lerp(bottom, top, top.a)      (src/engine/shader/terrain.psh)
	// UV0 samples BaseColor, UV1 samples TopColor.  The atlas sub-rect is baked
	// into the UVs here, so UVTransform/TopUVTransform stay at identity and ONE
	// material serves the whole zone.
	// Built in-engine — a fresh clone must not need a Python step, and a master
	// left half-written (e.g. a sampler with no texture) is repaired rather than
	// used, because it would render the whole zone untextured.
	UMaterialInterface* Master = RoseMaterials::EnsureTerrainMaster();
	if (!Master)
	{
		UE_LOG(LogRoseImport, Error, TEXT("could not create the terrain master %s"), kTerrainMaster);
		return false;
	}

	const FString MatPkgName = FString::Printf(TEXT("%s/Terrain/MI_%s_Terrain"), *NativeDir, *Options.Zone);
	UPackage* MatPkg = MakePackage(MatPkgName);
	UMaterialInstanceConstant* Mat = NewObject<UMaterialInstanceConstant>(
		MatPkg, *FPackageName::GetShortName(MatPkgName), RF_Public | RF_Standalone);
	Mat->SetParentEditorOnly(Master);
	Mat->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("BaseColor")), AtlasTex);
	Mat->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("TopColor")), AtlasTex);
	Mat->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("UVTransform")), FLinearColor(1, 1, 0, 0));
	Mat->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("TopUVTransform")), FLinearColor(1, 1, 0, 0));
	Mat->PostEditChange();
	FAssetRegistryModule::AssetCreated(Mat);
	SavePackageToDisk(MatPkg, Mat, FPackageName::GetAssetPackageExtension());

	// ── 7. one static mesh per chunk ───────────────────────────────────────
	const double TimeMeshStart = FPlatformTime::Seconds();
	TArray<UStaticMesh*> ChunkMeshes;
	ChunkMeshes.Reserve(Chunks.Num());

	for (const FChunk& Chunk : Chunks)
	{
		const float OriginX = Chunk.FileX * kTileWorldSize;
		const float OriginY = Chunk.WorldTileY * kTileWorldSize;
		const int32 GlobalColBase = Chunk.FileX * kQuadsPerChunk;
		const int32 GlobalRowBase = Chunk.WorldTileY * kQuadsPerChunk;

		FMeshDescription MeshDesc;
		FStaticMeshAttributes Attributes(MeshDesc);
		Attributes.Register();

		TVertexAttributesRef<FVector3f> VertexPositions = Attributes.GetVertexPositions();
		TVertexInstanceAttributesRef<FVector3f> InstanceNormals = Attributes.GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector2f> InstanceUVs = Attributes.GetVertexInstanceUVs();
		// 0 = bottom tile, 1 = top tile, 2 = CHUNK-space lightmap.
		//
		// UV0/UV1 are per-PATCH (each patch samples one atlas cell); UV2 must
		// span the whole chunk, because _PLANELIGHTINGMAP.DDS is ONE texture per
		// chunk covering all 16x16 patches.
		InstanceUVs.SetNumChannels(3);

		const FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();
		Attributes.GetPolygonGroupMaterialSlotNames()[PolyGroup] = FName(TEXT("Terrain"));

		const int32 VertsPerPatch = (kGridsPerPatch + 1) * (kGridsPerPatch + 1);
		MeshDesc.ReserveNewVertices(kPatchesPerChunk * kPatchesPerChunk * VertsPerPatch);
		MeshDesc.ReserveNewVertexInstances(kPatchesPerChunk * kPatchesPerChunk * VertsPerPatch);
		MeshDesc.ReserveNewTriangles(kPatchesPerChunk * kPatchesPerChunk * kGridsPerPatch * kGridsPerPatch * 2);

		for (int32 PatchRow = 0; PatchRow < kPatchesPerChunk; ++PatchRow)
		{
			for (int32 PatchCol = 0; PatchCol < kPatchesPerChunk; ++PatchCol)
			{
				// (Row, Col) — do NOT transpose this.
				//
				// The port reads its TIL as [tileX, tileY] against an array
				// declared [height, width], which reads like the file row is
				// driven by world X.  Matching that here turns the ground into a
				// diagonal CHECKERBOARD, so their index convention does not carry
				// over — their vertex axes differ from ours (they build
				// (x*stride, h, y*stride) in a Y-up frame; we build X across and
				// -Y down in a Z-up one).  Tried and measured, not assumed.
				const int32 TileNo = Chunk.TIL.At(PatchRow, PatchCol);
				if (!Zon.Tiles.IsValidIndex(TileNo))
					continue;
				const FRoseZonTile& Tile = Zon.Tiles[TileNo];

				const FAtlasEntry* Bottom = AtlasRects.Find(Tile.BottomIndex());
				const FAtlasEntry* Top = AtlasRects.Find(Tile.TopIndex());
				if (!Bottom)
					continue;                    // nothing to draw without a base layer
				if (!Top)
					Top = Bottom;                // no valid 2nd map -> lerp is identity

				TArray<FVertexInstanceID, TInlineAllocator<25>> Instances;
				Instances.Reserve(VertsPerPatch);

				for (int32 IY = 0; IY <= kGridsPerPatch; ++IY)
				{
					for (int32 IX = 0; IX <= kGridsPerPatch; ++IX)
					{
						const int32 LocalRow = PatchRow * kGridsPerPatch + IY;
						const int32 LocalCol = PatchCol * kGridsPerPatch + IX;
						const int32 GlobalCol = GlobalColBase + LocalCol;
						const int32 GlobalRow = GlobalRowBase + LocalRow;

						const float Height = Chunk.HIM.At(LocalRow, LocalCol);

						// Vertices are emitted in UE world centimetres directly.
						// X passes through; Y is NEGATED.  ROSE world Y grows
						// north, UE's grows south, and every object in the zone
						// is already placed at -(y+520000) (the IFO->UE rule).
						// The old pipeline got this from the glTF round trip
						// (ROSE cm -> glTF m -> UE cm), which is NOT identity —
						// assuming it was put this terrain at +Y and mirrored
						// the whole zone away from its own NPCs and spawners.
						const FVector3f Position(
							OriginX + LocalCol * kGridStep,
							-(OriginY + LocalRow * kGridStep),
							Height);

						// Normal from the heightfield, sampled ACROSS chunks —
						// geometric normals would crease at every patch and
						// chunk seam because boundary vertices are duplicated.
						// The Y derivative carries the negation above: one more
						// step in LocalRow is one step in -Y.
						const float HL = SampleHeight(GlobalCol - 1, GlobalRow);
						const float HR = SampleHeight(GlobalCol + 1, GlobalRow);
						const float HD = SampleHeight(GlobalCol, GlobalRow - 1);
						const float HU = SampleHeight(GlobalCol, GlobalRow + 1);
						const FVector3f Normal = FVector3f(
							-(HR - HL) / (2.f * kGridStep),
							 (HU - HD) / (2.f * kGridStep),
							1.f).GetSafeNormal();

						// The rotation applies to BOTH layers, not just the top.
						// feeds the same rotMatrix into GetUVBottom and
						// GetUVTop; rotating only the top leaves the base tile
						// misaligned against the overlay it is meant to blend
						// with, so the seam never reads as continuous.
						// U from the column, V from the row — NOT transposed.
						//
						// The authority is the engine, zz_mesh_tool.cpp
						// get_uv_by_type():
						//     ouv.x = ix / face_width;
						//     ouv.y = iy / face_width;
						//     default (ZZ_UV_NORMAL): uv = ouv;
						// and RoseRotateTileUV below reproduces its other five
						// cases exactly.
					
						float U = IX / (float)kGridsPerPatch;
						float V = IY / (float)kGridsPerPatch;
						RoseRotateTileUV(U, V, Tile.Rotation);

						// NOW mirror V, because OUR MESH is mirrored.
						//
						// Everything above is in ROSE's UV space, which is the
						// only space RoseRotateTileUV's table is valid in — so the
						// rotation must be applied first and this correction
						// afterwards.
						//
						// The vertex Y is NEGATED a few lines up: one more step in
						// LocalRow is one step in -Y world.  ROSE has no such
						// negation, so there V grows along +Y.  Leaving V = IY/4
						// therefore runs the tile's V axis BACKWARDS across the
						// ground and mirrors every blend mask against the terrain
						// it sits on.
						//
						// This is what made the ground read as discrete blocks.
						// The masks are authored so that neighbouring patches
						// dissolve into one another; mirrored, a mask that should
						// fade toward one edge fades toward the opposite one, so
						// patches that were designed to meet butt up hard and you
						// see every patch's cut-off.  Worse, it is not uniform:
						// a mirrored V turns FlipVertical into no-flip and back,
						// so the ~30% of patches carrying flips land correctly
						// while the rest do not, which is the patchy, parquet
						// appearance rather than an even offset.
						//
						// Note every INPUT still measures perfectly correct with
						// this bug present — tile alphas, the atlas, the rotation
						// table, the TIL stride.  That is why it survived so much
						// verification: nothing is wrong with the data, only with
						// the axis it is laid onto.
						V = 1.f - V;

						const float TU = U, TV = V;

						const FVertexID Vertex = MeshDesc.CreateVertex();
						VertexPositions[Vertex] = Position;

						const FVertexInstanceID Instance = MeshDesc.CreateVertexInstance(Vertex);
						InstanceNormals[Instance] = Normal;
						InstanceUVs.Set(Instance, 0, Bottom->UVMin + FVector2f(U, V) * Bottom->UVScale);
						InstanceUVs.Set(Instance, 1, Top->UVMin + FVector2f(TU, TV) * Top->UVScale);
						// Lightmap: 0..1 across the CHUNK, not the patch, and in
						// the SAME axis convention as the tile UVs above.
						//
						// This used to be (row, 1 - col), That was wrong for the same reason the tile UVs
						// were: their axes sit 90 degrees from ours, so
						// transcribing their expression into our loop transposes
						// and flips the lightmap across the chunk.
						//
						// It shows up badly because LightmapScale is 5 —
						// albedo * (1 + light*5).  A misaligned lightmap at that
						// strength paints soft-edged rectangular bright patches
						// that do not follow the ground, which reads as pale
						// staggered slabs over the terrain and is easily
						// mistaken for a tile-blending fault.
						// V mirrored for the same reason as the tile UVs above: the
						// vertex Y is negated, so a raw LocalRow/64 runs the
						// lightmap backwards across the chunk and its baked
						// shading lands on the wrong side of every slope.
						InstanceUVs.Set(Instance, 2, FVector2f(
							LocalCol / (float)kQuadsPerChunk,
							1.f - LocalRow / (float)kQuadsPerChunk));

						Instances.Add(Instance);
					}
				}

				const int32 Stride = kGridsPerPatch + 1;
				for (int32 IY = 0; IY < kGridsPerPatch; ++IY)
				{
					for (int32 IX = 0; IX < kGridsPerPatch; ++IX)
					{
						const int32 V0 = IY * Stride + IX;
						// Winding must make faces point UP (+Z).
						//
						// TWO flips are in play and they CANCEL, so this order
						// is mapforge's unchanged:
						//   1. negating Y above mirrors the surface, which
						//      reverses triangle orientation;
						//   2. UE computes the face normal as
						//      Cross(P2-P0, P1-P0) — the REVERSE cross product
						//      ("left-handed coordinate system, but a
						//      counter-clockwise winding order",
						//      StaticMeshOperations.cpp) — which reverses it
						//      again.
						// Flipping the winding for (1) alone points every face
						// DOWN; with a two-sided material UE then flips the
						// shading normal on the visible side and the ground
						// renders dark from above and lit from below.
						MeshDesc.CreateTriangle(PolyGroup,
							{ Instances[V0], Instances[V0 + 1], Instances[V0 + Stride] });
						MeshDesc.CreateTriangle(PolyGroup,
							{ Instances[V0 + 1], Instances[V0 + Stride + 1], Instances[V0 + Stride] });
					}
				}
			}
		}

		const FString MeshName = FString::Printf(TEXT("SM_%s_%d_%d"),
			*Options.Zone, Chunk.FileX, Chunk.FileY);
		const FString MeshPkgName = FString::Printf(TEXT("%s/Terrain/%s"), *NativeDir, *MeshName);

		// ── per-chunk baked lightmap (terrain.psh: mul_x2 r0.rgb, r0, t2) ──
		// One _PLANELIGHTINGMAP.DDS per chunk, so the material has to be
		// per-chunk too — the atlas params are identical, only the lightmap
		// differs.  Falls back to the shared zone material when the texture is
		// absent, which is every zone that has no classic counterpart.
		UMaterialInterface* ChunkMat = Mat;
		if (Options.bImportLightmaps)
		{
			// Sits in the zone tree beside the HIM/TIL, recovered from the VFS
			// by name-hash (see the header note).
			const FString LmPath = ZoneDir / Chunk.Stem /
				(Chunk.Stem + TEXT("_PLANELIGHTINGMAP.DDS"));
			const FString Rel = LmPath;

			FRoseImage LmImg;
			if (!LmPath.IsEmpty() && RoseLoadDDS(LmPath, LmImg) && LmImg.IsValid())
			{
				const FString LmTexName = FString::Printf(TEXT("T_%s_%s_LM"),
					*Options.Zone, *Chunk.Stem);
				UPackage* LmPkg = MakePackage(
					FString::Printf(TEXT("%s/Terrain/%s"), *NativeDir, *LmTexName));
				UTexture2D* LmTex = FindObject<UTexture2D>(LmPkg, *LmTexName);
				if (!LmTex)
					LmTex = NewObject<UTexture2D>(LmPkg, *LmTexName, RF_Public | RF_Standalone);
				LmTex->Source.Init(LmImg.Width, LmImg.Height, 1, 1, TSF_BGRA8, LmImg.Pixels.GetData());
				LmTex->SRGB = true;
				LmTex->CompressionSettings = TC_Default;
				LmTex->CompressionNoAlpha = true;      // lighting only, no alpha
				LmTex->MipGenSettings = TMGS_FromTextureGroup;
				LmTex->LODGroup = TEXTUREGROUP_World;
				// Clamp: the lightmap covers the chunk exactly, so wrapping at
				// the seam would sample the far edge and stripe the border.
				LmTex->AddressX = TA_Clamp;
				LmTex->AddressY = TA_Clamp;
				LmTex->PostEditChange();
				FAssetRegistryModule::AssetCreated(LmTex);
				SavePackageToDisk(LmPkg, LmTex, FPackageName::GetAssetPackageExtension());

				const FString LmMatName = FString::Printf(TEXT("MI_%s_%s_Terrain"),
					*Options.Zone, *Chunk.Stem);
				UPackage* LmMatPkg = MakePackage(
					FString::Printf(TEXT("%s/Terrain/%s"), *NativeDir, *LmMatName));
				UMaterialInstanceConstant* CMat = FindObject<UMaterialInstanceConstant>(
					LmMatPkg, *LmMatName);
				if (!CMat)
					CMat = NewObject<UMaterialInstanceConstant>(
						LmMatPkg, *LmMatName, RF_Public | RF_Standalone);
				CMat->SetParentEditorOnly(Master);
				CMat->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("BaseColor")), AtlasTex);
				CMat->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("TopColor")), AtlasTex);
				CMat->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("Lightmap")), LmTex);
				//  Only set where a lightmap is actually
				// bound; the master defaults it to 0 so zones without one render
				// as plain albedo rather than being darkened by a white default.
				// 1 = apply ROSE's baked lightmap in full (albedo * lightmap*2).
				//
				// Not optional: ROSE authors its ground textures bright BECAUSE
				// the engine multiplies them down by this map.  With it at 0 the
				// plaza's 0.74 albedo renders near-white.
				CMat->SetScalarParameterValueEditorOnly(FMaterialParameterInfo(TEXT("LightmapScale")), 1.f);
				CMat->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("UVTransform")), FLinearColor(1, 1, 0, 0));
				CMat->SetVectorParameterValueEditorOnly(FMaterialParameterInfo(TEXT("TopUVTransform")), FLinearColor(1, 1, 0, 0));
				CMat->PostEditChange();
				FAssetRegistryModule::AssetCreated(CMat);
				SavePackageToDisk(LmMatPkg, CMat, FPackageName::GetAssetPackageExtension());

				ChunkMat = CMat;
				++Result.LightmapsImported;
			}
			else if (Result.LightmapsMissing < 3)
			{
				UE_LOG(LogRoseImport, Warning, TEXT("no lightmap for chunk %s (%s)"),
					*Chunk.Stem, *Rel);
				++Result.LightmapsMissing;
			}
			else
			{
				++Result.LightmapsMissing;
			}
		}

		UPackage* MeshPkg = MakePackage(MeshPkgName);
		// Re-use rather than NewObject over a live name (which renames the
		// incumbent and leaves a stale mesh in the package), then RESET: both
		// AddSourceModel and the material array append, so re-imports stack
		// LODs until the 9th asserts inside Build().
		UStaticMesh* Mesh = FindObject<UStaticMesh>(MeshPkg, *MeshName);
		if (!Mesh)
			Mesh = NewObject<UStaticMesh>(MeshPkg, *MeshName, RF_Public | RF_Standalone);
		Mesh->InitResources();
		Mesh->SetLightingGuid();
		Mesh->SetNumSourceModels(0);
		Mesh->GetStaticMaterials().Empty();
		Mesh->GetStaticMaterials().Add(FStaticMaterial(ChunkMat, TEXT("Terrain"), TEXT("Terrain")));

		FStaticMeshSourceModel& SrcModel = Mesh->AddSourceModel();
		SrcModel.BuildSettings.bRecomputeNormals = false;   // heightfield normals, above
		SrcModel.BuildSettings.bRecomputeTangents = true;
		SrcModel.BuildSettings.bUseMikkTSpace = true;
		SrcModel.BuildSettings.bGenerateLightmapUVs = false;
		SrcModel.BuildSettings.bRemoveDegenerates = true;

		FMeshDescription* Target = Mesh->CreateMeshDescription(0);
		*Target = MoveTemp(MeshDesc);
		Mesh->CommitMeshDescription(0);

		// Nanite stays OFF.  Terrain vertices are world-baked ~5e7 units from
		// the mesh origin, and Nanite's bounds-relative quantisation cracked
		// every patch seam when Interchange enabled it (docs/STATUS.md).
		Mesh->GetNaniteSettings().bEnabled = false;

		Mesh->Build(false);
		Mesh->CreateBodySetup();
		Mesh->GetBodySetup()->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
		Mesh->PostEditChange();

		FAssetRegistryModule::AssetCreated(Mesh);
		SavePackageToDisk(MeshPkg, Mesh, FPackageName::GetAssetPackageExtension());

		if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0)
		{
			Result.TotalVertices += Mesh->GetRenderData()->LODResources[0].GetNumVertices();
			Result.TotalTriangles += Mesh->GetRenderData()->LODResources[0].GetNumTriangles();
		}

		ChunkMeshes.Add(Mesh);
		++Result.MeshesBuilt;
	}
	Result.SecondsMeshes = FPlatformTime::Seconds() - TimeMeshStart;

	// ── 8. THE LEVEL ───────────────────────────────────────────────────────
	// This is the deliverable.  Importing meshes and materials as loose assets
	// throws away every transform — the level is what the game loads, so the
	// importer creates a real UWorld and places an actor per chunk in it.
	const double TimeSaveStart = FPlatformTime::Seconds();

	const FString LevelName = FString::Printf(TEXT("L_%s%s"), *Options.Zone, *Options.LevelSuffix);
	const FString LevelPkgName = FString::Printf(TEXT("/Game/Maps/%s/%s"), *Options.Zone, *LevelName);

	UPackage* LevelPkg = nullptr;
	UWorld* World = nullptr;

	// RE-IMPORT: if the level already exists, reuse its world.  Calling
	// UWorld::CreateWorld into a package that already holds one is a FATAL
	// error ("Cannot generate unique name for 'WorldSettings'"), which makes
	// the importer a one-shot — it would work on a clean tree and take the
	// editor down on every run after that.
	if (FPackageName::DoesPackageExist(LevelPkgName))
	{
		LevelPkg = LoadPackage(nullptr, *LevelPkgName, LOAD_None);
		if (LevelPkg)
			World = UWorld::FindWorldInPackage(LevelPkg);
	}

	if (World)
	{
		// Clear what a previous run placed, but leave the level's own furniture
		// (WorldSettings, the default brush) alone — destroying those corrupts
		// the level.
		TArray<AActor*> Stale;
		for (AActor* Actor : World->PersistentLevel->Actors)
		{
			if (!Actor || Actor->IsA(AWorldSettings::StaticClass()))
				continue;

			// APostProcessVolume derives from ABrush, so the "leave brushes
			// alone" rule below was sparing OUR OWN RoseExposure volume and
			// every re-import stacked another one (2 after the second import,
			// 3 after the third...).  Volumes we placed are ours to remove;
			// only the level's genuine default brush must survive.
			if (Actor->IsA(ABrush::StaticClass())
				&& !Actor->IsA(APostProcessVolume::StaticClass()))
			{
				continue;
			}

			Stale.Add(Actor);
		}
		for (AActor* Actor : Stale)
			World->DestroyActor(Actor);

		UE_LOG(LogRoseImport, Log, TEXT("re-import: cleared %d actors from %s"),
			Stale.Num(), *LevelPkgName);
	}
	else
	{
		LevelPkg = MakePackage(LevelPkgName);

		// INACTIVE, deliberately — water is NOT this commandlet's job.
		//
		// An Editor world is what UWaterSubsystem::DoesSupportWorldType needs,
		// and switching to it here does make water import.  It also brings up
		// WorldPartitionSubsystem and friends, which then have to be torn down
		// or the process dies on shutdown with "destroyed while still
		// initialized" — measured, not theoretical.
		//
		// Water is handled by -run=RoseImportWater instead: a separate, additive
		// pass that owns that world lifecycle properly and cannot regress a map
		// import that already works for all 53 zones.
		World = UWorld::CreateWorld(EWorldType::Inactive, /*bInformEngineOfWorld*/ false,
			FName(*LevelName), LevelPkg);
		if (!World)
		{
			UE_LOG(LogRoseImport, Error, TEXT("could not create world %s"), *LevelPkgName);
			return false;
		}
		FAssetRegistryModule::AssetCreated(World);
	}

	World->SetFlags(RF_Public | RF_Standalone);

	for (int32 i = 0; i < ChunkMeshes.Num(); ++i)
	{
		FActorSpawnParameters Params;
		Params.OverrideLevel = World->PersistentLevel;
		Params.ObjectFlags = RF_Transactional;

		AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
			AStaticMeshActor::StaticClass(), FTransform::Identity, Params);
		if (!Actor)
			continue;

		// The mesh already carries absolute world coordinates, so the actor sits
		// at the origin — same convention the current scene import produces.
		Actor->SetMobility(EComponentMobility::Static);
		Actor->GetStaticMeshComponent()->SetStaticMesh(ChunkMeshes[i]);
		Actor->GetStaticMeshComponent()->SetMobility(EComponentMobility::Static);
		Actor->SetActorLabel(FString::Printf(TEXT("Terrain_%s"), *ChunkMeshes[i]->GetName()));
		++Result.ActorsPlaced;
	}

	// ── 9. ZSC objects + IFO entities ──────────────────────────────────────
	const double TimeObjStart = FPlatformTime::Seconds();
	FRoseAssetCache Cache(NativeDir, Resolver);

	if (Options.bImportObjects || Options.bImportEntities)
	{
		// The deco/cnst ZSC packs are named by the zone's LIST_ZONE.STB row.
		// Find the row by matching the .ZON path's basename against the zone —
		// that avoids needing a zone id from anywhere else.
		FRoseSTB ZoneList;
		FString DecoPack, CnstPack;
		if (ZoneList.Load(FPaths::Combine(Options.AssetRoot, TEXT("3DDATA"), TEXT("STB"), TEXT("LIST_ZONE.STB"))))
		{
			// Bound through the schema, not by index: LIST_ZONE is the table that
			// differs most between client eras — 41 of its 42 shared columns hold
			// something else in classic, where column 11 is "Object Table" rather
			// than Arua's "DECO Table".
			const FRoseStbBinding Col = RoseStb::Bind(ZoneList, RoseStb::ZoneSchema());

			for (int32 Row = 0; Row < ZoneList.Rows; ++Row)
			{
				const FString& ZonPath = Col.Get(Row, TEXT("ZonPath"));
				if (ZonPath.IsEmpty())
					continue;
				FString Clean = ZonPath;
				Clean.ReplaceInline(TEXT("\\"), TEXT("/"));
				if (FPaths::GetBaseFilename(Clean).Equals(Options.Zone, ESearchCase::IgnoreCase))
				{
					DecoPack = Col.Get(Row, TEXT("DecoTable"));
					CnstPack = Col.Get(Row, TEXT("CnstTable"));
					break;
				}
			}
		}

		FRoseZSC DecoZsc, CnstZsc;
		const bool bHasDeco = !DecoPack.IsEmpty() && DecoZsc.Load(Resolver.Resolve(DecoPack));
		const bool bHasCnst = !CnstPack.IsEmpty() && CnstZsc.Load(Resolver.Resolve(CnstPack));

		// What the packs actually carry, before anything is placed.  ROSE flags
		// its lamp bulbs itself (DayNight / LightContainer), so this is the real
		// count of lights the zone wants, not an estimate from object names.
		for (const TPair<const TCHAR*, const FRoseZSC*>& Pack :
			{ TPair<const TCHAR*, const FRoseZSC*>(TEXT("deco"), &DecoZsc),
			  TPair<const TCHAR*, const FRoseZSC*>(TEXT("cnst"), &CnstZsc) })
		{
			int32 Normal = 0, DayNight = 0, Container = 0, ObjsWithFx = 0;
			for (const FRoseZscObject& Obj : Pack.Value->Objects)
			{
				if (Obj.Effects.Num() > 0)
					++ObjsWithFx;
				for (const FRoseZscEffect& Fx : Obj.Effects)
				{
					if (Fx.EffectType == ROSE_ZSC_EFFECT_DAYNIGHT) ++DayNight;
					else if (Fx.EffectType == ROSE_ZSC_EFFECT_LIGHTCONTAINER) ++Container;
					else ++Normal;
				}
			}
			if (ObjsWithFx > 0)
			{
				UE_LOG(LogRoseImport, Log,
					TEXT("%s ZSC: %d objects carry effects — %d normal, %d DAYNIGHT, %d LIGHT-CONTAINER (%d effect files)"),
					Pack.Key, ObjsWithFx, Normal, DayNight, Container,
					Pack.Value->EffectFiles.Num());

				// Name them.  The effect FILE is the ground truth for whether a
				// socket is a lamp: the type flag alone does not say what the
				// effect is, and a "Normal" socket on a lamp post is still a lamp.
				for (int32 o = 0; o < Pack.Value->Objects.Num(); ++o)
				{
					for (const FRoseZscEffect& Fx : Pack.Value->Objects[o].Effects)
					{
						const FString File = Pack.Value->EffectFiles.IsValidIndex(Fx.EffectId)
							? FPaths::GetCleanFilename(Pack.Value->EffectFiles[Fx.EffectId])
							: FString(TEXT("?"));
						UE_LOG(LogRoseImport, Log,
							TEXT("   %s obj %-4d type %d  %s"),
							Pack.Key, o, Fx.EffectType, *File);
					}
				}
			}
		}
		UE_LOG(LogRoseImport, Log, TEXT("packs: deco '%s' (%s), cnst '%s' (%s)"),
			*DecoPack, bHasDeco ? TEXT("ok") : TEXT("MISSING"),
			*CnstPack, bHasCnst ? TEXT("ok") : TEXT("MISSING"));

		// LOUD, because the failure mode is a silent success.
		//
		// With neither pack the zone still imports its terrain, saves a level and
		// exits 0 — it is just empty of every building, prop and staircase.  That
		// is indistinguishable from a good run unless you read the object count,
		// and it is what a single renamed STB header produced (QQ-iROSE calls
		// col 1 "ZON" where the classic client says "Zone Path", so the zone row
		// never matched and both names came back blank).
		if (!bHasDeco && !bHasCnst)
		{
			UE_LOG(LogRoseImport, Error,
				TEXT("%s: NO object packs resolved — the level will contain terrain "
				     "only, with no buildings, props or collision. Check the "
				     "LIST_ZONE.STB column aliases in RoseStbSchema (DecoTable / "
				     "CnstTable / ZonPath) against this client's headers."),
				*Options.Zone);
		}

		// Places one ZSC object: walk its parts, compose each part's world
		// matrix (respecting parenting) and spawn a static mesh actor.
		auto PlaceZscObject =
			[&](const FRoseZSC& Zsc, const FRoseIfoObject& Placement, const TCHAR* GroupName)
		{
			if (!Zsc.Objects.IsValidIndex(Placement.ObjectId))
				return;
			const TArray<FRoseZscPart>& Parts = Zsc.Objects[Placement.ObjectId].Parts;
			if (Parts.Num() == 0)
				return;

			const FMatrix PlacementMatrix =
				RoseCompose(Placement.Position, Placement.Rotation, Placement.Scale);

			TArray<FMatrix> WorldMatrices;
			WorldMatrices.SetNum(Parts.Num());

			for (int32 p = 0; p < Parts.Num(); ++p)
			{
				const FRoseZscPart& Part = Parts[p];
				const FMatrix Local = RoseCompose(Part.Position, Part.Rotation, Part.Scale);

				// UE matrices are row-vectors, so "child then parent" reverses
				// relative to mapforge's column-vector `parent @ local`.
				const bool bParented =
					Part.ParentIdx >= 0 && Part.ParentIdx < p;
				WorldMatrices[p] = bParented
					? Local * WorldMatrices[Part.ParentIdx]
					: Local * PlacementMatrix;

				if (!Zsc.MeshFiles.IsValidIndex(Part.MeshId))
					continue;

				// Build collision geometry only for parts that actually collide.
				//
				// HEIGHTONLY (0x20) is NOT excluded.  In ROSE that bit means the
				// part contributes to the walkable HEIGHT field rather than acting
				// as a blocking wall — you still stand on it.  Skipping it here
				// did not make those parts non-blocking, it removed their floor,
				// so the player fell/walked straight through: in JPT01 the seven
				// height-only parts are portstair, portstair03, castlegate02b/06b/
				// 08b/08g and waterway04 — the staircases and gate thresholds.
				//
				// The shape mask still gates this.  That is the check that matters
				// for the bushes-and-grass snagging described in
				// RoseObjectFormats.h: a part with NO shape bits never collides,
				// whatever other flags it carries.
				const bool bPartCollides = Part.HasCollision();
				UStaticMesh* Mesh = Cache.GetMesh(Zsc.MeshFiles[Part.MeshId], bPartCollides);
				if (!Mesh)
					continue;

				UMaterialInstanceConstant* PartMat = nullptr;
				if (Zsc.Materials.IsValidIndex(Part.MaterialId))
					PartMat = Cache.GetMaterial(Zsc.Materials[Part.MaterialId]);

				FActorSpawnParameters SpawnParams;
				SpawnParams.OverrideLevel = World->PersistentLevel;
				SpawnParams.ObjectFlags = RF_Transactional;

				AStaticMeshActor* Actor = World->SpawnActor<AStaticMeshActor>(
					AStaticMeshActor::StaticClass(),
					RoseToUnrealTransform(WorldMatrices[p]), SpawnParams);
				if (!Actor)
					continue;

				UStaticMeshComponent* Comp = Actor->GetStaticMeshComponent();

				// SWITCH_CNST_ANI parts are driven at runtime by
				// RoseMapAnimManager, so they cannot be Static.
				const bool bAnimated = !Part.CnstAnimFile.IsEmpty();
				Actor->SetMobility(bAnimated ? EComponentMobility::Movable : EComponentMobility::Static);
				Comp->SetStaticMesh(Mesh);
				if (PartMat)
					Comp->SetMaterial(0, PartMat);

				// Collision comes from the SHAPE BITS of zz_collision_level, not
				// the whole word — the engine masks with 0x7 everywhere
				// (ZZ_IS_POLYGON_LEVEL et al).  Testing the raw value gave solid
				// collision to every shape-NONE part carrying a flag (0x20
				// height-only, 0x50 non-pickable + no-camera), which is what
				// makes the player and camera snag on bushes and grass.
				//
				// HEIGHTONLY is treated as non-blocking: it contributes ground
				// height in ROSE, and UE gets its ground from the terrain mesh,
				// so leaving it solid would block movement ROSE never blocked.
				if (!Part.HasCollision() || Part.IsHeightOnly())
				{
					Comp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				}
				else
				{
					Comp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

					// Per-channel opt-outs the ZSC asks for explicitly.
					if (Part.IsNotPickable())
						Comp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
					if (Part.IsNoCameraCollision())
						Comp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
				}

				if (bAnimated)
				{
					Actor->Tags.Add(FName(TEXT("RoseAnim")));
					Actor->Tags.Add(FName(*Part.CnstAnimFile));
					++Result.AnimatedParts;
				}

				Actor->SetActorLabel(FString::Printf(TEXT("%s_%d_%d"),
					GroupName, Placement.ObjectId, p));
				++Result.ObjectActors;
			}

			// ── lamps ──────────────────────────────────────────────────────
			//
			// ROSE flags its own light sockets, so nothing here matches on
			// names: an effect of type 1 (DayNight) or 2 (LightContainer) IS a
			// light.  In JPT01 the type-1 sockets resolve to `streetlight01l.eft`
			// on deco objects 172 and 173 — the street lamps.
			//
			// The socket transform is the object-local position of the bulb, so
			// it goes through the same parent chain as a part: a lamp head sits
			// at the top of its post, not at the post's origin.
			if (!Options.bImportLights)
				return;

			for (const FRoseZscEffect& Fx : Zsc.Objects[Placement.ObjectId].Effects)
			{
				if (!Fx.IsLight())
					continue;

				const FMatrix Local = RoseCompose(Fx.Position, Fx.Rotation, Fx.Scale);
				const bool bParented =
					Fx.ParentIdx >= 0 && WorldMatrices.IsValidIndex(Fx.ParentIdx);
				const FMatrix World4 = bParented
					? Local * WorldMatrices[Fx.ParentIdx]
					: Local * PlacementMatrix;

				FActorSpawnParameters LightParams;
				LightParams.OverrideLevel = World->PersistentLevel;
				LightParams.ObjectFlags = RF_Transactional;

				APointLight* Lamp = World->SpawnActor<APointLight>(
					APointLight::StaticClass(),
					RoseToUnrealTransform(World4), LightParams);
				if (!Lamp)
					continue;

				// Stationary, not Static: a night-only lamp has to be switched
				// on and off at runtime, which a Static light cannot do.
				Lamp->SetMobility(EComponentMobility::Stationary);

				if (UPointLightComponent* L = Cast<UPointLightComponent>(Lamp->GetLightComponent()))
				{
					// Warm bulb, and a radius in CENTIMETRES — ROSE world units
					// are cm, so 1200 is a ~12 m pool of light around the post.
					L->SetIntensity(Options.LampIntensity);
					L->SetLightColor(FLinearColor(1.f, 0.78f, 0.52f));
					L->SetAttenuationRadius(Options.LampRadius);
					L->SetSourceRadius(8.f);
					// Lamps DO cast shadows.  A street light that does not throw
					// the post's own shadow reads as a floating glow.  Cost is
					// bounded by the small attenuation radius and by these being
					// Stationary, so they use the shadow cache rather than a full
					// dynamic pass every frame.
					L->SetCastShadows(true);
				}

				// Tagged so a day/night controller can find every lamp without
				// walking the whole level, and so a re-import can identify them.
				Lamp->Tags.Add(FName(TEXT("RoseLamp")));
				if (Fx.IsNightOnly())
					Lamp->Tags.Add(FName(TEXT("RoseNightOnly")));

				Lamp->SetActorLabel(FString::Printf(TEXT("Lamp_%s_%d"),
					GroupName, Placement.ObjectId));
				++Result.Lamps;
			}
		};

		// Collected across every chunk, then turned into water bodies once — a
		// zone's ocean is described per-IFO but is one continuous surface.
		TArray<FRoseOceanBlock> OceanPatches;

		for (const FChunk& Chunk : Chunks)
		{
			FRoseIFO Ifo;
			if (!Ifo.Load(ZoneDir / (Chunk.Stem + TEXT(".IFO"))))
				continue;

			if (Options.bImportObjects)
			{
				if (bHasDeco)
					if (const TArray<FRoseIfoObject>* Deco = Ifo.Get(ROSE_IFO_OBJECT))
						for (const FRoseIfoObject& O : *Deco)
							PlaceZscObject(DecoZsc, O, TEXT("Deco"));

				if (bHasCnst)
					if (const TArray<FRoseIfoObject>* Cnst = Ifo.Get(ROSE_IFO_BUILDING))
						for (const FRoseIfoObject& O : *Cnst)
							PlaceZscObject(CnstZsc, O, TEXT("Cnst"));
			}

			// Water comes from the IFO's OCEAN block, not its WATER block.
			// The WATER block is a per-cell has_water/height grid and is EMPTY in
			// every zone measured (0 wet cells in JPT01, JDT01 and EJT01); the
			// Ocean block is what actually carries the surfaces — flat,
			// axis-aligned rectangles at a single height.
			for (const FRoseOceanBlock& O : Ifo.Ocean)
				OceanPatches.Add(O);

			if (!Options.bImportEntities)
				continue;

			FActorSpawnParameters EntityParams;
			EntityParams.OverrideLevel = World->PersistentLevel;
			EntityParams.ObjectFlags = RF_Transactional;

			// NPCs — object_id is the LIST_NPC row; the CON filename is the
			// dialog link (matched case-insensitively against LIST_EVENT).
			if (const TArray<FRoseIfoObject>* Npcs = Ifo.Get(ROSE_IFO_NPC))
			{
				for (const FRoseIfoObject& O : *Npcs)
				{
					// NPCs are YAW-ONLY, derived the same way the working
					// pipeline does it (tools/export_npcs.py):
					//     ue_yaw = -degrees(2 * atan2(qz, qw))
					// The general matrix path gives the same angle, but going
					// through the formula keeps this identical to the reference
					// even if an IFO carries stray pitch/roll — NPCs stand on
					// the ground and only ever face a direction.
					const float RoseYaw = 2.f * FMath::Atan2(O.Rotation.Z, O.Rotation.W);
					const float UeYaw = -FMath::RadiansToDegrees(RoseYaw) + Options.NpcYawOffset;

					const FTransform T(
						FRotator(0.f, UeYaw, 0.f),
						FVector(O.Position.X, -O.Position.Y, O.Position.Z));

					if (ARoseNpc* Npc = World->SpawnActor<ARoseNpc>(
						ARoseNpc::StaticClass(), T, EntityParams))
					{
						Npc->NpcId = O.ObjectId;
						Npc->AiId = O.NpcAi;
						Npc->ConStem = FPaths::GetBaseFilename(O.NpcCon);
						Npc->EventId = O.EventId;
						Npc->SetActorLabel(FString::Printf(TEXT("Npc_%d"), O.ObjectId));
						++Result.NpcActors;
					}
				}
			}

			// Monster regen points — CRegenPOINT.  The file stores interval in
			// SECONDS and range in METRES; the spawner wants seconds and cm.
			if (const TArray<FRoseIfoObject>* Spawns = Ifo.Get(ROSE_IFO_MONSTER))
			{
				for (const FRoseIfoObject& O : *Spawns)
				{
					const FTransform T = RoseToUnrealTransform(
						RoseCompose(O.Position, FQuat4f::Identity, FVector3f(1, 1, 1)));
					if (ARoseMonsterSpawner* Spawner = World->SpawnActor<ARoseMonsterSpawner>(
						ARoseMonsterSpawner::StaticClass(), T, EntityParams))
					{
						for (const FRoseIfoSpawnEntry& E : O.SpawnBasic)
						{
							FRoseSpawnEntry Entry;
							Entry.NpcId = E.NpcId;
							Entry.Count = E.Count;
							Spawner->BasicSpawns.Add(Entry);
						}
						for (const FRoseIfoSpawnEntry& E : O.SpawnTactic)
						{
							FRoseSpawnEntry Entry;
							Entry.NpcId = E.NpcId;
							Entry.Count = E.Count;
							Spawner->TacticSpawns.Add(Entry);
						}
						Spawner->Interval = FMath::Max(1.f, (float)O.SpawnInterval);
						Spawner->LimitCount = O.SpawnLimit;
						Spawner->Range = FMath::Max(100.f, O.SpawnRange * 100.f);
						Spawner->TacticPoints = O.SpawnTacticPoints;
						Spawner->NpcId = 0;   // list-driven, not the simple path
						Spawner->SetActorLabel(FString::Printf(TEXT("Regen_%s"), *O.Name));
						++Result.SpawnerActors;
					}
				}
			}

			// Warp gates.  The destination needs WARP.STB + the target zone's
			// ZON event block, which this pass does not read yet — the trigger
			// is placed and left unwired rather than pointed somewhere wrong.
			if (const TArray<FRoseIfoObject>* Warps = Ifo.Get(ROSE_IFO_WARP))
			{
				for (const FRoseIfoObject& O : *Warps)
				{
					const FTransform T = RoseToUnrealTransform(
						RoseCompose(O.Position, O.Rotation, FVector3f(1, 1, 1)));
					if (ARoseWarpPortal* Portal = World->SpawnActor<ARoseWarpPortal>(
						ARoseWarpPortal::StaticClass(), T, EntityParams))
					{
						Portal->SetActorLabel(FString::Printf(TEXT("Warp_%d"), O.WarpId));
						Portal->Tags.Add(FName(*FString::Printf(TEXT("WarpId=%d"), O.WarpId)));
						++Result.PortalActors;
					}
				}
			}
		}

		if (Options.bImportWater && OceanPatches.Num() > 0)
			RoseSpawnWater(World, OceanPatches, Options, Result);

		Result.UniqueMeshes = Cache.NumMeshes();
		Result.UniqueTextures = Cache.NumTextures();
		Result.UniqueMaterials = Cache.NumMaterials();
		Result.MissingAssets = Cache.NumMissing();

		// Save every asset the caches produced, once, at the end.
		if (const int32 SaveFailures = Cache.SaveCreated())
		{
			UE_LOG(LogRoseImport, Error,
				TEXT("%d asset package(s) failed to save"), SaveFailures);
		}
	}
	Result.SecondsObjects = FPlatformTime::Seconds() - TimeObjStart;

	if (Options.bAddLighting)
	{
		// Sun 4 / sky 3, matching the tuned rig in tools/ue5_fix_lighting.py.
		// The importer used to spawn 2 / 6 — a near-ambient wash that was fine
		// while the ground was unlit and ignored it, but with a lit ground and
		// real shadows the key light has to actually read as a key light.
		FActorSpawnParameters Params;
		Params.OverrideLevel = World->PersistentLevel;

		if (ADirectionalLight* Sun = World->SpawnActor<ADirectionalLight>(
			ADirectionalLight::StaticClass(),
			// Pitch -45, yaw 30 — the angle ue5_import_map_scene.py uses
			// (its Rotator is roll/pitch/yaw, ours is Pitch/Yaw/Roll).
			FTransform(FRotator(-45.f, 30.f, 0.f), FVector(0, 0, 100000.f)), Params))
		{
			Sun->SetMobility(EComponentMobility::Stationary);
			Sun->GetLightComponent()->SetIntensity(Options.SunIntensity);

			if (UDirectionalLightComponent* D =
				Cast<UDirectionalLightComponent>(Sun->GetLightComponent()))
			{
				// THIS LIGHT IS THE ATMOSPHERE'S SUN.
				//
				// Without it the SkyAtmosphere below has no sun bound to it and
				// renders as a default bright dome — and the real-time-capture
				// SkyLight then captures that dome and floods the level with
				// uniform bright ambient.  The result is a washed out,
				// low-contrast, shadowless-looking map: "terrain looks like a
				// bright sun".  It is not an exposure problem and not a terrain
				// material problem, which is why changing those never fixed it.
				//
				// tools/ue5_import_map_scene.py sets `atmosphere_sun_light` on
				// its sun; the native importer never did.
				D->SetAtmosphereSunLight(true);

				// Shadows, explicitly.  This is the light that has to throw
				// every building and tree onto the ground.
				D->SetCastShadows(true);
				D->SetDynamicShadowDistanceStationaryLight(50000.f);
			}
			Sun->SetActorLabel(TEXT("RoseSun"));
		}

		if (ASkyLight* Sky = World->SpawnActor<ASkyLight>(
			ASkyLight::StaticClass(), FTransform(FVector(0, 0, 100000.f)), Params))
		{
			// ASkyLight has no SetMobility of its own — go through the component.
			Sky->GetLightComponent()->SetMobility(EComponentMobility::Stationary);
			Sky->GetLightComponent()->SetIntensity(Options.SkyIntensity);

			// REAL-TIME CAPTURE.  Not optional, and the reason the ground looked
			// like a white sheet.
			//
			// A SkyLight with SourceType = CapturedScene captures ONCE unless
			// this is set.  The import runs headless in a commandlet, where
			// there is no meaningful scene to capture, so it bakes a garbage
			// (very bright) cubemap and floods the whole level with ambient —
			// washed out, low contrast, and immune to the exposure clamps
			// because the LIGHT is wrong, not the exposure.
			//
			// The old Python rig sets it in both places it touches a SkyLight
			// (ue5_fix_lighting.py, ue5_import_map_scene.py); the native
			// importer never did, which is the whole difference between its
			// levels and theirs.
			Sky->GetLightComponent()->bRealTimeCapture = true;
			// Without this the underside of everything crushes to black.
			Sky->GetLightComponent()->bLowerHemisphereIsBlack = false;
			Sky->SetActorLabel(TEXT("RoseSky"));
		}

		// UNBOUND PostProcessVolume with CLAMPED auto-exposure.
		//
		// This is not a nicety, it is the difference between a readable ground
		// and a white one.  The project runs histogram auto-exposure (it has to:
		// a physical SkyAtmosphere sun with fixed exposure renders blown out in
		// PIE).  Unclamped, the adaptation chases a bright albedo ground and
		// pushes it to white — which is exactly what "the terrain is blown out"
		// looked like, and why relighting the terrain master kept seeming to be
		// the culprit when it never was.
		//
		// The old Python rig created this volume (tools/ue5_fix_lighting.py) and
		// the native importer never did, so a natively imported level was
		// missing the one actor that made the lighting sane.  Values are that
		// script's defaults.
		if (APostProcessVolume* PPV = World->SpawnActor<APostProcessVolume>(
			APostProcessVolume::StaticClass(), FTransform::Identity, Params))
		{
			PPV->bUnbound = true;

			FPostProcessSettings& S = PPV->Settings;
			S.bOverride_AutoExposureBias = true;
			S.AutoExposureBias = Options.ExposureBias;

			// A gentle range around daylight: interiors and night cannot
			// over-brighten, noon cannot crush.
			S.bOverride_AutoExposureMinBrightness = true;
			S.bOverride_AutoExposureMaxBrightness = true;
			S.AutoExposureMinBrightness = -2.f;
			S.AutoExposureMaxBrightness = 6.f;

			PPV->SetActorLabel(TEXT("RoseExposure"));
		}

		// The maps use a physical sky; without it the SkyLight has nothing to
		// capture and the horizon is flat black.
		if (ASkyAtmosphere* Atmos = World->SpawnActor<ASkyAtmosphere>(
			ASkyAtmosphere::StaticClass(), FTransform::Identity, Params))
		{
			Atmos->SetActorLabel(TEXT("RoseSkyAtmosphere"));
		}
	}

	// ── PlayerStart ────────────────────────────────────────────────────────
	// The zone's own spawn point: ZON LUMP_EVENT_OBJECT holds named world
	// positions and "start" is the one the client uses.  Falling back to the
	// zone centre would drop the player somewhere arbitrary, so prefer the data.
	{
		FVector3f StartRose(
			Zon.CenterX * kTileWorldSize, Zon.CenterY * kTileWorldSize, 0.f);
		bool bFoundStart = false;
		for (const TPair<FString, FVector3f>& Event : Zon.EventObjects)
		{
			if (Event.Key.Contains(TEXT("start"), ESearchCase::IgnoreCase))
			{
				StartRose = Event.Value;
				bFoundStart = true;
				break;
			}
		}

		FActorSpawnParameters Params;
		Params.OverrideLevel = World->PersistentLevel;
		const FVector StartUE(StartRose.X, -StartRose.Y, StartRose.Z + 200.f);
		if (APlayerStart* Start = World->SpawnActor<APlayerStart>(
			APlayerStart::StaticClass(), FTransform(StartUE), Params))
		{
			Start->SetActorLabel(TEXT("PlayerStart"));
			UE_LOG(LogRoseImport, Log, TEXT("PlayerStart at (%.0f, %.0f, %.0f) [%s]"),
				StartUE.X, StartUE.Y, StartUE.Z,
				bFoundStart ? TEXT("ZON 'start' event") : TEXT("zone centre fallback"));
		}
	}

	const bool bSaved = SavePackageToDisk(LevelPkg, World, FPackageName::GetMapPackageExtension());
	Result.SecondsSave = FPlatformTime::Seconds() - TimeSaveStart;
	Result.LevelPackage = LevelPkgName;

	if (!bSaved)
	{
		UE_LOG(LogRoseImport, Error, TEXT("failed to save level %s"), *LevelPkgName);
		return false;
	}

	// ── 10. retire the superseded glTF/Interchange assets ──────────────────
	// Deliberately LAST and only on success: the replacement level and meshes
	// are already written and saved by this point, so a failure earlier leaves
	// the old assets untouched.  Content/ is not in git — there is no undo.
	if (Options.bDeleteLegacyAssets)
	{
		// Delete the package FILES, not via ObjectTools::ForceDeleteObjects.
		//
		// ForceDelete does per-asset reference fixup, which took 16 MINUTES for
		// EJ01's 2,422 assets — 10+ hours across the game.  None of that work
		// is wanted here: the only thing that referenced these was the level we
		// just overwrote.  A recursive directory delete does the same job in
		// milliseconds; the asset registry catches up on the next scan.
		const FString LegacyDir = FPaths::Combine(
			FPaths::ProjectContentDir(), TEXT("Maps"), Options.Zone, TEXT("Scene"));

		if (IFileManager::Get().DirectoryExists(*LegacyDir))
		{
			TArray<FString> Files;
			IFileManager::Get().FindFilesRecursive(Files, *LegacyDir, TEXT("*.uasset"), true, false);
			const int32 Count = Files.Num();

			if (IFileManager::Get().DeleteDirectory(*LegacyDir, false, true))
			{
				Result.LegacyAssetsDeleted = Count;
				UE_LOG(LogRoseImport, Log, TEXT("retired %d legacy assets (%s)"),
					Count, *LegacyDir);
			}
			else
			{
				UE_LOG(LogRoseImport, Warning,
					TEXT("could not delete %s — files may be open"), *LegacyDir);
			}
		}
	}

	Result.SecondsTotal = FPlatformTime::Seconds() - TimeStart;
	Result.bSuccess = true;
	return true;
}

