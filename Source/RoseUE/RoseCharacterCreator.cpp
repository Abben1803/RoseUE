#include "RoseCharacterCreator.h"
#include "RoseHairData.h"          // RoseHairBase — filter hair to base styles

#include "Components/SkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Animation/AnimSequence.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"

ARoseCharacterCreator::ARoseCharacterCreator()
{
	PrimaryActorTick.bCanEverTick = false;

	Body = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Body"));
	RootComponent = Body;

	Hair = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Hair"));
	Hair->SetupAttachment(Body);
	Face = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Face"));
	Face->SetupAttachment(Body);            // was Face->SetupAttachment(Face) — self-attach bug
	Feet = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Feet"));
	Feet->SetupAttachment(Body);
	Arms = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("Arms"));
	Arms->SetupAttachment(Body);
}

void ARoseCharacterCreator::BeginPlay()
{
	Super::BeginPlay();
	ScanOptions();
	RebuildAll();
}

USkeletalMesh* ARoseCharacterCreator::LoadPart(const FString& Slot, int32 Id) const
{
	// NATIVE first (RoseEditor): /Game/Rose/Characters/<G>/<SLOT>/SK_<G>_<SLOT>_<id>
	{
		const TCHAR* G = Gender.Equals(TEXT("Female"), ESearchCase::IgnoreCase)
			? TEXT("F") : TEXT("M");
		const FString SlotUpper = Slot.ToUpper();
		const FString Asset = FString::Printf(TEXT("SK_%s_%s_%d"), G, *SlotUpper, Id);
		const FString Path = FString::Printf(TEXT("/Game/Rose/Characters/%s/%s/%s.%s"),
			G, *SlotUpper, *Asset, *Asset);
		if (USkeletalMesh* M = LoadObject<USkeletalMesh>(nullptr, *Path))
			return M;
	}

	// LEGACY (Interchange's nested folders).
	const FString Name = FString::Printf(TEXT("%s_%d"), *Slot.ToLower(), Id);
	const FString Path = FString::Printf(
		TEXT("/Game/Characters/Modular/%s/%s/%s/SkeletalMeshes/%s.%s"),
		*Gender, *Name, *Name, *Name, *Name);
	return LoadObject<USkeletalMesh>(nullptr, *Path);
}

void ARoseCharacterCreator::ScanOptions()
{
	HairIds.Reset();
	FaceIds.Reset();

	FAssetRegistryModule& ARM =
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
	IAssetRegistry& AR = ARM.Get();

	const TCHAR* G = Gender.Equals(TEXT("Female"), ESearchCase::IgnoreCase)
		? TEXT("F") : TEXT("M");

	// NATIVE first: SK_<G>_HAIR_<id> / SK_<G>_FACE_<id> under
	// /Game/Rose/Characters/<G>.  Fall back to the legacy modular folder, whose
	// names are hair_<id> / face_<id>, only if the native scan finds nothing —
	// listing options from one era while LoadPart resolves from the other would
	// offer styles that cannot load.
	auto Collect = [&AR](const FString& PackagePath, const FString& HairTag, const FString& FaceTag,
		TSet<int32>& OutHairs, TSet<int32>& OutFaces)
	{
		FARFilter Filter;
		Filter.PackagePaths.Add(FName(*PackagePath));
		Filter.bRecursivePaths = true;
		Filter.ClassPaths.Add(USkeletalMesh::StaticClass()->GetClassPathName());

		TArray<FAssetData> Assets;
		AR.GetAssets(Filter, Assets);

		for (const FAssetData& A : Assets)
		{
			const FString Name = A.AssetName.ToString();
			FString Rest;
			if (Name.Split(HairTag, nullptr, &Rest) && Rest.IsNumeric())
			{
				const int32 Id = FCString::Atoi(*Rest);
				if (RoseHairBase(Id) == Id)   // base styles only (skip cap-clip variants)
					OutHairs.Add(Id);
			}
			else if (Name.Split(FaceTag, nullptr, &Rest) && Rest.IsNumeric())
			{
				OutFaces.Add(FCString::Atoi(*Rest));
			}
		}
	};

	TSet<int32> Hairs, Faces;
	Collect(FString::Printf(TEXT("/Game/Rose/Characters/%s"), G),
		FString::Printf(TEXT("_%s_HAIR_"), G), FString::Printf(TEXT("_%s_FACE_"), G),
		Hairs, Faces);

	if (Hairs.Num() == 0 && Faces.Num() == 0)
	{
		Collect(FString::Printf(TEXT("/Game/Characters/Modular/%s"), *Gender),
			TEXT("hair_"), TEXT("face_"), Hairs, Faces);
	}
	HairIds = Hairs.Array();
	FaceIds = Faces.Array();
	HairIds.Sort();
	FaceIds.Sort();

	UE_LOG(LogTemp, Log, TEXT("[RoseCC] %s: %d base hairs, %d faces"),
		*Gender, HairIds.Num(), FaceIds.Num());
}

void ARoseCharacterCreator::RebuildAll()
{
	if (USkeletalMesh* B = LoadPart(TEXT("body"), 1)) Body->SetSkeletalMeshAsset(B);
	if (USkeletalMesh* A = LoadPart(TEXT("arms"), 1)) Arms->SetSkeletalMeshAsset(A);
	if (USkeletalMesh* F = LoadPart(TEXT("foot"), 1)) Feet->SetSkeletalMeshAsset(F);

	Arms->SetLeaderPoseComponent(Body);
	Feet->SetLeaderPoseComponent(Body);

	ApplyHair();
	ApplyFace();
	PlayIdle();
}

void ARoseCharacterCreator::ApplyHair()
{
	if (USkeletalMesh* M = LoadPart(TEXT("hair"), HairId))
	{
		Hair->SetSkeletalMeshAsset(M);
		Hair->SetLeaderPoseComponent(Body);
	}
}

void ARoseCharacterCreator::ApplyFace()
{
	if (USkeletalMesh* M = LoadPart(TEXT("face"), FaceId))
	{
		Face->SetSkeletalMeshAsset(M);
		Face->SetLeaderPoseComponent(Body);
	}
}

void ARoseCharacterCreator::PlayIdle()
{
	// Female base is animated by *_F1 select/idle clips, male by *_M1.  These are
	// ZMO stems; the legacy assets carry a "base" prefix, the native ones do not.
	const TCHAR* IdleStems[] = { TEXT("AVATAR_SELECT05_F1"), TEXT("AVATAR_WARNING_F1"),
	                             TEXT("AVATAR_SELECT05_M1"), TEXT("AVATAR_WARNING_M1") };
	const TCHAR* G = Gender.Equals(TEXT("Female"), ESearchCase::IgnoreCase)
		? TEXT("F") : TEXT("M");

	auto Play = [this](UAnimSequence* Idle)
	{
		Body->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		Body->PlayAnimation(Idle, true);
	};

	// NATIVE first (RoseEditor), then the legacy `base` GLB clips.
	for (const TCHAR* S : IdleStems)
	{
		const FString P = FString::Printf(
			TEXT("/Game/Rose/Characters/%s/Anims/A_%s_%s.A_%s_%s"), G, G, S, G, S);
		if (UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, *P))
		{
			Play(Idle);
			return;
		}
	}
	for (const TCHAR* S : IdleStems)
	{
		const FString P = FString::Printf(
			TEXT("/Game/Characters/Modular/%s/base/base/SkeletalMeshes/base%s.base%s"),
			*Gender, S, S);
		if (UAnimSequence* Idle = LoadObject<UAnimSequence>(nullptr, *P))
		{
			Play(Idle);
			return;
		}
	}
}

void ARoseCharacterCreator::SetGender(const FString& InGender)
{
	if (Gender.Equals(InGender, ESearchCase::IgnoreCase))
		return;
	Gender = InGender;
	ScanOptions();
	// Snap current ids into the new gender's available range.
	if (HairIds.Num() && !HairIds.Contains(HairId)) HairId = HairIds[0];
	if (FaceIds.Num() && !FaceIds.Contains(FaceId)) FaceId = FaceIds[0];
	RebuildAll();
}

static int32 CycleId(const TArray<int32>& Ids, int32 Cur, int32 Dir)
{
	if (Ids.Num() == 0) return Cur;
	int32 Idx = Ids.IndexOfByKey(Cur);
	if (Idx == INDEX_NONE) Idx = 0;
	Idx = (Idx + Dir + Ids.Num()) % Ids.Num();
	return Ids[Idx];
}

void ARoseCharacterCreator::SetHairId(int32 Id) { HairId = Id; ApplyHair(); }
void ARoseCharacterCreator::SetFaceId(int32 Id) { FaceId = Id; ApplyFace(); }

void ARoseCharacterCreator::NextHair() { HairId = CycleId(HairIds, HairId, +1); ApplyHair(); }
void ARoseCharacterCreator::PrevHair() { HairId = CycleId(HairIds, HairId, -1); ApplyHair(); }
void ARoseCharacterCreator::NextFace() { FaceId = CycleId(FaceIds, FaceId, +1); ApplyFace(); }
void ARoseCharacterCreator::PrevFace() { FaceId = CycleId(FaceIds, FaceId, -1); ApplyFace(); }
