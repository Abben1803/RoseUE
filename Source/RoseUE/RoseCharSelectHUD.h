// ARoseCharSelectHUD — builds the front-end Slate over the Title platform scene
// and puts the player controller into UI input mode (cursor shown).  The
// GameMode sets this as HUDClass.
//
// Two stages: the LOGIN screen first, then Character Select once the player is
// signed in.  With no backend configured the login screen offers "Play Offline"
// and Character Select falls back to the local save game, so single-player
// development needs no server.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RoseCharSelectHUD.generated.h"

UCLASS()
class ROSEUE_API ARoseCharSelectHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	// Called by the login screen once the player is authenticated (or has
	// chosen to play offline): tears the login form down and shows the roster.
	void ShowCharacterSelect();

private:
	void ClearRoot();
	TSharedPtr<class SWidget> Root;
};

// Built in RoseCharSelectUI.cpp / RoseLoginUI.cpp.
TSharedRef<class SWidget> RoseCharSelect_Make(class ARoseCharSelectHUD* HUD);
TSharedRef<class SWidget> RoseLogin_Make(class ARoseCharSelectHUD* HUD);
