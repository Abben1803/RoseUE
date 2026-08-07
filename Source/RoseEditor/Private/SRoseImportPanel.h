// The ROSE Importer panel — Tools -> ROSE Importer.
//
// Everything the commandlet does, driven from inside the editor.  The
// commandlet stays: it is what a batch run over all 39 zones uses.  This is for
// the one-zone-at-a-time loop, where launching a headless editor to re-import a
// single map costs more than the import itself.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SEditableTextBox;
class SVerticalBox;
class SScrollBox;

class SRoseImportPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseImportPanel) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	static const FName TabId;

private:
	// ── settings ──
	FString AssetRoot = TEXT("C:/QQ-iROSE Online/extracted");
	FString SelectedZone;       // folder id, e.g. JPT01
	FString SelectedZoneName;   // readable name, e.g. Zant
	// Empty = write L_<ZONE> directly.  This importer is the base path now.
	FString LevelSuffix;
	// Retire /Game/Maps/<ZONE>/Scene after a successful import of that zone.
	bool bDeleteLegacy = false;

	bool bImportTerrain = true;
	bool bImportObjects = true;
	bool bImportEntities = true;
	bool bAddLighting = true;

	// ── equipment ──
	bool bEquipWeapons = true;
	bool bEquipSubWeapons = true;
	bool bEquipBack = true;
	bool bEquipPat = true;
	bool bEquipSkipExisting = false;

	// ── characters / armour (skinned) ──
	bool bCharFemale = true;
	bool bCharMale = true;
	bool bCharBody = true;
	bool bCharArms = true;
	bool bCharFoot = true;
	bool bCharCap = true;
	bool bCharFace = true;
	bool bCharHair = true;
	bool bCharFaceItem = true;
	// MOTION/AVATAR/*.ZMO -> UAnimSequence.  Needs at least one skinned part in
	// the same run (or from an earlier one): the skeleton's bone tree is built
	// from a mesh, and an animation cannot bind to an empty skeleton.
	bool bCharAnims = true;
	bool bCharSkipExisting = false;

	// True while "Import Everything" is driving the individual stage handlers,
	// so their ClearLog() calls do not wipe each other's results.
	bool bChaining = false;

	// RAII: suppress modal message dialogs for the duration of an import.
	//
	// A batch import must never stop to ask a question.  One failing save per
	// asset means thousands of Cancel/Retry/Continue prompts, which is not a
	// choice anyone can meaningfully make — and answering them is not how a
	// problem gets fixed, the log is.  With GIsRunningUnattendedScript set,
	// FMessageDialog::Open returns the default and logs instead of blocking.
	struct FScopedNoModalDialogs
	{
		FScopedNoModalDialogs();
		~FScopedNoModalDialogs();
	private:
		bool bPrevious = false;
	};

	// A zone is its folder (JPT01) plus the readable name from
	// LIST_ZONE.STB (Zant).  The list shows the NAME — the folder is an
	// internal id, not something to pick from.
	struct FZoneEntry
	{
		FString Folder;
		FString Name;
		FString Display() const { return Name.IsEmpty() ? Folder : Name; }
	};

	TArray<TSharedPtr<FZoneEntry>> Zones;
	TSharedPtr<class SListView<TSharedPtr<FZoneEntry>>> ZoneList;
	TSharedPtr<SScrollBox> LogBox;
	TSharedPtr<SVerticalBox> LogLines;
	FString StatusText;
	bool bBusy = false;

	// Walk <AssetRoot>/3DDATA/MAPS/<planet>/<zone> for anything with a .ZON.
	void RefreshZones();

	void AppendLog(const FString& Line, bool bError = false);
	void ClearLog();

	FReply OnBrowseAssetRoot();
	FReply OnRefreshZones();
	FReply OnImportZone();
	FReply OnImportAllZones();
	FReply OnImportEquipment();
	FReply OnImportCharacters();
	FReply OnImportTables();
	// One click, bare clone -> playable content: characters + equipment + every
	// zone, in dependency order.
	FReply OnImportEverything();

	// Shared by the single-zone and all-zones buttons.
	void RunZoneImport(const TArray<FString>& Folders);

	// Refuses to touch the level the editor currently has open — re-importing
	// it underneath the editor is how content gets corrupted, and Content/ is
	// not in git.
	bool IsTargetLevelOpen(const FString& LevelPackage, FString& OutReason) const;

	TSharedRef<class ITableRow> MakeZoneRow(TSharedPtr<FZoneEntry> Item,
		const TSharedRef<class STableViewBase>& OwnerTable);
};
