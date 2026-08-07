// Faithful ROSE effect runtime — plays converted .EFT/.PTL effects
// (tools/build_effects.py → <Project>/Effects/eft_<id>.json + imported
// particle textures).  The simulation transcribes the client engine's
// zz_particle_event_sequence: emitters spawn quads at EmitRate with per-
// particle keyframed events (size/color/alpha/velocity/frame/rotation, with
// the engine's fade-interpolation semantics), D3D blend modes map to the
// additive/alpha particle materials, and sprite sheets play through the
// material's per-instance frame index.
//
// ARoseEffect::SpawnById(World, FILE_EFFECT row id, Location, Follow) is the
// entry point; it returns null when the effect isn't converted so callers can
// fall back to the generic ARoseSkillFX.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "RoseEffect.generated.h"

// ── Parsed effect data (mirrors the converter's JSON) ────────────────────────

struct FRoseFXEvent
{
	int32 Type = 0;          // EVENT_TYPE (zz_particle_event.h): 1 size .. 13 rotation
	float T0 = 0.f, T1 = 0.f;   // seconds into the particle's life (randomized)
	bool bFade = false;      // interpolate from the previous same-channel key
	TArray<float> Vals;      // payload: (min.., max..)
};

struct FRoseFXSequence
{
	FString Name;
	float LifeMin = 1.f, LifeMax = 1.f;       // particle life (s)
	float RateMin = 1.f, RateMax = 1.f;       // particles / s
	int32 Loops = 0;                          // >0: stop after Loops*Num spawns
	FVector SpawnDirMin = FVector::ZeroVector, SpawnDirMax = FVector::ZeroVector; // cm/s
	FVector RadiusMin = FVector::ZeroVector, RadiusMax = FVector::ZeroVector;     // cm
	FVector GravityMin = FVector::ZeroVector, GravityMax = FVector::ZeroVector;   // cm/s^2
	FString Texture;                          // /Game/Effects/Particles/<name>
	int32 NumParticles = 1;
	int32 Align = 0;                          // 0 billboard / 1 world / 2 z-axis
	int32 Coord = 1;                          // 0 world / 1,2 local
	int32 GridW = 1, GridH = 1;               // sprite sheet
	bool bAdditive = true;                    // D3D dest blend ONE vs INVSRCALPHA
	TArray<FRoseFXEvent> Events;
};

struct FRoseFXEmitterEntry
{
	FVector Pos = FVector::ZeroVector;        // UE cm, effect-local
	FRotator Rot = FRotator::ZeroRotator;
	float DelaySec = 0.f;
	bool bLink = true;                        // follows the effect root
	TArray<FRoseFXSequence> Sequences;
};

struct FRoseFXDef
{
	int32 Id = 0;
	FString Name;
	TArray<FRoseFXEmitterEntry> Emitters;
};

// Loads + caches eft_<id>.json from <ProjectDir>/Effects/.  Null = not converted.
const FRoseFXDef* RoseFX_GetDef(int32 EffectId);

// ── One PTL sequence = one instanced-quad emitter component ─────────────────

UCLASS()
class ROSEUE_API URoseParticleSeqComponent : public UInstancedStaticMeshComponent
{
	GENERATED_BODY()

public:
	URoseParticleSeqComponent();
	void Setup(const FRoseFXSequence& InSeq, float InDelaySec);
	virtual void TickComponent(float Dt, ELevelTick TickType,
		FActorComponentTickFunction* ThisTickFunction) override;
	bool IsFinished() const { return bFinished; }

protected:
	// A nailed keyframe: time + concrete (randomized) value.
	struct FKey { float Time; FVector4 Val; bool bFade; };
	// One live particle.
	struct FPart
	{
		bool bAlive = false;
		float Age = 0.f, Life = 1.f;
		FVector Pos = FVector::ZeroVector;    // local cm (world cm when Coord==0)
		FVector Vel = FVector::ZeroVector;    // cm/s (events may override axes)
		FVector Gravity = FVector::ZeroVector;
		FVector2D Size = FVector2D(10.f, 10.f);  // HALF extents, cm
		FLinearColor Color = FLinearColor::White;
		float Frame = 0.f;
		float AngleDeg = 0.f;
		bool bUseAngle = false;
		// Keyframes nailed at spawn, per channel.
		TArray<FKey, TInlineAllocator<8>> KSize, KColor, KVel, KFrame, KAngle;
	};

	FRoseFXSequence Seq;
	TArray<FPart> Parts;
	float Delay = 0.f;
	float EmitCarry = 0.f;
	int32 TotalSpawned = 0;
	bool bEmitting = true;
	bool bFinished = false;

	void SpawnParticle(FPart& P);
	static void NailChannel(TArray<FKey, TInlineAllocator<8>>& Out,
		const TArray<const FRoseFXEvent*>& Evs, int32 Dim, FRandomStream& Rand);
	static FVector4 SampleChannel(const TArray<FKey, TInlineAllocator<8>>& K,
		float Age, const FVector4& Default, bool* bFound = nullptr);
	void WriteInstances();
};

// ── The effect actor: one component per (EFT entry × PTL sequence) ──────────

UCLASS()
class ROSEUE_API ARoseEffect : public AActor
{
	GENERATED_BODY()

public:
	ARoseEffect();
	virtual void Tick(float Dt) override;

	// Spawn FILE_EFFECT row `EffectId` at Location; with Follow, linked
	// emitters track the actor (buff/cast effects).  Null = not converted.
	// bForceLink: treat every emitter as linked — status/buff loops must follow
	// the character (the classic client force-links them via New_EFFECT); the
	// EFT's own bLink=0 would leave a world-space particle trail while moving.
	static ARoseEffect* SpawnById(UWorld* World, int32 EffectId,
		const FVector& Location, AActor* Follow = nullptr,
		float MaxLifeSeconds = 8.f, bool bForceLink = false);

protected:
	UPROPERTY() TArray<URoseParticleSeqComponent*> Emitters;
	float MaxLife = 8.f;
	float Age = 0.f;
};
