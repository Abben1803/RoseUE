#include "RoseAuditMergeCommandlet.h"

#include "RoseEditor.h"

#include "Animation/Skeleton.h"
#include "Engine/SkeletalMesh.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "SkeletalMergingLibrary.h"

URoseAuditMergeCommandlet::URoseAuditMergeCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

namespace
{
	void Report(const TCHAR* Label, USkeletalMesh* Mesh)
	{
		if (!Mesh)
		{
			UE_LOG(LogRoseImport, Warning, TEXT("  %-22s MISSING"), Label);
			return;
		}

		const FBoxSphereBounds B = Mesh->GetBounds();
		int32 Verts = 0, Sections = 0, Bones = 0;
		if (FSkeletalMeshRenderData* RD = Mesh->GetResourceForRendering())
		{
			if (RD->LODRenderData.Num() > 0)
			{
				Verts = RD->LODRenderData[0].GetNumVertices();
				Sections = RD->LODRenderData[0].RenderSections.Num();
			}
		}
		Bones = Mesh->GetRefSkeleton().GetNum();

		UE_LOG(LogRoseImport, Display,
			TEXT("  %-22s Z %7.1f..%7.1f  extent(%5.1f,%5.1f,%5.1f)  verts %5d  sec %2d  bones %3d  mats %2d"),
			Label,
			B.Origin.Z - B.BoxExtent.Z, B.Origin.Z + B.BoxExtent.Z,
			B.BoxExtent.X, B.BoxExtent.Y, B.BoxExtent.Z,
			Verts, Sections, Bones, Mesh->GetMaterials().Num());
	}
}

int32 URoseAuditMergeCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	const FString G = ParamsMap.Contains(TEXT("gender")) ? ParamsMap[TEXT("gender")] : TEXT("F");

	auto IdFor = [&ParamsMap](const TCHAR* Key, int32 Default) -> int32
	{
		if (const FString* V = ParamsMap.Find(Key))
			return FCString::Atoi(**V);
		return Default;
	};

	// The same slots ARoseCharacter merges.
	struct FSlot { const TCHAR* Name; int32 Id; };
	const FSlot Slots[] =
	{
		{ TEXT("BODY"), IdFor(TEXT("body"), 1) },
		{ TEXT("ARMS"), IdFor(TEXT("arms"), 1) },
		{ TEXT("FOOT"), IdFor(TEXT("foot"), 1) },
		{ TEXT("FACE"), IdFor(TEXT("face"), 1) },
		{ TEXT("HAIR"), IdFor(TEXT("hair"), 1) },
	};

	USkeleton* Skeleton = LoadObject<USkeleton>(nullptr,
		*FString::Printf(TEXT("/Game/Rose/Characters/SK_Rose_%s_Skeleton.SK_Rose_%s_Skeleton"), *G, *G));
	if (!Skeleton)
	{
		UE_LOG(LogRoseImport, Error, TEXT("no native skeleton for gender '%s'"), *G);
		return 1;
	}

	UE_LOG(LogRoseImport, Display, TEXT("=== native merge audit (%s) ==="), *G);
	UE_LOG(LogRoseImport, Display, TEXT("skeleton '%s': %d bones, %d sockets"),
		*Skeleton->GetName(), Skeleton->GetReferenceSkeleton().GetNum(), Skeleton->Sockets.Num());

	TArray<USkeletalMesh*> Parts;
	for (const FSlot& S : Slots)
	{
		const FString Path = FString::Printf(
			TEXT("/Game/Rose/Characters/%s/%s/SK_%s_%s_%d.SK_%s_%s_%d"),
			*G, S.Name, *G, S.Name, S.Id, *G, S.Name, S.Id);

		USkeletalMesh* M = LoadObject<USkeletalMesh>(nullptr, *Path);
		Report(*FString::Printf(TEXT("%s_%d"), S.Name, S.Id), M);

		if (M)
		{
			// A part built on a DIFFERENT skeleton cannot merge sanely — the
			// merge silently retargets by bone index and produces a scrambled
			// character.  Worth naming, not assuming.
			if (M->GetSkeleton() != Skeleton)
			{
				UE_LOG(LogRoseImport, Error,
					TEXT("      ^ skeleton MISMATCH: '%s' (expected '%s')"),
					*GetNameSafe(M->GetSkeleton()), *Skeleton->GetName());
			}
			Parts.Add(M);
		}
	}

	if (Parts.Num() == 0)
	{
		UE_LOG(LogRoseImport, Error, TEXT("no parts loaded — nothing to merge"));
		return 1;
	}

	FSkeletalMeshMergeParams MergeParams;
	MergeParams.MeshesToMerge = Parts;
	MergeParams.Skeleton = Skeleton;

	USkeletalMesh* Merged = USkeletalMergingLibrary::MergeMeshes(MergeParams);
	if (!Merged)
	{
		UE_LOG(LogRoseImport, Error, TEXT("MergeMeshes returned null"));
		return 1;
	}

	UE_LOG(LogRoseImport, Display, TEXT("--- merged ---"));
	Report(TEXT("MERGED"), Merged);

	// WHICH materials survived the merge, not just how many.
	//
	// This is the only step between "the asset is correct in the mesh editor"
	// and "the character is wrong in game": MergeMeshes builds a NEW mesh at
	// runtime, and if it substitutes its own materials instead of carrying the
	// M_RoseChar instances across, nothing on disk would ever show it.
	for (const FSkeletalMaterial& SM : Merged->GetMaterials())
	{
		UMaterialInterface* MI = SM.MaterialInterface;
		if (!MI)
		{
			UE_LOG(LogRoseImport, Error, TEXT("    merged slot: NULL material"));
			continue;
		}
		UMaterial* Base = MI->GetMaterial();
		UE_LOG(LogRoseImport, Display, TEXT("    merged slot: %-42s base=%s"),
			*MI->GetName(), Base ? *Base->GetName() : TEXT("?"));
	}

	// The merged result should span roughly the union of its parts.  If it is
	// dramatically taller, shorter or off-origin, the merge is where the
	// character breaks — not the import.
	// NORMALS on the merged result.
	//
	// A merged mesh with a zero/degenerate tangent basis renders BLACK under
	// real lighting while every source part stays perfect in the mesh editor —
	// the editor previews the SOURCE asset, not the runtime merge.  That is the
	// one failure mode consistent with "assets correct, character black in a
	// default level", so measure it rather than infer it.
	auto ReportNormals = [](const TCHAR* Label, USkeletalMesh* Mesh)
	{
		FSkeletalMeshRenderData* RD = Mesh ? Mesh->GetResourceForRendering() : nullptr;
		if (!RD || RD->LODRenderData.Num() == 0)
			return;

		const FSkeletalMeshLODRenderData& LOD = RD->LODRenderData[0];
		const FStaticMeshVertexBuffer& VB = LOD.StaticVertexBuffers.StaticMeshVertexBuffer;
		const uint32 N = VB.GetNumVertices();
		if (N == 0)
		{
			UE_LOG(LogRoseImport, Error, TEXT("  %s: no vertex buffer"), Label);
			return;
		}

		int32 Zero = 0, Down = 0;
		double SumZ = 0.0;
		for (uint32 v = 0; v < N; ++v)
		{
			const FVector3f Nrm = VB.VertexTangentZ(v);
			const float Len = Nrm.Size();
			if (Len < 0.5f)
				++Zero;
			if (Nrm.Z < -0.5f)
				++Down;
			SumZ += Nrm.Z;
		}
		UE_LOG(LogRoseImport, Display,
			TEXT("  %s normals: %u verts, %d degenerate, %d pointing DOWN, mean Z %.3f"),
			Label, N, Zero, Down, SumZ / N);
		if (Zero > 0)
		{
			UE_LOG(LogRoseImport, Error,
				TEXT("  %s: %d degenerate normals — this mesh renders BLACK"), Label, Zero);
		}
	};

	if (Parts.Num() > 0)
		ReportNormals(TEXT("source part[0]"), Parts[0]);
	ReportNormals(TEXT("MERGED"), Merged);

	// Is the MASTER actually wired?
	//
	// Every instance-level check passes (texture bound, blend mode, two-sided,
	// skeletal usage) even when the MASTER's BaseColor input is not connected —
	// and an unconnected BaseColor renders BLACK no matter what the instance
	// binds.  This is the last link in the chain that has never been verified.
	if (UMaterialInterface* MI = Merged->GetMaterials().Num() ? Merged->GetMaterials()[0].MaterialInterface : nullptr)
	{
		if (UMaterial* Base = MI->GetMaterial())
		{
			auto Wired = [Base](EMaterialProperty Prop, const TCHAR* Name)
			{
				const FExpressionInput* In = Base->GetExpressionInputForProperty(Prop);
				const bool bConnected = In && In->Expression != nullptr;
				if (bConnected)
				{
					UE_LOG(LogRoseImport, Display, TEXT("  %s.%s : connected"),
						*Base->GetName(), Name);
				}
				else
				{
					UE_LOG(LogRoseImport, Error, TEXT("  %s.%s : *** NOT CONNECTED ***"),
						*Base->GetName(), Name);
				}
			};
			UE_LOG(LogRoseImport, Display, TEXT("--- master wiring ---"));
			Wired(MP_BaseColor,     TEXT("BaseColor"));
			Wired(MP_OpacityMask,   TEXT("OpacityMask"));
			Wired(MP_Roughness,     TEXT("Roughness"));
			Wired(MP_Specular,      TEXT("Specular"));
			Wired(MP_Metallic,      TEXT("Metallic"));
			Wired(MP_EmissiveColor, TEXT("Emissive"));
		}
	}

	// -save writes the MERGED mesh out as a real asset.
	//
	// The mesh editor previews SOURCE parts, which look correct; the game
	// renders the RUNTIME MERGE, which does not.  Nothing measurable separates
	// them — normals, materials, bounds and vert counts all match — so the next
	// step is to look at the merged mesh in the same editor that shows the parts
	// as fine.  If the saved merge renders black there too, the merge is the
	// culprit and the source parts are exonerated for good.
	if (Switches.Contains(TEXT("save")))
	{
		const FString PkgName = TEXT("/Game/Rose/Debug/SK_MergeTest");
		UPackage* Pkg = CreatePackage(*PkgName);
		Pkg->FullyLoad();

		if (USkeletalMesh* Copy = DuplicateObject<USkeletalMesh>(
			Merged, Pkg, TEXT("SK_MergeTest")))
		{
			Copy->SetFlags(RF_Public | RF_Standalone);
			FAssetRegistryModule::AssetCreated(Copy);
			Pkg->MarkPackageDirty();

			FSavePackageArgs Args;
			Args.TopLevelFlags = RF_Public | RF_Standalone;
			const FString File = FPackageName::LongPackageNameToFilename(
				PkgName, FPackageName::GetAssetPackageExtension());
			if (UPackage::SavePackage(Pkg, Copy, *File, Args))
			{
				UE_LOG(LogRoseImport, Display,
					TEXT("saved the merged mesh to %s — open it in the mesh editor "
						 "and compare against the source parts"), *PkgName);
			}
			else
			{
				UE_LOG(LogRoseImport, Error, TEXT("failed to save %s"), *PkgName);
			}
		}
	}

	UE_LOG(LogRoseImport, Display,
		TEXT("expected: Z ~0..185 for a ~180cm ROSE avatar, %d parts merged"), Parts.Num());

	return 0;
}
