#include "RoseSkyDome.h"

#include "RoseWeather.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "Components/SkyLightComponent.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/SkyLight.h"
#include "EngineUtils.h"
#include "Components/StaticMeshComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	struct FRoseSkyRow
	{
		FString Material;
		FString Mesh;
	};

	// A zone's day-cycle boundaries, straight out of LIST_ZONE.
	struct FRoseZoneDay
	{
		int32 SkyRow = 0;
		float Period = 0.f;
		float Morning = 0.f;
		float Day = 0.f;
		float Evening = 0.f;
		float Night = 0.f;
		int32 Weather = 0;
		FString Planet;
	};

	struct FRoseSkyManifest
	{
		TMap<int32, FRoseSkyRow> Rows;
		TMap<FString, FRoseZoneDay> Zones;
		bool bLoaded = false;
	};

	// Parsed once. sky.json is written by -run=RoseImportSky.
	FRoseSkyManifest& Manifest()
	{
		static FRoseSkyManifest M;
		if (M.bLoaded)
			return M;
		M.bLoaded = true;

		FString Raw;
		const FString Path = FPaths::ProjectContentDir() / TEXT("Sky/sky.json");
		if (!FFileHelper::LoadFileToString(Raw, *Path))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Rose] sky: %s missing — run -run=RoseImportSky"), *Path);
			return M;
		}

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
		if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[Rose] sky: %s is not valid JSON"), *Path);
			return M;
		}

		const TArray<TSharedPtr<FJsonValue>>* Skies = nullptr;
		if (Root->TryGetArrayField(TEXT("skies"), Skies) && Skies)
		{
			for (const TSharedPtr<FJsonValue>& V : *Skies)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				if (!O.IsValid())
					continue;
				FRoseSkyRow Row;
				Row.Material = O->GetStringField(TEXT("material"));
				Row.Mesh = O->GetStringField(TEXT("mesh"));
				M.Rows.Add((int32)O->GetNumberField(TEXT("row")), MoveTemp(Row));
			}
		}

		const TArray<TSharedPtr<FJsonValue>>* Zones = nullptr;
		if (Root->TryGetArrayField(TEXT("zones"), Zones) && Zones)
		{
			for (const TSharedPtr<FJsonValue>& V : *Zones)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				if (!O.IsValid())
					continue;
				FRoseZoneDay D;
				D.SkyRow  = (int32)O->GetNumberField(TEXT("sky"));
				D.Period  = (float)O->GetNumberField(TEXT("dayCycle"));
				D.Morning = (float)O->GetNumberField(TEXT("morning"));
				D.Day     = (float)O->GetNumberField(TEXT("day"));
				D.Evening = (float)O->GetNumberField(TEXT("evening"));
				D.Night   = (float)O->GetNumberField(TEXT("night"));
				D.Weather = (int32)O->GetNumberField(TEXT("weather"));
				D.Planet  = O->GetStringField(TEXT("planet")).ToUpper();
				M.Zones.Add(O->GetStringField(TEXT("zone")).ToUpper(), D);
			}
		}

		UE_LOG(LogTemp, Log, TEXT("[Rose] sky: %d row(s), %d zone(s)"),
			M.Rows.Num(), M.Zones.Num());
		return M;
	}
}

ARoseSkyDome::ARoseSkyDome()
{
	PrimaryActorTick.bCanEverTick = true;
	// Tick AFTER the camera has moved, or the dome lags a frame behind and the
	// horizon visibly swims when the player turns.
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	Dome = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Dome"));
	SetRootComponent(Dome);

	// The dome is scenery, not geometry: nothing may collide with it, it must
	// not cast or receive shadows, and it must never be picked by the
	// click-to-move trace (which is why collision is disabled outright rather
	// than set to a channel).
	Dome->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Dome->SetCastShadow(false);
	Dome->bReceivesDecals = false;
	Dome->SetCanEverAffectNavigation(false);

	// Drawn behind everything.  The dome is a finite mesh, so without this a
	// tall object near the far plane can poke through it.
	// A full-screen unlit dome is the WORST possible emissive light source: it
	// surrounds the camera, so Lumen would light the entire world with the sky
	// texture and wash out every shadow.  Off, like every other unlit surface.
	Dome->SetEmissiveLightSource(false);

	Dome->bRenderInDepthPass = false;
	Dome->SetTranslucentSortPriority(-1000);

	// ROSE scales the dome to sit outside the view; the ZMS is authored small.
	Dome->SetRelativeScale3D(FVector(100.f));
}

void ARoseSkyDome::BeginPlay()
{
	Super::BeginPlay();

	// Pick up the zone the level was imported as.  GetMapName carries the PIE
	// prefix ("UEDPIE_0_JPT01"), which would never match the manifest.
	FString Map = FString(GetWorld()->GetMapName());
	Map.RemoveFromStart(GetWorld()->StreamingLevelsPrefix);
	ApplyZone(ZoneKeyFromMapName(Map));
}

void ARoseSkyDome::SetSkyRow(int32 SkyRow)
{
	if (SkyRow == CurrentRow)
		return;

	const FRoseSkyRow* Row = Manifest().Rows.Find(SkyRow);
	if (!Row)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Rose] sky: no row %d in the manifest"), SkyRow);
		return;
	}

	if (UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, *Row->Mesh))
		Dome->SetStaticMesh(Mesh);
	else
		UE_LOG(LogTemp, Warning, TEXT("[Rose] sky: dome mesh %s missing"), *Row->Mesh);

	if (UMaterialInterface* Mat = LoadObject<UMaterialInterface>(nullptr, *Row->Material))
	{
		DomeMID = UMaterialInstanceDynamic::Create(Mat, this);
		// Every section, not just slot 0 — the dome ZMS is one piece today but a
		// multi-section dome would otherwise render half-skinned.
		const int32 Num = Dome->GetNumMaterials();
		for (int32 i = 0; i < Num; ++i)
			Dome->SetMaterial(i, DomeMID);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[Rose] sky: material %s missing"), *Row->Material);
	}

	CurrentRow = SkyRow;
	PushBlend();
}

FString ARoseSkyDome::ZoneKeyFromMapName(const FString& MapName)
{
	FString Key = MapName;

	// PIE prefix first ("UEDPIE_0_L_JPT01"), then the level prefix.  Order
	// matters: the L_ is INSIDE the PIE prefix, not before it.
	int32 Underscore = INDEX_NONE;
	if (Key.StartsWith(TEXT("UEDPIE_")) && Key.FindLastChar(TEXT('_'), Underscore))
	{
		// UEDPIE_<n>_<name> — take everything after the second underscore.
		FString Rest;
		if (Key.Split(TEXT("_"), nullptr, &Rest))          // drop "UEDPIE"
			if (Rest.Split(TEXT("_"), nullptr, &Rest))      // drop the instance number
				Key = Rest;
	}

	Key.RemoveFromStart(TEXT("L_"), ESearchCase::IgnoreCase);
	return Key.ToUpper();
}

FString ARoseSkyDome::GetZonePlanet(const FString& Zone)
{
	if (const FRoseZoneDay* D = Manifest().Zones.Find(Zone.ToUpper()))
		return D->Planet;
	return FString();
}

void ARoseSkyDome::ApplyZone(const FString& Zone)
{
	const FString Key = Zone.ToUpper();
	if (Key == CurrentZone)
		return;
	CurrentZone = Key;

	if (const FRoseZoneDay* D = Manifest().Zones.Find(Key))
	{
		DayPeriod   = D->Period;
		MorningUnit = D->Morning;
		DayUnit     = D->Day;
		EveningUnit = D->Evening;
		NightUnit   = D->Night;
		SetSkyRow(D->SkyRow);

		// Start at the zone's DAY boundary, not its morning one.
		//
		// "Morning" is where dawn BEGINS, and at that unit the blend is still
		// fully night — JPT01's morning is unit 0, which is midnight, so opening
		// there means opening in the dark and waiting 110 s for sunrise.  The
		// day boundary (unit 11) is the first fully-lit moment.
		//
		// nightBias shifts that: Eldeon is authored as a night world, so its
		// zones open at the night boundary instead of midday.  The bias comes
		// from weather.json's planet block, which is where every other authored
		// per-planet decision lives.
		const float Bias = URoseWeatherSubsystem::GetPlanetNightBias(D->Planet);
		const float DefaultStart = (Bias >= 0.5f) ? NightUnit : DayUnit;
		ZoneTime = StartUnit >= 0.f ? StartUnit : DefaultStart;

		UE_LOG(LogTemp, Log,
			TEXT("[Rose] sky: %s -> row %d, day period %.0f units (%.0f s), "
			     "morning %.0f day %.0f evening %.0f night %.0f"),
			*Key, D->SkyRow, DayPeriod, DayPeriod * SecondsPerUnit,
			MorningUnit, DayUnit, EveningUnit, NightUnit);
	}
	else
	{
		// Sky 0 is JUNON day/night — the sensible default for a zone the table
		// does not list, and far better than an unlit black dome.
		// WARNING, not Log.  This is never normal: it means the level name did
		// not resolve to a zone, which silently freezes the day cycle at period
		// 0 and leaves the planet blank so weather can never roll anything but
		// clear.  Both systems then look "broken" with nothing in the log to say
		// why — which is exactly what happened with L_JPT01.
		UE_LOG(LogTemp, Warning,
			TEXT("[Rose] sky: zone '%s' is NOT in sky.json — day/night and weather "
			     "are DISABLED here. Expected a key like JPT01; run -run=RoseImportSky "
			     "if the manifest is stale."), *Key);
		DayPeriod = 0.f;
		SetSkyRow(0);
	}
}

// ROSE's four phases, from CDayNNightProc.
//
//   morning [Morning, Day)     night -> day, the blend ramps 1 -> 0
//   day     [Day, Evening)     full day,   blend 0
//   evening [Evening, Night)   day -> night, the blend ramps 0 -> 1
//   night   [Night, Period)    full night, blend 1
//
// The processor holds a binary DN_DAY/DN_NIGHT state and drives the ratio one
// way or the other (setSkyMaterialBlendRatio(ratio) by day,
// (1 - ratio) by night); expressing it as a single 0..1 night-ness is the same
// curve without the state machine.
//
// The boundaries are NOT evenly spaced and must not be assumed so: JPT01's day
// runs 11..112 (101 units) while dusk is 112..128 (16 units), so dusk is a
// short event inside a long afternoon — which is exactly what makes it read as
// sunset rather than a permanent crossfade.
float ARoseSkyDome::BlendForZoneTime(float Units) const
{
	if (DayPeriod <= 0.f)
		return SkyBlend;      // interiors and dungeons: whatever it was set to

	const float T = FMath::Fmod(FMath::Max(0.f, Units), DayPeriod);

	// A zone with a degenerate ramp (evening == night, seen on the PvP arenas)
	// must not divide by zero — it simply snaps instead of fading.
	auto Ramp = [](float V, float A, float B)
	{
		return (B > A) ? FMath::Clamp((V - A) / (B - A), 0.f, 1.f) : 1.f;
	};

	if (T < MorningUnit)              return 1.f;                       // pre-dawn
	if (T < DayUnit)                  return 1.f - Ramp(T, MorningUnit, DayUnit);
	if (T < EveningUnit)              return 0.f;                       // daytime
	if (T < NightUnit)                return Ramp(T, EveningUnit, NightUnit);
	return 1.f;                                                          // night
}

void ARoseSkyDome::SetSkyTint(FLinearColor Tint)
{
	SkyTint = Tint;
	if (DomeMID)
		DomeMID->SetVectorParameterValue(TEXT("SkyTint"), SkyTint);
}

void ARoseSkyDome::PushBlend()
{
	if (DomeMID)
	{
		DomeMID->SetScalarParameterValue(TEXT("SkyBlend"), SkyBlend);
		DomeMID->SetVectorParameterValue(TEXT("SkyTint"), SkyTint);
	}
}

void ARoseSkyDome::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Ride with the viewer so the sky never gets nearer.  Position only — the
	// dome must NOT inherit camera rotation or the stars would turn with the
	// player instead of staying put.
	if (const APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (const APlayerCameraManager* Cam = PC->PlayerCameraManager)
			SetActorLocation(Cam->GetCameraLocation());
	}

	// Advance ROSE's clock.  SecondsPerUnit is the engine's DAYENVIR_TICK
	// (10 s per zone unit); TimeScale is ours, for not waiting 20 minutes to
	// look at dusk.
	if (DayPeriod > 0.f && SecondsPerUnit > 0.f && TimeScale > 0.f)
	{
		ZoneTime = FMath::Fmod(
			ZoneTime + (DeltaSeconds * TimeScale) / SecondsPerUnit, DayPeriod);
		SkyBlend = BlendForZoneTime(ZoneTime);
	}

	PushBlend();

	if (bDriveSceneLighting)
		ApplySceneLighting();
}

// The world under the sky, not just the sky.
//
// CDayNNightProc::SetGlobalIllumination lerps the FOG colour between a day
// value (200,200,200) and a night one (10,10,10) alongside the sky blend — so
// in ROSE the whole scene darkens, not only the dome.  Driving the sun's
// intensity as well is ours: ROSE is unlit and has no sun to dim, but UE's
// terrain and objects are lit, and leaving a full-strength sun up at midnight
// is what makes a "night" that is merely a dark sky over a sunlit town.
void ARoseSkyDome::ApplySceneLighting()
{
	const float Night = FMath::Clamp(SkyBlend, 0.f, 1.f);

	if (!SunLight.IsValid() || !FogActor.IsValid() || !SkyLight.IsValid())
	{
		// Cached, because TActorIterator over a town-sized level every frame is
		// not free. Re-resolved only while something is missing.
		for (TActorIterator<ADirectionalLight> It(GetWorld()); It; ++It)
		{
			SunLight = *It;
			break;
		}
		for (TActorIterator<AExponentialHeightFog> It(GetWorld()); It; ++It)
		{
			FogActor = *It;
			break;
		}
		for (TActorIterator<ASkyLight> It(GetWorld()); It; ++It)
		{
			SkyLight = *It;
			break;
		}
	}

	// Weather folds in HERE, as a multiplier, so the sun and fog are written
	// once per frame from one place.  Two writers to an absolute value is a
	// race the later ticker wins — that bug read as a sky that was permanently
	// sunny no matter what the clock said.
	float WeatherSun = 1.f;
	float WeatherFog = 1.f;
	if (const UWorld* World = GetWorld())
	{
		if (const URoseWeatherSubsystem* Weather = World->GetSubsystem<URoseWeatherSubsystem>())
		{
			WeatherSun = Weather->GetSunScale();
			WeatherFog = Weather->GetFogScale();
		}
	}

	if (ADirectionalLight* Sun = SunLight.Get())
	{
		if (ULightComponent* L = Sun->GetLightComponent())
		{
			// Learn the level's own key light ONCE.  Reading it every frame
			// would read back our own output and compound it.
			if (SunIntensityDay < 0.f)
				SunIntensityDay = L->Intensity;

			// Night bottoms out at 2% rather than 5%: at 5% of a daylight sun
			// the town was still legible enough to read as overcast noon.
			const float TimeOfDay = FMath::Lerp(1.f, 0.02f, Night);
			L->SetIntensity(SunIntensityDay * TimeOfDay * WeatherSun);

			// Warm at noon, cold at midnight — the cheapest cue that reads as
			// time of day on lit geometry.
			L->SetLightColor(FMath::Lerp(
				FLinearColor(1.f, 0.96f, 0.88f), FLinearColor(0.35f, 0.45f, 0.85f), Night));
		}
	}

	if (AExponentialHeightFog* Fog = FogActor.Get())
	{
		if (UExponentialHeightFogComponent* C = Fog->GetComponent())
		{
			if (BaseFogDensity < 0.f)
				BaseFogDensity = C->FogDensity;
			C->SetFogDensity(BaseFogDensity * WeatherFog);

			// ROSE's own two fog colours, /255 (CDayNNightProc::s_FogColor).
			C->SetFogInscatteringColor(FMath::Lerp(
				FLinearColor(200.f / 255.f, 200.f / 255.f, 200.f / 255.f),
				FLinearColor(10.f / 255.f, 10.f / 255.f, 10.f / 255.f), Night));
		}
	}

	// The SKY LIGHT is the other half of "it still looks like day".
	//
	// A real-time-capture sky light keeps filling the scene with bright ambient
	// from the captured sky, so dimming only the directional light leaves the
	// world evenly lit and shadowless — bright, flat, and unmistakably noon.
	if (ASkyLight* SkyL = SkyLight.Get())
	{
		if (USkyLightComponent* C = SkyL->GetLightComponent())
		{
			if (SkyLightDay < 0.f)
				SkyLightDay = C->Intensity;
			C->SetIntensity(SkyLightDay * FMath::Lerp(1.f, 0.08f, Night) * WeatherSun);
		}
	}
}
