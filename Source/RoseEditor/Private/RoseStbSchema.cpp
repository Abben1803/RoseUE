#include "RoseStbSchema.h"

#include "RoseObjectFormats.h"

DEFINE_LOG_CATEGORY_STATIC(LogRoseStb, Log, All);

const TCHAR* RoseStbProfileName(ERoseStbProfile Profile)
{
	switch (Profile)
	{
	case ERoseStbProfile::Arua:    return TEXT("arua");
	case ERoseStbProfile::Classic: return TEXT("classic");
	default:                       return TEXT("unknown");
	}
}

// ═══════════════════════════════════════════════════════════════════════════
//  Accessors
// ═══════════════════════════════════════════════════════════════════════════
const FString& FRoseStbBinding::Get(int32 Row, FName Key) const
{
	static const FString Empty;
	const int32 C = Col(Key);
	return (Table && C != INDEX_NONE) ? Table->Get(Row, C) : Empty;
}

int32 FRoseStbBinding::GetInt(int32 Row, FName Key, int32 Default) const
{
	const int32 C = Col(Key);
	return (Table && C != INDEX_NONE) ? Table->GetInt(Row, C, Default) : Default;
}

float FRoseStbBinding::GetFloat(int32 Row, FName Key, float Default) const
{
	const int32 C = Col(Key);
	return (Table && C != INDEX_NONE) ? Table->GetFloat(Row, C, Default) : Default;
}

// ═══════════════════════════════════════════════════════════════════════════
//  Resolution
// ═══════════════════════════════════════════════════════════════════════════
namespace
{
	// Column indices whose header matches any of the field's aliases, in order.
	void MatchingColumns(const FRoseSTB& Table, const FRoseStbField& Field,
		TArray<int32>& Out)
	{
		Out.Reset();

		TArray<FString> Wanted;
		Wanted.Reserve(Field.Aliases.Num() + 1);
		for (const FString& A : Field.Aliases)
			Wanted.Add(RoseStb::Normalize(A));
		// The logical key is an implicit alias, so a table whose header happens
		// to match our own naming binds without declaring it twice.
		Wanted.AddUnique(RoseStb::Normalize(Field.Key.ToString()));

		for (int32 c = 0; c < Table.Headers.Num(); ++c)
		{
			const FString H = RoseStb::Normalize(Table.Headers[c]);
			if (!H.IsEmpty() && Wanted.Contains(H))
				Out.Add(c);
		}
	}
}

FString RoseStb::Normalize(const FString& S)
{
	// Alphanumerics AND '%'.
	//
	// '%' has to survive because it is the only thing distinguishing a rate
	// column from its value column: dropping it collapses "Status 2 % Power"
	// onto "Status 2 Power", and "State1 %" onto "State 1".  The audit caught
	// IncRate2 binding to column 25 (IncValue2's) for exactly that reason.
	FString Out;
	Out.Reserve(S.Len());
	for (TCHAR Ch : S)
		if (FChar::IsAlnum(Ch) || Ch == TEXT('%'))
			Out.AppendChar(FChar::ToLower(Ch));
	return Out;
}

ERoseStbProfile RoseStb::DetectProfile(const FRoseSTB& Table, const FRoseStbSchema& Schema)
{
	// Score each profile by how many of its pinned columns actually carry a
	// header this field is known by.  A tree that matches neither scores ~0 and
	// stays Unknown, which routes every field through name resolution — the
	// path that makes an unseen client work at all.
	int32 Best = 0;
	ERoseStbProfile BestProfile = ERoseStbProfile::Unknown;

	for (int32 p = 1; p < (int32)ERoseStbProfile::Count; ++p)
	{
		int32 Score = 0, Pins = 0;
		for (const FRoseStbField& F : Schema.Fields)
		{
			const int32 Pin = F.Pinned[p];
			if (Pin == INDEX_NONE || !Table.Headers.IsValidIndex(Pin))
				continue;
			if (F.Signature[p].IsEmpty())
				continue;
			++Pins;

			// Compare against THIS profile's own header only.  Scoring against
			// the alias list would match a foreign client at our indices and
			// misidentify it — classic scores 56/59 that way and would then be
			// read with Arua's column numbers.
			if (RoseStb::Normalize(Table.Headers[Pin]) == RoseStb::Normalize(F.Signature[p]))
				++Score;
		}

		UE_LOG(LogRoseStb, Verbose, TEXT("  %s: profile '%s' scores %d/%d"),
			*Schema.TableName, RoseStbProfileName((ERoseStbProfile)p), Score, Pins);

		// Require a real majority — a couple of coincidental header matches must
		// not be enough to pin a whole table to the wrong era.
		if (Pins > 0 && Score * 2 > Pins && Score > Best)
		{
			Best = Score;
			BestProfile = (ERoseStbProfile)p;
		}
	}

	return BestProfile;
}

FRoseStbBinding RoseStb::Bind(const FRoseSTB& Table, const FRoseStbSchema& Schema)
{
	FRoseStbBinding B;
	B.Table = &Table;
	B.TableName = Schema.TableName;
	B.Profile = DetectProfile(Table, Schema);

	int32 ByPin = 0, ByName = 0;

	for (const FRoseStbField& F : Schema.Fields)
	{
		int32 Resolved = INDEX_NONE;

		// 1. Pinned index for the detected profile.  This deliberately outranks
		//    the header, because some headers are wrong: LIST_SKILL column 1
		//    reads "Skill Level" in Arua but holds the base skill id.
		if (B.Profile != ERoseStbProfile::Unknown)
		{
			const int32 Pin = F.Pinned[(int32)B.Profile];
			if (Pin != INDEX_NONE && Pin < Table.Cols)
			{
				Resolved = Pin;
				++ByPin;
			}
		}

		// 2. Otherwise match the declared header spellings.
		if (Resolved == INDEX_NONE)
		{
			TArray<int32> Matches;
			MatchingColumns(Table, F, Matches);
			if (Matches.IsValidIndex(F.Occurrence))
			{
				Resolved = Matches[F.Occurrence];
				++ByName;
			}
		}

		B.Cols.Add(F.Key, Resolved);
		if (Resolved == INDEX_NONE)
			B.Unresolved.Add(F.Key);
	}

	UE_LOG(LogRoseStb, Log,
		TEXT("%s: %d cols, profile '%s' — %d bound by index, %d by header, %d UNRESOLVED"),
		*Schema.TableName, Table.Cols, RoseStbProfileName(B.Profile),
		ByPin, ByName, B.Unresolved.Num());

	// Two fields on one column means an alias is ambiguous — the normalised
	// spellings of two different headers collided, or an Occurrence is missing.
	// Report it: this is what exposed "Status 2 % Power" and "Status 2 Power"
	// normalising to the same string and binding IncRate2 onto IncValue2's
	// column.
	{
		TMap<int32, FName> Seen;
		for (const FRoseStbField& F : Schema.Fields)
		{
			const int32 C = B.Col(F.Key);
			if (C == INDEX_NONE)
				continue;
			if (const FName* Prev = Seen.Find(C))
			{
				UE_LOG(LogRoseStb, Warning,
					TEXT("  %s: '%s' and '%s' both bound to column %d (\"%s\") — ambiguous alias"),
					*Schema.TableName, *Prev->ToString(), *F.Key.ToString(), C,
					Table.Headers.IsValidIndex(C) ? *Table.Headers[C] : TEXT("?"));
			}
			else
			{
				Seen.Add(C, F.Key);
			}
		}
	}

	if (B.Unresolved.Num() > 0)
	{
		// Not fatal: those fields read as their default.  Worth shouting about,
		// because it is the difference between "this client lacks the field" and
		// "we are about to import a wrong number".
		FString List;
		for (const FName& N : B.Unresolved)
			List += (List.IsEmpty() ? TEXT("") : TEXT(", ")) + N.ToString();
		UE_LOG(LogRoseStb, Warning, TEXT("  %s: no column for: %s"),
			*Schema.TableName, *List);
	}

	return B;
}

void RoseStb::LogBindingReport(const FRoseStbBinding& Binding, const FRoseStbSchema& Schema)
{
	UE_LOG(LogRoseStb, Display, TEXT("--- %s  (profile '%s') ---"),
		*Schema.TableName, RoseStbProfileName(Binding.Profile));

	for (const FRoseStbField& F : Schema.Fields)
	{
		const int32 C = Binding.Col(F.Key);
		if (C == INDEX_NONE)
		{
			UE_LOG(LogRoseStb, Warning, TEXT("  %-22s  ***  no column"), *F.Key.ToString());
			continue;
		}
		const FString Header = Binding.Table && Binding.Table->Headers.IsValidIndex(C)
			? Binding.Table->Headers[C] : FString();
		UE_LOG(LogRoseStb, Display, TEXT("  %-22s  col %-3d  \"%s\""),
			*F.Key.ToString(), C, *Header);
	}
}

// ═══════════════════════════════════════════════════════════════════════════
//  LIST_SKILL
// ═══════════════════════════════════════════════════════════════════════════
//
// Pinned for Arua only.  The Arua indices are the ones this importer has been
// running on, so pinning them keeps that path bit-for-bit unchanged.  Classic
// is left to resolve by header, which works because its own spellings are
// declared below as aliases — and where classic genuinely lacks a field
// (columns 21-23 there are Warp Zone No./X/Y, not stat bonuses) it resolves to
// nothing and reads as the default rather than importing map coordinates.
const FRoseStbSchema& RoseStb::SkillSchema()
{
	static FRoseStbSchema Schema = []
	{
		FRoseStbSchema S;
		S.TableName = TEXT("LIST_SKILL");

		// The FIRST alias must be the ARUA spelling — it doubles as the
		// signature that identifies an Arua table.  Later aliases are the other
		// eras' names and take no part in detection.
		auto Add = [&S](const TCHAR* Key, int32 AruaCol,
			std::initializer_list<const TCHAR*> Aliases, int32 Occurrence = 0)
		{
			check(Aliases.size() > 0);

			FRoseStbField F;
			F.Key = FName(Key);
			F.Occurrence = Occurrence;
			for (const TCHAR* A : Aliases)
				F.Aliases.Add(A);
			F.Pinned[(int32)ERoseStbProfile::Arua] = AruaCol;
			F.Signature[(int32)ERoseStbProfile::Arua] = *Aliases.begin();
			S.Fields.Add(MoveTemp(F));
		};

		Add(TEXT("SkillName"),        0,  { TEXT("Name") });
		Add(TEXT("BaseSkillId"),      1,  { TEXT("Skill Level"), TEXT("id Index") });
		Add(TEXT("SkillLevel"),       2,  { TEXT("Level") });
		Add(TEXT("PointCost"),        3,  { TEXT("SP") });
		Add(TEXT("Tab"),              4,  { TEXT("Skill Category"), TEXT("Skill Tab Type") });
		Add(TEXT("SkillType"),        5,  { TEXT("Skill Type") });
		Add(TEXT("Range"),            6,  { TEXT("Range (Distance)"), TEXT("Range/Planet") });
		Add(TEXT("TargetFilter"),     7,  { TEXT("Target Filter") });
		Add(TEXT("Radius"),           8,  { TEXT("Scope") });
		Add(TEXT("Power"),            9,  { TEXT("Attack Power"), TEXT("Power/ITEM_MAKE_NUM") });
		Add(TEXT("Harm"),            10,  { TEXT("Hostility Check"), TEXT("Skill Harm") });
		Add(TEXT("Status1"),         11,  { TEXT("State 1"), TEXT("Status1") });
		Add(TEXT("Status2"),         12,  { TEXT("State 2"), TEXT("Status2") });
		Add(TEXT("SuccessRatio"),    13,  { TEXT("Success Rate (%)"), TEXT("Success Chance") });
		Add(TEXT("Duration"),        14,  { TEXT("Skill Duration"), TEXT("Duration") });
		Add(TEXT("DamageType"),      15,  { TEXT("Formula Type"), TEXT("Damage Type") });
		Add(TEXT("UseAbility1"),     16,  { TEXT("Ability Consumption 1"), TEXT("Ability Consulation") });
		Add(TEXT("UseAmount1"),      17,  { TEXT("Consumption 1 amount"), TEXT("Consumption Amount1") });
		Add(TEXT("UseAbility2"),     18,  { TEXT("Ability Consumption 2"), TEXT("Ability Consumption2") });
		Add(TEXT("UseAmount2"),      19,  { TEXT("Consumtion 2 amount"), TEXT("Consumption Amount2") });
		Add(TEXT("CooldownTicks"),   20,  { TEXT("Reload Time"), TEXT("Cooldown") });

		// Arua 21-26 are the stat-bonus block.  Classic only has the second half
		// (its 21-23 are Warp Zone No./X/Y), so IncAbility1/Value1/Rate1 stay
		// unresolved there by design.
		Add(TEXT("IncAbility1"),     21,  { TEXT("State1 stat") });
		Add(TEXT("IncValue1"),       22,  { TEXT("State1 amount") });
		Add(TEXT("IncRate1"),        23,  { TEXT("State1 %") });
		Add(TEXT("IncAbility2"),     24,  { TEXT("State2 stat"), TEXT("Status 2 Stat") });
		Add(TEXT("IncValue2"),       25,  { TEXT("State2 amount"), TEXT("Status 2 Power") });
		Add(TEXT("IncRate2"),        26,  { TEXT("State2 %"), TEXT("Status 2 % Power") });

		Add(TEXT("CooldownGroup"),   27,  { TEXT("Cooldown Type") });
		Add(TEXT("SummonPet"),       28,  { TEXT("Summon Mob No.") });
		Add(TEXT("ActionMode"),      29,  { TEXT("Action Mode") });

		// Classic names all five of these "Equipment Requirement"; Occurrence
		// picks the right one.
		Add(TEXT("NeedWeapon1"),     30,  { TEXT("Equipment Requirement 1"), TEXT("Equipment Requirement") }, 0);
		Add(TEXT("NeedWeapon2"),     31,  { TEXT("Equipment Requirement 2"), TEXT("Equipment Requirement") }, 1);
		Add(TEXT("NeedWeapon3"),     32,  { TEXT("Equipment Requirement 3"), TEXT("Equipment Requirement") }, 2);
		Add(TEXT("NeedWeapon4"),     33,  { TEXT("Equipment Requirement 4"), TEXT("Equipment Requirement") }, 3);
		Add(TEXT("NeedWeapon5"),     34,  { TEXT("Equipment Requirement 5"), TEXT("Equipment Requirement") }, 4);

		Add(TEXT("RequiredClassSet"),35,  { TEXT("Job 1"), TEXT("Job1") });
		Add(TEXT("RequiredUnion1"),  36,  { TEXT("Job 2"), TEXT("Job2") });

		Add(TEXT("NeedSkill1"),      39,  { TEXT("Required Skill 1"), TEXT("Necessary Skill 1") });
		Add(TEXT("NeedSkillLevel1"), 40,  { TEXT("Required Skill 1 Level"), TEXT("Necessary Skill Level 1") });
		Add(TEXT("NeedSkill2"),      41,  { TEXT("Required Skill 2"), TEXT("Necessary Skill2") });
		Add(TEXT("NeedSkillLevel2"), 42,  { TEXT("Required Skill 2 Level"), TEXT("Necessary Skill Level2") });
		Add(TEXT("NeedSkill3"),      43,  { TEXT("Required Skill 3"), TEXT("Necessary Skill3") });
		Add(TEXT("NeedSkillLevel3"), 44,  { TEXT("Required Skill 3 Level"), TEXT("Necessary Skill Levl3") });

		Add(TEXT("NeedAbility1"),      45, { TEXT("Stat Requirement 1"), TEXT("Stat Requirement") });
		Add(TEXT("NeedAbilityValue1"), 46, { TEXT("Stat Value 1"), TEXT("Requirement Value1") });
		Add(TEXT("NeedAbility2"),      47, { TEXT("Stat Requirement 2") });
		Add(TEXT("NeedAbilityValue2"), 48, { TEXT("Stat Value 2"), TEXT("Requirement Value2") });

		Add(TEXT("IconIdx"),         51,  { TEXT("Icon") });
		Add(TEXT("CastMotion"),      52,  { TEXT("Casting Motion") });
		Add(TEXT("CastMotionSpeed"), 53,  { TEXT("Casting Speed") });
		Add(TEXT("CastEffect"),      56,  { TEXT("Casting Effect0"), TEXT("Casting Effect1") });
		Add(TEXT("ActionMotion"),    68,  { TEXT("Actual Motion") });
		Add(TEXT("ActionMotionSpeed"),69, { TEXT("Motion Speed") });
		Add(TEXT("HitCount"),        70,  { TEXT("Number of Hits (not used)"), TEXT("Number of Hit") });
		Add(TEXT("BulletNo"),        71,  { TEXT("Bullet (list_effect.stb)"), TEXT("Bullet No.") });
		Add(TEXT("FireSound"),       73,  { TEXT("Bullet Fire Sound"), TEXT("Shooting Sound Effect") });
		Add(TEXT("HitEffect"),       74,  { TEXT("Hit Effect"), TEXT("Hitting Effect") });
		Add(TEXT("HitSound"),        76,  { TEXT("Hit Sound Effect"), TEXT("Hitting Sound Effect") });
		Add(TEXT("ZulyCost"),        85,  { TEXT("Zuly Required Level"), TEXT("Levelup Zuly") });

		// STL sits at 90 in Arua and 89 in classic — the one field that was
		// already resolved by name before this schema existed.
		Add(TEXT("StlKey"),          90,  { TEXT("STL") });

		return S;
	}();

	return Schema;
}

// ═══════════════════════════════════════════════════════════════════════════
//  LIST_ZONE
// ═══════════════════════════════════════════════════════════════════════════
//
// The worst offender of the tables measured: 41 of 42 shared columns differ
// between Arua and classic, and only 2 of 62 header names appear in both.
// Note ".zon Path" and "Zone Path" do NOT normalise to the same string, so
// both spellings have to be declared.
const FRoseStbSchema& RoseStb::ZoneSchema()
{
	static FRoseStbSchema Schema = []
	{
		FRoseStbSchema S;
		S.TableName = TEXT("LIST_ZONE");

		auto Add = [&S](const TCHAR* Key, int32 AruaCol,
			std::initializer_list<const TCHAR*> Aliases)
		{
			check(Aliases.size() > 0);

			FRoseStbField F;
			F.Key = FName(Key);
			for (const TCHAR* A : Aliases)
				F.Aliases.Add(A);
			F.Pinned[(int32)ERoseStbProfile::Arua] = AruaCol;
			F.Signature[(int32)ERoseStbProfile::Arua] = *Aliases.begin();
			S.Fields.Add(MoveTemp(F));
		};

		// QQ-iROSE renames two of these while keeping the SAME columns and the
		// SAME values: col 1 "Zone Path" -> "ZON", col 11 "Object Table" ->
		// "Object".  Col 1 is the damaging one — ZonPath is what the zone row is
		// matched on, so failing to resolve it means NO row ever matches and both
		// pack names come back empty.  JPT01 then imported its 48 terrain chunks
		// with "packs: deco '' (MISSING), cnst '' (MISSING)" and **zero** object
		// parts: a town with no buildings, props or staircases, and no error.
		Add(TEXT("Name"),      0,  { TEXT("Name"), TEXT("Region Name") });
		Add(TEXT("ZonPath"),   1,  { TEXT(".zon Path"), TEXT("Zone Path"), TEXT("ZON") });
		Add(TEXT("DecoTable"), 11, { TEXT("DECO Table"), TEXT("Object Table"), TEXT("Object") });
		Add(TEXT("CnstTable"), 12, { TEXT("CNST Table"), TEXT("Building Table") });

		// The sky index, and the macro name for it is a trap.
		//
		// stb.h calls column 7 ZONE_BG_IMAGE, which reads like a loading-screen
		// picture, but cgamestatepreparemain.cpp passes it straight into
		// CSkyDOME::Init as nSkyIDX — it is the LIST_SKY row.  QQ-iROSE names the
		// header "Sky", which agrees, and its values are 0..15 against LIST_SKY's
		// 16 rows.  Bind on the header, never on the macro's name.
		Add(TEXT("Sky"),        7, { TEXT("Sky"), TEXT("BG Image"), TEXT("Background Image") });

		// The day-cycle block, and QQ-iROSE RENAMES THREE OF THE FIVE.
		//
		// The columns sit exactly where stb.h says, but "Day Cycle" ships as
		// "Day Period", "Day Time" as "Daytime Time" and "Night Time" as
		// "Nighttime Time".  Only Morning and Evening matched, so the other
		// three resolved to nothing and read back as 0 — a day whose length,
		// noon and dusk are all zero, with no error anywhere.  Measured on
		// JPT01: period 160, morning 0, day 11, evening 112, night 128.
		Add(TEXT("DayCycle"),  13, { TEXT("Day Cycle"), TEXT("DayCycle"), TEXT("Day Period") });
		Add(TEXT("MorningTime"), 14, { TEXT("Morning Time"), TEXT("Morning") });
		Add(TEXT("DayTime"),   15, { TEXT("Day Time"), TEXT("Daytime Time") });
		Add(TEXT("EveningTime"), 16, { TEXT("Evening Time"), TEXT("Evening") });
		Add(TEXT("NightTime"), 17, { TEXT("Night Time"), TEXT("Nighttime Time") });

		// Weather type — SE_WeatherEffect's argument (game_func.cpp).
		Add(TEXT("Weather"),   27, { TEXT("Weather"), TEXT("Weather Type") });

		return S;
	}();

	return Schema;
}

const FRoseStbSchema& RoseStb::SkySchema()
{
	static FRoseStbSchema Schema = []
	{
		FRoseStbSchema S;
		S.TableName = TEXT("LIST_SKY");

		auto Add = [&S](const TCHAR* Key, int32 AruaCol,
			std::initializer_list<const TCHAR*> Aliases)
		{
			check(Aliases.size() > 0);

			FRoseStbField F;
			F.Key = FName(Key);
			for (const TCHAR* A : Aliases)
				F.Aliases.Add(A);
			F.Pinned[(int32)ERoseStbProfile::Arua] = AruaCol;
			F.Signature[(int32)ERoseStbProfile::Arua] = *Aliases.begin();
			S.Fields.Add(MoveTemp(F));
		};

		// LIST_SKY ships with its headers mojibaked in every client we have (the
		// Korean column names do not survive the codepage), so the ALIASES here
		// will not match and every field falls back to its pinned index.  That is
		// fine and deliberate for this one table: it is 16 rows, its layout is
		// identical across the eras we support, and stb.h documents it exactly —
		// SKY_MESH col 0, SKY_TEXTURE(T) col 1+T, four of them.
		Add(TEXT("Mesh"),     0, { TEXT("Mesh"), TEXT("Sky Mesh") });
		Add(TEXT("DayTex"),   1, { TEXT("Sky Texture 1"), TEXT("Texture1"), TEXT("Day") });
		Add(TEXT("NightTex"), 2, { TEXT("Sky Texture 2"), TEXT("Texture2"), TEXT("Night") });

		return S;
	}();

	return Schema;
}
