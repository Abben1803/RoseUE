#include "RoseLegacyDummyCommandlet.h"

#include "RoseEditor.h"
#include "RosePathResolver.h"
#include "RoseSkeletalFormats.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

URoseLegacyDummyCommandlet::URoseLegacyDummyCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

namespace
{
	// Compose a ZMD node chain into ROSE-space world transforms.
	// ZMD translations are centimetres and quaternions are stored W-first (the
	// loader has already reordered them).
	FTransform RoseBoneWorld(const FRoseZMD& Zmd, int32 BoneIndex)
	{
		FTransform T = FTransform::Identity;
		int32 Cur = BoneIndex;

		// Walk to the root, accumulating. Guarded against a malformed parent
		// chain looping forever.
		for (int32 Guard = 0; Guard < 256 && Zmd.Bones.IsValidIndex(Cur); ++Guard)
		{
			const FRoseBone& B = Zmd.Bones[Cur];
			const FTransform Local(FQuat(B.Rotation), FVector(B.Translation));
			T = T * Local;

			if (Cur == B.ParentId || B.ParentId < 0)
				break;               // bone 0's parent is itself
			Cur = B.ParentId;
		}
		return T;
	}

	// The legacy skeleton's own reference-pose world transform for a bone.
	bool LegacyBoneWorld(const USkeleton* Skeleton, FName BoneName, FTransform& Out)
	{
		const FReferenceSkeleton& Ref = Skeleton->GetReferenceSkeleton();
		int32 Index = Ref.FindBoneIndex(BoneName);
		if (Index == INDEX_NONE)
			return false;

		const TArray<FTransform>& Pose = Ref.GetRefBonePose();

		Out = FTransform::Identity;
		for (int32 Guard = 0; Guard < 256 && Index != INDEX_NONE; ++Guard)
		{
			if (!Pose.IsValidIndex(Index))
				return false;
			Out = Out * Pose[Index];
			Index = Ref.GetParentIndex(Index);
		}
		return true;
	}
}

int32 URoseLegacyDummyCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	const FString AssetRoot = ParamsMap.Contains(TEXT("assetroot"))
		? ParamsMap[TEXT("assetroot")]
		: TEXT("C:/QQ-iROSE Online/extracted");
	const bool bDryRun = Switches.Contains(TEXT("dryrun"));

	FRosePathResolver Resolver(AssetRoot);

	struct FGender
	{
		const TCHAR* Folder;    // /Game/Characters/Modular/<Folder>/...
		const TCHAR* Zmd;
	};
	const FGender Genders[] =
	{
		{ TEXT("Female"), TEXT("AVATAR/FEMALE.ZMD") },
		{ TEXT("Male"),   TEXT("AVATAR/MALE.ZMD")   },
	};

	int32 TotalAdded = 0, Failures = 0;

	for (const FGender& G : Genders)
	{
		const FString SkelPath = FString::Printf(
			TEXT("/Game/Characters/Modular/%s/base/base/SkeletalMeshes/base_Skeleton.base_Skeleton"),
			G.Folder);

		USkeleton* Skeleton = LoadObject<USkeleton>(nullptr, *SkelPath);
		if (!Skeleton)
		{
			UE_LOG(LogRoseImport, Error, TEXT("%s: no legacy skeleton at %s"), G.Folder, *SkelPath);
			++Failures;
			continue;
		}

		FRoseZMD Zmd;
		const FString ZmdPath = Resolver.Resolve(G.Zmd);
		if (ZmdPath.IsEmpty() || !Zmd.Load(ZmdPath))
		{
			UE_LOG(LogRoseImport, Error, TEXT("%s: cannot load %s"), G.Folder, G.Zmd);
			++Failures;
			continue;
		}

		UE_LOG(LogRoseImport, Display, TEXT("=== %s: %d bones, %d dummies in ZMD; skeleton has %d bones ==="),
			G.Folder, Zmd.Bones.Num(), Zmd.Dummies.Num(),
			Skeleton->GetReferenceSkeleton().GetNum());

		int32 Added = 0;
		for (int32 i = 0; i < Zmd.Dummies.Num(); ++i)
		{
			const FRoseDummy& D = Zmd.Dummies[i];
			if (!Zmd.Bones.IsValidIndex(D.ParentId))
				continue;

			const FName BoneName(*Zmd.Bones[D.ParentId].Name);

			// The legacy rig must actually carry the ZMD's bone names; if it
			// does not, this whole mapping is meaningless and saying so is far
			// better than writing a socket onto the wrong bone.
			FTransform LegacyBone;
			if (!LegacyBoneWorld(Skeleton, BoneName, LegacyBone))
			{
				UE_LOG(LogRoseImport, Warning,
					TEXT("  dummy %d: bone '%s' not in the legacy skeleton — skipped"),
					i, *BoneName.ToString());
				continue;
			}

			// Derive the ROSE -> legacy mapping FROM THIS BONE, then carry the
			// dummy through it.  No basis is assumed anywhere.
			const FTransform RoseBone = RoseBoneWorld(Zmd, D.ParentId);
			const FTransform RoseDummyLocal(FQuat(D.Rotation), FVector(D.Translation));
			const FTransform RoseDummyWorld = RoseDummyLocal * RoseBone;

			const FTransform M = RoseBone.Inverse() * LegacyBone;   // ROSE -> legacy
			const FTransform DummyWorldUE = RoseDummyWorld * M;
			const FTransform SocketLocal = DummyWorldUE * LegacyBone.Inverse();

			const FName SocketName(*FString::Printf(TEXT("rose_dummy_%d"), i));

			USkeletalMeshSocket* Existing = Skeleton->FindSocket(SocketName);
			if (Existing && !bDryRun)
			{
				Skeleton->Sockets.Remove(Existing);
				Existing = nullptr;
			}

			UE_LOG(LogRoseImport, Display,
				TEXT("  %s -> bone '%s'  loc=(%.1f, %.1f, %.1f)  rot=(%.1f, %.1f, %.1f)"),
				*SocketName.ToString(), *BoneName.ToString(),
				SocketLocal.GetLocation().X, SocketLocal.GetLocation().Y, SocketLocal.GetLocation().Z,
				SocketLocal.Rotator().Pitch, SocketLocal.Rotator().Yaw, SocketLocal.Rotator().Roll);

			if (bDryRun)
				continue;

			USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(Skeleton);
			Socket->SocketName = SocketName;
			Socket->BoneName = BoneName;
			Socket->RelativeLocation = SocketLocal.GetLocation();
			Socket->RelativeRotation = SocketLocal.Rotator();
			Skeleton->Sockets.Add(Socket);
			++Added;
		}

		UE_LOG(LogRoseImport, Display, TEXT("%s: %d sockets %s"),
			G.Folder, Added, bDryRun ? TEXT("(dry run, nothing written)") : TEXT("added"));
		TotalAdded += Added;

		if (Added > 0 && !bDryRun)
		{
			Skeleton->MarkPackageDirty();

			UPackage* Pkg = Skeleton->GetOutermost();
			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			const FString File = FPackageName::LongPackageNameToFilename(
				Pkg->GetName(), FPackageName::GetAssetPackageExtension());
			if (!UPackage::SavePackage(Pkg, nullptr, *File, Args))
			{
				UE_LOG(LogRoseImport, Error, TEXT("%s: FAILED to save the skeleton"), G.Folder);
				++Failures;
			}
		}
	}

	UE_LOG(LogRoseImport, Display,
		TEXT("=== legacy dummy sockets: %d added, %d failure(s) ==="), TotalAdded, Failures);
	return Failures > 0 ? 1 : 0;
}
