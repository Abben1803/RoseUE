// Little-endian stream reader over a whole file held in memory.
//
// Mirrors tools/rose_parser/reader.py.  ROSE files are small (the largest
// terrain chunk is a few hundred KB), so reading them whole and walking a
// cursor is both simpler and faster than buffered IO — and lump-based formats
// (ZON, IFO) need random seeks anyway.
#pragma once

#include "CoreMinimal.h"
#include "Misc/FileHelper.h"

class FRoseBinaryReader
{
public:
	bool LoadFile(const FString& Path)
	{
		Data.Reset();
		Pos = 0;
		return FFileHelper::LoadFileToArray(Data, *Path);
	}

	bool IsValid() const { return Data.Num() > 0; }
	int64 Num() const { return Data.Num(); }
	int64 Tell() const { return Pos; }
	void Seek(int64 To) { Pos = FMath::Clamp<int64>(To, 0, Data.Num()); }
	void Skip(int64 Count) { Seek(Pos + Count); }
	bool AtEnd() const { return Pos >= Data.Num(); }

	// Every read is bounds-checked and returns zero past the end rather than
	// crashing: ROSE ships a handful of truncated files, and a zone import must
	// report a bad chunk, not take the editor down with it.
	template <typename T>
	T Read()
	{
		T Value{};
		if (Pos + (int64)sizeof(T) <= Data.Num())
		{
			FMemory::Memcpy(&Value, Data.GetData() + Pos, sizeof(T));
			Pos += sizeof(T);
		}
		else
		{
			Pos = Data.Num();
			bOverran = true;
		}
		return Value;
	}

	int32  I32() { return Read<int32>(); }
	uint32 U32() { return Read<uint32>(); }
	int16  I16() { return Read<int16>(); }
	uint16 U16() { return Read<uint16>(); }
	uint8  U8()  { return Read<uint8>(); }
	float  F32() { return Read<float>(); }

	FVector3f Vec3()
	{
		const float X = F32(), Y = F32(), Z = F32();
		return FVector3f(X, Y, Z);
	}

	// ROSE strings are a length byte followed by raw bytes (no terminator).
	FString ByteStr()
	{
		const int32 Len = U8();
		return FixedStr(Len);
	}

	FString FixedStr(int32 Len)
	{
		if (Len <= 0 || Pos + Len > Data.Num())
		{
			Skip(Len);
			return FString();
		}
		FString Out;
		Out.Reserve(Len);
		for (int32 i = 0; i < Len; ++i)
		{
			const uint8 C = Data[Pos + i];
			if (C == 0) break;          // some tables pad with NULs
			Out.AppendChar((TCHAR)C);
		}
		Pos += Len;
		return Out;
	}

	bool HasOverrun() const { return bOverran; }

private:
	TArray<uint8> Data;
	int64 Pos = 0;
	bool bOverran = false;
};
