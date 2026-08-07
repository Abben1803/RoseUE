#include "RoseSkeletalImporter.h"

#include "RoseEditor.h"
#include "RoseMaterialBuilder.h"
#include "RoseObjectBuilder.h"
#include "RoseObjectFormats.h"
#include "RosePathResolver.h"
#include "RoseSkeletalFormats.h"

#include "Animation/AnimData/IAnimationDataController.h"
#include "Animation/AnimSequence.h"
#include "Animation/Skeleton.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/SkeletalMeshSocket.h"
#include "Engine/SkinnedAssetCommon.h"
#include "HAL/FileManager.h"
#include "ImportUtils/SkeletalMeshImportUtils.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Rendering/SkeletalMeshLODImporterData.h"
#include "Rendering/SkeletalMeshModel.h"
#include "ReferenceSkeleton.h"
#include "SkeletalMeshBuilder.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* kSkeletalRoot = TEXT("/Game/Rose/Characters");
	// Character parts parent to M_RoseChar, NOT the object master.
	const TCHAR* kCharacterMaster = TEXT("/Game/Rose/Characters/M_RoseChar.M_RoseChar");

	// ROSE -> UE for a CHARACTER RIG is the IDENTITY.  This is not laziness; it
	// is the only conversion that keeps the character's handedness.
	//
	// ROSE and UE are both LEFT-handed and both Z-up.  Same handedness, same up
	// axis => no basis change is required at all.  (ROSE calls +Y "north" and UE
	// calls +X "forward", but that is a YAW, not a basis change — it is why NPC
	// placement carries ROSE_YAW_OFF = 90.)
	//
	// What we must NOT do is negate Y.  That matrix has determinant -1: it is a
	// REFLECTION, and a reflected rig is a mirrored character — its left hand is
	// where the right should be, so `b1_rhand` holds the sword on the wrong side
	// and asymmetric armour is backwards.  The old glTF pipeline did exactly this
	// (BASIS with det -1) and paid for it with a permanent SetRelativeScale3D
	// (-1,1,1) on the body to flip it back at runtime. 
	//
	// Bind pose will not reveal a mistake here (skinning resolves to identity);
	// it shows up as a mirrored character under animation.
	//
	// The map importer's Y negate is a separate, world-PLACEMENT convention
	// matched against the legacy levels.  It does not apply to a rig's own space:
	// where a character stands is the actor transform's job.
	// IDENTITY, verified against the engine source.
	//
	// zz_skeleton.cpp reads a bone as read_float3(translation) + a W-FIRST
	// quaternion and applies NO axis swap — only ZZ_SCALE_IN.  Our reader
	// already matches (ReadQuatWFirst), so there is nothing left to convert.
	//
	// An (X,Z,Y) swap was tried here and reverted: it contradicts the loader,
	// and swapping two axes is a determinant -1 basis change, which mirrors the
	// rig.  Bind pose hides that completely — it only shows under animation.
	// Bone names are kept EXACTLY as the ZMD has them.
	//
	// Renaming b1_lhand <-> b1_rhand was tried and removed: measuring the
	// animation settles it.  In ONEHAND_ATTACK01_F1.ZMO the moving chain is
	// b1_rhand (13.34), b1_rclavicle (12.45), b1_rupperarm (4.89) against
	// b1_lupperarm (4.42) — ROSE swings the b1_r* arm, so b1_rhand IS the
	// weapon hand and anything attached there follows the swing.  Renaming it
	// points the weapon at the arm that stays still.
	FORCEINLINE FString RoseFixBoneName(const FString& In)
	{
		return In;
	}

	FORCEINLINE FVector3f RoseToUEPos(const FVector3f& V)
	{
		return V;
	}

	FORCEINLINE FQuat4f RoseToUERot(const FQuat4f& Q)
	{
		return Q;
	}

	struct FSlotSpec
	{
		const TCHAR* Key;       // "BODY"
		const TCHAR* ZscFmt;    // "AVATAR/LIST_%sBODY.ZSC" (%s = M or W)
		bool FRoseSkeletalImportOptions::* Flag;
	};

	// FACEITEM has no gender prefix AND the shipped filename is misspelt.
	const TCHAR* kFaceItemZsc = TEXT("AVATAR/LIST_FACEIEM.ZSC");

	// FullyLoad right after CreatePackage, NEVER just before SavePackage.
	//
	// SavePackage refuses a package that is not fully loaded (it would drop the
	// exports never read in) — but doing it late is worse than not at all: it
	// pulls the on-disk exports in ON TOP of the object already built, and the
	// bulkdata then fails validation with "invalid payload", fatally, at the end
	// of a long run.
	UPackage* MakeWritablePackage(const FString& PkgName)
	{
		UPackage* Pkg = CreatePackage(*PkgName);
		if (Pkg)
			Pkg->FullyLoad();
		return Pkg;
	}

	FString SanitiseName(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (TCHAR C : In)
			Out.AppendChar((FChar::IsAlnum(C) || C == TEXT('_')) ? C : TEXT('_'));
		return Out;
	}

	void SavePkg(UPackage* Pkg, UObject* Asset)
	{
		if (!Pkg || !Asset)
			return;
		Pkg->MarkPackageDirty();
		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;
		if (UPackage::SavePackage(Pkg, Asset,
			*FPackageName::LongPackageNameToFilename(
				Pkg->GetName(), FPackageName::GetAssetPackageExtension()),
			Args))
		{
			// Leave it clean: a dirty package is one the editor autosave will
			// re-serialise later, which is where texture bulkdata saves blow up.
			Pkg->SetDirtyFlag(false);
		}
		else
		{
			UE_LOG(LogRoseImport, Error, TEXT("failed to save %s"), *Pkg->GetName());
		}
	}

	// ── skeleton ───────────────────────────────────────────────────────────
	// ZMD bone transforms are LOCAL (relative to the parent), which is what
	// FReferenceSkeleton wants, so no accumulation is needed here.
	void BuildRefSkeleton(const FRoseZMD& Zmd, FReferenceSkeleton& OutRef, USkeleton* Skeleton)
	{
		OutRef.Empty();
		FReferenceSkeletonModifier Modifier(OutRef, Skeleton);

		for (int32 i = 0; i < Zmd.Bones.Num(); ++i)
		{
			const FRoseBone& B = Zmd.Bones[i];
			// Bone 0 is the root; its stored parent id is 0 and must become
			// INDEX_NONE or the skeleton is cyclic.
			const int32 Parent = (i == 0) ? INDEX_NONE : B.ParentId;

			const FTransform Local(
				FQuat(RoseToUERot(B.Rotation)),
				FVector(RoseToUEPos(B.Translation)),
				FVector::OneVector);

			// Bone names must be UNIQUE — FReferenceSkeletonModifier::Add asserts
			// FindRawBoneIndex(Name) == INDEX_NONE.
			//
			// Avatar ZMDs never repeat a name, but NPC ones do (several of the
			// 274 monster skeletons reuse a label), and the assert kills the whole
			// import rather than that one model.  Disambiguating is safe here:
			// ZMO tracks bind by bone INDEX, not by name, so a renamed duplicate
			// still animates correctly.
			FString BoneLabel = RoseFixBoneName(B.Name);
			if (BoneLabel.IsEmpty())
				BoneLabel = FString::Printf(TEXT("bone_%d"), i);
			if (OutRef.FindRawBoneIndex(FName(*BoneLabel)) != INDEX_NONE)
			{
				const FString Base = BoneLabel;
				for (int32 Suffix = 1; ; ++Suffix)
				{
					BoneLabel = FString::Printf(TEXT("%s_%d"), *Base, Suffix);
					if (OutRef.FindRawBoneIndex(FName(*BoneLabel)) == INDEX_NONE)
						break;
				}
			}
			Modifier.Add(FMeshBoneInfo(FName(*BoneLabel), BoneLabel, Parent), Local);
		}
	}

	// Bone bind-world transforms, accumulated down the ZMD hierarchy.  Rigid
	// parts are authored in a bone's LOCAL space, so their vertices have to be
	// pre-multiplied by this to land in mesh space.
	TArray<FTransform> BuildBindWorld(const FRoseZMD& Zmd)
	{
		TArray<FTransform> World;
		World.SetNum(Zmd.Bones.Num());
		for (int32 i = 0; i < Zmd.Bones.Num(); ++i)
		{
			const FRoseBone& B = Zmd.Bones[i];
			const FTransform Local(
				FQuat(RoseToUERot(B.Rotation)),
				FVector(RoseToUEPos(B.Translation)),
				FVector::OneVector);
			World[i] = (i == 0 || !World.IsValidIndex(B.ParentId) || B.ParentId == i)
				? Local
				: Local * World[B.ParentId];
		}
		return World;
	}

	// PER-SLOT ATTACHMENT CONVENTION.
	//
	// Rigid appearance parts carry NO skin weights and NO bone/dummy tag — the
	// client hardcodes where each body part attaches (CCharMODEL::CreatePART,
	// BODY_PART_* in cjustmodelavt.h), so the data alone cannot tell us.
	//
	// Avatar skeleton: b1_head is bone 4; dummies p_04/p_05/p_06 all parent to
	// it.  Cap uses DUMMY 6 (the top of the head) — the head BONE sits at the
	// neck, so binding a hat to it drops the hat through the skull.
	struct FSlotAttach { bool bDummy; int32 Index; };

	bool DefaultAttachment(const FString& SlotKey, const FRoseZMD& Zmd, FSlotAttach& Out)
	{
		auto FindBone = [&Zmd](const TCHAR* Name) -> int32
		{
			for (int32 i = 0; i < Zmd.Bones.Num(); ++i)
				if (Zmd.Bones[i].Name.Equals(Name, ESearchCase::IgnoreCase))
					return i;
			return INDEX_NONE;
		};

		if (SlotKey == TEXT("CAP"))
		{
			// DUMMY_IDX_CAP
			if (Zmd.Dummies.IsValidIndex(6)) { Out = { true, 6 }; return true; }
			return false;
		}
		if (SlotKey == TEXT("FACE") || SlotKey == TEXT("HAIR") || SlotKey == TEXT("FACEITEM"))
		{
			const int32 Head = FindBone(TEXT("b1_head"));
			if (Head != INDEX_NONE) { Out = { false, Head }; return true; }
			return false;
		}
		return false;
	}

	// Where a rigid part attaches: a DUMMY (preferred — cap uses dummy 6, the
	// top of the head, whereas the head BONE is at the neck) or a bone.
	// Returns the bone to bind to and the bind-world transform to bake in.
	bool ResolveAttachment(const FRoseZscPart& Part, const FString& SlotKey,
		const FRoseZMD& Zmd, const TArray<FTransform>& BindWorld,
		int32& OutBone, FTransform& OutWorld)
	{
		// The ZSC tag wins when present; otherwise fall back to the client's
		// per-slot convention.
		int32 DummyIdx = Part.DummyIdx;
		int32 BoneIdx = Part.BoneIdx;
		if (DummyIdx < 0 && BoneIdx < 0)
		{
			FSlotAttach Attach;
			if (DefaultAttachment(SlotKey, Zmd, Attach))
			{
				if (Attach.bDummy) DummyIdx = Attach.Index;
				else               BoneIdx = Attach.Index;
			}
		}

		if (DummyIdx >= 0 && Zmd.Dummies.IsValidIndex(DummyIdx))
		{
			const FRoseDummy& D = Zmd.Dummies[DummyIdx];
			if (!BindWorld.IsValidIndex(D.ParentId))
				return false;
			const FTransform DummyLocal(
				FQuat(RoseToUERot(D.Rotation)),
				FVector(RoseToUEPos(D.Translation)),
				FVector::OneVector);
			OutBone = D.ParentId;
			OutWorld = DummyLocal * BindWorld[D.ParentId];
			return true;
		}
		if (BoneIdx >= 0 && BindWorld.IsValidIndex(BoneIdx))
		{
			OutBone = BoneIdx;
			OutWorld = BindWorld[BoneIdx];
			return true;
		}
		return false;
	}

	// ── motion (ZMO -> UAnimSequence) ──────────────────────────────────────
	//
	// A ZMO is frame-major channel data: one channel per (bone, component), each
	// holding NumFrames values.  The transforms are LOCAL to the parent, exactly
	// like the ZMD bind pose, so they go straight into a bone track — with the
	// same Y mirror applied, or the animation fights the skeleton it plays on.
	//
	// Only bones the ZMO actually animates get a track; everything else falls
	// through to the reference pose, which is what ROSE does too.
	//
	// UE key counts: SetNumberOfFrames(N) yields N+1 KEYS, so a ZMO with F
	// frames is F-1 frames here.  Getting this off by one silently truncates or
	// duplicates the last pose of every clip.
	bool BuildAnimSequence(const FRoseZMO& Zmo, const FRoseZMD& Zmd, USkeleton* Skeleton,
		UAnimSequence* Seq)
	{
		const int32 NumFrames = Zmo.NumFrames;
		if (NumFrames <= 0 || Zmd.Bones.Num() == 0)
			return false;

		// Gather the channels per bone first: position and rotation arrive as
		// SEPARATE channels that share a refer_id.
		struct FBoneTrack
		{
			const TArray<FVector3f>* Positions = nullptr;
			const TArray<FQuat4f>* Rotations = nullptr;
			const TArray<float>* Scales = nullptr;
		};
		TMap<int32, FBoneTrack> Tracks;

		for (const FRoseZmoChannel& Ch : Zmo.Channels)
		{
			if (!Zmd.Bones.IsValidIndex(Ch.ReferId))
				continue;
			FBoneTrack& T = Tracks.FindOrAdd(Ch.ReferId);
			if ((Ch.ChannelType & ROSE_CTYPE_POSITION) && Ch.Positions.Num() >= NumFrames)
				T.Positions = &Ch.Positions;
			if ((Ch.ChannelType & ROSE_CTYPE_ROTATION) && Ch.Rotations.Num() >= NumFrames)
				T.Rotations = &Ch.Rotations;
			if ((Ch.ChannelType & ROSE_CTYPE_SCALE) && Ch.Scales.Num() >= NumFrames)
				T.Scales = &Ch.Scales;
		}
		if (Tracks.Num() == 0)
			return false;

		Seq->SetSkeleton(Skeleton);

		IAnimationDataController& Controller = Seq->GetController();
		// bShouldTransact = false throughout: this is an import, not a user edit,
		// and building an undo transaction per key is ruinously slow over 543 clips.
		Controller.OpenBracket(NSLOCTEXT("Rose", "ImportZMO", "Importing ROSE motion"), false);
		Controller.InitializeModel();
		Controller.SetFrameRate(FFrameRate((uint32)FMath::Max(1, Zmo.Fps), 1u), false);
		Controller.SetNumberOfFrames(FFrameNumber(NumFrames - 1), false);

		for (const TPair<int32, FBoneTrack>& Pair : Tracks)
		{
			const FRoseBone& Bone = Zmd.Bones[Pair.Key];
			const FBoneTrack& T = Pair.Value;

			// A track has to carry all three components for every key, so any
			// channel the ZMO omits is held at the bind-pose value.
			const FVector3f BindPos = RoseToUEPos(Bone.Translation);
			const FQuat4f BindRot = RoseToUERot(Bone.Rotation);

			TArray<FVector3f> PosKeys;
			TArray<FQuat4f> RotKeys;
			TArray<FVector3f> ScaleKeys;
			PosKeys.Reserve(NumFrames);
			RotKeys.Reserve(NumFrames);
			ScaleKeys.Reserve(NumFrames);

			for (int32 f = 0; f < NumFrames; ++f)
			{
				// ZMO positions are centimetres already — no scaling, only the
				// mirror (RoseSkeletalFormats.h).
				PosKeys.Add(T.Positions ? RoseToUEPos((*T.Positions)[f]) : BindPos);
				RotKeys.Add(T.Rotations ? RoseToUERot((*T.Rotations)[f]) : BindRot);
				const float S = T.Scales ? (*T.Scales)[f] : 1.f;
				ScaleKeys.Add(FVector3f(S, S, S));
			}

			const FName BoneName(*RoseFixBoneName(Bone.Name));
			Controller.AddBoneCurve(BoneName, false);
			Controller.SetBoneTrackKeys(BoneName, PosKeys, RotKeys, ScaleKeys, false);
		}

		Controller.NotifyPopulated();
		Controller.CloseBracket(false);
		return true;
	}

	// A USkeleton cannot be given a reference skeleton directly — its bone tree
	// is built FROM a skinned asset (RecreateBoneTree).  So the asset is created
	// empty here and populated by the first mesh that uses it.
	USkeleton* CreateSkeletonAsset(const FString& AssetName, TArray<UObject*>& OutCreated)
	{
		const FString PkgName = FString::Printf(TEXT("%s/%s"), kSkeletalRoot, *AssetName);
		UPackage* Pkg = MakeWritablePackage(PkgName);

		USkeleton* Skeleton = NewObject<USkeleton>(Pkg, *AssetName, RF_Public | RF_Standalone);
		FAssetRegistryModule::AssetCreated(Skeleton);
		OutCreated.Add(Skeleton);
		return Skeleton;
	}

	// FSkeletalMeshImportData carries its OWN copy of the skeleton; without it
	// the builder has nothing to bind influences against.
	void FillRefBones(const FRoseZMD& Zmd, FSkeletalMeshImportData& Data)
	{
		Data.RefBonesBinary.Reset(Zmd.Bones.Num());
		for (int32 i = 0; i < Zmd.Bones.Num(); ++i)
		{
			const FRoseBone& B = Zmd.Bones[i];

			SkeletalMeshImportData::FBone Bone;
			Bone.Name = RoseFixBoneName(B.Name);
			Bone.Flags = 0;
			Bone.ParentIndex = (i == 0) ? INDEX_NONE : B.ParentId;
			Bone.NumChildren = 0;
			for (int32 c = 0; c < Zmd.Bones.Num(); ++c)
				if (c != 0 && Zmd.Bones[c].ParentId == i)
					++Bone.NumChildren;

			Bone.BonePos.Transform = FTransform3f(
				RoseToUERot(B.Rotation), RoseToUEPos(B.Translation), FVector3f::OneVector);
			Bone.BonePos.Length = 0.f;
			Bone.BonePos.XSize = Bone.BonePos.YSize = Bone.BonePos.ZSize = 1.f;

			Data.RefBonesBinary.Add(MoveTemp(Bone));
		}
	}
}

bool RoseImportSkeletal(const FRoseSkeletalImportOptions& Options,
	FRoseSkeletalImportResult& Result)
{
	const double TimeStart = FPlatformTime::Seconds();

	FRosePathResolver Resolver(Options.AssetRoot);
	// Build/repair the avatar master before anything parents to it.  The version
	// ue5_import_modular.py produced left roughness and specular at UE's 0.5
	// defaults, which renders ROSE's flat diffuse as shiny plastic.
	RoseMaterials::EnsureCharacterMaster();

	FRoseAssetCache Cache(kSkeletalRoot, Resolver, kCharacterMaster, /*bForceTwoSided=*/true);
	TArray<UObject*> Created;

	struct FGender
	{
		const TCHAR* Key;        // "F" / "M" — how the rest of the codebase keys it
		const TCHAR* ZscPrefix;  // "W" / "M" — how the FILES are actually named
		const TCHAR* Zmd;
		bool bWanted;
		USkeleton* Skeleton = nullptr;
		FRoseZMD Zmd_;
	};

	FGender Genders[] =
	{
		{ TEXT("F"), TEXT("W"), TEXT("AVATAR/FEMALE.ZMD"), Options.bFemale },
		{ TEXT("M"), TEXT("M"), TEXT("AVATAR/MALE.ZMD"),   Options.bMale   },
	};

	// ── 1. skeletons ───────────────────────────────────────────────────────
	for (FGender& G : Genders)
	{
		if (!G.bWanted)
			continue;

		const FString ZmdPath = Resolver.Resolve(G.Zmd);
		if (ZmdPath.IsEmpty() || !G.Zmd_.Load(ZmdPath))
		{
			UE_LOG(LogRoseImport, Error, TEXT("cannot read %s"), G.Zmd);
			continue;
		}

		const FString SkelName = FString::Printf(TEXT("SK_Rose_%s_Skeleton"), G.Key);
		G.Skeleton = CreateSkeletonAsset(SkelName, Created);
		if (G.Skeleton)
		{
			++Result.SkeletonsBuilt;
			UE_LOG(LogRoseImport, Log, TEXT("skeleton %s: %d bones, %d dummies"),
				*SkelName, G.Zmd_.Bones.Num(), G.Zmd_.Dummies.Num());
		}
		if (FCString::Strcmp(G.Key, TEXT("F")) == 0)
			Result.BonesFemale = G.Zmd_.Bones.Num();
		else
			Result.BonesMale = G.Zmd_.Bones.Num();
	}

	// ── 2. skinned parts ───────────────────────────────────────────────────
	const FSlotSpec Slots[] =
	{
		{ TEXT("BODY"), TEXT("AVATAR/LIST_%sBODY.ZSC"), &FRoseSkeletalImportOptions::bBody },
		{ TEXT("ARMS"), TEXT("AVATAR/LIST_%sARMS.ZSC"), &FRoseSkeletalImportOptions::bArms },
		{ TEXT("FOOT"), TEXT("AVATAR/LIST_%sFOOT.ZSC"), &FRoseSkeletalImportOptions::bFoot },
		{ TEXT("CAP"),  TEXT("AVATAR/LIST_%sCAP.ZSC"),  &FRoseSkeletalImportOptions::bCap  },
		{ TEXT("FACE"), TEXT("AVATAR/LIST_%sFACE.ZSC"), &FRoseSkeletalImportOptions::bFace },
		{ TEXT("HAIR"), TEXT("AVATAR/LIST_%sHAIR.ZSC"), &FRoseSkeletalImportOptions::bHair },
	};

	for (FGender& G : Genders)
	{
		if (!G.bWanted || !G.Skeleton)
			continue;

		const TArray<FTransform> BindWorld = BuildBindWorld(G.Zmd_);

		TArray<TPair<FString, FString>> Packs;   // (slot key, zsc path)
		for (const FSlotSpec& Slot : Slots)
		{
			if (!(Options.*(Slot.Flag)))
				continue;
			Packs.Emplace(Slot.Key,
				FString(Slot.ZscFmt).Replace(TEXT("%s"), G.ZscPrefix));
		}
		// Face items are shared between genders — import them under F only so
		// the same mesh is not built twice.
		if (Options.bFaceItem && FCString::Strcmp(G.Key, TEXT("F")) == 0)
			Packs.Emplace(TEXT("FACEITEM"), kFaceItemZsc);

		for (const TPair<FString, FString>& Pack : Packs)
		{
			const FString ZscFull = Resolver.Resolve(Pack.Value);
			FRoseZSC Zsc;
			if (ZscFull.IsEmpty() || !Zsc.Load(ZscFull))
			{
				UE_LOG(LogRoseImport, Warning, TEXT("%s/%s: cannot read %s"),
					G.Key, *Pack.Key, *Pack.Value);
				continue;
			}

			FRoseSkeletalPackResult PackResult;
			PackResult.Kind = FString::Printf(TEXT("%s/%s"), G.Key, *Pack.Key);
			PackResult.ObjectsInPack = Zsc.Objects.Num();

			const int32 Limit = Options.MaxItemsPerPack > 0
				? FMath::Min(Options.MaxItemsPerPack, Zsc.Objects.Num())
				: Zsc.Objects.Num();

			for (int32 Id = 0; Id < Limit; ++Id)
			{
				// -item=<id> builds exactly that object and nothing else, so a
				// material or transform change can be checked in seconds instead
				// of a twenty-minute full pass.
				if (Options.OnlyItemId >= 0 && Id != Options.OnlyItemId)
					continue;

				const TArray<FRoseZscPart>& Parts = Zsc.Objects[Id].Parts;
				if (Parts.Num() == 0)
				{
					++PackResult.Empty;
					continue;
				}

				const FString AssetName = SanitiseName(FString::Printf(
					TEXT("SK_%s_%s_%d"), G.Key, *Pack.Key, Id));
				const FString PkgName = FString::Printf(TEXT("%s/%s/%s/%s"),
					kSkeletalRoot, G.Key, *Pack.Key, *AssetName);

				if (Options.bSkipExisting && FPackageName::DoesPackageExist(PkgName))
				{
					++PackResult.Skipped;
					continue;
				}

				// One FSkeletalMeshImportData per item, every ZSC part appended
				// as its own material section.
				FSkeletalMeshImportData ImportData;
				ImportData.bHasNormals = true;
				// TRUE, with every wedge WHITE — not false.
				//
				// With this false the build still produces a colour buffer, and
				// it comes out PURE BLACK (measured: 422/422 vertices on
				// SK_F_BODY_0).  Black vertex colour is not neutral: anything
				// that multiplies by it renders black, and under Substrate
				// vertex colour participates in shading paths where the legacy
				// renderer ignored it — the same Substrate quirk that makes
				// BLEND_Translucent render black/dithered here.
				//
				// Declaring the channel and filling it WHITE makes it neutral
				// wherever it is read, instead of leaving a black buffer behind
				// that every other check passes over.
				ImportData.bHasVertexColors = true;
				ImportData.NumTexCoords = 1;
				FillRefBones(G.Zmd_, ImportData);

				TArray<UMaterialInstanceConstant*> SectionMaterials;
				bool bAnyGeometry = false;

				for (int32 p = 0; p < Parts.Num(); ++p)
				{
					const FRoseZscPart& Part = Parts[p];
					if (!Zsc.MeshFiles.IsValidIndex(Part.MeshId))
					{
						UE_LOG(LogRoseImport, Warning, TEXT("%s %d part %d: bad mesh id %d"),
							*PackResult.Kind, Id, p, Part.MeshId);
						continue;
					}

					const FString ZmsFull = Resolver.Resolve(Zsc.MeshFiles[Part.MeshId]);
					FRoseZMS Zms;
					if (ZmsFull.IsEmpty() || !Zms.Load(ZmsFull))
					{
						UE_LOG(LogRoseImport, Warning, TEXT("%s %d part %d: ZMS unreadable '%s'"),
							*PackResult.Kind, Id, p, *Zsc.MeshFiles[Part.MeshId]);
						continue;
					}
					// Rigid parts carry no skin weights: pin every vertex to
					// one bone and bake the attachment's bind-world transform
					// into the positions.
					int32 RigidBone = INDEX_NONE;
					FTransform RigidWorld = FTransform::Identity;
					if (!Zms.HasSkin())
					{
						if (!ResolveAttachment(Part, Pack.Key, G.Zmd_, BindWorld, RigidBone, RigidWorld))
						{
							UE_LOG(LogRoseImport, Warning,
								TEXT("%s %d part %d: no skin and no bone/dummy (bone %d dummy %d)"),
								*PackResult.Kind, Id, p, Part.BoneIdx, Part.DummyIdx);
							continue;
						}
					}

					const float VertexScale = (Zms.Version >= 7) ? 100.f : 1.f;
					const int32 MatIndex = ImportData.Materials.Num();

					SkeletalMeshImportData::FMaterial Mat;
					Mat.MaterialImportName = FString::Printf(TEXT("Mat%d"), p);
					ImportData.Materials.Add(Mat);

					UMaterialInstanceConstant* PartMat = nullptr;
					if (Zsc.Materials.IsValidIndex(Part.MaterialId))
						PartMat = Cache.GetMaterial(Zsc.Materials[Part.MaterialId]);
					SectionMaterials.Add(PartMat);

					// ── eye alternates -> their own sections ──────────────────
					//
					// A face ZMS stores BOTH eye states in one index buffer: the
					// first NumClipFaces triangles are the closed eyes and the
					// last NumClipFaces the open ones (zz_renderer_d3d.cpp draws
					// a sub-range to pick one).  Emitting them as a single
					// section renders both at once and leaves nothing to toggle,
					// so they get their own sections here and the runtime hides
					// whichever is not wanted.
					const int32 ClipFaces =
						(Zms.NumClipFaces > 0 && Zms.NumClipFaces * 2 <= Zms.Faces.Num())
						? Zms.NumClipFaces : 0;
					int32 EyesClosedMat = INDEX_NONE, EyesOpenMat = INDEX_NONE;
					if (ClipFaces > 0)
					{
						EyesClosedMat = ImportData.Materials.Num();
						SkeletalMeshImportData::FMaterial MC;
						MC.MaterialImportName = FString::Printf(TEXT("Mat%d_eyesclosed"), p);
						ImportData.Materials.Add(MC);
						SectionMaterials.Add(PartMat);

						EyesOpenMat = ImportData.Materials.Num();
						SkeletalMeshImportData::FMaterial MO;
						MO.MaterialImportName = FString::Printf(TEXT("Mat%d_eyesopen"), p);
						ImportData.Materials.Add(MO);
						SectionMaterials.Add(PartMat);
					}

					const int32 PointBase = ImportData.Points.Num();

					for (int32 v = 0; v < Zms.Positions.Num(); ++v)
					{
						FVector3f P = RoseToUEPos(Zms.Positions[v] * VertexScale);
						if (RigidBone != INDEX_NONE)
							P = FVector3f(RigidWorld.TransformPosition(FVector(P)));
						ImportData.Points.Add(P);
						ImportData.PointToRawMap.Add(PointBase + v);

						if (RigidBone != INDEX_NONE)
						{
							SkeletalMeshImportData::FRawBoneInfluence Inf;
							Inf.VertexIndex = PointBase + v;
							Inf.BoneIndex = RigidBone;
							Inf.Weight = 1.f;
							ImportData.Influences.Add(Inf);
						}

						// Influences: the ZMS blend index is an index into the
						// ZMS's OWN bone table, which maps to the ZMD bone.
						if (Zms.HasSkin())
						{
							const FRoseSkinVertex& S = Zms.Skin[v];
							for (int32 w = 0; w < 4; ++w)
							{
								if (S.Weights[w] <= 0.f)
									continue;
								const int32 Local = S.Bones[w];
								if (!Zms.BoneTable.IsValidIndex(Local))
									continue;
								const int32 BoneIndex = Zms.BoneTable[Local];
								if (BoneIndex < 0 || BoneIndex >= G.Zmd_.Bones.Num())
									continue;

								SkeletalMeshImportData::FRawBoneInfluence Inf;
								Inf.VertexIndex = PointBase + v;
								Inf.BoneIndex = BoneIndex;
								Inf.Weight = S.Weights[w];
								ImportData.Influences.Add(Inf);
							}
						}
					}

					for (int32 FaceIdx = 0; FaceIdx < Zms.Faces.Num(); ++FaceIdx)
					{
						const FIntVector& Face = Zms.Faces[FaceIdx];

						// First ClipFaces = closed eyes, last ClipFaces = open.
						int32 UseMat = MatIndex;
						if (ClipFaces > 0)
						{
							if (FaceIdx < ClipFaces)
								UseMat = EyesClosedMat;
							else if (FaceIdx >= Zms.Faces.Num() - ClipFaces)
								UseMat = EyesOpenMat;
						}

						if (!Zms.Positions.IsValidIndex(Face.X) ||
							!Zms.Positions.IsValidIndex(Face.Y) ||
							!Zms.Positions.IsValidIndex(Face.Z))
							continue;
						// Degenerates ship in real data and break the builder.
						if (Face.X == Face.Y || Face.Y == Face.Z || Face.X == Face.Z)
							continue;

						SkeletalMeshImportData::FTriangle Tri;
						Tri.MatIndex = UseMat;
						Tri.SmoothingGroups = 1;
						Tri.AuxMatIndex = 0;

						// ROSE winding is kept AS-IS.  Verified correct in-engine —
						// reversing it was tried and rejected.
						const int32 Order[3] = { Face.X, Face.Y, Face.Z };
						for (int32 c = 0; c < 3; ++c)
						{
							const int32 SrcV = Order[c];

							SkeletalMeshImportData::FVertex Wedge;
							Wedge.VertexIndex = PointBase + SrcV;
							Wedge.MatIndex = (uint8)UseMat;
							Wedge.Color = FColor::White;
							Wedge.UVs[0] = Zms.UV0.IsValidIndex(SrcV)
								? Zms.UV0[SrcV] : FVector2f::ZeroVector;

							Tri.WedgeIndex[c] = ImportData.Wedges.Add(Wedge);
							FVector3f N = Zms.Normals.IsValidIndex(SrcV)
								? RoseToUEPos(Zms.Normals[SrcV]) : FVector3f::ZAxisVector;
							if (RigidBone != INDEX_NONE)
								N = FVector3f(RigidWorld.TransformVector(FVector(N)).GetSafeNormal());
							Tri.TangentZ[c] = N;
							Tri.TangentX[c] = FVector3f::ZeroVector;
							Tri.TangentY[c] = FVector3f::ZeroVector;
						}

						ImportData.Faces.Add(Tri);
						bAnyGeometry = true;
					}
				}

				if (!bAnyGeometry)
				{
					++PackResult.Failed;
					continue;
				}

				UPackage* Pkg = MakeWritablePackage(PkgName);
				USkeletalMesh* Mesh = NewObject<USkeletalMesh>(
					Pkg, *AssetName, RF_Public | RF_Standalone);

				FReferenceSkeleton Ref;
				BuildRefSkeleton(G.Zmd_, Ref, G.Skeleton);
				Mesh->SetRefSkeleton(Ref);
				Mesh->SetSkeleton(G.Skeleton);

				FSkeletalMeshModel* Model = Mesh->GetImportedModel();
				Model->LODModels.Empty();
				Model->LODModels.Add(new FSkeletalMeshLODModel());

				FSkeletalMeshLODInfo& LodInfo = Mesh->AddLODInfo();
				LodInfo.ReductionSettings.NumOfTrianglesPercentage = 1.f;
				LodInfo.BuildSettings.bRecomputeNormals = false;
				LodInfo.BuildSettings.bRecomputeTangents = true;
				LodInfo.BuildSettings.bUseMikkTSpace = true;

				// FSkeletalMeshImportData -> FMeshDescription -> the LOD.
				//
				// SaveLODImportedData/SetLODImportedDataVersions are deprecated
				// (5.4) in favour of CommitMeshDescription; the versions call has
				// no replacement at all because mesh-description bulk data does
				// not surface versioning.  GetMeshDescription needs the ref
				// skeleton and the build settings already set on the mesh, which
				// is why it runs after SetRefSkeleton and AddLODInfo above.
				FMeshDescription MeshDesc;
				if (!ImportData.GetMeshDescription(Mesh, &LodInfo.BuildSettings, MeshDesc))
				{
					UE_LOG(LogRoseImport, Error,
						TEXT("%s: could not convert import data to a mesh description"),
						*AssetName);
					++PackResult.Failed;
					continue;
				}
				Mesh->CreateMeshDescription(0, MoveTemp(MeshDesc));
				Mesh->CommitMeshDescription(0);

				for (int32 s = 0; s < SectionMaterials.Num(); ++s)
				{
					// Never a null material — see the same guard in
					// RoseEquipmentImporter.  A ZSC part can carry a sentinel
					// material id (65535), the IsValidIndex check above leaves the
					// pointer null, and the null reaches the async build worker as
					// an access violation that kills the entire run.  The skinned
					// ZSCs happen not to contain one today; that is luck, not a
					// guarantee.
					UMaterialInterface* SlotMat = SectionMaterials[s];
					if (!SlotMat)
					{
						SlotMat = UMaterial::GetDefaultMaterial(MD_Surface);
						UE_LOG(LogRoseImport, Warning,
							TEXT("skeletal section %d has no material (bad ZSC "
							     "material id) — using the engine default"), s);
					}
					const FName SlotName(*FString::Printf(TEXT("Mat%d"), s));
					Mesh->GetMaterials().Add(
						FSkeletalMaterial(SlotMat, true, false, SlotName, SlotName));
				}

				Mesh->CalculateInvRefMatrices();

				Mesh->Build();
				Mesh->PostEditChange();

				// The skeleton asset is empty until a mesh populates it; the
				// first one recreates the bone tree, the rest merge into it.
				if (G.Skeleton->GetReferenceSkeleton().GetNum() == 0)
					G.Skeleton->RecreateBoneTree(Mesh);
				else
					G.Skeleton->MergeAllBonesToBoneTree(Mesh);

				FAssetRegistryModule::AssetCreated(Mesh);
				SavePkg(Pkg, Mesh);
				++PackResult.Built;
			}

			UE_LOG(LogRoseImport, Log, TEXT("%s: built %d, empty %d, skipped %d, failed %d"),
				*PackResult.Kind, PackResult.Built, PackResult.Empty,
				PackResult.Skipped, PackResult.Failed);
			Result.Packs.Add(PackResult);
		}
	}

	// ── 2b. dummy sockets ──────────────────────────────────────────────────
	// ROSE links equipment to ZMD DUMMIES, not to bones, and the dummy carries
	// both the offset and the orientation (DUMMY_IDX_CAP is the top of the head
	// while the head BONE is at the neck; the hand dummies orient a weapon in
	// the grip).  Expose each dummy as a UE socket so the runtime can attach to
	// it by name instead of carrying a hand-tuned transform per slot.
	//
	// This has to live on the SKELETON rather than be baked into the item: a
	// weapon is gender-neutral, but the hand dummy differs between FEMALE.ZMD
	// and MALE.ZMD.  Per-item grip orientation is the ZSC part transform, baked
	// into the mesh by the equipment importer — the two compose.
	//
	// Sockets are added after the parts loop because a skeleton has no bone tree
	// until a skinned mesh populates it (RecreateBoneTree above), and a socket
	// on an unknown bone is silently useless.
	for (FGender& G : Genders)
	{
		if (!G.bWanted || !G.Skeleton)
			continue;
		if (G.Skeleton->GetReferenceSkeleton().GetNum() == 0)
			continue;

		int32 Added = 0;
		for (int32 i = 0; i < G.Zmd_.Dummies.Num(); ++i)
		{
			const FRoseDummy& D = G.Zmd_.Dummies[i];
			if (!G.Zmd_.Bones.IsValidIndex(D.ParentId))
				continue;

			// Indexed, not named: the ZMD names are p_00, p_01, ... and the
			// client addresses them by index (dummy 0 = R_HAND, 1 = L_HAND,
			// 2 = L_SHIELD, 3 = back, 4 = DUMMY_IDX_MOUSE, 6 = DUMMY_IDX_CAP).
			const FName SocketName(*FString::Printf(TEXT("rose_dummy_%d"), i));
			if (G.Skeleton->FindSocket(SocketName))
				continue;

			USkeletalMeshSocket* Socket = NewObject<USkeletalMeshSocket>(G.Skeleton);
			Socket->SocketName = SocketName;
			Socket->BoneName = FName(*G.Zmd_.Bones[D.ParentId].Name);
			Socket->RelativeLocation = FVector(RoseToUEPos(D.Translation));
			Socket->RelativeRotation = FQuat(RoseToUERot(D.Rotation)).Rotator();
			G.Skeleton->Sockets.Add(Socket);
			++Added;
		}

		// Log unconditionally.  A silent step that produced nothing is
		// indistinguishable from a step that never ran, and the weapon grip
		// depends entirely on these sockets existing.
		UE_LOG(LogRoseImport, Log,
			TEXT("%s skeleton: %d dummy sockets added (%d dummies in ZMD, %d bones in tree)"),
			G.Key, Added, G.Zmd_.Dummies.Num(), G.Skeleton->GetReferenceSkeleton().GetNum());

		if (Added > 0)
		{
			G.Skeleton->MarkPackageDirty();
			SavePkg(G.Skeleton->GetOutermost(), G.Skeleton);
		}
	}

	// ── 3. motions ─────────────────────────────────────────────────────────
	// MOTION/AVATAR/*.ZMO -> every clip onto BOTH skeletons.
	//
	// The _F1/_M1 suffix says which body a clip was AUTHORED for, not which one
	// may play it: the male and female avatar rigs share their bone names, and
	// the runtime's own tables cross over — RoseLocoFemale asks for
	// baseMAGIC_ATTACK01_M1 and baseKARTAR_ATTACK01_M1 on a FEMALE character
	// (RoseWeaponData.h).  Importing by suffix would leave exactly those rows
	// resolving to nothing, which is the long-standing "Female base needs a
	// rebuild for the _M1 skill anims" gap.  Importing both is what makes a
	// one-click run produce a character with no missing animations.
	if (Options.bAnimations)
	{
		const FString MotionDir = Resolver.ResolveDir(TEXT("MOTION/AVATAR"));
		TArray<FString> ZmoFiles;
		if (!MotionDir.IsEmpty())
			IFileManager::Get().FindFiles(ZmoFiles, *(MotionDir / TEXT("*.ZMO")), true, false);

		if (ZmoFiles.Num() == 0)
			UE_LOG(LogRoseImport, Warning, TEXT("no ZMO files under MOTION/AVATAR"));

		for (FGender& G : Genders)
		{
			if (!G.bWanted || !G.Skeleton)
				continue;

			// Animations bind to the skeleton's bone tree, which stays EMPTY
			// until a skinned mesh populates it (RecreateBoneTree above).
			if (G.Skeleton->GetReferenceSkeleton().GetNum() == 0)
			{
				UE_LOG(LogRoseImport, Error,
					TEXT("%s skeleton has no bones yet — import at least one skinned "
						 "part before animations, or the anims bind to nothing"), G.Key);
				continue;
			}

			for (const FString& File : ZmoFiles)
			{
				const FString Stem = FPaths::GetBaseFilename(File);

				const FString AssetName = SanitiseName(
					FString::Printf(TEXT("A_%s_%s"), G.Key, *Stem));
				const FString PkgName = FString::Printf(TEXT("%s/%s/Anims/%s"),
					kSkeletalRoot, G.Key, *AssetName);

				if (Options.bSkipExisting && FPackageName::DoesPackageExist(PkgName))
					continue;

				FRoseZMO Zmo;
				if (!Zmo.Load(MotionDir / File))
					continue;

				// Vertex-morph ZMOs (flags, banners) address vertex GROUPS, not
				// bones, and cannot drive a skeleton.  The test is "does any
				// channel land on a bone", not "do they all" — a motion with a
				// few stray channels is still a usable motion, and that is what
				// rose_combine_anims.py does (it skips the channel, not the clip).
				bool bAnyBoneChannel = false;
				for (const FRoseZmoChannel& Ch : Zmo.Channels)
				{
					const bool bBoneComponent = (Ch.ChannelType &
						(ROSE_CTYPE_POSITION | ROSE_CTYPE_ROTATION | ROSE_CTYPE_SCALE)) != 0;
					if (bBoneComponent && G.Zmd_.Bones.IsValidIndex(Ch.ReferId))
					{
						bAnyBoneChannel = true;
						break;
					}
				}
				if (!bAnyBoneChannel)
				{
					++Result.AnimationsSkippedMorph;
					continue;
				}

				UPackage* Pkg = MakeWritablePackage(PkgName);
				UAnimSequence* Seq = FindObject<UAnimSequence>(Pkg, *AssetName);
				if (!Seq)
					Seq = NewObject<UAnimSequence>(Pkg, *AssetName, RF_Public | RF_Standalone);

				if (!BuildAnimSequence(Zmo, G.Zmd_, G.Skeleton, Seq))
					continue;

				Seq->PostEditChange();
				FAssetRegistryModule::AssetCreated(Seq);
				SavePkg(Pkg, Seq);
				++Result.AnimationsBuilt;
			}
		}

		UE_LOG(LogRoseImport, Log, TEXT("motions: built %d, skipped %d vertex-morph"),
			Result.AnimationsBuilt, Result.AnimationsSkippedMorph);
	}

	// Save the shared textures/materials once.
	if (const int32 SaveFailures = Cache.SaveCreated())
	{
		UE_LOG(LogRoseImport, Error,
			TEXT("%d shared asset package(s) failed to save"), SaveFailures);
	}

	Result.UniqueTextures = Cache.NumTextures();
	Result.UniqueMaterials = Cache.NumMaterials();
	Result.MissingAssets = Cache.NumMissing();
	Result.SecondsTotal = FPlatformTime::Seconds() - TimeStart;
	Result.bSuccess = Result.SkeletonsBuilt > 0;
	return Result.bSuccess;
}

// ─────────────────────────────────────────────────────────────────────────────
// NPCs / monsters (CHR-driven).  Lives here, not in its own translation unit,
// so it reuses this file's validated helpers — SanitiseName, MakeWritablePackage,
// BuildBindWorld, FillRefBones, BuildRefSkeleton, SavePkg — rather than growing a
// second mesh builder that can drift from the first.
// ─────────────────────────────────────────────────────────────────────────────
#include "RoseNpcImporter.h"
#include "RoseCharFormats.h"

namespace
{
	const TCHAR* kNpcRoot = TEXT("/Game/Rose/Npcs");

	// One USkeleton per distinct ZMD.  NPCs do NOT share a rig the way avatars
	// do — a monkey and a knight have different bone counts — so the skeleton is
	// keyed on the ZMD path and reused by every NPC that names it.
	struct FNpcRig
	{
		USkeleton* Skeleton = nullptr;
		FRoseZMD Zmd;
		TArray<FTransform> BindWorld;
	};
}

bool RoseImportNpcs(const FRoseNpcImportOptions& Options, FRoseNpcImportResult& Result)
{
	const double TimeStart = FPlatformTime::Seconds();
	FRosePathResolver Resolver(Options.AssetRoot);

	// Build the master FIRST.  FRoseAssetCache::GetMaterial returns nullptr the
	// moment Master is null, so on a project where the equipment import has not
	// run yet every NPC section would fall through to the engine's default grey
	// with no error anywhere.
	RoseMaterials::EnsureCharacterMaster();

	// bForceTwoSided — the SAME reason the avatar parts need it.
	//
	// NPC meshes are open shells like avatar parts, and most carry 2side=0 in
	// PART_NPC.ZSC.  Taking that flag literally sets bOverride_TwoSided with
	// TwoSided=false, which overrides M_RoseChar's two-sided default back to
	// ONE-sided — and RoseMonster then applies SetRelativeScale3D(-S,S,S), a
	// negative-determinant scale that REVERSES triangle winding.  Every front
	// face becomes back-facing and is culled, so what draws is the inside of the
	// far surface: a hollow shell that reads as a black (or pale) silhouette.
	//
	// This is the half of the character material fix that never reached the NPC
	// path; the unlit shading model was already inherited from the master.
	FRoseAssetCache Cache(kNpcRoot, Resolver, kCharacterMaster, /*bForceTwoSided=*/true,
		/*bAlphaIsSpecular=*/true);

	FRoseCHR Chr;
	if (!Chr.Load(Resolver.Resolve(TEXT("NPC/LIST_NPC.CHR"))))
	{
		UE_LOG(LogRoseImport, Error, TEXT("npc: cannot read NPC/LIST_NPC.CHR"));
		return false;
	}
	FRoseZSC Zsc;
	if (!Zsc.Load(Resolver.Resolve(TEXT("NPC/PART_NPC.ZSC"))))
	{
		UE_LOG(LogRoseImport, Error, TEXT("npc: cannot read NPC/PART_NPC.ZSC"));
		return false;
	}
	Result.ModelsInChr = Chr.Models.Num();

	TMap<FString, FNpcRig> Rigs;
	const int32 Limit = Options.MaxNpcs > 0
		? FMath::Min(Options.MaxNpcs, Chr.Models.Num()) : Chr.Models.Num();

	for (int32 Id = 0; Id < Chr.Models.Num(); ++Id)
	{
		if (Options.OnlyNpcId >= 0 && Id != Options.OnlyNpcId)
			continue;
		if (Options.OnlyNpcId < 0 && Result.Built + Result.Failed >= Limit)
			break;

		const FRoseChrModel& M = Chr.Models[Id];
		// An empty CHR slot is normal: LIST_NPC has gaps.
		if (!M.bValid || M.BodyPartIndices.Num() == 0)
		{
			++Result.Empty;
			continue;
		}

		const FString AssetName = SanitiseName(FString::Printf(TEXT("SK_NPC_%d"), Id));
		const FString PkgName = FString::Printf(TEXT("%s/%s"), kNpcRoot, *AssetName);
		if (Options.bSkipExisting && FPackageName::DoesPackageExist(PkgName))
		{
			++Result.Skipped;
			continue;
		}

		const FString ZmdRel = Chr.SkeletonPathFor(Id);
		if (ZmdRel.IsEmpty())
		{
			UE_LOG(LogRoseImport, Warning, TEXT("npc %d: no skeleton in the CHR"), Id);
			++Result.Failed;
			continue;
		}
		FNpcRig* Rig = Rigs.Find(ZmdRel);
		if (!Rig)
		{
			FNpcRig New;
			const FString ZmdFull = Resolver.Resolve(ZmdRel);
			if (ZmdFull.IsEmpty() || !New.Zmd.Load(ZmdFull))
			{
				UE_LOG(LogRoseImport, Warning, TEXT("npc %d: ZMD unreadable"), Id);
				++Result.Failed;
				continue;
			}
			const FString SkelName = SanitiseName(FString::Printf(
				TEXT("SK_NPC_%s_Skeleton"), *FPaths::GetBaseFilename(ZmdRel)));
			const FString SkelPkgName = FString::Printf(TEXT("%s/%s"), kNpcRoot, *SkelName);
			UPackage* SkelPkg = MakeWritablePackage(SkelPkgName);
			New.Skeleton = FindObject<USkeleton>(SkelPkg, *SkelName);
			if (!New.Skeleton)
			{
				New.Skeleton = NewObject<USkeleton>(SkelPkg, *SkelName,
					RF_Public | RF_Standalone);
				++Result.SkeletonsBuilt;
			}
			New.BindWorld = BuildBindWorld(New.Zmd);
			Rig = &Rigs.Add(ZmdRel, MoveTemp(New));
		}

		FSkeletalMeshImportData ImportData;
		FillRefBones(Rig->Zmd, ImportData);

		TArray<UMaterialInstanceConstant*> SectionMaterials;
		bool bAnyGeometry = false;

		for (int32 ObjIdx : M.BodyPartIndices)
		{
			if (!Zsc.Objects.IsValidIndex(ObjIdx))
				continue;
			for (const FRoseZscPart& Part : Zsc.Objects[ObjIdx].Parts)
			{
				if (!Zsc.MeshFiles.IsValidIndex(Part.MeshId))
					continue;
				const FString ZmsFull = Resolver.Resolve(Zsc.MeshFiles[Part.MeshId]);
				FRoseZMS Zms;
				if (ZmsFull.IsEmpty() || !Zms.Load(ZmsFull))
					continue;

				// Rigid parts pin to a bone.  NPC parts are normally skinned, and
				// the avatar per-slot attachment convention does not apply here,
				// so an untagged rigid part goes to its ZSC bone or the root
				// rather than being dropped.
				int32 RigidBone = INDEX_NONE;
				FTransform RigidWorld = FTransform::Identity;
				if (!Zms.HasSkin())
				{
					const int32 B = Part.BoneIdx >= 0 ? Part.BoneIdx : 0;
					RigidBone = Rig->BindWorld.IsValidIndex(B) ? B : 0;
					if (Rig->BindWorld.IsValidIndex(RigidBone))
						RigidWorld = Rig->BindWorld[RigidBone];
				}

				const float VertexScale = (Zms.Version >= 7) ? 100.f : 1.f;
				const int32 MatIndex = ImportData.Materials.Num();
				SkeletalMeshImportData::FMaterial Mat;
				Mat.MaterialImportName = FString::Printf(TEXT("Mat%d"), MatIndex);
				ImportData.Materials.Add(Mat);

				UMaterialInstanceConstant* PartMat = nullptr;
				if (Zsc.Materials.IsValidIndex(Part.MaterialId))
					PartMat = Cache.GetMaterial(Zsc.Materials[Part.MaterialId]);
				SectionMaterials.Add(PartMat);

				const int32 PointBase = ImportData.Points.Num();
				for (int32 v = 0; v < Zms.Positions.Num(); ++v)
				{
					FVector3f P = RoseToUEPos(Zms.Positions[v]) * VertexScale;
					if (RigidBone != INDEX_NONE)
						P = FVector3f(RigidWorld.TransformPosition(FVector(P)));
					ImportData.Points.Add(P);

					if (Zms.HasSkin())
					{
						// The ZMS blend index addresses the ZMS's OWN bone table,
						// which then maps to the ZMD bone — not the ZMD directly.
						const FRoseSkinVertex& S = Zms.Skin[v];
						for (int32 w = 0; w < 4; ++w)
						{
							if (S.Weights[w] <= 0.f)
								continue;
							const int32 Local = S.Bones[w];
							if (!Zms.BoneTable.IsValidIndex(Local))
								continue;
							const int32 BoneIndex = Zms.BoneTable[Local];
							if (BoneIndex < 0 || BoneIndex >= Rig->Zmd.Bones.Num())
								continue;
							SkeletalMeshImportData::FRawBoneInfluence Inf;
							Inf.VertexIndex = PointBase + v;
							Inf.BoneIndex = BoneIndex;
							Inf.Weight = S.Weights[w];
							ImportData.Influences.Add(Inf);
						}
					}
					else
					{
						SkeletalMeshImportData::FRawBoneInfluence Inf;
						Inf.VertexIndex = PointBase + v;
						Inf.BoneIndex = RigidBone;
						Inf.Weight = 1.f;
						ImportData.Influences.Add(Inf);
					}
				}

				for (const FIntVector& Face : Zms.Faces)
				{
					if (Face.X == Face.Y || Face.Y == Face.Z || Face.X == Face.Z)
						continue;
					SkeletalMeshImportData::FTriangle Tri;
					Tri.MatIndex = MatIndex;
					Tri.SmoothingGroups = 1;
					Tri.AuxMatIndex = 0;
					const int32 Order[3] = { Face.X, Face.Y, Face.Z };
					for (int32 c = 0; c < 3; ++c)
					{
						const int32 SrcV = Order[c];
						SkeletalMeshImportData::FVertex Wedge;
						Wedge.VertexIndex = PointBase + SrcV;
						Wedge.MatIndex = (uint8)MatIndex;
						Wedge.Color = FColor::White;
						Wedge.UVs[0] = Zms.UV0.IsValidIndex(SrcV)
							? Zms.UV0[SrcV] : FVector2f::ZeroVector;
						Tri.WedgeIndex[c] = ImportData.Wedges.Add(Wedge);
						FVector3f N = Zms.Normals.IsValidIndex(SrcV)
							? RoseToUEPos(Zms.Normals[SrcV]) : FVector3f::ZAxisVector;
						if (RigidBone != INDEX_NONE)
							N = FVector3f(RigidWorld.TransformVector(FVector(N)).GetSafeNormal());
						Tri.TangentZ[c] = N;
						Tri.TangentX[c] = FVector3f::ZeroVector;
						Tri.TangentY[c] = FVector3f::ZeroVector;
					}
					ImportData.Faces.Add(Tri);
					bAnyGeometry = true;
				}
			}
		}

		if (!bAnyGeometry)
		{
			++Result.Failed;
			continue;
		}

		UPackage* Pkg = MakeWritablePackage(PkgName);
		USkeletalMesh* Mesh = NewObject<USkeletalMesh>(Pkg, *AssetName,
			RF_Public | RF_Standalone);

		FReferenceSkeleton Ref;
		BuildRefSkeleton(Rig->Zmd, Ref, Rig->Skeleton);
		Mesh->SetRefSkeleton(Ref);
		Mesh->SetSkeleton(Rig->Skeleton);

		FSkeletalMeshModel* Model = Mesh->GetImportedModel();
		Model->LODModels.Empty();
		Model->LODModels.Add(new FSkeletalMeshLODModel());

		FSkeletalMeshLODInfo& LodInfo = Mesh->AddLODInfo();
		LodInfo.ReductionSettings.NumOfTrianglesPercentage = 1.f;
		LodInfo.BuildSettings.bRecomputeNormals = false;
		LodInfo.BuildSettings.bRecomputeTangents = true;
		LodInfo.BuildSettings.bUseMikkTSpace = true;

		FMeshDescription MeshDesc;
		if (!ImportData.GetMeshDescription(Mesh, &LodInfo.BuildSettings, MeshDesc))
		{
			UE_LOG(LogRoseImport, Error, TEXT("npc %d: mesh description failed"), Id);
			++Result.Failed;
			continue;
		}
		Mesh->CreateMeshDescription(0, MoveTemp(MeshDesc));
		Mesh->CommitMeshDescription(0);

		for (int32 s = 0; s < SectionMaterials.Num(); ++s)
		{
			// Never a NULL material: a ZSC sentinel material id reaches the async
			// build worker as an access violation and kills the whole run.
			UMaterialInterface* SlotMat = SectionMaterials[s];
			if (!SlotMat)
				SlotMat = UMaterial::GetDefaultMaterial(MD_Surface);
			const FName SlotName(*FString::Printf(TEXT("Mat%d"), s));
			Mesh->GetMaterials().Add(
				FSkeletalMaterial(SlotMat, true, false, SlotName, SlotName));
		}

		Mesh->CalculateInvRefMatrices();
		Mesh->Build();
		Mesh->PostEditChange();

		if (Rig->Skeleton->GetReferenceSkeleton().GetNum() == 0)
			Rig->Skeleton->RecreateBoneTree(Mesh);
		else
			Rig->Skeleton->MergeAllBonesToBoneTree(Mesh);

		FAssetRegistryModule::AssetCreated(Mesh);
		SavePkg(Pkg, Mesh);
		++Result.Built;
	}

	for (TPair<FString, FNpcRig>& P : Rigs)
		if (P.Value.Skeleton)
			SavePkg(P.Value.Skeleton->GetOutermost(), P.Value.Skeleton);

	// SAVE THE MATERIALS AND TEXTURES.
	//
	// FRoseAssetCache builds them in memory and writes nothing until this is
	// called.  Leaving it out does not fail, warn, or lose the meshes — they save
	// fine and reference material packages that were never written, so every NPC
	// renders with the default grey.  That is exactly what the first full run
	// produced: 1,619 meshes, 0 failures, and no Materials/ or Textures/ folder
	// anywhere under /Game/Rose/Npcs.
	if (const int32 SaveFailures = Cache.SaveCreated())
	{
		UE_LOG(LogRoseImport, Error,
			TEXT("npc: %d material/texture package(s) failed to save"), SaveFailures);
	}
	Result.UniqueTextures = Cache.NumTextures();
	Result.UniqueMaterials = Cache.NumMaterials();

	Result.SecondsTotal = FPlatformTime::Seconds() - TimeStart;
	Result.bSuccess = true;

	UE_LOG(LogRoseImport, Display,
		TEXT("=== npc import OK ===  models %d  built %d  empty %d  skipped %d  "
		     "failed %d  rigs %d  textures %d  materials %d  %.2fs"),
		Result.ModelsInChr, Result.Built, Result.Empty, Result.Skipped,
		Result.Failed, Rigs.Num(), Result.UniqueTextures, Result.UniqueMaterials,
		Result.SecondsTotal);
	return true;
}
