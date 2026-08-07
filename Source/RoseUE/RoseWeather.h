// Weather — OURS, not ROSE's.
//
// ROSE's own weather is a single integer per zone (LIST_ZONE 'Weather', measured
// 0 = clear in every zone we ship) handed to a Lua hook, SE_WeatherEffect, which
// plays a character-attached particle effect.  There is no intensity, no
// transition, no per-weather scene state — nothing worth reproducing.
//
// So this is a small system of our own.  A weather is a named bundle of scene
// parameters; ROSE's zone integer is used only to pick a sensible DEFAULT for a
// zone, and anything can override it at runtime.  Add a weather by adding a row
// to Content/Sky/weather.json — no code change, no recompile.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "RoseWeather.generated.h"

class UAudioComponent;

USTRUCT(BlueprintType)
struct FRoseWeatherState
{
	GENERATED_BODY()

	// Name it is referenced by: "clear", "rain", "storm", "snow", "fog".
	UPROPERTY(BlueprintReadWrite, Category = "Rose|Weather")
	FString Name = TEXT("clear");

	// 0..1. Drives particle spawn rate and the wet look.
	UPROPERTY(BlueprintReadWrite, Category = "Rose|Weather")
	float Precipitation = 0.f;

	// Multiplies the scene's fog density; 1 = leave the level's own value.
	UPROPERTY(BlueprintReadWrite, Category = "Rose|Weather")
	float FogDensityScale = 1.f;

	// Multiplies the sun. Overcast skies are dimmer, not just wetter.
	UPROPERTY(BlueprintReadWrite, Category = "Rose|Weather")
	float SunScale = 1.f;

	// Tints the sky dome through M_RoseSky's SkyTint. Grey for overcast.
	UPROPERTY(BlueprintReadWrite, Category = "Rose|Weather")
	FLinearColor SkyTint = FLinearColor::White;

	// Looping ambient sound while this weather is active. Empty = silent.
	UPROPERTY(BlueprintReadWrite, Category = "Rose|Weather")
	FString AmbientSound;

	// Optional Niagara system for the precipitation itself. Empty = none, and
	// the weather still works (tint, fog, sun and sound all apply) — so rain is
	// usable before anyone authors a particle asset for it.
	UPROPERTY(BlueprintReadWrite, Category = "Rose|Weather")
	FString ParticleSystem;
};

UCLASS()
class ROSEUE_API URoseWeatherSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void OnWorldBeginPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual TStatId GetStatId() const override
	{
		RETURN_QUICK_DECLARE_CYCLE_STAT(URoseWeatherSubsystem, STATGROUP_Tickables);
	}

	// Change weather by name. Unknown names are refused loudly rather than
	// silently falling back to clear.
	UFUNCTION(BlueprintCallable, Category = "Rose|Weather")
	bool SetWeather(const FString& Name, float BlendSeconds = 4.f);

	UFUNCTION(BlueprintPure, Category = "Rose|Weather")
	FString GetWeather() const { return Target.Name; }

	// What this zone starts with, from weather.json's zone defaults.
	UFUNCTION(BlueprintCallable, Category = "Rose|Weather")
	void ApplyZoneDefault(const FString& Zone);

	UFUNCTION(BlueprintPure, Category = "Rose|Weather")
	TArray<FString> GetWeatherNames() const;

	// 0..1 — how much this planet reads as a night world.  Eldeon is 1, so its
	// zones open at night.  Lives here rather than in sky.json because it is an
	// AUTHORED decision, not something imported from ROSE.
	static float GetPlanetNightBias(const FString& Planet);

	// Read by ARoseSkyDome, which is the SINGLE writer of the sun, sky light and
	// fog.  Weather never assigns them itself — see ApplyToScene.
	float GetSunScale() const;
	float GetFogScale() const;

private:
	FRoseWeatherState Current;   // what is on screen right now
	FRoseWeatherState Target;    // what we are blending toward
	float BlendRate = 0.25f;     // 1/seconds

	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> Ambient;

	// The precipitation shell: a cylinder locked to the camera, drawn from the
	// inside with M_RosePrecip.  Spawned on demand and kept hidden while the
	// weather is dry, so a clear sky costs nothing.
	UPROPERTY(Transient)
	TObjectPtr<class ARosePrecipitation> PrecipActor;

	// Ensures the level has a sky dome and a precipitation shell.  Spawned at
	// runtime rather than saved into all 53 levels: they are pure cosmetics that
	// follow the camera, so a level has no business storing them, and a re-import
	// cannot then leave a stale one behind.
	void EnsureSkyDome();
	void EnsurePrecip();
	void UpdatePrecip();

	// Captured ONCE from the level's authored values, so weather scales from a
	// fixed base instead of compounding its own output. -1 = not yet captured.
	float BaseFogDensity = -1.f;
	float BaseSunIntensity = -1.f;

	void ApplyToScene();
};
