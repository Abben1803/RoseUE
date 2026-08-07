// STB -> UDataTable, natively.
//
// WHY THIS EXISTS
// The runtime is deliberately data-driven: URoseSkillComponent and friends read
// DataTables by id and contain NO per-item or per-skill code.  Those tables were
// produced by ~20 `tools/gen_*.py` scripts run by hand with `py -3.9`, which is
// the last thing standing between a bare clone and a playable project.  This
// module regenerates them in the same editor launch as everything else.
//
// It writes the SAME row structs the runtime already consumes
// (RoseSkillTypes.h, RoseItemTypes.h, RoseNpcTypes.h ...), so nothing on the
// gameplay side changes — only where the rows come from.
//
// COLUMN INDICES ARE NOT GUESSES.  The indices here are ported from the Python
// generators, which carry their own provenance: verified against the server
// headers (`src/common/io_skill.h` SKILL_* macros) and spot-checked against
// known live rows (Double Attack 321, Leap Attack 341, Meditation 821, Bow
// Mastery 1401).  Do not "tidy" them against `rose/io/stb.h` — those macros are
// written for the CLASSIC layout and are stale for Arua, and a wrong column does
// not fail, it silently yields a plausible wrong number.  FRoseSTB::FindCol is
// available where a header name is the safer key.
#pragma once

#include "CoreMinimal.h"

struct FRoseTableImportOptions
{
	FString AssetRoot;

	// Skill trees: skills + jobs (LIST_CLASS) + skill points.
	bool bSkills = true;

	// Where the DataTables are written.  The runtime loads them from
	// /Game/DataTables/<name>, so this is not freely movable.
	FString PackageRoot = TEXT("/Game/DataTables");
};

struct FRoseTablePackResult
{
	FString Name;        // asset name, e.g. "skills"
	int32 RowsIn = 0;    // rows in the STB
	int32 RowsOut = 0;   // rows written (blank/unnamed rows are skipped)
	bool bSaved = false;
};

struct FRoseTableImportResult
{
	bool bSuccess = false;
	TArray<FRoseTablePackResult> Packs;
	// Rows whose STL key resolved to no string.  Non-fatal, but a large number
	// means the wrong STL or the wrong language.
	int32 MissingNames = 0;
	double SecondsTotal = 0.0;
};

bool RoseImportTables(const FRoseTableImportOptions& Options, FRoseTableImportResult& Result);
