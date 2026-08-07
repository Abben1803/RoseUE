// Native ROSE map import: binaries -> assets -> a LEVEL, in one process.
//
// The output is a real UWorld with placed actors, not a pile of assets.  That
// distinction is the whole point: an assets-only import loses every transform,
// and the level is what the game actually loads.
#pragma once

#include "CoreMinimal.h"

struct FRoseMapImportOptions
{
	// Zone folder name, e.g. "JPT01".
	FString Zone;
	// 3DDATA root; the zone folder is found by scanning MAPS/<planet>/<zone>.
	FString AssetRoot;
	// Suffix on the created level.  EMPTY by default: this importer is the base
	// path now, so it writes L_<ZONE> directly.  Set a suffix to import
	// side-by-side instead.
	FString LevelSuffix;
	// After a SUCCESSFUL import, delete the superseded glTF/Interchange assets
	// for this zone (/Game/Maps/<ZONE>/Scene).  Ordered that way on purpose:
	// nothing is ever removed before its replacement exists.
	bool bDeleteLegacyAssets = false;
	// Atlas gutter in pixels.  Cross-cell mip bleed is what produced the black
	// edges on the gear atlas; the terrain atlas gets the same treatment.
	int32 AtlasGutter = 8;
	// Add a sun + sky so the level is viewable straight away.
	bool bAddLighting = true;
	// ZSC deco + construction objects (the bulk of a zone).
	bool bImportObjects = true;
	// IFO NPCs, monster spawners and warp portals as the real gameplay actors.
	bool bImportEntities = true;
	// Model-forward offset for NPC actors, in degrees.
	//
	// ARoseMonster mounts its mesh with a 90 deg yaw AND a -X mirror
	// (RoseMonster.cpp ctor), so the actor's yaw is not the model's facing.
	// The old pipeline applies the same correction as ROSE_YAW_OFF in
	// tools/ue5_import_npcs.py; without it every NPC faces 90 deg to its left.
	float NpcYawOffset = 90.f;

	// ── baked ground lighting ──────────────────────────────────────────────
	// ROSE's terrain shader is three lines (src/engine/shader/terrain.psh):
	//     lrp    r0.rgb, t1.a, t1, r0   // tile blend      <- we do this
	//     mul_x2 r0.rgb, r0, t2         // ground lightmap <- this
	//     mul    r0.rgb, r0, t3         // shadow map      <- not yet
	// Without the lightmap the ground is flat albedo with no baked shading.
	//
	// <chunk>/<chunk>_PLANELIGHTINGMAP.DDS, ARUA-NATIVE.  These looked absent
	// for a long time: the Arua VFS indexes by one-way filename hash with no
	// name table, so the extractor had to infer names from file CONTENT, and a
	// DDS embeds no path — every one it could not identify was silently
	// dropped.  They were recovered by hashing the paths directly
	// (tools/arua_vfs_extract.py --lightmaps: 16,781 files, 0 missing), so
	// there is no classic dependency and no era mixing here.
	bool bImportLightmaps = true;

	// Place a real light at every ROSE light socket.  ROSE marks these itself
	// (ZSC effect type 1 DayNight / 2 LightContainer) — in JPT01 the type-1
	// sockets are `streetlight01l.eft` on the street-lamp objects — so no name
	// matching is involved.
	bool bImportLights = true;

	// Water from the IFO OCEAN block (the WATER block is empty in every zone
	// measured).  Needs the Water plugin, enabled in RoseUE.uproject.
	bool bImportWater = true;

	// How far to grow every ocean patch outward, in CENTIMETRES.
	//
	// ROSE's patches are discrete rectangles laid edge to edge (JPT01's are
	// 16000 apart), and one body per patch means neighbours only ever touch —
	// so a seam, or a patch the zone simply never defined, reads as a hole in
	// the water.  Growing each rectangle makes neighbours OVERLAP, which closes
	// the seams, and pushes the outer edge under the shoreline so the water
	// disappears into the bank instead of stopping short of it.
	//
	// 800 is 5% of a patch: enough to bury the seams and tuck the edge into the
	// terrain, small enough not to flood anything. Raise it to spread water
	// further inland, or set 0 for exactly the patches ROSE defines.
	float WaterPatchExpand = 800.f;

	// Point-light settings for those lamps.  Radius is in CENTIMETRES, the unit
	// the whole ROSE world is in: 1200 is roughly a 12 m pool around a post.
	float LampIntensity = 8000.f;
	float LampRadius = 1200.f;

	// Sun / sky, matching the tuned rig in tools/ue5_fix_lighting.py.  The
	// importer used to spawn 2 / 6 — near-ambient, which was survivable while
	// the terrain was unlit and ignored lighting entirely, but a lit ground that
	// has to catch shadows needs a real key light.
	float SunIntensity = 4.f;
	float SkyIntensity = 3.f;

	// Auto-exposure compensation on the unbound RoseExposure volume.  The
	// project runs histogram auto-exposure; without a volume clamping it, the
	// adaptation chases the bright albedo ground and blows it to white.
	float ExposureBias = 0.5f;
};

struct FRoseMapImportResult
{
	bool bSuccess = false;
	int32 ChunksLoaded = 0;
	int32 MeshesBuilt = 0;
	int32 ActorsPlaced = 0;
	int32 AtlasTiles = 0;
	int32 AtlasSize = 0;
	// Per-chunk baked ground lightmaps actually found and applied.  Missing is
	// normal for any zone with no classic counterpart — the terrain simply
	// keeps the shared material and the neutral mid-grey default.
	int32 LightmapsImported = 0;
	int32 LightmapsMissing = 0;
	int32 ObjectActors = 0;      // placed ZSC parts
	int32 UniqueMeshes = 0;      // after dedup
	int32 UniqueTextures = 0;
	int32 UniqueMaterials = 0;
	int32 MissingAssets = 0;
	int32 NpcActors = 0;
	int32 SpawnerActors = 0;
	int32 PortalActors = 0;
	int32 AnimatedParts = 0;
	int32 Lamps = 0;             // lights placed from ROSE light sockets
	int32 WaterBodies = 0;       // water bodies built from ocean patches
	double SecondsObjects = 0.0;
	int32 LegacyAssetsDeleted = 0;
	int64 TotalVertices = 0;
	int64 TotalTriangles = 0;
	double SecondsParse = 0.0;
	double SecondsAtlas = 0.0;
	double SecondsMeshes = 0.0;
	double SecondsSave = 0.0;
	double SecondsTotal = 0.0;
	FString LevelPackage;
};

bool RoseImportMap(const FRoseMapImportOptions& Options, FRoseMapImportResult& Result);
