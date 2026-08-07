// Water for an imported zone, from the IFO's OCEAN block.
//
// ROSE has TWO water representations and only one of them is used:
//   - block 7 (Water)  a per-cell has_water/height grid.  Measured EMPTY in
//                      every zone checked — 0 wet cells in JPT01, JDT01, EJT01.
//   - block 9 (Ocean)  a list of axis-aligned rectangles, each with a start and
//                      end corner.  THIS is the water that gets drawn.
//
// Every patch measured is flat: one height for the whole rectangle (JPT01's 45
// patches all sit at ROSE Y = -800). So water is horizontal rectangles, which
// is why these become water bodies with a four-point rectangular spline rather
// than anything river-like.

#pragma once

#include "CoreMinimal.h"

struct FRoseOceanBlock;
struct FRoseMapImportOptions;
struct FRoseMapImportResult;
class UWorld;

// Spawns the WaterZone the plugin needs plus one water body per merged patch
// run.  Safe to call with an empty array (does nothing).
void RoseSpawnWater(
	UWorld* World,
	const TArray<FRoseOceanBlock>& Patches,
	const FRoseMapImportOptions& Options,
	FRoseMapImportResult& Result);
