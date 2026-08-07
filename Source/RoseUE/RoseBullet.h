// ARoseBullet — a ROSE weapon bullet (CBulletDIRECTION, src/client/bullet.cpp):
// fired from the weapon at the attack's contact frame, homes on its target at
// the LIST_EFFECT row's speed (the client re-aims every frame — bullets always
// connect), and applies the pre-rolled damage on arrival together with the
// row's hit .EFT and hit sound.  The bullet visual is the row's bullet .EFT
// (critical variant when the roll crit) riding this actor.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoseBullet.generated.h"

class ARoseMonster;
class ARoseEffect;

UCLASS()
class ROSEUE_API ARoseBullet : public AActor
{
	GENERATED_BODY()

public:
	ARoseBullet();
	virtual void Tick(float DeltaSeconds) override;

	// WeaponEffectRow = LIST_EFFECT row (weapon BulletEffect column).
	// Damage < 0 = the attack roll missed (shows MISS on arrival).
	static ARoseBullet* Fire(UWorld* World, const FVector& Muzzle,
		ARoseMonster* Target, int32 WeaponEffectRow, int32 Damage, bool bCritical);

protected:
	TWeakObjectPtr<ARoseMonster> Target;
	UPROPERTY() ARoseEffect* TrailFX = nullptr;   // bullet .EFT riding along
	float Speed = 3000.f;      // cm/s (EFFECT_BULLET_SPEED)
	int32 Damage = -1;
	bool bCrit = false;
	int32 HitEffectId = 0;     // FILE_EFFECT row played on the victim
	int32 HitSoundId = 0;      // FILE_SOUND row
	float LifeGuard = 4.f;     // failsafe

	void Impact();
};
