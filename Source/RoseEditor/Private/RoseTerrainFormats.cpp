#include "RoseTerrainFormats.h"

#include "RoseBinaryReader.h"
#include "RoseEditor.h"

bool FRoseHIM::Load(const FString& Path)
{
	FRoseBinaryReader R;
	if (!R.LoadFile(Path))
	{
		UE_LOG(LogRoseImport, Warning, TEXT("HIM missing: %s"), *Path);
		return false;
	}

	Width = R.I32();
	Height = R.I32();
	PatchGridCount = R.I32();
	PatchSize = R.F32();

	if (Width <= 0 || Height <= 0 || Width > 4096 || Height > 4096)
	{
		UE_LOG(LogRoseImport, Warning, TEXT("HIM %s: bad dimensions %dx%d"), *Path, Width, Height);
		return false;
	}

	// The file stores rows south-last; flip on load so row 0 is the south edge
	// and increasing row means increasing world Y (matches mapforge).
	Heights.SetNumZeroed(Width * Height);
	for (int32 Row = Height - 1; Row >= 0; --Row)
	{
		for (int32 Col = 0; Col < Width; ++Col)
		{
			Heights[Row * Width + Col] = R.F32();
		}
	}

	return !R.HasOverrun();
}

bool FRoseTIL::Load(const FString& Path)
{
	FRoseBinaryReader R;
	if (!R.LoadFile(Path))
	{
		UE_LOG(LogRoseImport, Warning, TEXT("TIL missing: %s"), *Path);
		return false;
	}

	Width = R.I32();
	Height = R.I32();

	if (Width <= 0 || Height <= 0 || Width > 256 || Height > 256)
	{
		UE_LOG(LogRoseImport, Warning, TEXT("TIL %s: bad dimensions %dx%d"), *Path, Width, Height);
		return false;
	}

	TileNo.SetNumZeroed(Width * Height);
	for (int32 Row = Height - 1; Row >= 0; --Row)
	{
		for (int32 Col = 0; Col < Width; ++Col)
		{
			R.U8();                 // brush_no
			R.U8();                 // tile_idx
			R.U8();                 // tile_set
			TileNo[Row * Width + Col] = R.I32();
		}
	}

	return !R.HasOverrun();
}

namespace
{
	constexpr int32 LUMP_ZONE_INFO = 0;
	constexpr int32 LUMP_EVENT_OBJECT = 1;
	constexpr int32 LUMP_ZONE_TILE = 2;
	constexpr int32 LUMP_BRUSHES = 3;
	constexpr int32 LUMP_ECONOMY = 4;
}

bool FRoseZON::Load(const FString& Path)
{
	FRoseBinaryReader R;
	if (!R.LoadFile(Path))
	{
		UE_LOG(LogRoseImport, Error, TEXT("ZON missing: %s"), *Path);
		return false;
	}

	const int32 LumpCount = R.I32();
	if (LumpCount <= 0 || LumpCount > 64)
	{
		UE_LOG(LogRoseImport, Error, TEXT("ZON %s: bad lump count %d"), *Path, LumpCount);
		return false;
	}

	TArray<TPair<int32, int32>> Lumps;
	Lumps.Reserve(LumpCount);
	for (int32 i = 0; i < LumpCount; ++i)
	{
		const int32 Type = R.I32();
		const int32 Offset = R.I32();
		Lumps.Emplace(Type, Offset);
	}

	for (const TPair<int32, int32>& Lump : Lumps)
	{
		R.Seek(Lump.Value);
		switch (Lump.Key)
		{
		case LUMP_ZONE_INFO:
		{
			R.I32();                       // unused
			Width = R.I32();
			Height = R.I32();
			PatchGridCount = R.I32();
			GridSize = R.F32();
			CenterX = R.I32();
			CenterY = R.I32();
			break;
		}
		case LUMP_ZONE_TILE:
		{
			const int32 Count = R.I32();
			TileTextures.Reserve(Count);
			for (int32 i = 0; i < Count; ++i)
			{
				TileTextures.Add(R.ByteStr());
			}
			break;
		}
		case LUMP_BRUSHES:
		{
			const int32 Count = R.I32();
			Tiles.Reserve(Count);
			for (int32 i = 0; i < Count; ++i)
			{
				FRoseZonTile T;
				T.Layer1 = R.I32();
				T.Layer2 = R.I32();
				T.Offset1 = R.I32();
				T.Offset2 = R.I32();
				T.bBlend = R.I32();
				T.Rotation = R.I32();
				T.TileType = R.I32();
				Tiles.Add(T);
			}
			break;
		}
		case LUMP_ECONOMY:
		{
			ZoneName = R.ByteStr();
			break;
		}
		case LUMP_EVENT_OBJECT:
		{
			const int32 Count = R.I32();
			for (int32 i = 0; i < Count && i < 4096; ++i)
			{
				// File order is x, HEIGHT, y — not x, y, z.
				const float EvtX = R.F32();
				const float EvtZ = R.F32();
				const float EvtY = R.F32();
				const FString Name = R.ByteStr();
				EventObjects.Emplace(Name,
					FVector3f(EvtX + 520000.f, EvtY + 520000.f, EvtZ));
			}
			break;
		}
		default:
			break;
		}
	}

	if (CenterY == 0) CenterY = 32;   // a few zones leave it unset

	UE_LOG(LogRoseImport, Log,
		TEXT("ZON %s: %d tile textures, %d tiles, centre (%d,%d), grid %.1f"),
		*FPaths::GetCleanFilename(Path), TileTextures.Num(), Tiles.Num(),
		CenterX, CenterY, GridSize);

	return TileTextures.Num() > 0 && Tiles.Num() > 0;
}

void RoseRotateTileUV(float& U, float& V, int32 Rotation)
{
	switch (Rotation)
	{
	case 2: U = 1.f - U; break;                         // FlipHorizontal
	case 3: V = 1.f - V; break;                         // FlipVertical
	case 4: U = 1.f - U; V = 1.f - V; break;            // Flip (180)
	case 5: { const float T = U; U = V; V = 1.f - T; } break;   // Clockwise 90
	case 6: { const float T = U; U = 1.f - V; V = T; } break;   // CCW 90
	default: break;                                     // 0 Unknown / 1 None
	}
}
