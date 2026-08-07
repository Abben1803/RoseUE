#include "RoseTableImporter.h"

#include "RoseEditor.h"
#include "RoseObjectFormats.h"
#include "RosePathResolver.h"
#include "RoseStbSchema.h"
#include "RoseStringTable.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/DataTable.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

// The runtime's row structs ARE the schema — RoseEditor.Build.cs puts the RoseUE
// module root on the include path for exactly this.
#include "RoseSkillTypes.h"

namespace
{
	const TCHAR* kStbDir = TEXT("STB");

	FString Clean(const FString& In)
	{
		FString Out = In;
		Out.TrimStartAndEndInline();
		return Out;
	}

	// Create (or reuse) a DataTable asset and save it.
	//
	// FindObject before NewObject for the same reason the asset cache does it:
	// NewObject over a live name renames the incumbent and leaves it in the
	// package, which is how a package ends up unsaveable.
	template <typename TRow>
	UDataTable* MakeTable(const FString& PackageRoot, const FString& AssetName,
		const TMap<FName, TRow>& Rows, bool& bOutSaved)
	{
		const FString PkgName = FString::Printf(TEXT("%s/%s"), *PackageRoot, *AssetName);
		UPackage* Pkg = CreatePackage(*PkgName);

		// These tables already exist (the Python generators wrote them), so in
		// the editor the package is on disk and only lazily loaded.  SavePackage
		// refuses a partially-loaded package; fully load before touching it.
		Pkg->FullyLoad();

		UDataTable* Table = FindObject<UDataTable>(Pkg, *AssetName);
		if (!Table)
			Table = NewObject<UDataTable>(Pkg, *AssetName, RF_Public | RF_Standalone);

		Table->RowStruct = TRow::StaticStruct();
		Table->EmptyTable();
		for (const TPair<FName, TRow>& Pair : Rows)
			Table->AddRow(Pair.Key, Pair.Value);

		Table->Modify();
		FAssetRegistryModule::AssetCreated(Table);
		Pkg->MarkPackageDirty();

		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		const FString Filename = FPackageName::LongPackageNameToFilename(
			PkgName, FPackageName::GetAssetPackageExtension());

		bOutSaved = UPackage::SavePackage(Pkg, Table, *Filename, Args);
		if (bOutSaved)
			Pkg->SetDirtyFlag(false);
		else
			UE_LOG(LogRoseImport, Error, TEXT("failed to save %s"), *PkgName);

		return Table;
	}
}

bool RoseImportTables(const FRoseTableImportOptions& Options, FRoseTableImportResult& Result)
{
	const double TimeStart = FPlatformTime::Seconds();
	FRosePathResolver Resolver(Options.AssetRoot);

	auto ResolveStb = [&Resolver](const TCHAR* File)
	{
		return Resolver.Resolve(FString::Printf(TEXT("%s/%s"), kStbDir, File));
	};

	// ── skill trees ────────────────────────────────────────────────────────
	if (Options.bSkills)
	{
		// ---- skills ----
		FRoseSTB Skill, Status;
		FRoseSTL SkillStl;
		const FString SkillPath = ResolveStb(TEXT("LIST_SKILL.STB"));

		if (SkillPath.IsEmpty() || !Skill.Load(SkillPath))
		{
			UE_LOG(LogRoseImport, Error, TEXT("cannot read STB/LIST_SKILL.STB"));
		}
		else
		{
			Status.Load(ResolveStb(TEXT("LIST_STATUS.STB")));
			SkillStl.Load(ResolveStb(TEXT("LIST_SKILL_S.STL")));

			// LIST_STATUS col 1 = STATE_TYPE (eING_TYPE), col 3 =
			// STATE_PRIFITS_LOSSES.  Resolved here so the runtime maps buffs
			// without shipping the status table.  Row 0 is the blank sentinel.
			auto StateType = [&Status](int32 Sid)
			{
				return (Sid > 0 && Sid < Status.Rows) ? Status.GetInt(Sid, 1) : 0;
			};
			auto StateHarmful = [&Status](int32 Sid)
			{
				return (Sid > 0 && Sid < Status.Rows) ? Status.GetInt(Sid, 3) : 0;
			};

			// Columns are resolved through the schema, NOT by literal index.
			//
			// An index is only valid for the client era it was read off.  Across
			// the trees in this repo, 73 of LIST_SKILL's 90 shared columns hold
			// something different in classic than in Arua; columns 21-23 are the
			// stat-bonus block in Arua but Warp Zone No./X/Y in classic.  Reading
			// those by index against another client imports map coordinates as
			// stat bonuses, with no error to notice.
			//
			// RoseStb::Bind pins the known-good Arua indices (so this path is
			// unchanged for Arua, headers being untrustworthy in places) and
			// resolves anything else by header name, leaving genuinely absent
			// fields at their default instead of reading a neighbour.
			const FRoseStbSchema& SkillSchema = RoseStb::SkillSchema();
			const FRoseStbBinding Col = RoseStb::Bind(Skill, SkillSchema);

			static const FName F_SkillName(TEXT("SkillName"));
			static const FName F_StlKey(TEXT("StlKey"));

			TMap<FName, FRoseSkillRow> Rows;
			FRoseTablePackResult Pack;
			Pack.Name = TEXT("skills");
			Pack.RowsIn = Skill.Rows;

			for (int32 i = 1; i < Skill.Rows; ++i)
			{
				// Unnamed filler row — the game skips skill 0 and blanks too.
				if (Clean(Col.Get(i, F_SkillName)).IsEmpty())
					continue;

				FRoseSkillRow R;
				R.Id = i;
				R.SkillName        = Clean(Col.Get(i, F_SkillName));
				R.BaseSkillId      = Col.GetInt(i, TEXT("BaseSkillId"));
				R.SkillLevel       = Col.GetInt(i, TEXT("SkillLevel"));
				R.PointCost        = Col.GetInt(i, TEXT("PointCost"));
				R.Tab              = Col.GetInt(i, TEXT("Tab"));
				R.SkillType        = Col.GetInt(i, TEXT("SkillType"));
				R.Range            = Col.GetInt(i, TEXT("Range"));
				R.TargetFilter     = Col.GetInt(i, TEXT("TargetFilter"));
				R.Radius           = Col.GetInt(i, TEXT("Radius"));
				R.Power            = Col.GetInt(i, TEXT("Power"));
				R.Harm             = Col.GetInt(i, TEXT("Harm"));

				R.Status1          = Col.GetInt(i, TEXT("Status1"));
				R.Status2          = Col.GetInt(i, TEXT("Status2"));
				R.StatusType1      = StateType(R.Status1);
				R.StatusType2      = StateType(R.Status2);
				R.StatusHarmful1   = StateHarmful(R.Status1);
				R.StatusHarmful2   = StateHarmful(R.Status2);

				R.SuccessRatio     = Col.GetInt(i, TEXT("SuccessRatio"));
				R.Duration         = Col.GetInt(i, TEXT("Duration"));
				R.DamageType       = Col.GetInt(i, TEXT("DamageType"));
				R.UseAbility1      = Col.GetInt(i, TEXT("UseAbility1"));
				R.UseAmount1       = Col.GetInt(i, TEXT("UseAmount1"));
				R.UseAbility2      = Col.GetInt(i, TEXT("UseAbility2"));
				R.UseAmount2       = Col.GetInt(i, TEXT("UseAmount2"));

				// Reuse delay: ms = ticks*200 - 100 (CSkillLIST::LoadSkillTable,
				// io_skill.cpp:105).  0 ticks means no cooldown at all — without
				// the guard that would come out as -0.1s.
				const int32 Ticks = Col.GetInt(i, TEXT("CooldownTicks"));
				R.CooldownSec = Ticks > 0
					? FMath::RoundToFloat(FMath::Max(0, Ticks * 200 - 100) / 10.f) / 100.f
					: 0.f;
				R.CooldownGroup    = Col.GetInt(i, TEXT("CooldownGroup"));

				R.IncAbility1      = Col.GetInt(i, TEXT("IncAbility1"));
				R.IncValue1        = Col.GetInt(i, TEXT("IncValue1"));
				R.IncRate1         = Col.GetInt(i, TEXT("IncRate1"));
				R.IncAbility2      = Col.GetInt(i, TEXT("IncAbility2"));
				R.IncValue2        = Col.GetInt(i, TEXT("IncValue2"));
				R.IncRate2         = Col.GetInt(i, TEXT("IncRate2"));

				R.SummonPet        = Col.GetInt(i, TEXT("SummonPet"));
				R.ActionMode       = Col.GetInt(i, TEXT("ActionMode"));
				R.NeedWeapon1      = Col.GetInt(i, TEXT("NeedWeapon1"));
				R.NeedWeapon2      = Col.GetInt(i, TEXT("NeedWeapon2"));
				R.NeedWeapon3      = Col.GetInt(i, TEXT("NeedWeapon3"));
				R.NeedWeapon4      = Col.GetInt(i, TEXT("NeedWeapon4"));
				R.NeedWeapon5      = Col.GetInt(i, TEXT("NeedWeapon5"));
				R.RequiredClassSet = Col.GetInt(i, TEXT("RequiredClassSet"));
				R.RequiredUnion1   = Col.GetInt(i, TEXT("RequiredUnion1"));

				R.NeedSkill1       = Col.GetInt(i, TEXT("NeedSkill1"));
				R.NeedSkillLevel1  = Col.GetInt(i, TEXT("NeedSkillLevel1"));
				R.NeedSkill2       = Col.GetInt(i, TEXT("NeedSkill2"));
				R.NeedSkillLevel2  = Col.GetInt(i, TEXT("NeedSkillLevel2"));
				R.NeedSkill3       = Col.GetInt(i, TEXT("NeedSkill3"));
				R.NeedSkillLevel3  = Col.GetInt(i, TEXT("NeedSkillLevel3"));

				R.NeedAbility1      = Col.GetInt(i, TEXT("NeedAbility1"));
				R.NeedAbilityValue1 = Col.GetInt(i, TEXT("NeedAbilityValue1"));
				R.NeedAbility2      = Col.GetInt(i, TEXT("NeedAbility2"));
				R.NeedAbilityValue2 = Col.GetInt(i, TEXT("NeedAbilityValue2"));

				R.IconIdx          = Col.GetInt(i, TEXT("IconIdx"));
				R.CastMotion       = Col.GetInt(i, TEXT("CastMotion"));
				R.CastMotionSpeed  = Col.GetInt(i, TEXT("CastMotionSpeed"));
				R.ActionMotion     = Col.GetInt(i, TEXT("ActionMotion"));
				R.ActionMotionSpeed= Col.GetInt(i, TEXT("ActionMotionSpeed"));
				R.HitCount         = Col.GetInt(i, TEXT("HitCount"));
				R.BulletNo         = Col.GetInt(i, TEXT("BulletNo"));
				R.FireSound        = Col.GetInt(i, TEXT("FireSound"));
				R.HitSound         = Col.GetInt(i, TEXT("HitSound"));
				R.ZulyCost         = Col.GetInt(i, TEXT("ZulyCost"));
				R.CastEffect       = Col.GetInt(i, TEXT("CastEffect"));
				R.HitEffect        = Col.GetInt(i, TEXT("HitEffect"));

				R.StlKey = Col.Get(i, F_StlKey);
				if (const FRoseStlRow* Text = SkillStl.Find(R.StlKey))
				{
					R.DisplayName = Clean(Text->Text);
					R.Description = Clean(Text->Description);
				}
				else if (!R.StlKey.IsEmpty())
				{
					++Result.MissingNames;
				}

				Rows.Add(FName(*FString::Printf(TEXT("skill_%d"), i)), R);
			}

			Pack.RowsOut = Rows.Num();
			MakeTable<FRoseSkillRow>(Options.PackageRoot, TEXT("skills"), Rows, Pack.bSaved);
			Result.Packs.Add(Pack);
			UE_LOG(LogRoseImport, Log, TEXT("skills: %d rows"), Pack.RowsOut);
		}

		// ---- jobs (LIST_CLASS: a NAMED SET of job ids, not a job) ----
		FRoseSTB Class;
		FRoseSTL ClassStl;
		const FString ClassPath = ResolveStb(TEXT("LIST_CLASS.STB"));
		if (!ClassPath.IsEmpty() && Class.Load(ClassPath))
		{
			ClassStl.Load(ResolveStb(TEXT("LIST_CLASS_S.STL")));

			TMap<FName, FRoseJobRow> Rows;
			FRoseTablePackResult Pack;
			Pack.Name = TEXT("jobs");
			Pack.RowsIn = Class.Rows;

			for (int32 i = 0; i < Class.Rows; ++i)
			{
				// Skip fully blank rows; unlike skills, col 0 alone is not a
				// reliable emptiness test here.
				bool bAny = false;
				for (int32 c = 0; c < Class.Cols && !bAny; ++c)
					bAny = !Clean(Class.Get(i, c)).IsEmpty();
				if (!bAny)
					continue;

				FRoseJobRow R;
				R.Id = i;
				R.JobName = Clean(Class.Get(i, 0));
				int32* const Jobs[] = { &R.Job1, &R.Job2, &R.Job3, &R.Job4, &R.Job5,
										&R.Job6, &R.Job7, &R.Job8, &R.Job9, &R.Job10 };
				for (int32 c = 1; c <= 10; ++c)
					*Jobs[c - 1] = Class.GetInt(i, c);

				R.StlKey = Class.Get(i, 11);
				R.DisplayName = Clean(ClassStl.GetText(R.StlKey));

				Rows.Add(FName(*FString::Printf(TEXT("job_%d"), i)), R);
			}

			Pack.RowsOut = Rows.Num();
			MakeTable<FRoseJobRow>(Options.PackageRoot, TEXT("jobs"), Rows, Pack.bSaved);
			Result.Packs.Add(Pack);
			UE_LOG(LogRoseImport, Log, TEXT("jobs: %d rows"), Pack.RowsOut);
		}
		else
		{
			UE_LOG(LogRoseImport, Error, TEXT("cannot read STB/LIST_CLASS.STB"));
		}

		// ---- skill points per level ----
		FRoseSTB SkillP;
		const FString SkillPPath = ResolveStb(TEXT("LIST_SKILL_P.STB"));
		if (!SkillPPath.IsEmpty() && SkillP.Load(SkillPPath))
		{
			TMap<FName, FRoseSkillPointRow> Rows;
			FRoseTablePackResult Pack;
			Pack.Name = TEXT("skill_points");
			Pack.RowsIn = SkillP.Rows;

			for (int32 i = 0; i < SkillP.Rows; ++i)
			{
				const int32 Level = SkillP.GetInt(i, 0);
				if (Level <= 0)
					continue;

				FRoseSkillPointRow R;
				R.Level = Level;
				R.Points = SkillP.GetInt(i, 1);
				Rows.Add(FName(*FString::Printf(TEXT("sp_%d"), Level)), R);
			}

			Pack.RowsOut = Rows.Num();
			MakeTable<FRoseSkillPointRow>(Options.PackageRoot, TEXT("skill_points"), Rows, Pack.bSaved);
			Result.Packs.Add(Pack);
			UE_LOG(LogRoseImport, Log, TEXT("skill_points: %d rows"), Pack.RowsOut);
		}
		else
		{
			UE_LOG(LogRoseImport, Error, TEXT("cannot read STB/LIST_SKILL_P.STB"));
		}
	}

	Result.SecondsTotal = FPlatformTime::Seconds() - TimeStart;
	Result.bSuccess = Result.Packs.Num() > 0;
	return Result.bSuccess;
}
