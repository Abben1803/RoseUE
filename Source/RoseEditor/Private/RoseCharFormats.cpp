#include "RoseCharFormats.h"

#include "RoseBinaryReader.h"
#include "RoseEditor.h"

namespace
{
	// CStr::ReadString: terminates on the null byte.  Same helper the ZMD/ZMO
	// reader uses; duplicated rather than exported because the two format files
	// are otherwise independent.
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
}

bool FRoseCHR::Load(const FString& Path)
{
	FRoseBinaryReader R;
	if (!R.LoadFile(Path))
	{
		UE_LOG(LogRoseImport, Error, TEXT("cannot read CHR %s"), *Path);
		return false;
	}

	SkeletonFiles.Reset();
	MotionFiles.Reset();
	EffectFiles.Reset();
	Models.Reset();

	auto ReadList = [&R](TArray<FString>& Out)
	{
		const int32 Count = R.I16();
		Out.Reserve(FMath::Max(0, Count));
		for (int32 i = 0; i < Count && !R.AtEnd(); ++i)
			Out.Add(ReadNullStr(R));
	};

	ReadList(SkeletonFiles);
	ReadList(MotionFiles);
	ReadList(EffectFiles);

	const int32 ModelCount = R.I16();
	Models.Reserve(FMath::Max(0, ModelCount));
	for (int32 i = 0; i < ModelCount && !R.AtEnd(); ++i)
	{
		FRoseChrModel M;

		// An invalid slot has NOTHING after the flag — reading a skeleton index
		// here would desync the whole rest of the file, and because the models
		// are positional (row id == index) that corrupts every NPC after it
		// rather than failing loudly.
		if (R.U8() == 0)
		{
			Models.Add(MoveTemp(M));
			continue;
		}

		M.bValid = true;
		M.SkeletonIndex = R.I16();
		M.Name = ReadNullStr(R);

		const int32 PartCount = R.I16();
		M.BodyPartIndices.Reserve(FMath::Max(0, PartCount));
		for (int32 p = 0; p < PartCount; ++p)
			M.BodyPartIndices.Add(R.I16());

		const int32 AnimCount = R.I16();
		for (int32 a = 0; a < AnimCount; ++a)
		{
			const int32 AnimType = R.I16();
			const int32 MotionIdx = R.I16();
			if (AnimType >= 0)
				M.Animations.Add(AnimType, MotionIdx);
		}

		const int32 EffectCount = R.I16();
		M.BoneEffects.Reserve(FMath::Max(0, EffectCount));
		for (int32 e = 0; e < EffectCount; ++e)
		{
			FRoseChrBoneEffect BE;
			BE.BoneIndex = R.I16();
			BE.EffectFileIndex = R.I16();
			M.BoneEffects.Add(BE);
		}

		Models.Add(MoveTemp(M));
	}

	int32 Valid = 0;
	for (const FRoseChrModel& M : Models)
		if (M.bValid) ++Valid;

	UE_LOG(LogRoseImport, Log,
		TEXT("CHR %s: %d model(s) (%d valid), %d skeleton(s), %d motion(s)"),
		*FPaths::GetCleanFilename(Path), Models.Num(), Valid,
		SkeletonFiles.Num(), MotionFiles.Num());
	return Models.Num() > 0;
}
