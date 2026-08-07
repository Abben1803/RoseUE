#include "RoseBullet.h"

#include "RoseEffect.h"
#include "RoseMonster.h"
#include "RoseSoundData.h"
#include "RoseWeaponEffectData.h"

#include "Engine/World.h"

ARoseBullet::ARoseBullet()
{
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

ARoseBullet* ARoseBullet::Fire(UWorld* World, const FVector& Muzzle,
	ARoseMonster* InTarget, int32 WeaponEffectRow, int32 InDamage, bool bInCrit)
{
	const FRoseWeaponEffectRow* Row = RoseGetWeaponEffect(WeaponEffectRow);
	if (!World || !InTarget || !Row)
		return nullptr;

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ARoseBullet* B = World->SpawnActor<ARoseBullet>(
		ARoseBullet::StaticClass(), Muzzle, FRotator::ZeroRotator, SP);
	if (!B)
		return nullptr;

	B->Target = InTarget;
	B->Damage = InDamage;
	B->bCrit = bInCrit;
	B->Speed = FMath::Max(500.f, (float)Row->BulletSpeed);
	// Critical rolls use the critical bullet/hit effects when authored.
	const int32 BulletFx = (bInCrit && Row->BulletCritical > 0) ? Row->BulletCritical : Row->BulletNormal;
	B->HitEffectId = (bInCrit && Row->HitCritical > 0) ? Row->HitCritical : Row->HitNormal;
	B->HitSoundId = Row->HitSound;

	if (Row->FireSound > 0)
		RosePlaySoundId(World, Row->FireSound, Muzzle);
	// The bullet's own .EFT rides this actor (endless-loop emitters — we
	// destroy it explicitly at impact so nothing lingers mid-air).
	B->TrailFX = ARoseEffect::SpawnById(World, BulletFx, Muzzle, B, /*MaxLife*/ 10.f);
	return B;
}

void ARoseBullet::Impact()
{
	if (ARoseMonster* M = Target.Get())
	{
		if (Damage < 0)
			M->ShowMiss();
		else
			M->ApplyHit((float)Damage,
				(M->GetActorLocation() - GetActorLocation()).GetSafeNormal2D(), bCrit);
		ARoseEffect::SpawnById(GetWorld(), HitEffectId, M->GetActorLocation(), M);
		if (HitSoundId > 0)
			RosePlaySoundId(GetWorld(), HitSoundId, M->GetActorLocation());
	}
	if (TrailFX)
		TrailFX->Destroy();
	Destroy();
}

void ARoseBullet::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	LifeGuard -= DeltaSeconds;
	ARoseMonster* M = Target.Get();
	if (!M || LifeGuard <= 0.f)
	{
		if (TrailFX)
			TrailFX->Destroy();
		Destroy();
		return;
	}

	// Home on the target's chest (Cal_MoveVEC re-aims every frame → 100% hit).
	const FVector Aim = M->GetActorLocation() + FVector(0, 0, 40.f);
	const FVector To = Aim - GetActorLocation();
	const float Step = Speed * DeltaSeconds;
	if (To.Size() <= FMath::Max(Step, 60.f))
	{
		Impact();
		return;
	}
	SetActorLocation(GetActorLocation() + To.GetSafeNormal() * Step);
	SetActorRotation(To.GetSafeNormal().Rotation());
}
