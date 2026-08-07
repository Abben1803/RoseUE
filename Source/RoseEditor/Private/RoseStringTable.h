// STL — ROSE string table (display names and descriptions).
//
// Transcribed from rose-tools/rose-lib/src/files/stl.rs.  This is the ONLY
// source of human-readable names: the STBs carry internal names and ids, and
// every table that shows text to the player joins to an STL by its string key.
//
// STLs are names ONLY.  They never carry icon indexes — an icon comes from the
// owning STB's icon column, used as-is.
//
// Layout:
//   identifier   str_u8      "NRST01" | "ITST01" | "QEST01"
//   row_count    u32
//   keys         { name str_u8, id u32 } * row_count
//   lang_count   u32
//   lang_offset  u32 * lang_count        -> each points at a row-offset table
//     row_offset u32 * row_count         -> each points at the row's strings
//       Normal : text
//       Item   : text, description
//       Quest  : text, description, start_message, end_message
//   (row strings are varbyte-length-prefixed)
#pragma once

#include "CoreMinimal.h"

struct FRoseStlRow
{
	FString Text;           // display name
	FString Description;    // Item/Quest formats only
	FString StartMessage;   // Quest only
	FString EndMessage;     // Quest only
};

struct FRoseSTL
{
	// Language index in the file.  ROSE order is Korean 0, English 1,
	// Japanese 2, ChineseTraditional 3, ChineseSimplified 4.
	static constexpr int32 LangEnglish = 1;

	FString Format;                       // NRST01 / ITST01 / QEST01
	TArray<FString> Keys;                 // per row, the STB's string key
	TArray<FRoseStlRow> Rows;             // parallel to Keys, for the language read

	// Key -> row index, built at load (keys are what the STBs store).
	TMap<FString, int32> KeyToRow;

	const FRoseStlRow* Find(const FString& Key) const
	{
		const int32* Row = KeyToRow.Find(Key);
		return Row && Rows.IsValidIndex(*Row) ? &Rows[*Row] : nullptr;
	}

	FString GetText(const FString& Key) const
	{
		const FRoseStlRow* Row = Find(Key);
		return Row ? Row->Text : FString();
	}

	FString GetDescription(const FString& Key) const
	{
		const FRoseStlRow* Row = Find(Key);
		return Row ? Row->Description : FString();
	}

	// Reads one language (default English).  Falls back to language 0 when the
	// requested one is absent, because a missing language is common in partial
	// extracts and an empty name is worse than a Korean one.
	bool Load(const FString& Path, int32 LanguageIndex = LangEnglish);
};
