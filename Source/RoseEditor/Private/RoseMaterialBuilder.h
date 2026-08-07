// The master materials, built IN ENGINE.
//
// These used to be produced by tools/ue5_terrain_master.py + ue5_fix_master.py,
// which meant a fresh clone needed a Python round trip before a zone would
// render.  UMaterialEditingLibrary is a C++ API — the Python was only ever a
// thin wrapper over it — so the graphs are built here instead and the importer
// can guarantee its own master exists.
//
// Rebuilt IN PLACE (DeleteAllMaterialExpressions, never delete+recreate the
// asset — a delete+create of the same name in one run returns null from
// create_asset and corrupts the package; see CLAUDE.md).
#pragma once

#include "CoreMinimal.h"

class UMaterial;

namespace RoseMaterials
{
	// /Game/Atlas/M_RoseTerrain — the ROSE terrain pass in one opaque material.
	//
	// src/engine/shader/terrain.psh:
	//     mov    r0, t0                 // 1st map
	//     lrp    r0.rgb, t1.a, t1, r0   // lerp(bottom, top, top.a)
	//     mul_x2 r0.rgb, r0, t2         // baked ground lightmap, DOUBLED
	//     mul    r0.rgb, r0, t3         // shadow map (not implemented)
	//
	// Parameters:
	//   BaseColor / UVTransform      bottom tile page + sub-rect,  UV0
	//   TopColor  / TopUVTransform   top tile page + sub-rect,     UV1
	//   Lightmap  / LightmapScale    per-chunk baked light,        UV2
	//
	// LightmapScale defaults to 1.0 with a WHITE default texture, so a zone with
	// no lightmap is exactly neutral.  The importer sets it to 2.0 when it binds
	// a real lightmap, which is where mul_x2 comes from.  Encoding the x2 as a
	// parameter rather than a constant is what makes "no lightmap" safe — baking
	// the 2 in would double-expose every zone that lacks one.
	UMaterial* EnsureTerrainMaster(bool bForceRebuild = false);

	// /Game/Characters/Materials/M_RoseChar — the avatar master.
	//
	// Characters do NOT use the object master.  M_RoseChar is the one the
	// working character pipeline parented avatar parts to: texture RGB to
	// BaseColor, texture ALPHA to the opacity mask, two-sided.
	//
	// It is built here because the version tools/ue5_import_modular.py produced
	// never set ROUGHNESS or SPECULAR, so both fell back to UE's defaults of
	// 0.5 — a half-glossy, half-specular surface.  On ROSE's flat hand-painted
	// diffuse textures that reads as shiny plastic, and under a dim sun the
	// specular response makes the whole character look dark.  M_RoseMaster pins
	// them at 1.0 / 0.0 (fully rough, no specular) and world geometry looks
	// right because of it; the avatar master needs the same.
	//
	// Metallic is pinned to 0 explicitly rather than left to default: a metallic
	// surface with nothing to reflect renders black.
	UMaterial* EnsureCharacterMaster(bool bForceRebuild = false);

	// /Game/Rose/Sky/M_RoseSky — the sky dome, day and night in one material.
	//
	// LIST_SKY.STB gives every sky TWO textures (col 'Sky Texture 1' = day,
	// col 2 = night) and CSkyDOME blends between them with
	// setSkyMaterialBlendRatio.  That is the entire day/night effect in ROSE:
	// one dome mesh, two textures, one ratio.
	//
	// Parameters:
	//   DayTex / NightTex   the two LIST_SKY textures
	//   SkyBlend            0 = full day, 1 = full night
	//
	// UNLIT, and bIsSky — the latter is what stops height fog and the
	// atmosphere from tinting the dome, matching CSkyDOME's
	// ::setReceiveFog(m_hSKY, 0).  Without it the sky is fogged like world
	// geometry and the horizon washes to a flat band.
	UMaterial* EnsureSkyMaster(bool bForceRebuild = false);

	// /Game/Rose/Sky/M_RosePrecip — rain and snow, PROCEDURALLY.
	//
	// Deliberately texture-free and Niagara-free: it draws on the inside of a
	// cylinder locked to the camera, so precipitation works on a fresh clone
	// with no authored particle asset and no artist round trip.  One material
	// covers both by parameter — rain is fast, stretched and thin; snow is slow,
	// round and bright.
	//
	// Parameters:
	//   Precipitation  0..1, the master fade (0 = fully invisible)
	//   FallSpeed      downward pan rate
	//   Stretch        vertical smear: high = rain streaks, ~1 = snow flakes
	//   Density        how many drops across the cylinder
	//   Sharpness      contrast; higher = thinner, more separated drops
	//   PrecipColor    tint
	UMaterial* EnsurePrecipMaster(bool bForceRebuild = false);

	// /Game/Rose/Sky/M_RoseParticle — the material for REAL precipitation
	// instances (ARosePrecipitation), as opposed to the camera-locked shell.
	//
	// A shell can never parallax: every pixel is the same distance away, so it
	// reads as drifting fog however sharp the pattern is.  Actual instances in
	// the world move past the camera at their own depths, which is what makes
	// snow look like snow.  This material is what each instance draws with — a
	// soft round dot the instance transform stretches into a streak for rain.
	UMaterial* EnsureParticleMaster(bool bForceRebuild = false);
}
