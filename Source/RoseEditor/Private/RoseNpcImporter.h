// NPCs and monsters — the CHR-driven half of the skinned pipeline.
//
//   definition  NPC/LIST_NPC.CHR    model index == LIST_NPC.STB row id
//   skeleton    the CHR model's own ZMD (NPCs do NOT share one rig)
//   parts       CHR BodyPartIndices -> NPC/PART_NPC.ZSC objects -> ZMS + material
//   motions     the CHR model's motion list -> ZMO -> UAnimSequence
//
// NOT like avatars: LIST_NPC.STB has no model-index column, and PART_NPC.ZSC
// holds 916 objects against 2,201 NPC rows — so "npc id == ZSC object" reads an
// unrelated model.  Everything routes through the CHR.
//
// Replaces tools/build_monsters.py (GLB -> Interchange), which needed an editor
// cycle per chunk.  Output lives beside the avatars under /Game/Rose/Npcs.
#pragma once

#include "CoreMinimal.h"

struct FRoseNpcImportOptions
{
	FString AssetRoot;
	int32 OnlyNpcId = -1;      // import ONE row (smoke test); negative = all
	int32 MaxNpcs = 0;         // 0 = all
	bool bSkipExisting = false;
	bool bAnimations = true;
};

struct FRoseNpcImportResult
{
	bool bSuccess = false;
	int32 ModelsInChr = 0;
	int32 Built = 0;
	int32 Empty = 0;        // invalid/!bValid CHR slots — normal, not a failure
	int32 Skipped = 0;
	int32 Failed = 0;
	int32 SkeletonsBuilt = 0;
	int32 AnimationsBuilt = 0;
	int32 UniqueTextures = 0;
	int32 UniqueMaterials = 0;
	double SecondsTotal = 0.0;
};

bool RoseImportNpcs(const FRoseNpcImportOptions& Options, FRoseNpcImportResult& Result);
