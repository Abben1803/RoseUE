// Avatars, armour and animation: the SKINNED half of the game.
//
//   skeleton  AVATAR/FEMALE.ZMD, AVATAR/MALE.ZMD   -> USkeleton (one per gender)
//   parts     AVATAR/LIST_{W,M}{BODY,ARMS,FOOT,CAP,FACE,HAIR}.ZSC
//             AVATAR/LIST_FACEIEM.ZSC  (sic — the shipped filename is misspelt)
//                                                  -> USkeletalMesh per item
//   motions   MOTION/AVATAR/*.ZMO                  -> UAnimSequence
//
// Gender prefixes in the ZSC filenames are M (male) and **W** (woman) — not F,
// even though the rest of the codebase keys female parts on "F".
//
// UNITS (RoseSkeletalFormats.h): ZMD/ZMO are centimetres and pass through
// unchanged; ZMS vertices are metres and are scaled x100.
//
// ⚠ Bind pose validates almost nothing here.  A skinned mesh at bind pose
// resolves to identity, so a wrong bone binding, a wrong basis or a wrong
// scale all look perfect until something animates.  Judge this importer with a
// motion playing, never from a static preview.
#pragma once

#include "CoreMinimal.h"

struct FRoseSkeletalImportOptions
{
	FString AssetRoot;
	// Both genders by default; the ZSC packs and the ZMD are per gender.
	bool bFemale = true;
	bool bMale = true;
	// Equipment/appearance slots.
	bool bBody = true;
	bool bArms = true;
	bool bFoot = true;
	bool bCap = true;
	bool bFace = true;
	bool bHair = true;
	bool bFaceItem = true;
	// MOTION/AVATAR/*.ZMO -> UAnimSequence on the matching gender skeleton.
	bool bAnimations = true;

	int32 MaxItemsPerPack = 0;   // 0 = all; small values for a smoke test
	bool bSkipExisting = false;

	// Import ONE object id only (-item=0), across whichever packs/genders are
	// still enabled.  A full run is ~18,500 meshes and takes about twenty
	// minutes, which is far too slow to iterate a material or transform fix on.
	// Negative = disabled.
	int32 OnlyItemId = -1;
};

struct FRoseSkeletalPackResult
{
	FString Kind;      // "F/BODY" etc.
	int32 ObjectsInPack = 0;
	int32 Built = 0;
	int32 Empty = 0;
	int32 Failed = 0;
	int32 Skipped = 0;
};

struct FRoseSkeletalImportResult
{
	bool bSuccess = false;
	int32 SkeletonsBuilt = 0;
	int32 BonesFemale = 0;
	int32 BonesMale = 0;
	TArray<FRoseSkeletalPackResult> Packs;
	int32 AnimationsBuilt = 0;
	int32 AnimationsSkippedMorph = 0;   // vertex-morph ZMOs cannot drive bones
	int32 UniqueTextures = 0;
	int32 UniqueMaterials = 0;
	int32 MissingAssets = 0;
	double SecondsTotal = 0.0;
};

bool RoseImportSkeletal(const FRoseSkeletalImportOptions& Options,
	FRoseSkeletalImportResult& Result);
