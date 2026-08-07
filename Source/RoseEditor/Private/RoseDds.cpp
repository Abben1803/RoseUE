#include "RoseDds.h"

#include "RoseBinaryReader.h"
#include "RoseEditor.h"

namespace
{
	constexpr uint32 DDS_MAGIC = 0x20534444;   // "DDS "
	constexpr uint32 FOURCC_DXT1 = 0x31545844;
	constexpr uint32 FOURCC_DXT3 = 0x33545844;
	constexpr uint32 FOURCC_DXT5 = 0x35545844;

	constexpr uint32 DDPF_ALPHAPIXELS = 0x1;
	constexpr uint32 DDPF_FOURCC = 0x4;
	constexpr uint32 DDPF_RGB = 0x40;

	// RGB565 -> 8:8:8, the expansion every BCn colour block starts from.
	FORCEINLINE void Unpack565(uint16 Packed, uint8& R, uint8& G, uint8& B)
	{
		const uint32 R5 = (Packed >> 11) & 0x1F;
		const uint32 G6 = (Packed >> 5) & 0x3F;
		const uint32 B5 = Packed & 0x1F;
		// Replicate the high bits into the low ones so 31 maps to 255, not 248.
		R = (uint8)((R5 << 3) | (R5 >> 2));
		G = (uint8)((G6 << 2) | (G6 >> 4));
		B = (uint8)((B5 << 3) | (B5 >> 2));
	}

	// One 4x4 BC1 colour block.  bPunchThrough is BC1's 1-bit-alpha mode, which
	// only applies when the block is NOT carried by a separate alpha block
	// (BC2/BC3 always use the 4-colour path).
	void DecodeColourBlock(const uint8* Block, FColor OutTexels[16], bool bPunchThrough)
	{
		const uint16 C0 = (uint16)(Block[0] | (Block[1] << 8));
		const uint16 C1 = (uint16)(Block[2] | (Block[3] << 8));

		uint8 R[4], G[4], B[4];
		uint8 A[4] = { 255, 255, 255, 255 };

		Unpack565(C0, R[0], G[0], B[0]);
		Unpack565(C1, R[1], G[1], B[1]);

		if (C0 > C1 || !bPunchThrough)
		{
			R[2] = (uint8)((2 * R[0] + R[1]) / 3);
			G[2] = (uint8)((2 * G[0] + G[1]) / 3);
			B[2] = (uint8)((2 * B[0] + B[1]) / 3);
			R[3] = (uint8)((R[0] + 2 * R[1]) / 3);
			G[3] = (uint8)((G[0] + 2 * G[1]) / 3);
			B[3] = (uint8)((B[0] + 2 * B[1]) / 3);
		}
		else
		{
			R[2] = (uint8)((R[0] + R[1]) / 2);
			G[2] = (uint8)((G[0] + G[1]) / 2);
			B[2] = (uint8)((B[0] + B[1]) / 2);
			R[3] = G[3] = B[3] = 0;
			A[3] = 0;                      // index 3 is transparent black
		}

		const uint32 Bits = Block[4] | (Block[5] << 8) | (Block[6] << 16) | ((uint32)Block[7] << 24);
		for (int32 i = 0; i < 16; ++i)
		{
			const uint32 Idx = (Bits >> (i * 2)) & 0x3;
			OutTexels[i] = FColor(R[Idx], G[Idx], B[Idx], A[Idx]);
		}
	}

	// BC2: 16 explicit 4-bit alphas.
	void DecodeAlphaBC2(const uint8* Block, FColor OutTexels[16])
	{
		for (int32 i = 0; i < 16; ++i)
		{
			const uint8 Nibble = (i & 1) ? (Block[i >> 1] >> 4) : (Block[i >> 1] & 0x0F);
			OutTexels[i].A = (uint8)((Nibble << 4) | Nibble);
		}
	}

	// BC3: two endpoints + 3-bit indices into an interpolated ramp.
	void DecodeAlphaBC3(const uint8* Block, FColor OutTexels[16])
	{
		uint8 A[8];
		A[0] = Block[0];
		A[1] = Block[1];
		if (A[0] > A[1])
		{
			for (int32 i = 1; i < 7; ++i)
				A[i + 1] = (uint8)(((7 - i) * A[0] + i * A[1]) / 7);
		}
		else
		{
			for (int32 i = 1; i < 5; ++i)
				A[i + 1] = (uint8)(((5 - i) * A[0] + i * A[1]) / 5);
			A[6] = 0;
			A[7] = 255;
		}

		uint64 Bits = 0;
		for (int32 i = 0; i < 6; ++i)
			Bits |= (uint64)Block[2 + i] << (8 * i);

		for (int32 i = 0; i < 16; ++i)
		{
			const uint32 Idx = (uint32)((Bits >> (i * 3)) & 0x7);
			OutTexels[i].A = A[Idx];
		}
	}

	void BlitBlock(FRoseImage& Img, const FColor Texels[16], int32 BlockX, int32 BlockY)
	{
		for (int32 Y = 0; Y < 4; ++Y)
		{
			const int32 PY = BlockY * 4 + Y;
			if (PY >= Img.Height) break;
			for (int32 X = 0; X < 4; ++X)
			{
				const int32 PX = BlockX * 4 + X;
				if (PX >= Img.Width) continue;
				Img.SetPixel(PX, PY, Texels[Y * 4 + X]);
			}
		}
	}
}

bool RoseLoadDDS(const FString& Path, FRoseImage& Out)
{
	FRoseBinaryReader R;
	if (!R.LoadFile(Path))
	{
		UE_LOG(LogRoseImport, Warning, TEXT("DDS missing: %s"), *Path);
		return false;
	}

	if (R.U32() != DDS_MAGIC)
	{
		UE_LOG(LogRoseImport, Warning, TEXT("DDS %s: bad magic"), *Path);
		return false;
	}

	R.U32();                        // header size (124)
	R.U32();                        // flags
	const int32 Height = (int32)R.U32();
	const int32 Width = (int32)R.U32();
	R.U32();                        // pitch / linear size
	R.U32();                        // depth
	R.U32();                        // mip count
	R.Skip(11 * 4);                 // reserved

	// DDS_PIXELFORMAT
	R.U32();                        // pf size
	const uint32 PfFlags = R.U32();
	const uint32 FourCC = R.U32();
	const uint32 RgbBitCount = R.U32();
	const uint32 RMask = R.U32();
	const uint32 GMask = R.U32();
	const uint32 BMask = R.U32();
	const uint32 AMask = R.U32();

	R.Skip(5 * 4);                  // caps 1-4 + reserved

	if (Width <= 0 || Height <= 0 || Width > 16384 || Height > 16384)
	{
		UE_LOG(LogRoseImport, Warning, TEXT("DDS %s: bad size %dx%d"), *Path, Width, Height);
		return false;
	}

	Out.Width = Width;
	Out.Height = Height;
	Out.Pixels.SetNumUninitialized(Width * Height * 4);
	FMemory::Memset(Out.Pixels.GetData(), 0xFF, Out.Pixels.Num());

	const int32 BlocksX = FMath::DivideAndRoundUp(Width, 4);
	const int32 BlocksY = FMath::DivideAndRoundUp(Height, 4);

	if (PfFlags & DDPF_FOURCC)
	{
		const int32 BlockBytes = (FourCC == FOURCC_DXT1) ? 8 : 16;
		const int64 Needed = (int64)BlocksX * BlocksY * BlockBytes;
		if (R.Tell() + Needed > R.Num())
		{
			UE_LOG(LogRoseImport, Warning, TEXT("DDS %s: truncated (need %lld bytes)"), *Path, Needed);
			return false;
		}

		for (int32 By = 0; By < BlocksY; ++By)
		{
			for (int32 Bx = 0; Bx < BlocksX; ++Bx)
			{
				uint8 Block[16];
				for (int32 i = 0; i < BlockBytes; ++i)
					Block[i] = R.U8();

				FColor Texels[16];
				switch (FourCC)
				{
				case FOURCC_DXT1:
					DecodeColourBlock(Block, Texels, /*bPunchThrough*/ true);
					break;
				case FOURCC_DXT3:
					DecodeColourBlock(Block + 8, Texels, false);
					DecodeAlphaBC2(Block, Texels);
					break;
				case FOURCC_DXT5:
					DecodeColourBlock(Block + 8, Texels, false);
					DecodeAlphaBC3(Block, Texels);
					break;
				default:
					UE_LOG(LogRoseImport, Warning,
						TEXT("DDS %s: unsupported FourCC 0x%08X"), *Path, FourCC);
					return false;
				}
				BlitBlock(Out, Texels, Bx, By);
			}
		}
		return true;
	}

	if (PfFlags & DDPF_RGB)
	{
		const int32 BytesPerPixel = (int32)(RgbBitCount / 8);
		// 16-bit formats (A1R5G5B5, A4R4G4B4, R5G6B5) are used by a handful of
		// weapon and sub-weapon textures.
		if (BytesPerPixel != 2 && BytesPerPixel != 3 && BytesPerPixel != 4)
		{
			UE_LOG(LogRoseImport, Warning,
				TEXT("DDS %s: unsupported bit count %u"), *Path, RgbBitCount);
			return false;
		}

		// Channel layout comes from the MASKS, never from an assumed order —
		// ROSE ships BGRA, RGBA and several 16-bit packings.
		struct FChannel
		{
			uint32 Mask = 0;
			int32 Shift = 0;
			int32 Bits = 0;

			void Init(uint32 InMask)
			{
				Mask = InMask;
				if (!Mask) { Shift = 0; Bits = 0; return; }
				Shift = (int32)FMath::CountTrailingZeros(Mask);
				Bits = FMath::CountBits(Mask);
			}

			// Expand an N-bit channel to 8 bits by REPLICATING the high bits,
			// so a full 5-bit value (31) maps to 255 and not 248.
			uint8 Extract(uint32 Packed) const
			{
				if (!Bits) return 255;
				uint32 V = (Packed & Mask) >> Shift;
				if (Bits >= 8)
					return (uint8)(V >> (Bits - 8));
				V <<= (8 - Bits);
				return (uint8)(V | (V >> Bits));
			}
		};

		FChannel Rc, Gc, Bc, Ac;
		Rc.Init(RMask); Gc.Init(GMask); Bc.Init(BMask); Ac.Init(AMask);
		const bool bHasAlpha = (PfFlags & DDPF_ALPHAPIXELS) && AMask != 0;

		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				uint32 Packed = 0;
				for (int32 i = 0; i < BytesPerPixel; ++i)
					Packed |= (uint32)R.U8() << (8 * i);

				Out.SetPixel(X, Y, FColor(
					Rc.Extract(Packed), Gc.Extract(Packed), Bc.Extract(Packed),
					bHasAlpha ? Ac.Extract(Packed) : 255));
			}
		}
		return true;
	}

	UE_LOG(LogRoseImport, Warning, TEXT("DDS %s: unsupported pixel format flags 0x%08X"), *Path, PfFlags);
	return false;
}
