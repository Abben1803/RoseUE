// ARoseMobHUD — screen-space overhead plates for monsters: name + level and an
// HP bar drawn over every living mob near the camera (replaces the old
// TextRenderComponent text).  The player's click-target gets a bigger,
// gold-rimmed plate with the exact HP numbers.  Set as the HUDClass by
// ARoseGameMode.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "RoseMobHUD.generated.h"

class ARoseMonster;

UCLASS()
class ROSEUE_API ARoseMobHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;

	// Plates draw for mobs within this range of the camera (cm).
	UPROPERTY(EditAnywhere, Category = "Rose|UI") float PlateDrawDistance = 4000.f;

protected:
	void DrawMobPlate(ARoseMonster* Mob, bool bIsTarget);
};
