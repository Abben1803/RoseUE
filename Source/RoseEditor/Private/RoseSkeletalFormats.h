// ZMD (skeleton) and ZMO (motion).
//
// Transcribed from tools/rose_parser/formats/{zmd,zmo}.py, validated against
// src/engine/src/zz_skeleton.cpp and zz_motion.cpp.
//
// UNITS — the rule that makes or breaks every animation:
//   ZMD bone translations : CENTIMETRES  -> used as-is in UE
//   ZMO position channels : CENTIMETRES  -> used as-is in UE
//   ZMS vertices          : METRES       -> x100  (see RoseObjectFormats)
// The Python parsers scale ZMD/ZMO by 1/100 because glTF is in
// metres; UE is already centimetres, so that scale is NOT applied here.
#pragma once

#include "CoreMinimal.h"

struct FRoseBone
{
	int32 ParentId = 0;         // index into Bones; bone 0 is the root
	FString Name;
	FVector3f Translation = FVector3f::ZeroVector;   // centimetres
	FQuat4f Rotation = FQuat4f::Identity;            // file order is W,X,Y,Z
};

struct FRoseDummy
{
	int32 ParentId = 0;
	FString Name;
	FVector3f Translation = FVector3f::ZeroVector;
	FQuat4f Rotation = FQuat4f::Identity;            // v3 only
};

struct FRoseZMD
{
	int32 Version = 0;
	TArray<FRoseBone> Bones;
	// Attachment points (weapon grips, the cap point, the back point).  ROSE
	// links equipment to these, not to bones — DUMMY_IDX_CAP is the top of the
	// head, whereas the head BONE is at the neck.
	TArray<FRoseDummy> Dummies;

	bool Load(const FString& Path);
};

// ── ZMO channel types (src/engine/include/zz_channel.h) ────────────────────
enum : uint32
{
	ROSE_CTYPE_POSITION    = 1u << 1,
	ROSE_CTYPE_ROTATION    = 1u << 2,
	ROSE_CTYPE_NORMAL      = 1u << 3,
	ROSE_CTYPE_ALPHA       = 1u << 4,
	ROSE_CTYPE_UV0         = 1u << 5,
	ROSE_CTYPE_UV1         = 1u << 6,
	ROSE_CTYPE_UV2         = 1u << 7,
	ROSE_CTYPE_UV3         = 1u << 8,
	ROSE_CTYPE_TEXTUREANIM = 1u << 9,
	ROSE_CTYPE_SCALE       = 1u << 10,
};

struct FRoseZmoChannel
{
	uint32 ChannelType = 0;
	// Bone index for POSITION/ROTATION/SCALE; a vertex group for the morph
	// channels.  A ZMO whose refer_id is not a bone is a VERTEX-MORPH motion
	// (flags, banners) and cannot drive a skeleton.
	int32 ReferId = 0;

	TArray<FVector3f> Positions;   // filled when POSITION
	TArray<FQuat4f> Rotations;     // filled when ROTATION
	TArray<float> Scales;          // filled when SCALE
};

struct FRoseZMO
{
	int32 Fps = 30;
	int32 NumFrames = 0;
	TArray<FRoseZmoChannel> Channels;

	float GetDuration() const { return Fps > 0 ? (float)NumFrames / (float)Fps : 0.f; }
	// True when EVERY channel targets a bone index.
	//
	// Note this is stricter than the importer wants: a clip with a few stray
	// channels is still usable, so RoseSkeletalImporter tests "does ANY channel
	// land on a bone" and skips the odd channel rather than the whole clip
	// (matching rose_combine_anims.py).  Use this only where you genuinely need
	// "all channels are bones".
	bool IsSkeletal(int32 BoneCount) const;

	bool Load(const FString& Path);
};
