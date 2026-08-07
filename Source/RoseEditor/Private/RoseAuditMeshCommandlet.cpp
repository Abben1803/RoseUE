#include "RoseAuditMeshCommandlet.h"

#include "RoseEditor.h"

#include "Engine/SkeletalMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"

URoseAuditMeshCommandlet::URoseAuditMeshCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

namespace
{
	void ReportMaterial(int32 Index, UMaterialInterface* MI)
	{
		if (!MI)
		{
			UE_LOG(LogRoseImport, Warning, TEXT("  [%d] material is NULL"), Index);
			return;
		}

		FString Blend = TEXT("?");
		FString Sides = TEXT("?");
		float Clip = -1.f;
		if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(MI))
		{
			const FMaterialInstanceBasePropertyOverrides& O = MIC->BasePropertyOverrides;

			// Two-sidedness is not cosmetic here: a one-sided avatar part culls
			// its backfaces and reads as SEE-THROUGH, because ROSE's character
			// meshes are open shells rather than closed solids.
			Sides = O.bOverride_TwoSided
				? (O.TwoSided ? TEXT("2side") : TEXT("1side"))
				: TEXT("(from parent)");
			if (O.bOverride_OpacityMaskClipValue)
				Clip = O.OpacityMaskClipValue;

			if (O.bOverride_BlendMode)
			{
				switch (O.BlendMode)
				{
				case BLEND_Opaque:      Blend = TEXT("Opaque");      break;
				case BLEND_Masked:      Blend = TEXT("Masked");      break;
				case BLEND_Translucent: Blend = TEXT("Translucent"); break;
				case BLEND_Additive:    Blend = TEXT("Additive");    break;
				default:                Blend = TEXT("other");       break;
				}
			}
			else
			{
				Blend = TEXT("(from parent)");
			}
		}

		// The bound BaseColor texture is the whole question when a part renders
		// the wrong colour: an unset parameter silently falls through to the
		// master's default rather than erroring.
		UTexture* Tex = nullptr;
		MI->GetTextureParameterValue(FMaterialParameterInfo(TEXT("BaseColor")), Tex);

		UE_LOG(LogRoseImport, Display,
			TEXT("  [%d] %-42s blend=%-11s %-13s clip=%-5.2f BaseColor=%s"),
			Index, *MI->GetName(), *Blend, *Sides, Clip,
			Tex ? *Tex->GetName() : TEXT("*** UNSET (falls back to master default) ***"));

		// Mean colour of the imported texture, to compare against the source
		// DDS.  A part that renders far darker than its .DDS is either decoded
		// wrong on import or shaded wrong at runtime, and this separates the two.
		if (UTexture2D* T2 = Cast<UTexture2D>(Tex))
		{
			TArray64<uint8> Pixels;
			if (T2->Source.IsValid() && T2->Source.GetMipData(Pixels, 0)
				&& T2->Source.GetFormat() == TSF_BGRA8)
			{
				const int64 N = Pixels.Num() / 4;
				int64 SumB = 0, SumG = 0, SumR = 0, SumA = 0;
				for (int64 p = 0; p < N; ++p)
				{
					SumB += Pixels[p * 4 + 0];
					SumG += Pixels[p * 4 + 1];
					SumR += Pixels[p * 4 + 2];
					SumA += Pixels[p * 4 + 3];
				}
				if (N > 0)
				{
					UE_LOG(LogRoseImport, Display,
						TEXT("       %dx%d  mean RGBA = (%lld, %lld, %lld, %lld)  sRGB=%d"),
						T2->Source.GetSizeX(), T2->Source.GetSizeY(),
						SumR / N, SumG / N, SumB / N, SumA / N, T2->SRGB ? 1 : 0);
				}
			}
		}

		UMaterialInterface* Parent = nullptr;
		if (UMaterialInstanceConstant* MIC = Cast<UMaterialInstanceConstant>(MI))
			Parent = MIC->Parent;
		UE_LOG(LogRoseImport, Display, TEXT("       parent=%s"),
			Parent ? *Parent->GetPathName() : TEXT("*** NONE ***"));

		// The usage flag is the classic silent failure: a master authored for
		// static meshes renders every SKELETAL mesh using it as the engine's
		// Default Material instead — no error at import, only a runtime warning
		// and a flat untextured character.
		if (UMaterial* Base = MI->GetMaterial())
		{
			const bool bSkelOK = Base->GetUsageByFlag(MATUSAGE_SkeletalMesh);
			const int32 Shading = (int32)Base->GetShadingModels().GetFirstShadingModel();
			if (bSkelOK)
			{
				UE_LOG(LogRoseImport, Display,
					TEXT("       base=%s  bUsedWithSkeletalMesh=YES  shading=%d"),
					*Base->GetName(), Shading);
			}
			else
			{
				UE_LOG(LogRoseImport, Error,
					TEXT("       base=%s  bUsedWithSkeletalMesh=NO -> DEFAULT MATERIAL AT RUNTIME  shading=%d"),
					*Base->GetName(), Shading);
			}
		}
	}
}

int32 URoseAuditMeshCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamsMap;
	ParseCommandLine(*Params, Tokens, Switches, ParamsMap);

	const FString* AssetPath = ParamsMap.Find(TEXT("asset"));
	if (!AssetPath)
	{
		UE_LOG(LogRoseImport, Error, TEXT("need -asset=/Game/..."));
		return 1;
	}

	USkeletalMesh* Mesh = LoadObject<USkeletalMesh>(nullptr, **AssetPath);
	if (!Mesh)
	{
		UE_LOG(LogRoseImport, Error, TEXT("cannot load %s"), **AssetPath);
		return 1;
	}

	UE_LOG(LogRoseImport, Display, TEXT("=== %s ==="), *Mesh->GetPathName());

	// Bounds, so parts can be compared against each other.  A part built at the
	// wrong unit scale is invisible in isolation — every material and texture
	// checks out — and only shows up as one piece dwarfing another once merged.
	{
		const FBoxSphereBounds B = Mesh->GetBounds();
		UE_LOG(LogRoseImport, Display,
			TEXT("bounds: origin (%.1f, %.1f, %.1f)  extent (%.1f, %.1f, %.1f)  radius %.1f"),
			B.Origin.X, B.Origin.Y, B.Origin.Z,
			B.BoxExtent.X, B.BoxExtent.Y, B.BoxExtent.Z, B.SphereRadius);
	}
	UE_LOG(LogRoseImport, Display, TEXT("skeleton: %s"),
		Mesh->GetSkeleton() ? *Mesh->GetSkeleton()->GetPathName() : TEXT("*** NONE ***"));

	const TArray<FSkeletalMaterial>& Mats = Mesh->GetMaterials();
	UE_LOG(LogRoseImport, Display, TEXT("materials: %d"), Mats.Num());
	for (int32 i = 0; i < Mats.Num(); ++i)
		ReportMaterial(i, Mats[i].MaterialInterface);

	if (FSkeletalMeshRenderData* RD = Mesh->GetResourceForRendering())
	{
		if (RD->LODRenderData.Num() > 0)
		{
			const FSkeletalMeshLODRenderData& LOD = RD->LODRenderData[0];

			// Vertex colours multiply into BaseColor in most masters, so an
			// all-black colour buffer darkens a part uniformly while every
			// texture and material still looks correct in isolation.
			const bool bHasColors = LOD.StaticVertexBuffers.ColorVertexBuffer.GetNumVertices() > 0;
			UE_LOG(LogRoseImport, Display,
				TEXT("LOD0: %d verts, %d sections, vertex colours: %s"),
				LOD.GetNumVertices(), LOD.RenderSections.Num(),
				bHasColors ? TEXT("PRESENT") : TEXT("none"));

			if (bHasColors)
			{
				const FColorVertexBuffer& CB = LOD.StaticVertexBuffers.ColorVertexBuffer;
				int32 Black = 0;
				const uint32 N = CB.GetNumVertices();
				for (uint32 v = 0; v < N; ++v)
				{
					const FColor C = CB.VertexColor(v);
					if (C.R == 0 && C.G == 0 && C.B == 0)
						++Black;
				}
				UE_LOG(LogRoseImport, Warning,
					TEXT("       %d/%u vertex colours are pure BLACK"), Black, N);
			}

			for (int32 s = 0; s < LOD.RenderSections.Num(); ++s)
			{
				UE_LOG(LogRoseImport, Display,
					TEXT("  section %d: material index %d, %d tris"),
					s, LOD.RenderSections[s].MaterialIndex,
					LOD.RenderSections[s].NumTriangles);
			}
		}
	}

	return 0;
}
