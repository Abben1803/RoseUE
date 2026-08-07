#include "RoseAuditLevelCommandlet.h"

#include "RoseEditor.h"

#include "Components/LightComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/DirectionalLight.h"
#include "Engine/Level.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"

URoseAuditLevelCommandlet::URoseAuditLevelCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URoseAuditLevelCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	const FString* LevelPath = ParamsMap.Find(TEXT("level"));
	if (!LevelPath)
	{
		UE_LOG(LogRoseImport, Error, TEXT("need -level=/Game/Maps/<Z>/L_<Z>"));
		return 1;
	}

	UWorld* World = LoadObject<UWorld>(nullptr, **LevelPath);
	if (!World)
	{
		UE_LOG(LogRoseImport, Error, TEXT("cannot load %s"), **LevelPath);
		return 1;
	}
	World->WorldType = EWorldType::Editor;

	UE_LOG(LogRoseImport, Display, TEXT("=== %s ==="), **LevelPath);

	int32 Total = 0, Lamps = 0, Water = 0, WaterZone = 0, PPV = 0;
	TMap<FString, int32> ByClass;

	// Walk PersistentLevel->Actors, NOT TActorIterator.
	//
	// TActorIterator needs an INITIALISED world; a world obtained with
	// LoadObject in a commandlet is not, so the iterator yields nothing and the
	// level looks empty when it is fully populated.  That reads as "the importer
	// produced nothing" and is purely an artifact of how it was opened.
	ULevel* Level = World->PersistentLevel;
	if (!Level)
	{
		UE_LOG(LogRoseImport, Error, TEXT("no PersistentLevel"));
		return 1;
	}

	for (AActor* A : Level->Actors)
	{
		if (!A)
			continue;
		++Total;

		const FString Cls = A->GetClass()->GetName();
		ByClass.FindOrAdd(Cls)++;

		if (A->Tags.Contains(FName(TEXT("RoseLamp"))))  ++Lamps;
		if (A->Tags.Contains(FName(TEXT("RoseWater")))) ++Water;
		if (Cls.Contains(TEXT("WaterZone")))            ++WaterZone;

		// The exposure volume is the one that decides whether the ground reads
		// as ground or as white paper.  Report its actual stored settings, not
		// that it merely exists.
		if (APostProcessVolume* V = Cast<APostProcessVolume>(A))
		{
			++PPV;
			const FPostProcessSettings& S = V->Settings;
			UE_LOG(LogRoseImport, Display,
				TEXT("  PostProcessVolume '%s': unbound=%d priority=%.0f blend=%.2f"),
				*V->GetActorLabel(), V->bUnbound ? 1 : 0, V->Priority, V->BlendWeight);
			UE_LOG(LogRoseImport, Display,
				TEXT("     bias override=%d value=%.2f | min override=%d value=%.2f | max override=%d value=%.2f"),
				S.bOverride_AutoExposureBias ? 1 : 0, S.AutoExposureBias,
				S.bOverride_AutoExposureMinBrightness ? 1 : 0, S.AutoExposureMinBrightness,
				S.bOverride_AutoExposureMaxBrightness ? 1 : 0, S.AutoExposureMaxBrightness);
			UE_LOG(LogRoseImport, Display,
				TEXT("     method override=%d method=%d"),
				S.bOverride_AutoExposureMethod ? 1 : 0, (int32)S.AutoExposureMethod);
		}

		if (ADirectionalLight* Sun = Cast<ADirectionalLight>(A))
		{
			UE_LOG(LogRoseImport, Display, TEXT("  Sun '%s': intensity %.2f casts=%d"),
				*Sun->GetActorLabel(), Sun->GetLightComponent()->Intensity,
				Sun->GetLightComponent()->CastShadows ? 1 : 0);
		}
		if (ASkyLight* Sky = Cast<ASkyLight>(A))
		{
			UE_LOG(LogRoseImport, Display, TEXT("  SkyLight: intensity %.2f"),
				Sky->GetLightComponent()->Intensity);
		}
	}

	UE_LOG(LogRoseImport, Display,
		TEXT("actors %d | lamps %d | water bodies %d | water zones %d | post-process volumes %d"),
		Total, Lamps, Water, WaterZone, PPV);

	if (PPV == 0)
		UE_LOG(LogRoseImport, Error, TEXT("NO PostProcessVolume — auto-exposure is unclamped"));
	if (Water == 0)
		UE_LOG(LogRoseImport, Warning, TEXT("NO water actors in this level"));

	// Terrain material: shading model and how much lightmap is being applied.
	for (AActor* Act : Level->Actors)
	{
		AStaticMeshActor* A = Cast<AStaticMeshActor>(Act);
		if (!A || !A->GetActorLabel().Contains(TEXT("Terrain")))
			continue;

		UMaterialInterface* MI = A->GetStaticMeshComponent()->GetMaterial(0);
		if (!MI)
			continue;

		float LmScale = -1.f;
		if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(MI))
			MIC->GetScalarParameterValue(FMaterialParameterInfo(TEXT("LightmapScale")), LmScale);

		UMaterial* Base = MI->GetMaterial();
		UE_LOG(LogRoseImport, Display,
			TEXT("  terrain MI '%s' base '%s' shading=%d LightmapScale=%.2f"),
			*MI->GetName(), Base ? *Base->GetName() : TEXT("?"),
			Base ? (int32)Base->GetShadingModels().GetFirstShadingModel() : -1, LmScale);
		break;   // one sample is enough; they all share the master
	}

	// Top actor classes, so a missing category is obvious at a glance.
	ByClass.ValueSort([](int32 A, int32 B) { return A > B; });
	int32 Shown = 0;
	for (const TPair<FString, int32>& P : ByClass)
	{
		UE_LOG(LogRoseImport, Display, TEXT("  %-40s %d"), *P.Key, P.Value);
		if (++Shown >= 12)
			break;
	}

	return 0;
}
