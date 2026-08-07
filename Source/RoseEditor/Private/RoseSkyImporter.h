// The ROSE sky dome — day/night, straight from LIST_SKY.STB.
//
// ROSE's whole sky is three things (src/client/cskydome.cpp):
//   mesh      LIST_SKY col 0, a .ZMS dome the camera sits inside
//   textures  cols 1 and 2 — day and night
//   blend     setSkyMaterialBlendRatio picks between them
// and the dome is excluded from fog (::setReceiveFog(m_hSKY, 0)).
//
// A zone picks its row with LIST_ZONE's 'Sky' column (the misleadingly named
// ZONE_BG_IMAGE macro; see RoseStbSchema.cpp), and the day-length columns next
// to it drive how fast the blend moves.
//
// Output:
//   /Game/Rose/Sky/SM_RoseSky_<n>      dome mesh (shared; only ~4 distinct)
//   /Game/Rose/Sky/Textures/T_...      day + night
//   /Game/Rose/Sky/MI_RoseSky_<row>    one instance per LIST_SKY row
//   /Game/Rose/Sky/DT_RoseSky          row -> assets, and zone -> row
#pragma once

#include "CoreMinimal.h"

struct FRoseSkyImportOptions
{
	FString AssetRoot;
};

struct FRoseSkyImportResult
{
	bool bSuccess = false;
	int32 SkyRows = 0;
	int32 MeshesBuilt = 0;
	int32 TexturesBuilt = 0;
	int32 MaterialsBuilt = 0;
	int32 ZonesMapped = 0;
	int32 Failed = 0;
	double SecondsTotal = 0.0;
};

bool RoseImportSky(const FRoseSkyImportOptions& Options, FRoseSkyImportResult& Result);
