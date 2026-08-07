// ARoseSkillFX — lightweight code-driven skill visuals: a short-lived emissive
// translucent shape (engine sphere + /Game/Effects/M_RoseFX, created by
// tools/ue5_make_fx_assets.py) animated per style.  This is the GENERIC visual
// backend — the skill row's CastEffect/HitEffect ids (FILE_EFFECT.STB → .EFT)
// are already in the DataTable, so a faithful EFT/PTL renderer can replace the
// styles here without touching the call sites in URoseSkillComponent.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoseSkillFX.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UENUM()
enum class ERoseFXStyle : uint8
{
	CastBurst,   // at the caster as the motion starts (swells + fades)
	HitBurst,    // at each victim on impact (fast pop)
	BuffAura,    // rises up the caster (heals/buffs)
	GroundRing,  // expanding flat shockwave (area skills)
};

UCLASS()
class ROSEUE_API ARoseSkillFX : public AActor
{
	GENERATED_BODY()

public:
	ARoseSkillFX();
	virtual void Tick(float DeltaSeconds) override;

	// One-call spawn; returns null if the world (or the FX material) is absent —
	// effects are strictly cosmetic and must never break a cast.
	static ARoseSkillFX* Spawn(UWorld* World, const FVector& Location, ERoseFXStyle Style,
		const FLinearColor& Color, float Scale = 1.f, float Lifetime = 0.6f,
		AActor* FollowActor = nullptr);

	// Color language for the generic visuals: magic = blue, weapon = orange,
	// unarmed = gold, buffs/heals = green.
	static FLinearColor ColorForDamageType(int32 DamageType);
	static FLinearColor BuffColor() { return FLinearColor(0.25f, 1.f, 0.35f); }

protected:
	void Init(ERoseFXStyle InStyle, const FLinearColor& InColor, float InScale,
		float InLifetime, AActor* InFollow);

	UPROPERTY() UStaticMeshComponent* Mesh = nullptr;
	UPROPERTY() UMaterialInstanceDynamic* MID = nullptr;
	TWeakObjectPtr<AActor> Follow;   // BuffAura tracks its actor

	ERoseFXStyle Style = ERoseFXStyle::CastBurst;
	FLinearColor Color = FLinearColor::White;
	float BaseScale = 1.f;
	float Lifetime = 0.6f;
	float Age = 0.f;
	float FollowZ = 0.f;   // height above the followed actor's origin
};
