// Client-agnostic STB column binding.
//
// THE PROBLEM
// -----------
// An STB column index is only meaningful for the client era the table came
// from.  Measured across the three trees in this repo (Arua, classic and
// Extracted_Titan, the last being byte-identical to classic):
//
//     table         Arua cols   classic cols   same-index mismatch
//     LIST_SKILL        91           90            73 / 90
//     LIST_WEAPON       51           49            29 / 49
//     LIST_ZONE         64           42            41 / 42
//     LIST_NPC          75           52            34 / 52
//     LIST_PAT          82           75            43 / 75
//
// So a hard-coded index does not merely drift by one or two, it lands in an
// unrelated field.  The sharpest example: LIST_SKILL columns 21-23 are
// "State1 stat / amount / %" in Arua but "Warp Zone No. / X / Y" in classic.
// Importing classic by Arua indices loads map coordinates as stat bonuses —
// no error, no crash, just wrong numbers in the DataTable.
//
// WHY NAMES ALONE DO NOT FIX IT
// -----------------------------
// Only 19/91 Arua LIST_SKILL headers appear verbatim in classic, and just
// 2/62 for LIST_ZONE.  The eras use different words for the same field
// ("Weapon Type"/"Item Type", "Price"/"Standard Price", "walk speed"/"Walking
// Speed").  Worse, some headers are outright wrong: LIST_SKILL column 1 reads
// "Skill Level" in Arua but holds the base skill id, and the JEMITEM columns
// are documented in this project as lying about themselves.
//
// THE SCHEME
// ----------
// Each logical field declares the header spellings it is known by, plus an
// optional pinned index PER CLIENT PROFILE for the cases where the header
// cannot be trusted.  Binding then resolves, in order:
//
//   1. the pinned index for the detected profile   (authoritative; headers may lie)
//   2. a normalised match against any declared alias
//   3. unresolved -> INDEX_NONE, the accessor returns the caller's default
//
// Step 3 is the point of the whole exercise: an unknown field reads as a
// default and is reported, instead of silently returning a neighbouring
// column's value.
//
// Profile detection is derived from the schema rather than a separate
// fingerprint table: for each candidate profile, count how many pinned
// indices carry a header that matches that field's aliases.  Best score wins,
// and a client resembling neither is reported as Unknown and resolved purely
// by name — which is what makes a brand-new client work.

#pragma once

#include "CoreMinimal.h"

struct FRoseSTB;

enum class ERoseStbProfile : uint8
{
	Unknown = 0,
	Arua,
	Classic,
	Count
};

const TCHAR* RoseStbProfileName(ERoseStbProfile Profile);

struct FRoseStbField
{
	// The name this code refers to the column by.
	FName Key;

	// Header spellings this field is known by, across every client era we have
	// seen.  Matched after normalisation, so punctuation and spacing differences
	// ("Mon File name" vs "Mon Filename") do not need separate entries.
	TArray<FString> Aliases;

	// Which matching column to take when the header is not unique.  Classic's
	// LIST_SKILL names columns 30-34 all "Equipment Requirement"; without this
	// every one of the five would bind to column 30.  0 = first match.
	int32 Occurrence = 0;

	// Per-profile pinned column index, or INDEX_NONE to resolve by name.
	// Use this only where the index is known-good and the header is not
	// trustworthy; a pin silently overrides the names.
	int32 Pinned[(int32)ERoseStbProfile::Count] = { INDEX_NONE, INDEX_NONE, INDEX_NONE };

	// The header this field carries IN that profile, used ONLY to recognise the
	// profile.  It must not be the alias union: aliases deliberately list every
	// era's spelling, so scoring against them lets a classic table match at
	// Arua's indices and be misidentified as Arua — which would then apply the
	// Arua pins and import the wrong columns, the exact failure this whole file
	// exists to prevent.
	FString Signature[(int32)ERoseStbProfile::Count];
};

struct FRoseStbSchema
{
	FString TableName;
	TArray<FRoseStbField> Fields;
};

// A schema resolved against one concrete table.
struct FRoseStbBinding
{
	const FRoseSTB* Table = nullptr;
	FString TableName;
	ERoseStbProfile Profile = ERoseStbProfile::Unknown;

	TMap<FName, int32> Cols;          // logical key -> column index
	TArray<FName> Unresolved;         // fields this table has no column for

	bool Has(FName Key) const
	{
		const int32* C = Cols.Find(Key);
		return C && *C != INDEX_NONE;
	}

	int32 Col(FName Key) const
	{
		const int32* C = Cols.Find(Key);
		return C ? *C : INDEX_NONE;
	}

	// All accessors fall back to the caller's default when the field is not
	// present in this client's table.  That is deliberate: a missing column must
	// never read as a neighbouring one.
	const FString& Get(int32 Row, FName Key) const;
	int32 GetInt(int32 Row, FName Key, int32 Default = 0) const;
	float GetFloat(int32 Row, FName Key, float Default = 0.f) const;
};

namespace RoseStb
{
	// Lowercase and strip everything that is not alphanumeric, so that
	// "Mon File name", "Mon Filename" and "MON_FILENAME" all compare equal.
	FString Normalize(const FString& S);

	ERoseStbProfile DetectProfile(const FRoseSTB& Table, const FRoseStbSchema& Schema);

	// Resolve a schema against a table and log a one-line summary plus every
	// unresolved field.
	FRoseStbBinding Bind(const FRoseSTB& Table, const FRoseStbSchema& Schema);

	// Print every field's resolution for a table — how it bound (pin or name),
	// which column, and the header sitting there.  Used by the audit commandlet
	// to verify a new client before trusting an import from it.
	void LogBindingReport(const FRoseStbBinding& Binding, const FRoseStbSchema& Schema);

	const FRoseStbSchema& SkillSchema();
	const FRoseStbSchema& ZoneSchema();
	const FRoseStbSchema& SkySchema();
}
