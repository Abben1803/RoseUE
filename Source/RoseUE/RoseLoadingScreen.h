// Between-zone loading screen (shown by the MoviePlayer during OpenLevel).
// URoseGameInstance::ShowLoadingScreen builds one of these just before travel.
#pragma once

#include "CoreMinimal.h"

class SWidget;

// A full-screen ROSE-themed loading widget for the given (already prettified)
// zone name.  Built in RoseLoadingScreen.cpp.
TSharedRef<SWidget> RoseLoadingScreen_Make(const FString& ZoneName);
