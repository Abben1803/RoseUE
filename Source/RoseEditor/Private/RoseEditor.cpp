#include "RoseEditor.h"

#include "SRoseImportPanel.h"

#include "Framework/Docking/TabManager.h"
#include "Styling/AppStyle.h"
#include "ToolMenus.h"
#include "Widgets/Docking/SDockTab.h"
#include "WorkspaceMenuStructure.h"
#include "WorkspaceMenuStructureModule.h"

#define LOCTEXT_NAMESPACE "RoseEditor"

DEFINE_LOG_CATEGORY(LogRoseImport);

void FRoseEditorModule::StartupModule()
{
	// Register the dockable tab, then add a Tools menu entry that summons it.
	// UToolMenus is not necessarily ready at module startup, so the menu is
	// registered through the startup callback.
	FGlobalTabmanager::Get()
		->RegisterNomadTabSpawner(SRoseImportPanel::TabId,
			FOnSpawnTab::CreateStatic(&FRoseEditorModule::SpawnImportTab))
		.SetDisplayName(LOCTEXT("TabTitle", "ROSE Importer"))
		.SetTooltipText(LOCTEXT("TabTooltip",
			"Import ROSE map data straight into the project — no glTF, no Interchange."))
		.SetGroup(WorkspaceMenu::GetMenuStructure().GetLevelEditorCategory())
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	UToolMenus::RegisterStartupCallback(
		FSimpleMulticastDelegate::FDelegate::CreateStatic(&FRoseEditorModule::RegisterMenus));
}

void FRoseEditorModule::ShutdownModule()
{
	UToolMenus::UnRegisterStartupCallback(this);
	UToolMenus::UnregisterOwner(this);
	FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(SRoseImportPanel::TabId);
}

TSharedRef<SDockTab> FRoseEditorModule::SpawnImportTab(const FSpawnTabArgs& Args)
{
	return SNew(SDockTab)
		.TabRole(ETabRole::NomadTab)
		[ SNew(SRoseImportPanel) ];
}

void FRoseEditorModule::RegisterMenus()
{
	FToolMenuOwnerScoped OwnerScoped(TEXT("RoseEditor"));

	UToolMenu* ToolsMenu = UToolMenus::Get()->ExtendMenu(TEXT("LevelEditor.MainMenu.Tools"));
	if (!ToolsMenu)
		return;

	FToolMenuSection& Section =
		ToolsMenu->FindOrAddSection(TEXT("Rose"), LOCTEXT("RoseSection", "ROSE"));

	Section.AddMenuEntry(
		TEXT("RoseImporter"),
		LOCTEXT("MenuLabel", "ROSE Importer"),
		LOCTEXT("MenuTooltip", "Import ROSE zones, objects and entities into this project."),
		FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"),
		FUIAction(FExecuteAction::CreateLambda([]()
		{
			FGlobalTabmanager::Get()->TryInvokeTab(SRoseImportPanel::TabId);
		})));
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FRoseEditorModule, RoseEditor);
