// CHR — ROSE's NPC / monster character definition (NPC/LIST_NPC.CHR).
//
// Transcribed from tools/rose_parser/formats/chr_.py, whose authority is
// src/client/cmodelchar.cpp (CCharModelDATA::Load + CCharMODEL::Load_MOBorNPC).
//
// WHY THIS EXISTS AT ALL: an NPC is not addressed like an avatar.  LIST_NPC.STB
// has NO model-index column — the row id IS the CHR model index, and the CHR
// says which skeleton, which body parts (indices into NPC/PART_NPC.ZSC) and
// which motions that NPC uses.  Assuming "npc id == ZSC object index" reads a
// completely unrelated model, or an empty one; PART_NPC.ZSC has 916 objects
// against 2,201 NPC rows.
//
// File layout (little-endian, all counts int16):
//   int16 SkeletonCount   ; per: null-terminated ZMD path
//   int16 MotionCount     ; per: null-terminated ZMO path
//   int16 EffectCount     ; per: null-terminated EFT path
//   int16 ModelCount      ; per model:
//       uint8  bValid          0 = the slot is empty, nothing follows
//       int16  SkeletonIndex   into the skeleton list
//       null-str Name
//       int16  PartCount      ; per: int16 part index into PART_NPC.ZSC
//       int16  AnimCount      ; per: int16 AnimType, int16 MotionIndex
//       int16  BoneEffectCount; per: int16 BoneIndex, int16 EffectIndex
#pragma once

#include "CoreMinimal.h"

struct FRoseChrBoneEffect
{
	int32 BoneIndex = 0;
	int32 EffectFileIndex = 0;
};

struct FRoseChrModel
{
	bool bValid = false;            // false = empty slot; skip it
	int32 SkeletonIndex = INDEX_NONE;
	FString Name;
	/** Indices into PART_NPC.ZSC's object list — the body parts to build. */
	TArray<int32> BodyPartIndices;
	/** AnimType (MOB_ANI_*) -> index into MotionFiles. */
	TMap<int32, int32> Animations;
	TArray<FRoseChrBoneEffect> BoneEffects;
};

struct FRoseCHR
{
	TArray<FString> SkeletonFiles;   // ZMD paths, VFS-relative ("3DDATA\...")
	TArray<FString> MotionFiles;     // ZMO paths
	TArray<FString> EffectFiles;     // EFT paths
	TArray<FRoseChrModel> Models;    // indexed by LIST_NPC row id

	bool Load(const FString& Path);

	/** ZMD path for a model, or empty when the slot is invalid/out of range. */
	FString SkeletonPathFor(int32 ModelIndex) const
	{
		if (!Models.IsValidIndex(ModelIndex))
			return FString();
		const FRoseChrModel& M = Models[ModelIndex];
		if (!M.bValid || !SkeletonFiles.IsValidIndex(M.SkeletonIndex))
			return FString();
		return SkeletonFiles[M.SkeletonIndex];
	}

	FString MotionPath(int32 MotionIndex) const
	{
		return MotionFiles.IsValidIndex(MotionIndex) ? MotionFiles[MotionIndex] : FString();
	}
};
