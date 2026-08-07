#include "SRoseImportPanel.h"

#include "RoseEditor.h"
#include "RoseEquipmentImporter.h"
#include "RoseMapImporter.h"
#include "RoseObjectFormats.h"   // FRoseSTB — LIST_ZONE.STB for readable names
#include "RoseStbSchema.h"
#include "RoseSkeletalImporter.h"
#include "RoseTableImporter.h"

#include "DesktopPlatformModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/FileManager.h"
#include "IDesktopPlatform.h"
#include "Materials/MaterialInterface.h"
#include "Misc/Paths.h"
#include "Misc/ScopedSlowTask.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSeparator.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/SBoxPanel.h"

#define LOCTEXT_NAMESPACE "RoseImporter"

const FName SRoseImportPanel::TabId(TEXT("RoseImporter"));

void SRoseImportPanel::Construct(const FArguments& InArgs)
{
	RefreshZones();

	auto Stage = [this](const FText& Label, bool* Flag) -> TSharedRef<SWidget>
	{
		return SNew(SCheckBox)
			.IsChecked_Lambda([Flag]() { return *Flag ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([Flag](ECheckBoxState S) { *Flag = (S == ECheckBoxState::Checked); })
			[ SNew(STextBlock).Text(Label) ];
	};

	ChildSlot
	[
		SNew(SVerticalBox)

		// ── asset root ──
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 2)
		[ SNew(STextBlock).Text(LOCTEXT("RootLabel", "3DDATA asset root (the folder CONTAINING 3DDATA)")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(AssetRoot); })
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type)
					{ AssetRoot = T.ToString(); RefreshZones(); })
			]
			+ SHorizontalBox::Slot().AutoWidth().Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("Browse", "Browse..."))
				.OnClicked(this, &SRoseImportPanel::OnBrowseAssetRoot)
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0)[ SNew(SSeparator) ]

		// ── one click ──
		// The whole point of the native importer: someone clones this repo with
		// no Content at all, points at their 3DDATA and presses ONE button.
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 8, 8, 2)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.IsEnabled_Lambda([this]() { return !bBusy; })
			.Text(LOCTEXT("ImportEverything", "IMPORT EVERYTHING (fresh project)"))
			.ToolTipText(LOCTEXT("ImportEverythingTip",
				"Characters + equipment + every zone, in dependency order.\n\n"
				"This is the from-nothing path: a clone with no Content needs only "
				"the 3DDATA root above.  It takes hours — leave the editor alone "
				"until the log says it is done."))
			.OnClicked(this, &SRoseImportPanel::OnImportEverything)
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 8)
		[
			SNew(STextBlock)
			.AutoWrapText(true)
			.Text(LOCTEXT("ImportEverythingNote",
				"Or run the stages below individually. Characters must come before "
				"animations (the skeleton's bone tree is built from a mesh)."))
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0)[ SNew(SSeparator) ]

		// ── zone list ──
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text_Lambda([this]()
					{ return FText::FromString(FString::Printf(TEXT("Zones (%d)"), Zones.Num())); })
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SButton)
				.Text(LOCTEXT("Rescan", "Rescan"))
				.OnClicked(this, &SRoseImportPanel::OnRefreshZones)
			]
		]
		+ SVerticalBox::Slot().FillHeight(0.5f).Padding(8, 0, 8, 6)
		[
			SNew(SBorder)
			[
				SAssignNew(ZoneList, SListView<TSharedPtr<FZoneEntry>>)
				.ListItemsSource(&Zones)
				.OnGenerateRow(this, &SRoseImportPanel::MakeZoneRow)
				.SelectionMode(ESelectionMode::Single)
				.OnSelectionChanged_Lambda(
					[this](TSharedPtr<FZoneEntry> Item, ESelectInfo::Type)
					{
						SelectedZone = Item.IsValid() ? Item->Folder : FString();
						SelectedZoneName = Item.IsValid() ? Item->Display() : FString();
					})
			]
		]

		// ── stages ──
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
			  [ Stage(LOCTEXT("Terrain", "Terrain"), &bImportTerrain) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
			  [ Stage(LOCTEXT("Objects", "Objects"), &bImportObjects) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
			  [ Stage(LOCTEXT("Entities", "NPCs / spawners / portals"), &bImportEntities) ]
			+ SHorizontalBox::Slot().AutoWidth()
			  [ Stage(LOCTEXT("Lighting", "Lighting"), &bAddLighting) ]
		]

		// ── level suffix + cleanup ──
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
			  [ SNew(STextBlock).Text(LOCTEXT("Suffix", "Level suffix")) ]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SEditableTextBox)
				.Text_Lambda([this]() { return FText::FromString(LevelSuffix); })
				.ToolTipText(LOCTEXT("SuffixTip",
					"Written as /Game/Maps/<ZONE>/L_<ZONE><suffix>.\n"
					"Empty (the default) writes L_<ZONE> directly, replacing the old level. "
					"Set a suffix to import side-by-side instead."))
				.OnTextCommitted_Lambda([this](const FText& T, ETextCommit::Type)
					{ LevelSuffix = T.ToString(); })
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SCheckBox)
				.ToolTipText(LOCTEXT("CleanTip",
					"After a SUCCESSFUL import, delete that zone's old glTF/Interchange assets "
					"under /Game/Maps/<ZONE>/Scene.\n\n"
					"Runs last and only on success, so nothing is removed before its "
					"replacement exists — but Content/ is not in git, so there is no undo."))
				.IsChecked_Lambda([this]() { return bDeleteLegacy ? ECheckBoxState::Checked : ECheckBoxState::Unchecked; })
				.OnCheckStateChanged_Lambda([this](ECheckBoxState S) { bDeleteLegacy = (S == ECheckBoxState::Checked); })
				[ SNew(STextBlock).Text(LOCTEXT("Clean", "Delete old /Scene assets after import")) ]
			]
		]

		// ── go ──
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 8)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.IsEnabled_Lambda([this]() { return !bBusy && !SelectedZone.IsEmpty(); })
				.Text_Lambda([this]()
				{
					return SelectedZoneName.IsEmpty()
						? LOCTEXT("PickZone", "Select a zone")
						: FText::FromString(FString::Printf(TEXT("Import %s"), *SelectedZoneName));
				})
				.OnClicked(this, &SRoseImportPanel::OnImportZone)
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6, 0, 0, 0)
			[
				SNew(SButton)
				.HAlign(HAlign_Center)
				.IsEnabled_Lambda([this]() { return !bBusy && Zones.Num() > 0; })
				.Text_Lambda([this]()
					{ return FText::FromString(FString::Printf(TEXT("Import All (%d)"), Zones.Num())); })
				.ToolTipText(LOCTEXT("ImportAllTip",
					"Import every zone in the list, one after another. Takes a while — "
					"leave the editor alone until the log says it is done."))
				.OnClicked(this, &SRoseImportPanel::OnImportAllZones)
			]
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0)[ SNew(SSeparator) ]

		// ── equipment ──
		// Not zone-scoped: these packs are global (LIST_WEAPON.ZSC etc.), so
		// they import once for the whole game, not per map.
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
		[ SNew(STextBlock).Text(LOCTEXT("EquipHeader", "Equipment (rigid slots — whole game, not per zone)")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
			  [ Stage(LOCTEXT("Weapons", "Weapons"), &bEquipWeapons) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
			  [ Stage(LOCTEXT("SubWpn", "Sub-weapons"), &bEquipSubWeapons) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
			  [ Stage(LOCTEXT("BackItems", "Back"), &bEquipBack) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
			  [ Stage(LOCTEXT("PatParts", "PAT"), &bEquipPat) ]
			+ SHorizontalBox::Slot().AutoWidth()
			  [ Stage(LOCTEXT("SkipExisting", "Skip existing"), &bEquipSkipExisting) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.IsEnabled_Lambda([this]() { return !bBusy; })
			.Text(LOCTEXT("ImportEquip", "Import Equipment"))
			.ToolTipText(LOCTEXT("ImportEquipTip",
				"Builds every weapon, sub-weapon, back item and PAT part from the ZSC packs "
				"into /Game/Rose/Equipment. Takes a couple of minutes for the full set."))
			.OnClicked(this, &SRoseImportPanel::OnImportEquipment)
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0)[ SNew(SSeparator) ]

		// ── characters / armour ──
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
		[ SNew(STextBlock).Text(LOCTEXT("CharHeader",
			"Characters & armour (skinned — whole game, not per zone)")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
			  [ Stage(LOCTEXT("CharF", "Female"), &bCharFemale) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 12, 0)
			  [ Stage(LOCTEXT("CharM", "Male"), &bCharMale) ]
			+ SHorizontalBox::Slot().AutoWidth()
			  [ Stage(LOCTEXT("CharSkip", "Skip existing"), &bCharSkipExisting) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
			  [ Stage(LOCTEXT("CharBody", "Body"), &bCharBody) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
			  [ Stage(LOCTEXT("CharArms", "Arms"), &bCharArms) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
			  [ Stage(LOCTEXT("CharFoot", "Foot"), &bCharFoot) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
			  [ Stage(LOCTEXT("CharCap", "Helmet/Hat"), &bCharCap) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
			  [ Stage(LOCTEXT("CharFace", "Face"), &bCharFace) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
			  [ Stage(LOCTEXT("CharHair", "Hair"), &bCharHair) ]
			+ SHorizontalBox::Slot().AutoWidth().Padding(0, 0, 10, 0)
			  [ Stage(LOCTEXT("CharMask", "Mask"), &bCharFaceItem) ]
			+ SHorizontalBox::Slot().AutoWidth()
			  [ Stage(LOCTEXT("CharAnims", "Animations"), &bCharAnims) ]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 4, 8, 8)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.IsEnabled_Lambda([this]() { return !bBusy; })
			.Text(LOCTEXT("ImportChars", "Import Characters & Armour"))
			.OnClicked(this, &SRoseImportPanel::OnImportCharacters)
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0)[ SNew(SSeparator) ]

		// ── data tables ──
		// STB -> UDataTable, straight into /Game/DataTables where the runtime
		// already looks.  Replaces the tools/gen_*.py scripts, which needed
		// py -3.9 and were the last manual step before a playable clone.
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 6, 8, 2)
		[ SNew(STextBlock).Text(LOCTEXT("TableHeader", "Data tables (STB → DataTable — whole game)")) ]
		+ SVerticalBox::Slot().AutoHeight().Padding(8, 2, 8, 8)
		[
			SNew(SButton)
			.HAlign(HAlign_Center)
			.IsEnabled_Lambda([this]() { return !bBusy; })
			.Text(LOCTEXT("ImportTables", "Import Data Tables & Skill Trees"))
			.ToolTipText(LOCTEXT("ImportTablesTip",
				"Regenerates /Game/DataTables from the STBs: skills, jobs (LIST_CLASS "
				"job sets) and skill points.\n\nDisplay names come from the matching "
				"STL, so this also needs LIST_*_S.STL beside the tables."))
			.OnClicked(this, &SRoseImportPanel::OnImportTables)
		]

		+ SVerticalBox::Slot().AutoHeight().Padding(8, 0, 8, 4)
		[ SNew(STextBlock).Text_Lambda([this]() { return FText::FromString(StatusText); }) ]

		// ── log ──
		+ SVerticalBox::Slot().FillHeight(1.f).Padding(8, 0, 8, 8)
		[
			SNew(SBorder)
			[
				SAssignNew(LogBox, SScrollBox)
				+ SScrollBox::Slot()[ SAssignNew(LogLines, SVerticalBox) ]
			]
		]
	];

	AppendLog(TEXT("Ready. Pick a zone and press Import."));
}

TSharedRef<ITableRow> SRoseImportPanel::MakeZoneRow(TSharedPtr<FZoneEntry> Item,
	const TSharedRef<STableViewBase>& OwnerTable)
{
	const FString Label = Item.IsValid() ? Item->Display() : FString();
	// The folder is the internal id — keep it reachable, out of the way.
	const FString Tip = Item.IsValid()
		? FString::Printf(TEXT("%s  (folder %s)"), *Item->Display(), *Item->Folder)
		: FString();

	return SNew(STableRow<TSharedPtr<FZoneEntry>>, OwnerTable)
		[
			SNew(STextBlock)
			.Text(FText::FromString(Label))
			.ToolTipText(FText::FromString(Tip))
		];
}

void SRoseImportPanel::RefreshZones()
{
	Zones.Reset();

	// Readable names come from LIST_ZONE.STB: game column 0 is the zone's name
	// ("Zant"), column 1 its .ZON path.  Match the row to the folder by the
	// .ZON basename, the same way the importer finds the deco/cnst packs.
	TMap<FString, FString> FolderToName;
	{
		FRoseSTB ZoneList_STB;
		if (ZoneList_STB.Load(FPaths::Combine(
			AssetRoot, TEXT("3DDATA"), TEXT("STB"), TEXT("LIST_ZONE.STB"))))
		{
			// Schema-bound: classic calls column 0 "Region Name" and column 1
			// "Zone Path", against Arua's "Name" and ".zon Path".
			const FRoseStbBinding Col = RoseStb::Bind(ZoneList_STB, RoseStb::ZoneSchema());

			for (int32 Row = 0; Row < ZoneList_STB.Rows; ++Row)
			{
				FString ZonPath = Col.Get(Row, TEXT("ZonPath"));
				if (ZonPath.IsEmpty())
					continue;
				ZonPath.ReplaceInline(TEXT("\\"), TEXT("/"));
				const FString Folder = FPaths::GetBaseFilename(ZonPath).ToUpper();
				const FString Name = Col.Get(Row, TEXT("Name")).TrimStartAndEnd();
				if (!Folder.IsEmpty() && !Name.IsEmpty())
					FolderToName.Add(Folder, Name);
			}
		}
	}

	const FString MapsRoot = FPaths::Combine(AssetRoot, TEXT("3DDATA"), TEXT("MAPS"));
	TArray<FString> Planets;
	IFileManager::Get().FindFiles(Planets, *(MapsRoot / TEXT("*")), false, true);

	for (const FString& Planet : Planets)
	{
		TArray<FString> ZoneDirs;
		IFileManager::Get().FindFiles(ZoneDirs, *(MapsRoot / Planet / TEXT("*")), false, true);
		for (const FString& Zone : ZoneDirs)
		{
			// A zone is a folder with a matching .ZON; everything else under
			// MAPS is shared art or leftovers.
			const FString Zon = MapsRoot / Planet / Zone / (Zone + TEXT(".ZON"));
			if (!IFileManager::Get().FileExists(*Zon))
				continue;

			TSharedPtr<FZoneEntry> Entry = MakeShared<FZoneEntry>();
			Entry->Folder = Zone.ToUpper();
			if (const FString* Name = FolderToName.Find(Entry->Folder))
				Entry->Name = *Name;
			Zones.Add(Entry);
		}
	}

	Zones.Sort([](const TSharedPtr<FZoneEntry>& A, const TSharedPtr<FZoneEntry>& B)
		{ return A->Display() < B->Display(); });
	if (ZoneList.IsValid())
		ZoneList->RequestListRefresh();
}

void SRoseImportPanel::AppendLog(const FString& Line, bool bError)
{
	if (!LogLines.IsValid())
		return;

	LogLines->AddSlot().AutoHeight()
	[
		SNew(STextBlock)
		.Text(FText::FromString(Line))
		.AutoWrapText(true)
		.ColorAndOpacity(bError ? FSlateColor(FLinearColor(1.f, 0.35f, 0.35f))
		                        : FSlateColor::UseForeground())
	];

	if (LogBox.IsValid())
		LogBox->ScrollToEnd();
}

SRoseImportPanel::FScopedNoModalDialogs::FScopedNoModalDialogs()
	: bPrevious(GIsRunningUnattendedScript)
{
	GIsRunningUnattendedScript = true;
}

SRoseImportPanel::FScopedNoModalDialogs::~FScopedNoModalDialogs()
{
	GIsRunningUnattendedScript = bPrevious;
}

void SRoseImportPanel::ClearLog()
{
	// "Import Everything" chains the individual stage handlers, each of which
	// starts by clearing the log.  Left alone that would throw away every
	// earlier stage's results and leave only the last one on screen.
	if (bChaining)
		return;
	if (LogLines.IsValid())
		LogLines->ClearChildren();
}

FReply SRoseImportPanel::OnBrowseAssetRoot()
{
	IDesktopPlatform* Desktop = FDesktopPlatformModule::Get();
	if (!Desktop)
		return FReply::Handled();

	FString Picked;
	const void* Window = FSlateApplication::Get().FindBestParentWindowHandleForDialogs(SharedThis(this));
	if (Desktop->OpenDirectoryDialog(Window, TEXT("Select the folder CONTAINING 3DDATA"),
		AssetRoot, Picked))
	{
		AssetRoot = Picked;
		RefreshZones();
	}
	return FReply::Handled();
}

FReply SRoseImportPanel::OnRefreshZones()
{
	RefreshZones();
	AppendLog(FString::Printf(TEXT("Rescanned: %d zones under %s"), Zones.Num(), *AssetRoot));
	return FReply::Handled();
}

bool SRoseImportPanel::IsTargetLevelOpen(const FString& LevelPackage, FString& OutReason) const
{
	if (!GEditor)
		return false;
	const UWorld* Editing = GEditor->GetEditorWorldContext().World();
	if (!Editing || !Editing->GetOutermost())
		return false;

	if (Editing->GetOutermost()->GetName() == LevelPackage)
	{
		OutReason = FString::Printf(
			TEXT("%s is open in the editor. Open a different level first — re-importing the ")
			TEXT("level you are editing would be overwritten by the editor's own copy on save."),
			*LevelPackage);
		return true;
	}
	return false;
}

FReply SRoseImportPanel::OnImportZone()
{
	if (bBusy || SelectedZone.IsEmpty())
		return FReply::Handled();

	RunZoneImport({ SelectedZone });
	return FReply::Handled();
}

FReply SRoseImportPanel::OnImportTables()
{
	if (bBusy)
		return FReply::Handled();

	FScopedNoModalDialogs NoDialogs;

	FRoseTableImportOptions Options;
	Options.AssetRoot = AssetRoot;

	ClearLog();
	AppendLog(TEXT("Importing data tables..."));
	StatusText = TEXT("Importing data tables...");
	bBusy = true;

	FRoseTableImportResult Result;
	RoseImportTables(Options, Result);

	bBusy = false;

	if (!Result.bSuccess)
	{
		AppendLog(TEXT("FAILED — see the Output Log (LogRoseImport)."), true);
		StatusText = TEXT("Table import failed.");
		return FReply::Handled();
	}

	for (const FRoseTablePackResult& Pack : Result.Packs)
	{
		AppendLog(FString::Printf(TEXT("  %-14s rows %5d / %5d   %s"),
			*Pack.Name, Pack.RowsOut, Pack.RowsIn,
			Pack.bSaved ? TEXT("saved") : TEXT("SAVE FAILED")), !Pack.bSaved);
	}
	if (Result.MissingNames > 0)
	{
		AppendLog(FString::Printf(
			TEXT("  %d rows had an STL key with no string — check the STL/language"),
			Result.MissingNames), true);
	}
	AppendLog(FString::Printf(TEXT("  %.2fs"), Result.SecondsTotal));

	StatusText = TEXT("Data tables done.");
	return FReply::Handled();
}

FReply SRoseImportPanel::OnImportEverything()
{
	if (bBusy)
		return FReply::Handled();

	// ORDER MATTERS.  Characters first: the ZMO stage binds animations to the
	// skeleton's bone tree, and that tree stays empty until a skinned mesh
	// populates it (USkeleton::RecreateBoneTree).  Zones last — they are by far
	// the longest leg, so a failure in the cheap stages surfaces early.
	ClearLog();
	AppendLog(TEXT("=== IMPORT EVERYTHING ==="));

	// Preflight.  This run takes hours; the two ways it can be pointlessly
	// wasted are a wrong 3DDATA root and a missing master material (every
	// texture would import and every material would come out unparented).
	// Both are one LoadObject to check, so check them now, not in hour three.
	{
		bool bBlocked = false;
		if (!FPaths::DirectoryExists(FPaths::Combine(AssetRoot, TEXT("3DDATA"))))
		{
			AppendLog(FString::Printf(
				TEXT("No 3DDATA folder under '%s' — set the asset root above."), *AssetRoot), true);
			bBlocked = true;
		}
		for (const TCHAR* Master : { TEXT("/Game/Atlas/M_RoseMaster.M_RoseMaster"),
									 TEXT("/Game/Atlas/M_RoseTerrain.M_RoseTerrain") })
		{
			if (!LoadObject<UMaterialInterface>(nullptr, Master))
			{
				AppendLog(FString::Printf(TEXT("Master material missing: %s"), Master), true);
				bBlocked = true;
			}
		}
		if (bBlocked)
		{
			AppendLog(TEXT("Aborted before doing any work."), true);
			StatusText = TEXT("Import blocked — see the log.");
			return FReply::Handled();
		}
	}

	TGuardValue<bool> Chaining(bChaining, true);

	// Every stage runs even if an earlier one reported failures: a broken pack
	// should not cost the user the other two hours of work, and each stage logs
	// its own result.
	//
	// Tables first — they are seconds, and everything downstream (item names,
	// skill ids, quest references) resolves against them.
	OnImportTables();
	OnImportCharacters();
	OnImportEquipment();

	if (Zones.Num() > 0)
	{
		TArray<FString> Queue;
		Queue.Reserve(Zones.Num());
		for (const TSharedPtr<FZoneEntry>& Z : Zones)
			if (Z.IsValid())
				Queue.Add(Z->Folder);
		RunZoneImport(Queue);
	}
	else
	{
		AppendLog(TEXT("No zones found — check the 3DDATA asset root."), true);
	}

	AppendLog(TEXT("=== IMPORT EVERYTHING done ==="));
	StatusText = TEXT("Full import complete.");
	return FReply::Handled();
}

FReply SRoseImportPanel::OnImportAllZones()
{
	if (bBusy || Zones.Num() == 0)
		return FReply::Handled();

	TArray<FString> Queue;
	Queue.Reserve(Zones.Num());
	for (const TSharedPtr<FZoneEntry>& Z : Zones)
		if (Z.IsValid())
			Queue.Add(Z->Folder);

	RunZoneImport(Queue);
	return FReply::Handled();
}

void SRoseImportPanel::RunZoneImport(const TArray<FString>& Queue)
{
	if (Queue.Num() == 0)
		return;

	FScopedNoModalDialogs NoDialogs;

	// Folder -> readable name, so the log reads like the list does.
	TMap<FString, FString> Names;
	for (const TSharedPtr<FZoneEntry>& Z : Zones)
		if (Z.IsValid())
			Names.Add(Z->Folder, Z->Display());

	FRoseMapImportOptions Options;
	Options.AssetRoot = AssetRoot;
	Options.LevelSuffix = LevelSuffix;
	Options.bImportObjects = bImportObjects;
	Options.bImportEntities = bImportEntities;
	Options.bAddLighting = bAddLighting;
	Options.bDeleteLegacyAssets = bDeleteLegacy;

	ClearLog();
	StatusText = TEXT("Importing...");
	bBusy = true;

	int32 Failures = 0;
	FRoseMapImportResult Result;
	{
		FScopedSlowTask Task((float)Queue.Num(), LOCTEXT("ZoneTask", "Importing ROSE zones"));
		Task.MakeDialog();

		for (const FString& Zone : Queue)
		{
			const FString LevelPackage = FString::Printf(
				TEXT("/Game/Maps/%s/L_%s%s"), *Zone, *Zone, *LevelSuffix);

			FString Reason;
			if (IsTargetLevelOpen(LevelPackage, Reason))
			{
				AppendLog(Reason, /*bError*/ true);
				++Failures;
				Task.EnterProgressFrame(1.f);
				continue;
			}

			Task.EnterProgressFrame(1.f, FText::FromString(
				FString::Printf(TEXT("Importing %s"), *Zone)));

			Options.Zone = Zone;
			FRoseMapImportResult ZoneResult;
			if (!RoseImportMap(Options, ZoneResult))
			{
				AppendLog(FString::Printf(TEXT("%s FAILED"), *Names.FindRef(Zone)), true);
				++Failures;
				continue;
			}

			if (Queue.Num() > 1)
			{
				AppendLog(FString::Printf(
					TEXT("  %-22s chunks %3d  objects %5d  npcs %3d  spawners %3d  "
					     "retired %4d  %.1fs"),
					*Names.FindRef(Zone), ZoneResult.ChunksLoaded, ZoneResult.ObjectActors,
					ZoneResult.NpcActors, ZoneResult.SpawnerActors,
					ZoneResult.LegacyAssetsDeleted, ZoneResult.SecondsTotal));
			}
			Result = ZoneResult;
		}
	}

	bBusy = false;

	if (Queue.Num() > 1)
	{
		StatusText = FString::Printf(TEXT("%d/%d zones imported"),
			Queue.Num() - Failures, Queue.Num());
		AppendLog(StatusText, Failures > 0);
		return;
	}

	if (Failures > 0 || !Result.bSuccess)
	{
		AppendLog(TEXT("FAILED — see the Output Log (LogRoseImport) for detail."), true);
		StatusText = TEXT("Import failed.");
		return;
	}

	AppendLog(FString::Printf(TEXT("OK  %s"), *Result.LevelPackage));
	AppendLog(FString::Printf(TEXT("  chunks %d   terrain meshes %d   actors %d"),
		Result.ChunksLoaded, Result.MeshesBuilt, Result.ActorsPlaced));
	AppendLog(FString::Printf(TEXT("  atlas %d tiles @ %dpx"), Result.AtlasTiles, Result.AtlasSize));
	AppendLog(FString::Printf(TEXT("  object parts %d (animated %d)"),
		Result.ObjectActors, Result.AnimatedParts));
	AppendLog(FString::Printf(TEXT("  unique %d meshes / %d textures / %d materials"),
		Result.UniqueMeshes, Result.UniqueTextures, Result.UniqueMaterials));
	AppendLog(FString::Printf(TEXT("  npcs %d   spawners %d   portals %d"),
		Result.NpcActors, Result.SpawnerActors, Result.PortalActors));
	if (Result.MissingAssets > 0)
	{
		AppendLog(FString::Printf(TEXT("  MISSING ASSETS: %d (see the Output Log)"),
			Result.MissingAssets), true);
	}
	if (Result.LegacyAssetsDeleted > 0)
		AppendLog(FString::Printf(TEXT("  retired %d old /Scene assets"), Result.LegacyAssetsDeleted));
	AppendLog(FString::Printf(TEXT("  %.2fs total  (parse %.2f, atlas %.2f, meshes %.2f, objects %.2f, save %.2f)"),
		Result.SecondsTotal, Result.SecondsParse, Result.SecondsAtlas,
		Result.SecondsMeshes, Result.SecondsObjects, Result.SecondsSave));

	StatusText = FString::Printf(TEXT("Done in %.1fs — %s"), Result.SecondsTotal, *Result.LevelPackage);
}

FReply SRoseImportPanel::OnImportEquipment()
{
	if (bBusy)
		return FReply::Handled();

	FScopedNoModalDialogs NoDialogs;

	FRoseEquipImportOptions Options;
	Options.AssetRoot = AssetRoot;
	Options.bWeapons = bEquipWeapons;
	Options.bSubWeapons = bEquipSubWeapons;
	Options.bBack = bEquipBack;
	Options.bPat = bEquipPat;
	Options.bSkipExisting = bEquipSkipExisting;

	ClearLog();
	AppendLog(TEXT("Importing equipment... (a few minutes for the full set)"));
	StatusText = TEXT("Importing equipment...");
	bBusy = true;

	FRoseEquipImportResult Result;
	{
		FScopedSlowTask Task(1.f, LOCTEXT("EquipTask", "Importing ROSE equipment"));
		Task.MakeDialog();
		RoseImportEquipment(Options, Result);
		Task.EnterProgressFrame(1.f);
	}

	bBusy = false;

	if (!Result.bSuccess)
	{
		AppendLog(TEXT("FAILED — see the Output Log (LogRoseImport)."), true);
		StatusText = TEXT("Equipment import failed.");
		return FReply::Handled();
	}

	for (const FRoseEquipPackResult& Pack : Result.Packs)
	{
		AppendLog(FString::Printf(
			TEXT("  %-8s built %5d   empty %5d   skipped %5d   failed %d"),
			*Pack.Kind, Pack.Built, Pack.Empty, Pack.Skipped, Pack.Failed));
	}
	AppendLog(FString::Printf(TEXT("  %d textures / %d materials"),
		Result.UniqueTextures, Result.UniqueMaterials));
	if (Result.MissingAssets > 0)
		AppendLog(FString::Printf(TEXT("  MISSING: %d"), Result.MissingAssets), true);
	AppendLog(FString::Printf(TEXT("  %.1fs total"), Result.SecondsTotal));

	StatusText = FString::Printf(TEXT("Equipment done in %.0fs"), Result.SecondsTotal);
	return FReply::Handled();
}

FReply SRoseImportPanel::OnImportCharacters()
{
	if (bBusy)
		return FReply::Handled();

	FScopedNoModalDialogs NoDialogs;

	FRoseSkeletalImportOptions Options;
	Options.AssetRoot = AssetRoot;
	Options.bFemale = bCharFemale;
	Options.bMale = bCharMale;
	Options.bBody = bCharBody;
	Options.bArms = bCharArms;
	Options.bFoot = bCharFoot;
	Options.bCap = bCharCap;
	Options.bFace = bCharFace;
	Options.bHair = bCharHair;
	Options.bFaceItem = bCharFaceItem;
	Options.bAnimations = bCharAnims;
	Options.bSkipExisting = bCharSkipExisting;

	ClearLog();
	AppendLog(TEXT("Importing characters and armour..."));
	StatusText = TEXT("Importing characters...");
	bBusy = true;

	FRoseSkeletalImportResult Result;
	{
		FScopedSlowTask Task(1.f, LOCTEXT("CharTask", "Importing ROSE characters"));
		Task.MakeDialog();
		RoseImportSkeletal(Options, Result);
		Task.EnterProgressFrame(1.f);
	}

	bBusy = false;

	if (!Result.bSuccess)
	{
		AppendLog(TEXT("FAILED — see the Output Log (LogRoseImport)."), true);
		StatusText = TEXT("Character import failed.");
		return FReply::Handled();
	}

	AppendLog(FString::Printf(TEXT("  skeletons %d  (F %d bones / M %d bones)"),
		Result.SkeletonsBuilt, Result.BonesFemale, Result.BonesMale));
	for (const FRoseSkeletalPackResult& Pack : Result.Packs)
	{
		AppendLog(FString::Printf(
			TEXT("  %-12s built %5d   empty %5d   skipped %4d   failed %4d"),
			*Pack.Kind, Pack.Built, Pack.Empty, Pack.Skipped, Pack.Failed));
	}
	if (bCharAnims)
	{
		AppendLog(FString::Printf(TEXT("  animations   built %5d   skipped %4d (vertex-morph)"),
			Result.AnimationsBuilt, Result.AnimationsSkippedMorph));
	}
	AppendLog(FString::Printf(TEXT("  %d textures / %d materials"),
		Result.UniqueTextures, Result.UniqueMaterials));
	if (Result.MissingAssets > 0)
		AppendLog(FString::Printf(TEXT("  MISSING: %d"), Result.MissingAssets), true);
	AppendLog(FString::Printf(TEXT("  %.1fs total"), Result.SecondsTotal));

	StatusText = FString::Printf(TEXT("Characters done in %.0fs"), Result.SecondsTotal);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
