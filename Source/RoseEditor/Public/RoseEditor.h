#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

ROSEEDITOR_API DECLARE_LOG_CATEGORY_EXTERN(LogRoseImport, Log, All);

class SDockTab;
class FSpawnTabArgs;   // declared as a CLASS in SlateCore

class FRoseEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

private:
	static TSharedRef<SDockTab> SpawnImportTab(const FSpawnTabArgs& Args);
	static void RegisterMenus();
};
