#include "RoseWaterBuilder.h"
#include "WaterSubsystem.h"

#include "RoseEditor.h"
#include "RoseMapImporter.h"
#include "RoseObjectFormats.h"

#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "WaterBodyActor.h"
#include "WaterBodyComponent.h"
#include "WaterBodyLakeActor.h"
#include "WaterEditorSettings.h"
#include "WaterRuntimeSettings.h"
#include "WaterSplineComponent.h"
#include "WaterZoneActor.h"

namespace
{
	// ROSE world -> UE world.  The map importer negates Y for everything it
	// places (ROSE Y grows north, UE's grows south), and the ocean corners are
	// already centred by the IFO reader, so the only change here is that sign.
	// Getting it wrong puts the water on the far side of the zone from the
	// coastline it belongs to — and a bounds check cannot see that, because the
	// mirrored box is the same box.
	FVector RoseToUE(const FVector3f& P)
	{
		return FVector(P.X, -P.Y, P.Z);
	}

	// ROSE stores a patch corner as (x, HEIGHT, y) — the same "x, height, y"
	// order the ZON/IFO readers already deal with.
	struct FRect
	{
		double MinX = 0, MinY = 0, MaxX = 0, MaxY = 0, Z = 0;

		bool IsValid() const { return MaxX > MinX && MaxY > MinY; }
		FVector Centre() const { return FVector((MinX + MaxX) * 0.5, (MinY + MaxY) * 0.5, Z); }
	};
}

void RoseSpawnWater(
	UWorld* World,
	const TArray<FRoseOceanBlock>& Patches,
	const FRoseMapImportOptions& Options,
	FRoseMapImportResult& Result)
{
	if (!World || Patches.Num() == 0)
		return;

	// Build UE-space rectangles, and the overall bounds for the WaterZone.
	TArray<FRect> Rects;
	Rects.Reserve(Patches.Num());

	FBox Bounds(ForceInit);

	for (const FRoseOceanBlock& P : Patches)
	{
		const FVector A = RoseToUE(P.Start);
		const FVector B = RoseToUE(P.End);

		FRect R;
		R.MinX = FMath::Min(A.X, B.X);
		R.MaxX = FMath::Max(A.X, B.X);
		R.MinY = FMath::Min(A.Y, B.Y);
		R.MaxY = FMath::Max(A.Y, B.Y);
		// Both corners carry the same height in every zone measured; take the
		// lower one so a stray patch cannot lift the surface above the shore.
		R.Z = FMath::Min(A.Z, B.Z);

		if (!R.IsValid())
			continue;

		// Grow the patch so neighbours overlap instead of merely touching.
		// Applied AFTER the validity check so the expansion can never rescue a
		// degenerate patch into looking like a real one.
		const double Grow = FMath::Max(0.f, Options.WaterPatchExpand);
		R.MinX -= Grow;
		R.MaxX += Grow;
		R.MinY -= Grow;
		R.MaxY += Grow;

		Rects.Add(R);
		Bounds += FVector(R.MinX, R.MinY, R.Z);
		Bounds += FVector(R.MaxX, R.MaxY, R.Z);
	}

	if (Rects.Num() == 0)
		return;

	// AWaterZone asserts on UWaterSubsystem in its post-spawn path
	// (WaterZoneActor.cpp:748).  That subsystem only exists on a world that has
	// been initialised for play/editor — a commandlet's freshly-created world
	// has none, so spawning water there takes the whole import down with an
	// assert rather than a catchable error.
	//
	// Skip water when the subsystem is absent: the terrain, objects and level
	// all still import, and the zone can be added when the level is opened in
	// the editor.  Losing water is a far better outcome than losing the map.
	if (!UWaterSubsystem::GetWaterSubsystem(World))
	{
		// EXPECTED during -run=RoseImportMap: that commandlet builds its level on
		// an EWorldType::Inactive world, which UWaterSubsystem does not support.
		// Water is added afterwards by -run=RoseImportWater, which owns the
		// world lifecycle needed to have one.
		UE_LOG(LogRoseImport, Log,
			TEXT("water: no UWaterSubsystem on this world — deferring %d water "
			     "bodies; run -run=RoseImportWater -zone=%s"),
			Rects.Num(), *Options.Zone);
		return;
	}

	FActorSpawnParameters Params;
	Params.OverrideLevel = World->PersistentLevel;
	Params.ObjectFlags = RF_Transactional;

	// The WaterZone is NOT optional.  In UE 5.3+ water bodies render through a
	// zone's water mesh; without one in the level the bodies exist, are
	// selectable, and draw nothing at all.
	AWaterZone* Zone = nullptr;
	{
		const FVector Centre = Bounds.GetCenter();
		const FVector Extent = Bounds.GetExtent();

		Zone = World->SpawnActor<AWaterZone>(
			AWaterZone::StaticClass(),
			FTransform(FVector(Centre.X, Centre.Y, Bounds.Max.Z)), Params);
		if (Zone)
		{
			// Pad generously: the zone has to cover every body plus the falloff
			// around it, and a body poking outside its zone stops rendering.
			const FVector2D ZoneExtent(
				FMath::Max(Extent.X * 2.5, 100000.0),
				FMath::Max(Extent.Y * 2.5, 100000.0));
			Zone->SetZoneExtent(ZoneExtent);
			Zone->SetActorLabel(TEXT("RoseWaterZone"));
			Zone->Tags.Add(FName(TEXT("RoseWater")));
		}
	}

	for (int32 i = 0; i < Rects.Num(); ++i)
	{
		const FRect& R = Rects[i];

		// A lake body, because every ROSE ocean patch is a flat closed
		// rectangle.  AWaterBodyOcean is driven by the zone's far-field and
		// would flood the whole level; AWaterBodyRiver needs a flow direction
		// these patches do not have.
		AWaterBodyLake* Body = World->SpawnActor<AWaterBodyLake>(
			AWaterBodyLake::StaticClass(), FTransform(R.Centre()), Params);
		if (!Body)
			continue;

		if (UWaterSplineComponent* Spline = Body->GetWaterSpline())
		{
			// Four corners, LOCAL to the actor (which sits at the rectangle's
			// centre), closed into a loop.  ClearSplinePoints leaves the default
			// shape behind otherwise and the body keeps its template outline
			// instead of the patch.
			const FVector C = R.Centre();
			const TArray<FVector> Corners =
			{
				FVector(R.MinX, R.MinY, R.Z) - C,
				FVector(R.MaxX, R.MinY, R.Z) - C,
				FVector(R.MaxX, R.MaxY, R.Z) - C,
				FVector(R.MinX, R.MaxY, R.Z) - C,
			};

			Spline->ClearSplinePoints(false);
			for (const FVector& Corner : Corners)
				Spline->AddSplinePoint(Corner, ESplineCoordinateSpace::Local, false);

			// Straight edges — the default curve rounds a rectangle into a blob.
			for (int32 p = 0; p < Spline->GetNumberOfSplinePoints(); ++p)
				Spline->SetSplinePointType(p, ESplinePointType::Linear, false);

			Spline->SetClosedLoop(true, false);
			Spline->UpdateSpline();
		}

		if (UWaterBodyComponent* Comp = Body->GetWaterBodyComponent())
		{
			// ASSIGN THE WATER MATERIALS EXPLICITLY.
			//
			// Placing a water body through the editor's own tools fills these in
			// from the Water plugin's editor settings.  SpawnActor does not — it
			// gives you a body with NO water material, which renders as flat
			// grey noise and looks like the water "was not added" at all.
			//
			// Nothing here is invented: this mirrors
			// UWaterBodyActorFactory::PostSpawnActor, which is what runs when a
			// body is placed through the editor.
			const FWaterBodyLakeDefaults& Defaults =
				GetDefault<UWaterEditorSettings>()->WaterBodyLakeDefaults;

			if (UMaterialInterface* M = Defaults.GetWaterMaterial())
				Comp->SetWaterMaterial(M);
			if (UMaterialInterface* M = Defaults.GetWaterStaticMeshMaterial())
				Comp->SetWaterStaticMeshMaterial(M);
			if (UMaterialInterface* M = Defaults.GetWaterHLODMaterial())
				Comp->SetHLODMaterial(M);
			if (UMaterialInterface* M = Defaults.GetUnderwaterPostProcessMaterial())
				Comp->SetUnderwaterPostProcessMaterial(M);

			// THE WATER INFO MATERIAL, and this is the one that decides whether
			// water is visible at all.
			//
			// It does not come from UWaterEditorSettings like the others — it
			// lives on UWaterRuntimeSettings, which is why assigning the whole
			// WaterBodyLakeDefaults block still left it None.  The zone renders
			// its water-info texture (depth, flow, flags) THROUGH this material
			// and the water mesh samples that texture; with it unset the zone has
			// nothing to render into and the surface never appears, even though
			// the body, its spline and its water material are all correct.
			Comp->SetWaterInfoMaterial(
				GetDefault<UWaterRuntimeSettings>()->GetDefaultWaterInfoMaterial());

			// Spline defaults too — the factory copies these, and they carry the
			// per-point water depth/velocity the body is authored against.
			if (UWaterSplineComponent* WaterSpline = Comp->GetWaterSpline())
				WaterSpline->WaterSplineDefaults = Defaults.SplineDefaults;

			Comp->PostEditChange();
		}

		// Bind the body to OUR zone explicitly, then register and rebuild it.
		//
		// Body-side, not zone-side: AWaterZone::UpdateOverlappingWaterBodies is
		// private.  SetWaterZoneOverride is the public, and more deterministic,
		// equivalent — it names the zone outright instead of relying on bounds
		// overlap, which matters because the zone is spawned before any body
		// exists and its owned-body list is therefore built empty.
		//
		// Without this the body belongs to no zone, no water mesh is generated,
		// and the actor just shows its placeholder droplet icon.
		if (UWaterBodyComponent* Comp = Body->GetWaterBodyComponent())
		{
			if (Zone)
				Comp->SetWaterZoneOverride(Zone);
			Comp->UpdateWaterZones();

			// A zone using LOCAL-ONLY tessellation draws nothing for bodies that
			// have no static mesh, so the factory turns them on in that case.
			// Checked after the zone is bound, because GetWaterZone() is what
			// answers the question.
			if (const AWaterZone* Owning = Comp->GetWaterZone())
			{
				if (Owning->IsLocalOnlyTessellationEnabled())
					Comp->SetWaterBodyStaticMeshEnabled(true);
			}

		}

		// Finish exactly the way DRAGGING THE ACTOR IN THE EDITOR finishes.
		//
		// This is the difference between water that exists and water you can
		// see.  With materials, spline and zone all correct, the bodies still
		// rendered nothing until one was nudged in the viewport — after which it
		// stayed visible even when the move was undone, because the nudge, not
		// the new position, is what built the zone's render data.
		//
		// UpdateAll(bShapeOrPositionChanged) was not enough.  AWaterBody::
		// PostEditMove does three more things:
		//   FixupEditorTransform()   reconciles the actor transform with the spline
		//   UpdateWaterHeight()      derives the surface height from that transform
		//   OnWaterBodyChanged(...)  the FULL notification, with bUserTriggered
		//                            set — which is what makes the zone treat it
		//                            as a real edit and regenerate.
		// Calling the editor's own entry point runs all of it in the right order
		// instead of reproducing two thirds of it.
		Body->PostEditMove(/*bFinished=*/true);

		Body->SetActorLabel(FString::Printf(TEXT("RoseWater_%d"), i));
		Body->Tags.Add(FName(TEXT("RoseWater")));
		++Result.WaterBodies;
	}

	// Now that every body is bound to it, regenerate the zone's water info
	// texture and water mesh — that is what actually draws the water.
	if (Zone)
	{
		Zone->MarkForRebuild(EWaterZoneRebuildFlags::All, Zone);
		Zone->Update();
	}

	UE_LOG(LogRoseImport, Log,
		TEXT("water: %d ocean patches -> %d water bodies, surface Z %.0f..%.0f, "
		     "patches grown %.0fcm"),
		Patches.Num(), Result.WaterBodies, Bounds.Min.Z, Bounds.Max.Z,
		Options.WaterPatchExpand);
}
