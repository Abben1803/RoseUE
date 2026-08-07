// The ROSE sky dome, day and night.
//
// Faithful to src/client/cskydome.cpp: ONE dome mesh the camera sits inside,
// with two textures (day and night) blended by a single ratio.  The dome is
// excluded from fog and rides with the viewer so it never gets closer.
//
// The zone picks its LIST_SKY row through LIST_ZONE's 'Sky' column; the mapping
// and the day-length columns come from Content/Sky/sky.json, written by
// -run=RoseImportSky.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoseSkyDome.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

UCLASS()
class ROSEUE_API ARoseSkyDome : public AActor
{
	GENERATED_BODY()

public:
	ARoseSkyDome();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	// 0 = full day, 1 = full night.  Set directly to freeze a time of day.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rose|Sky",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SkyBlend = 0.f;

	// ROSE's own clock, not an invented one.
	//
	// CDayNNightProc runs on a zone time that wraps at LIST_ZONE's "Day Period"
	// and compares against four boundaries in the same units (JPT01: period 160,
	// morning 0, day 11, evening 112, night 128).  DAYENVIR_TICK is 10000 ms and
	// the processor scales by (zoneTime - phaseStart) * 10000, so ONE UNIT IS
	// TEN SECONDS — which makes JPT01's day 160 * 10 = 1600s, about 27 minutes.
	// That is a real, playable day length, so it is the default rather than
	// something to override.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rose|Sky",
		meta = (ClampMin = "0.0"))
	float SecondsPerUnit = 10.f;

	// Multiplier on the clock, for looking at dusk without waiting for it.
	// 1 = ROSE speed. 0 freezes the sky wherever SkyBlend currently is.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rose|Sky",
		meta = (ClampMin = "0.0"))
	float TimeScale = 1.f;

	// Where in the zone's day to start, in zone units. Negative = start at the
	// zone's morning boundary.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rose|Sky")
	float StartUnit = -1.f;

	// Current zone time, in the zone's own units. Read to know the hour.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Rose|Sky")
	float ZoneTime = 0.f;

	// Drive the scene's fog and sun from the cycle too, not just the dome.
	// CDayNNightProc::SetGlobalIllumination lerps fog between a day and a night
	// colour; without this the sky changes and the world underneath does not.
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Rose|Sky")
	bool bDriveSceneLighting = true;

	UFUNCTION(BlueprintPure, Category = "Rose|Sky")
	bool IsNight() const { return SkyBlend > 0.5f; }

	// One full day in real seconds at TimeScale 1.
	UFUNCTION(BlueprintPure, Category = "Rose|Sky")
	float GetDayLengthSeconds() const { return DayPeriod * SecondsPerUnit; }

	// Swap to the sky a zone asks for.  Safe to call repeatedly with the same
	// zone; it only rebuilds when the row actually changes.
	UFUNCTION(BlueprintCallable, Category = "Rose|Sky")
	void ApplyZone(const FString& Zone);

	UFUNCTION(BlueprintCallable, Category = "Rose|Sky")
	void SetSkyRow(int32 SkyRow);

	// Weather tints the dome through the SAME material instance the day/night
	// blend drives, so overcast darkens whatever time of day it currently is
	// instead of replacing it.
	UFUNCTION(BlueprintCallable, Category = "Rose|Sky")
	void SetSkyTint(FLinearColor Tint);

	// Which planet a zone belongs to (JUNON/LUNAR/ELDEON/ORO/...), from the ZON
	// path recorded in sky.json.  Static because the weather system needs it
	// before any dome exists, and both read the same imported manifest.
	UFUNCTION(BlueprintPure, Category = "Rose|Sky")
	static FString GetZonePlanet(const FString& Zone);

	// Map/level name -> the zone key used by sky.json and weather.json.
	//
	// A level is named L_<ZONE> (RoseImportMap prefixes it) and in PIE it also
	// carries a UEDPIE_N_ prefix, while the manifests are keyed on the bare zone
	// (JPT01).  Both have to come off or every lookup misses — which is exactly
	// what froze the day cycle and pinned every zone to clear weather.
	UFUNCTION(BlueprintPure, Category = "Rose|Sky")
	static FString ZoneKeyFromMapName(const FString& MapName);

	// The zone this dome resolved to, for diagnostics.
	UFUNCTION(BlueprintPure, Category = "Rose|Sky")
	FString GetZone() const { return CurrentZone; }

private:
	UPROPERTY(VisibleAnywhere, Category = "Rose|Sky")
	TObjectPtr<UStaticMeshComponent> Dome;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DomeMID;

	int32 CurrentRow = INDEX_NONE;
	FString CurrentZone;
	FLinearColor SkyTint = FLinearColor::White;

	// The zone's day-cycle boundaries, in zone units, from sky.json.
	float DayPeriod = 0.f;      // 0 = this zone never changes (interiors)
	float MorningUnit = 0.f;
	float DayUnit = 0.f;
	float EveningUnit = 0.f;
	float NightUnit = 0.f;

	// Cached so the per-frame lighting drive does not iterate the level.
	TWeakObjectPtr<class ADirectionalLight> SunLight;
	TWeakObjectPtr<class AExponentialHeightFog> FogActor;
	TWeakObjectPtr<class ASkyLight> SkyLight;
	// Learned from the level's own values on first use; -1 = not yet learned.
	// Captured ONCE — reading them back each frame would read our own output.
	float SunIntensityDay = -1.f;
	float SkyLightDay = -1.f;
	float BaseFogDensity = -1.f;

	void PushBlend();
	float BlendForZoneTime(float Units) const;
	void ApplySceneLighting();
};
