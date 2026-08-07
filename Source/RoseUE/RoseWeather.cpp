#include "RoseWeather.h"

#include "RoseSkyDome.h"
#include "RosePrecipitation.h"

#include "Components/AudioComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/LightComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/PlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Sound/SoundBase.h"

namespace
{
	// A planet's authored rules: a weighted pool of weathers, and whether its
	// zones read as night worlds.
	struct FRosePlanetRule
	{
		TArray<TPair<FString, float>> Pool;   // weather name -> weight
		float NightBias = 0.f;

		float TotalWeight() const
		{
			float S = 0.f;
			for (const auto& P : Pool)
				S += FMath::Max(0.f, P.Value);
			return S;
		}
	};

	struct FRoseWeatherTable
	{
		TMap<FString, FRoseWeatherState> ByName;
		TMap<FString, FString> ZoneDefault;   // ZONE -> weather name (an override)
		TMap<FString, FRosePlanetRule> Planets;
		bool bLoaded = false;
	};

	// Built in once so the system works on a fresh clone with no data file, and
	// so there is always something to fall back to when a name is unknown.
	// weather.json REPLACES any entry it names and adds any it does not.
	void SeedBuiltIns(FRoseWeatherTable& T)
	{
		auto Add = [&T](const TCHAR* Name, float Precip, float Fog, float Sun,
			FLinearColor Tint)
		{
			FRoseWeatherState W;
			W.Name = Name;
			W.Precipitation = Precip;
			W.FogDensityScale = Fog;
			W.SunScale = Sun;
			W.SkyTint = Tint;
			T.ByName.Add(FString(Name).ToLower(), W);
		};

		//   name       precip  fog   sun   tint
		Add(TEXT("clear"),   0.f, 1.0f, 1.00f, FLinearColor::White);
		Add(TEXT("cloudy"),  0.f, 1.4f, 0.75f, FLinearColor(0.82f, 0.84f, 0.88f));
		Add(TEXT("rain"),   0.6f, 2.5f, 0.45f, FLinearColor(0.62f, 0.66f, 0.74f));
		Add(TEXT("storm"),  1.0f, 4.0f, 0.28f, FLinearColor(0.44f, 0.48f, 0.58f));
		Add(TEXT("snow"),   0.5f, 2.2f, 0.70f, FLinearColor(0.86f, 0.90f, 0.98f));
		Add(TEXT("fog"),    0.f,  6.0f, 0.55f, FLinearColor(0.78f, 0.80f, 0.84f));
		Add(TEXT("sandstorm"), 0.8f, 5.0f, 0.50f, FLinearColor(1.00f, 0.82f, 0.55f));
	}

	void ParsePlanets(const TSharedPtr<FJsonObject>& Root, FRoseWeatherTable& T)
	{
		const TSharedPtr<FJsonObject>* Planets = nullptr;
		if (!Root->TryGetObjectField(TEXT("planets"), Planets) || !Planets)
			return;

		for (const auto& Pair : (*Planets)->Values)
		{
			const TSharedPtr<FJsonObject> O = Pair.Value->AsObject();
			if (!O.IsValid())
				continue;

			FRosePlanetRule Rule;
			O->TryGetNumberField(TEXT("nightBias"), Rule.NightBias);

			const TSharedPtr<FJsonObject>* Pool = nullptr;
			if (O->TryGetObjectField(TEXT("weathers"), Pool) && Pool)
			{
				for (const auto& W : (*Pool)->Values)
				{
					const float Weight = (float)W.Value->AsNumber();
					// Weight 0 means NEVER, so drop it here rather than letting a
					// zero-weight entry sit in the pool and be picked when every
					// other weight is also zero.
					if (Weight > 0.f)
						Rule.Pool.Emplace(FString(W.Key).ToLower(), Weight);
				}
			}
			T.Planets.Add(FString(Pair.Key).ToUpper(), MoveTemp(Rule));
		}
	}

	FRoseWeatherTable& Table()
	{
		static FRoseWeatherTable T;
		if (T.bLoaded)
			return T;
		T.bLoaded = true;

		SeedBuiltIns(T);

		FString Raw;
		const FString Path = FPaths::ProjectContentDir() / TEXT("Sky/weather.json");
		if (!FFileHelper::LoadFileToString(Raw, *Path))
		{
			UE_LOG(LogTemp, Log,
				TEXT("[Rose] weather: no %s — using %d built-in types"),
				*Path, T.ByName.Num());
			return T;
		}

		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
		if (!FJsonSerializer::Deserialize(R, Root) || !Root.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[Rose] weather: %s is not valid JSON"), *Path);
			return T;
		}

		const TArray<TSharedPtr<FJsonValue>>* Types = nullptr;
		if (Root->TryGetArrayField(TEXT("weathers"), Types) && Types)
		{
			for (const TSharedPtr<FJsonValue>& V : *Types)
			{
				const TSharedPtr<FJsonObject> O = V->AsObject();
				if (!O.IsValid())
					continue;

				FRoseWeatherState W;
				W.Name = O->GetStringField(TEXT("name"));
				if (W.Name.IsEmpty())
					continue;

				// Every field optional: a row may tweak one thing and inherit the
				// rest from the built-in of the same name.
				if (const FRoseWeatherState* Base = T.ByName.Find(W.Name.ToLower()))
					W = *Base;

				O->TryGetNumberField(TEXT("precipitation"), W.Precipitation);
				O->TryGetNumberField(TEXT("fogDensityScale"), W.FogDensityScale);
				O->TryGetNumberField(TEXT("sunScale"), W.SunScale);
				O->TryGetStringField(TEXT("ambientSound"), W.AmbientSound);
				O->TryGetStringField(TEXT("particleSystem"), W.ParticleSystem);

				const TArray<TSharedPtr<FJsonValue>>* Tint = nullptr;
				if (O->TryGetArrayField(TEXT("skyTint"), Tint) && Tint && Tint->Num() >= 3)
				{
					W.SkyTint = FLinearColor(
						(float)(*Tint)[0]->AsNumber(),
						(float)(*Tint)[1]->AsNumber(),
						(float)(*Tint)[2]->AsNumber());
				}

				W.Name = O->GetStringField(TEXT("name"));
				T.ByName.Add(W.Name.ToLower(), W);
			}
		}

		const TSharedPtr<FJsonObject>* Zones = nullptr;
		if (Root->TryGetObjectField(TEXT("zoneDefaults"), Zones) && Zones)
		{
			// FString(Pair.Key) first: in 5.8 the JSON object's key is a
			// TSharedString, which has no ToUpper of its own.
			for (const auto& Pair : (*Zones)->Values)
				T.ZoneDefault.Add(FString(Pair.Key).ToUpper(), Pair.Value->AsString());
		}

		ParsePlanets(Root, T);

		UE_LOG(LogTemp, Log,
			TEXT("[Rose] weather: %d type(s), %d planet rule(s), %d zone override(s)"),
			T.ByName.Num(), T.Planets.Num(), T.ZoneDefault.Num());
		return T;
	}
}

float URoseWeatherSubsystem::GetPlanetNightBias(const FString& Planet)
{
	const FRosePlanetRule* R = Table().Planets.Find(Planet.ToUpper());
	return R ? R->NightBias : 0.f;
}

void URoseWeatherSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);

	FString Map = FString(InWorld.GetMapName());
	Map.RemoveFromStart(InWorld.StreamingLevelsPrefix);
	// Same normalisation the dome uses — L_JPT01 must resolve to JPT01.
	Map = ARoseSkyDome::ZoneKeyFromMapName(Map);

	// Dedicated servers render nothing; spawning sky and rain there is pure cost
	// and would replicate transient cosmetics to every client.
	if (InWorld.GetNetMode() != NM_DedicatedServer)
	{
		EnsureSkyDome();
		EnsurePrecip();
	}

	ApplyZoneDefault(Map);
}

TArray<FString> URoseWeatherSubsystem::GetWeatherNames() const
{
	TArray<FString> Out;
	for (const auto& Pair : Table().ByName)
		Out.Add(Pair.Value.Name);
	Out.Sort();
	return Out;
}

void URoseWeatherSubsystem::ApplyZoneDefault(const FString& Zone)
{
	const FString Key = Zone.ToUpper();

	// A per-zone entry is an explicit decision and wins outright.
	if (const FString* Name = Table().ZoneDefault.Find(Key))
	{
		SetWeather(*Name, 0.f);
		Current = Target;
		return;
	}

	// Otherwise roll the PLANET's pool.  Junon gets rain and fog, Luna snow,
	// Eldeon rain only, Oro sand — the rules live in weather.json, and the
	// planet comes from the ZON path recorded by the sky import, so a zone with
	// no planet letter in its name (SKTOWN, KCHURCH) still resolves correctly.
	const FString Planet = ARoseSkyDome::GetZonePlanet(Key);
	const FRosePlanetRule* Rule = Table().Planets.Find(Planet);

	FString Picked = TEXT("clear");
	if (Rule && Rule->Pool.Num() > 0)
	{
		const float Total = Rule->TotalWeight();
		if (Total > 0.f)
		{
			float Roll = FMath::FRandRange(0.f, Total);
			for (const auto& P : Rule->Pool)
			{
				Roll -= FMath::Max(0.f, P.Value);
				if (Roll <= 0.f)
				{
					Picked = P.Key;
					break;
				}
			}
		}
	}
	else if (!Planet.IsEmpty())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[Rose] weather: planet '%s' has no rule — defaulting to clear"), *Planet);
	}

	UE_LOG(LogTemp, Log, TEXT("[Rose] weather: %s (%s) opens %s"),
		*Key, *Planet, *Picked);

	// Snap, don't blend: this is the weather the zone opens with, not a change.
	SetWeather(Picked, 0.f);
	Current = Target;
}

bool URoseWeatherSubsystem::SetWeather(const FString& Name, float BlendSeconds)
{
	const FRoseWeatherState* W = Table().ByName.Find(Name.ToLower());
	if (!W)
	{
		// Loud, and lists what IS valid — a typo must not silently clear the sky.
		UE_LOG(LogTemp, Warning,
			TEXT("[Rose] weather: unknown type '%s'; known: %s"),
			*Name, *FString::Join(GetWeatherNames(), TEXT(", ")));
		return false;
	}

	Target = *W;
	BlendRate = BlendSeconds > 0.f ? (1.f / BlendSeconds) : 0.f;

	UE_LOG(LogTemp, Log, TEXT("[Rose] weather -> %s (over %.1fs)"), *Target.Name, BlendSeconds);
	return true;
}

void URoseWeatherSubsystem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// Ease the numeric parameters toward the target so weather ARRIVES rather
	// than snapping. Names swap immediately; only the look interpolates.
	if (BlendRate <= 0.f)
	{
		Current = Target;
	}
	else
	{
		const float A = FMath::Clamp(DeltaSeconds * BlendRate, 0.f, 1.f);
		Current.Name = Target.Name;
		Current.Precipitation = FMath::Lerp(Current.Precipitation, Target.Precipitation, A);
		Current.FogDensityScale = FMath::Lerp(Current.FogDensityScale, Target.FogDensityScale, A);
		Current.SunScale = FMath::Lerp(Current.SunScale, Target.SunScale, A);
		Current.SkyTint = FMath::Lerp(Current.SkyTint, Target.SkyTint, A);
		Current.AmbientSound = Target.AmbientSound;
		Current.ParticleSystem = Target.ParticleSystem;
	}

	ApplyToScene();
	UpdatePrecip();
}

void URoseWeatherSubsystem::ApplyToScene()
{
	UWorld* W = GetWorld();
	if (!W)
		return;

	// Sky tint goes through the dome, which owns the material instance.  The
	// dome also owns the day/night blend, so weather DARKENS the sky rather
	// than replacing whatever time of day it is.
	for (TActorIterator<ARoseSkyDome> It(W); It; ++It)
	{
		It->SetSkyTint(Current.SkyTint);
		break;
	}

	// THE SUN AND FOG ARE NOT TOUCHED HERE.
	//
	// They have exactly ONE writer, ARoseSkyDome::ApplySceneLighting, which
	// reads GetSunScale()/GetFogScale() below and folds weather into the
	// time-of-day value in a single assignment.
	//
	// This used to set them too, and that is what made the sky "always sunny":
	// the dome dimmed the sun for dusk, weather ticked afterwards and assigned
	// Base * SunScale — 1.0 for clear — stamping full daylight back over it
	// every single frame.  Two writers to one absolute value is a race that the
	// later ticker always wins, no matter how the multiplication is phrased.
	//
	// Scaling from a remembered base (rather than the live value) is still
	// essential and now lives in the dome: reading the current value and
	// multiplying compounds it every frame, fogging a level solid in a second.
}

float URoseWeatherSubsystem::GetSunScale() const
{
	return Current.SunScale;
}

float URoseWeatherSubsystem::GetFogScale() const
{
	return Current.FogDensityScale;
}

// ── the actors weather needs, spawned rather than saved ────────────────────
//
// Neither the dome nor the rain shell is stored in a level.  They are cosmetics
// that ride the camera, so saving them into all 53 maps would mean re-importing
// every one to change them, and a stale copy would survive any re-import that
// missed it.  Spawning at BeginPlay keeps one source of truth in code.

void URoseWeatherSubsystem::EnsureSkyDome()
{
	UWorld* W = GetWorld();
	if (!W)
		return;

	for (TActorIterator<ARoseSkyDome> It(W); It; ++It)
		return;   // a level that already has one keeps it

	FActorSpawnParameters P;
	P.ObjectFlags = RF_Transient;   // never saved into the level
	if (ARoseSkyDome* Dome = W->SpawnActor<ARoseSkyDome>(
		ARoseSkyDome::StaticClass(), FTransform::Identity, P))
	{
		Dome->SetActorLabel(TEXT("RoseSkyDome"));
	}
}

void URoseWeatherSubsystem::EnsurePrecip()
{
	UWorld* W = GetWorld();
	if (!W || PrecipActor)
		return;

	FActorSpawnParameters P;
	P.ObjectFlags = RF_Transient;
	ARosePrecipitation* A = W->SpawnActor<ARosePrecipitation>(
		ARosePrecipitation::StaticClass(), FTransform::Identity, P);
	if (!A)
		return;

	A->SetActorLabel(TEXT("RosePrecipitation"));
	PrecipActor = A;
}

void URoseWeatherSubsystem::UpdatePrecip()
{
	ARosePrecipitation* A = Cast<ARosePrecipitation>(PrecipActor);
	if (!A)
		return;

	// Name -> kind.  Anything with no particle form (fog, cloudy, clear) simply
	// resolves to None and the field empties itself.
	ERosePrecipKind Kind = ERosePrecipKind::None;
	if (Current.Precipitation > 0.001f)
	{
		if (Current.Name.Equals(TEXT("snow"), ESearchCase::IgnoreCase))
			Kind = ERosePrecipKind::Snow;
		else if (Current.Name.Equals(TEXT("sandstorm"), ESearchCase::IgnoreCase))
			Kind = ERosePrecipKind::Sand;
		else
			Kind = ERosePrecipKind::Rain;
	}

	// Sand is dust-coloured, not sky-coloured; rain and snow take the weather
	// tint so they sit in whatever light the time of day is giving.
	const FLinearColor Colour = (Kind == ERosePrecipKind::Sand)
		? FLinearColor(0.86f, 0.72f, 0.48f)
		: Current.SkyTint;

	A->Configure(Kind, Current.Precipitation, Colour);
}

// ── console commands ───────────────────────────────────────────────────────

static void RoseWeatherCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World)
		return;
	URoseWeatherSubsystem* S = World->GetSubsystem<URoseWeatherSubsystem>();
	if (!S)
		return;

	if (Args.Num() == 0)
	{
		UE_LOG(LogTemp, Display, TEXT("[Rose] weather is '%s'; available: %s"),
			*S->GetWeather(), *FString::Join(S->GetWeatherNames(), TEXT(", ")));
		return;
	}
	S->SetWeather(Args[0], Args.Num() > 1 ? FCString::Atof(*Args[1]) : 4.f);
}

static FAutoConsoleCommandWithWorldAndArgs GRoseWeatherCmd(
	TEXT("rose.Weather"),
	TEXT("rose.Weather [name] [blendSeconds] - set weather; no args lists them"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(RoseWeatherCmd));

static void RoseTimeScaleCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World || Args.Num() == 0)
		return;
	const float Scale = FCString::Atof(*Args[0]);
	for (TActorIterator<ARoseSkyDome> It(World); It; ++It)
	{
		It->TimeScale = Scale;
		UE_LOG(LogTemp, Display, TEXT("[Rose] sky: TimeScale %.2f (day is now %.0f s)"),
			Scale, It->GetDayLengthSeconds() / FMath::Max(0.01f, Scale));
	}
}

static FAutoConsoleCommandWithWorldAndArgs GRoseTimeScaleCmd(
	TEXT("rose.TimeScale"),
	TEXT("rose.TimeScale <n> - speed up the day/night cycle (1 = ROSE speed)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(RoseTimeScaleCmd));

static void RoseTimeOfDayCmd(const TArray<FString>& Args, UWorld* World)
{
	if (!World || Args.Num() == 0)
		return;
	const float Unit = FCString::Atof(*Args[0]);
	for (TActorIterator<ARoseSkyDome> It(World); It; ++It)
	{
		It->ZoneTime = Unit;
		UE_LOG(LogTemp, Display, TEXT("[Rose] sky: zone time %.1f units"), Unit);
	}
}

static FAutoConsoleCommandWithWorldAndArgs GRoseTimeOfDayCmd(
	TEXT("rose.TimeOfDay"),
	TEXT("rose.TimeOfDay <unit> - jump to a zone time (JPT01: 11 = dawn, 112 = dusk, 128 = night)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(RoseTimeOfDayCmd));
