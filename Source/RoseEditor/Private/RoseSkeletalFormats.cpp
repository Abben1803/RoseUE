#include "RoseSkeletalFormats.h"

#include "RoseBinaryReader.h"
#include "RoseEditor.h"

namespace
{
	FString ReadNullStr(FRoseBinaryReader& R)
	{
		FString Out;
		while (!R.AtEnd())
		{
			const uint8 C = R.U8();
			if (C == 0) break;
			Out.AppendChar((TCHAR)C);
		}
		return Out;
	}

	// ROSE quaternions in ZMD and ZMO are stored W FIRST.
	FQuat4f ReadQuatWFirst(FRoseBinaryReader& R)
	{
		const float W = R.F32(), X = R.F32(), Y = R.F32(), Z = R.F32();
		return FQuat4f(X, Y, Z, W);
	}
}

bool FRoseZMD::Load(const FString& Path)
{
	FRoseBinaryReader R;
	if (!R.LoadFile(Path))
	{
		UE_LOG(LogRoseImport, Warning, TEXT("ZMD missing: %s"), *Path);
		return false;
	}

	// Exactly 7 bytes, NOT null-terminated in the file.
	const FString Magic = R.FixedStr(7);
	if (Magic == TEXT("ZMD0002"))      Version = 2;
	else if (Magic == TEXT("ZMD0003")) Version = 3;
	else
	{
		UE_LOG(LogRoseImport, Warning, TEXT("ZMD %s: unknown magic '%s'"), *Path, *Magic);
		return false;
	}

	const int32 NumBones = (int32)R.U32();
	if (NumBones <= 0 || NumBones > 1024)
		return false;

	Bones.Reserve(NumBones);
	for (int32 i = 0; i < NumBones; ++i)
	{
		FRoseBone Bone;
		Bone.ParentId = (int32)R.U32();
		Bone.Name = ReadNullStr(R);
		// Centimetres already — do NOT apply the Python parsers' 1/100, which
		// exists only because glTF works in metres.
		Bone.Translation = R.Vec3();
		Bone.Rotation = ReadQuatWFirst(R);
		Bones.Add(MoveTemp(Bone));
	}

	const int32 NumDummies = (int32)R.U32();
	Dummies.Reserve(FMath::Max(0, NumDummies));
	for (int32 i = 0; i < NumDummies && i < 1024; ++i)
	{
		FRoseDummy Dummy;
		// NOTE the order differs from bones: name FIRST, then parent.
		Dummy.Name = ReadNullStr(R);
		Dummy.ParentId = (int32)R.U32();
		Dummy.Translation = R.Vec3();
		if (Version >= 3)
			Dummy.Rotation = ReadQuatWFirst(R);
		Dummies.Add(MoveTemp(Dummy));
	}

	return !R.HasOverrun() && Bones.Num() > 0;
}

bool FRoseZMO::IsSkeletal(int32 BoneCount) const
{
	for (const FRoseZmoChannel& Ch : Channels)
	{
		const bool bBoneChannel =
			(Ch.ChannelType & (ROSE_CTYPE_POSITION | ROSE_CTYPE_ROTATION | ROSE_CTYPE_SCALE)) != 0;
		if (bBoneChannel && (Ch.ReferId < 0 || Ch.ReferId >= BoneCount))
			return false;
	}
	return Channels.Num() > 0;
}

bool FRoseZMO::Load(const FString& Path)
{
	FRoseBinaryReader R;
	if (!R.LoadFile(Path))
		return false;

	const FString Magic = ReadNullStr(R);
	if (Magic != TEXT("ZMO0002"))
	{
		UE_LOG(LogRoseImport, Warning, TEXT("ZMO %s: unknown magic '%s'"), *Path, *Magic);
		return false;
	}

	Fps = (int32)R.U32();
	NumFrames = (int32)R.U32();
	const int32 NumChannels = (int32)R.U32();

	if (NumFrames <= 0 || NumFrames > 100000 || NumChannels <= 0 || NumChannels > 8192)
		return false;

	Channels.SetNum(NumChannels);
	for (int32 c = 0; c < NumChannels; ++c)
	{
		Channels[c].ChannelType = R.U32();
		Channels[c].ReferId = (int32)R.U32();
	}

	// Frame-major on disk: every channel's value for frame 0, then frame 1...
	for (int32 c = 0; c < NumChannels; ++c)
	{
		const uint32 T = Channels[c].ChannelType;
		if (T & ROSE_CTYPE_POSITION) Channels[c].Positions.Reserve(NumFrames);
		if (T & ROSE_CTYPE_ROTATION) Channels[c].Rotations.Reserve(NumFrames);
		if (T & ROSE_CTYPE_SCALE)    Channels[c].Scales.Reserve(NumFrames);
	}

	for (int32 f = 0; f < NumFrames; ++f)
	{
		for (int32 c = 0; c < NumChannels; ++c)
		{
			FRoseZmoChannel& Ch = Channels[c];
			const uint32 T = Ch.ChannelType;

			if (T & ROSE_CTYPE_POSITION)
				Ch.Positions.Add(R.Vec3());          // centimetres, kept as-is
			if (T & ROSE_CTYPE_ROTATION)
				Ch.Rotations.Add(ReadQuatWFirst(R));
			if (T & ROSE_CTYPE_NORMAL)
				R.Vec3();
			if (T & ROSE_CTYPE_ALPHA)
				R.F32();
			if (T & ROSE_CTYPE_UV0) { R.F32(); R.F32(); }
			if (T & ROSE_CTYPE_UV1) { R.F32(); R.F32(); }
			if (T & ROSE_CTYPE_UV2) { R.F32(); R.F32(); }
			if (T & ROSE_CTYPE_UV3) { R.F32(); R.F32(); }
			if (T & ROSE_CTYPE_TEXTUREANIM)
				R.F32();
			if (T & ROSE_CTYPE_SCALE)
				Ch.Scales.Add(R.F32());
		}
	}

	return !R.HasOverrun();
}
