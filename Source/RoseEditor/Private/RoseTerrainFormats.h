// HIM / TIL / ZON — the three files that define ROSE terrain.
//
// Transcribed from tools/rose_parser/formats/{him,til,zon}.py, which are in
// turn validated against src/client/io_terrain.cpp (CMAP::Load,
// CTERRAIN::LoadZONE) and rose-tools/rose-lib.  Per CLAUDE.md the local source
// is the authority — nothing here was taken from an online spec.
#pragma once

#include "CoreMinimal.h"

// ── HIM: heightmap for one terrain chunk ───────────────────────────────────
//   int32 width, height          (65 x 65 for the default 16 patches x 4 grids)
//   int32 patch_grid_count       (grids per patch axis, 4)
//   float patch_size
//   float[height][width]         heights, stored BOTTOM-TO-TOP (row 0 = south)
struct FRoseHIM
{
	int32 Width = 0;
	int32 Height = 0;
	int32 PatchGridCount = 0;
	float PatchSize = 0.f;
	// Row-major, row 0 = south edge (the file's rows are reversed on load).
	TArray<float> Heights;

	float At(int32 Row, int32 Col) const
	{
		if (Row < 0 || Row >= Height || Col < 0 || Col >= Width) return 0.f;
		return Heights[Row * Width + Col];
	}

	bool Load(const FString& Path);
};

// ── TIL: which ZON tile each of the 16x16 patches uses ─────────────────────
//   int32 width, height  (16 x 16)
//   per cell, bottom-to-top: uint8 brush, uint8 tile_idx, uint8 tile_set, int32 tile_no
struct FRoseTIL
{
	int32 Width = 0;
	int32 Height = 0;
	TArray<int32> TileNo;   // row-major, row 0 = south

	int32 At(int32 Row, int32 Col) const
	{
		if (Row < 0 || Row >= Height || Col < 0 || Col >= Width) return 0;
		return TileNo[Row * Width + Col];
	}

	bool Load(const FString& Path);
};

// ── ZON: zone definition (lump based) ──────────────────────────────────────
// Only the lumps terrain needs are read here; IFO objects come later.
struct FRoseZonTile
{
	// A tile is a PAIR of texture layers, blended by the top layer's alpha
	// (src/engine/shader/terrain.psh).  Layer1 = bottom, Layer2 = top.
	int32 Layer1 = 0;
	int32 Layer2 = 0;
	int32 Offset1 = 0;
	int32 Offset2 = 0;
	int32 bBlend = 0;
	int32 Rotation = 0;   // ZoneTileRotation, see RotateTileUV
	int32 TileType = 0;

	int32 BottomIndex() const { return Layer1 + Offset1; }
	int32 TopIndex() const { return Layer2 + Offset2; }
};

struct FRoseZON
{
	int32 Width = 0;
	int32 Height = 0;
	int32 PatchGridCount = 0;
	float GridSize = 0.f;
	int32 CenterX = 32;
	int32 CenterY = 32;

	TArray<FString> TileTextures;   // LUMP_ZONE_TILE — paths relative to 3DDATA
	TArray<FRoseZonTile> Tiles;     // LUMP_BRUSHES

	// LUMP_EVENT_OBJECT — named world positions.  "start" is the zone's spawn
	// point and warp targets are looked up here by name (zonefile.cpp
	// ReadEventObjINFO: file order x, height, y, then +520000 centring).
	TArray<TPair<FString, FVector3f>> EventObjects;

	FString ZoneName;

	bool Load(const FString& Path);
};

// ZON tile rotation applied to the SECOND (top) layer's UVs.
//   0 Unknown  1 None  2 FlipHorizontal  3 FlipVertical
//   4 Flip     5 Clockwise90             6 CounterClockwise90
// NOTE: treating this as "(rot-1) 90-degree rotations" is WRONG and was a real
// bug in mapforge — flips (2/3/4) are not rotations, and 2,562 of JPT01's
// 12,288 patches use one.
void RoseRotateTileUV(float& U, float& V, int32 Rotation);
