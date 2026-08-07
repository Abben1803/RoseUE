// ARoseCharSelectGameMode — game mode for L_CharacterSelect: a flying camera
// pawn (no gameplay character) + the character select/create HUD.  Set as the
// level's GameMode Override (the level builder does this).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RoseCharSelectGameMode.generated.h"

UCLASS()
class ROSEUE_API ARoseCharSelectGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARoseCharSelectGameMode();
};
