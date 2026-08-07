#include "RoseCharSelectGameMode.h"
#include "RoseCharSelectPawn.h"
#include "RoseCharSelectHUD.h"

ARoseCharSelectGameMode::ARoseCharSelectGameMode()
{
	DefaultPawnClass = ARoseCharSelectPawn::StaticClass();
	HUDClass = ARoseCharSelectHUD::StaticClass();
}
