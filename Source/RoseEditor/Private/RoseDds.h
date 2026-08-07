// DDS -> BGRA8, in process.
//
// Today the pipeline decodes DDS in Python, writes a lossless PNG, and imports
// that (tools/decode_dds_cache.py + ue5_import_dds_textures.py) because UE 5.8
// refuses BCn DDS through both import paths.  That is three file round trips
// per texture.
//
// A saved UTexture2D asset needs UNCOMPRESSED source data regardless — UE
// re-encodes per platform at cook time, and there is no compressed source
// format — so a decode is unavoidable.  Doing it here just removes the PNG and
// the importer: read the file, decode the blocks, hand the bytes to
// FTextureSource::Init.
#pragma once

#include "CoreMinimal.h"

struct FRoseImage
{
	int32 Width = 0;
	int32 Height = 0;
	// BGRA8, top-left origin, row-major.  Matches ETextureSourceFormat::TSF_BGRA8.
	TArray<uint8> Pixels;

	bool IsValid() const { return Width > 0 && Height > 0 && Pixels.Num() == Width * Height * 4; }

	FColor GetPixel(int32 X, int32 Y) const
	{
		const int32 I = (Y * Width + X) * 4;
		return FColor(Pixels[I + 2], Pixels[I + 1], Pixels[I + 0], Pixels[I + 3]);
	}

	void SetPixel(int32 X, int32 Y, const FColor& C)
	{
		const int32 I = (Y * Width + X) * 4;
		Pixels[I + 0] = C.B;
		Pixels[I + 1] = C.G;
		Pixels[I + 2] = C.R;
		Pixels[I + 3] = C.A;
	}
};

// Decodes DXT1 (BC1), DXT3 (BC2), DXT5 (BC3) and uncompressed 24/32-bit DDS.
// Only mip 0 is read — the atlas is rebuilt anyway and UE generates its own
// mip chain.  Returns false (and logs) on anything else.
bool RoseLoadDDS(const FString& Path, FRoseImage& Out);
