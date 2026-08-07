#include "RoseStringTable.h"

#include "RoseBinaryReader.h"
#include "RoseEditor.h"

namespace
{
	// Varbyte length prefix (rose-lib io/reader.rs::read_string_varbyte):
	// high bit clear -> the byte IS the length; set -> a second byte supplies
	// the upper 7 bits.  Reading this as a plain u8 truncates every string
	// longer than 127 characters, which is most quest text.
	FString ReadStringVarByte(FRoseBinaryReader& R)
	{
		const uint8 First = R.U8();
		if ((First & 128) == 0)
			return R.FixedStr(First);

		const uint8 Second = R.U8();
		const int32 Len = ((int32)Second << 7) | ((int32)First - 128);
		return R.FixedStr(Len);
	}
}

bool FRoseSTL::Load(const FString& Path, int32 LanguageIndex)
{
	FRoseBinaryReader R;
	if (!R.LoadFile(Path))
	{
		UE_LOG(LogRoseImport, Warning, TEXT("STL missing: %s"), *Path);
		return false;
	}

	Format = R.ByteStr();   // length-byte prefixed, same as STL's read_string_u8
	const bool bItem  = Format.StartsWith(TEXT("ITST"));
	const bool bQuest = Format.StartsWith(TEXT("QEST"));
	const bool bNormal = Format.StartsWith(TEXT("NRST"));
	if (!bItem && !bQuest && !bNormal)
	{
		UE_LOG(LogRoseImport, Warning, TEXT("STL %s: unknown format '%s'"), *Path, *Format);
		return false;
	}

	const int32 RowCount = (int32)R.U32();
	if (RowCount <= 0 || RowCount > 200000)
		return false;

	Keys.Reset(RowCount);
	KeyToRow.Reset();
	for (int32 i = 0; i < RowCount; ++i)
	{
		const FString Key = R.ByteStr();
		R.U32();                       // id — the STBs join by KEY, not by this
		KeyToRow.Add(Key, i);
		Keys.Add(Key);
	}

	const int32 LangCount = (int32)R.U32();
	if (LangCount <= 0)
		return false;

	// Language offsets are a flat table; read them all, then pick.  Falling back
	// to language 0 matters for partial extracts — a Korean name beats none.
	TArray<uint32> LangOffsets;
	LangOffsets.SetNum(LangCount);
	for (int32 i = 0; i < LangCount; ++i)
		LangOffsets[i] = R.U32();

	const int32 Lang = LangOffsets.IsValidIndex(LanguageIndex) ? LanguageIndex : 0;
	R.Seek(LangOffsets[Lang]);

	TArray<uint32> RowOffsets;
	RowOffsets.SetNum(RowCount);
	for (int32 i = 0; i < RowCount; ++i)
		RowOffsets[i] = R.U32();

	Rows.Reset(RowCount);
	Rows.SetNum(RowCount);
	for (int32 i = 0; i < RowCount; ++i)
	{
		R.Seek(RowOffsets[i]);
		FRoseStlRow& Row = Rows[i];
		Row.Text = ReadStringVarByte(R);
		if (bItem || bQuest)
			Row.Description = ReadStringVarByte(R);
		if (bQuest)
		{
			Row.StartMessage = ReadStringVarByte(R);
			Row.EndMessage = ReadStringVarByte(R);
		}
	}

	return true;
}
