#include "RoseRenderIconsCommandlet.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "CanvasTypes.h"
#include "Editor.h"
#include "Editor/UnrealEdEngine.h"
#include "Engine/TextureRenderTarget2D.h"
#include "ImageUtils.h"
#include "RenderingThread.h"
#include "ShaderCompiler.h"
#include "ThumbnailRendering/ThumbnailManager.h"
#include "ThumbnailRendering/ThumbnailRenderer.h"
#include "UnrealEdGlobals.h"
#include "Misc/FileHelper.h"
#include "Misc/ObjectThumbnail.h"
#include "Misc/Paths.h"
#include "ObjectTools.h"
#include "UObject/UObjectGlobals.h"

DEFINE_LOG_CATEGORY_STATIC(LogRoseIcons, Log, All);

namespace
{
	struct FIconSlot
	{
		const TCHAR* Slot;      // matches the DataTable slot key the UI asks for
		const TCHAR* Folder;    // package path holding the meshes
		const TCHAR* Prefix;    // asset-name prefix; the rest of the name is the id
	};

	// Only WORN slots.  The gender-split skinned slots render from the MALE mesh:
	// the icon has to be one image per item id, and ROSE itself uses a single
	// icon for both genders' variants of the same item.
	const FIconSlot kIconSlots[] =
	{
		{ TEXT("weapon"), TEXT("/Game/Rose/Equipment/Weapons"), TEXT("SM_weapon_") },
		{ TEXT("subwpn"), TEXT("/Game/Rose/Equipment/SubWpn"),  TEXT("SM_subwpn_") },
		{ TEXT("back"),   TEXT("/Game/Rose/Equipment/Back"),    TEXT("SM_back_")   },
		{ TEXT("pat"),    TEXT("/Game/Rose/Equipment/Pat"),     TEXT("SM_pat_")    },
		{ TEXT("body"),   TEXT("/Game/Rose/Characters/M/BODY"), TEXT("SK_M_BODY_") },
		{ TEXT("cap"),    TEXT("/Game/Rose/Characters/M/CAP"),  TEXT("SK_M_CAP_")  },
		{ TEXT("arms"),   TEXT("/Game/Rose/Characters/M/ARMS"), TEXT("SK_M_ARMS_") },
		{ TEXT("foot"),   TEXT("/Game/Rose/Characters/M/FOOT"), TEXT("SK_M_FOOT_") },
	};

	FString IconOutDir()
	{
		return FPaths::ProjectDir() / TEXT("SourceAssets/UI/ItemIcons3D");
	}

	/** Render one asset to a PNG on disk.  Returns false and logs on failure.
	 *
	 *  Drives the thumbnail renderer directly rather than going through
	 *  ThumbnailTools::RenderThumbnail.  That helper renders into the editor's
	 *  SCRATCH render target and reads it straight back, and in a commandlet the
	 *  readback lands before the draw has been flushed — every image comes out
	 *  uniformly black (RGB 0, alpha 255), for plain UTexture2D assets too, which
	 *  is how we know it is not the meshes or materials.  Owning the render
	 *  target lets us Flush_GameThread() the canvas and FlushRenderingCommands()
	 *  before reading a single pixel. */
	bool RenderOne(UObject* Asset, int32 Size, const FString& OutPath)
	{
		// UThumbnailManager::Get(), not GUnrealEd->GetThumbnailManager():
		// GUnrealEd is NULL in a commandlet (GEditor is a plain UEditorEngine),
		// so going through it reports "no thumbnail renderer" for every asset,
		// including static meshes that obviously have one.
		FThumbnailRenderingInfo* Info = UThumbnailManager::Get().GetRenderingInfo(Asset);
		if (!Info || !Info->Renderer)
		{
			UE_LOG(LogRoseIcons, Warning, TEXT("  no thumbnail renderer for %s"),
				*Asset->GetName());
			return false;
		}

		UTextureRenderTarget2D* RT = NewObject<UTextureRenderTarget2D>();
		RT->ClearColor = FLinearColor::Transparent;
		RT->TargetGamma = 1.f;
		RT->InitCustomFormat(Size, Size, PF_B8G8R8A8, /*bInForceLinearGamma*/ false);
		RT->UpdateResourceImmediate(true);
		FTextureRenderTargetResource* Res = RT->GameThread_GetRenderTargetResource();
		if (!Res)
			return false;

		// Nothing rasterises until the material shaders exist.  In the editor GUI
		// they are already warm; a commandlet reaches the draw with compilation
		// still queued and the canvas comes back holding only the clear colour.
		if (GShaderCompilingManager)
			GShaderCompilingManager->FinishAllCompilation();

		{
			FCanvas Canvas(Res, nullptr, FGameTime(), GMaxRHIFeatureLevel);
			Canvas.Clear(FLinearColor::Transparent);
			Info->Renderer->Draw(Asset, 0, 0, Size, Size, Res, &Canvas,
				/*bAdditionalViewFamily*/ false);
			Canvas.Flush_GameThread();
		}
		FlushRenderingCommands();

		TArray<FColor> Pixels;
		if (!Res->ReadPixels(Pixels) || Pixels.Num() < Size * Size)
		{
			UE_LOG(LogRoseIcons, Warning, TEXT("  readback failed for %s"),
				*Asset->GetName());
			return false;
		}

		TArray64<FColor> Wide(Pixels.GetData(), Pixels.Num());
		TArray64<uint8> Png;
		FImageUtils::PNGCompressImageArray(Size, Size, Wide, Png);
		if (Png.Num() == 0)
		{
			UE_LOG(LogRoseIcons, Warning, TEXT("  PNG compress failed for %s"),
				*Asset->GetName());
			return false;
		}
		return FFileHelper::SaveArrayToFile(Png, *OutPath);
	}
}

URoseRenderIconsCommandlet::URoseRenderIconsCommandlet()
{
	IsClient = false;
	IsServer = false;
	IsEditor = true;
	LogToConsole = true;
}

int32 URoseRenderIconsCommandlet::Main(const FString& Params)
{
	TArray<FString> Tokens, Switches;
	TMap<FString, FString> ParamVals;
	ParseCommandLine(*Params, Tokens, Switches, ParamVals);

	int32 Size = 128;
	if (const FString* S = ParamVals.Find(TEXT("size")))
		Size = FMath::Clamp(FCString::Atoi(**S), 16, 512);

	int32 MaxPerSlot = MAX_int32;
	if (const FString* S = ParamVals.Find(TEXT("max")))
		MaxPerSlot = FMath::Max(1, FCString::Atoi(**S));

	const bool bForce = Switches.Contains(TEXT("force"));

	TArray<FString> Only;
	if (const FString* S = ParamVals.Find(TEXT("only")))
		S->ParseIntoArray(Only, TEXT(","), true);
	for (FString& O : Only)
		O.TrimStartAndEndInline();

	// A commandlet runs with rendering DISABLED unless this flag is on the
	// command line, and the thumbnail renderer then silently hands back an empty
	// image for every asset — 12,136 "empty thumbnail" warnings and no clue why.
	// Fail loudly instead.
	if (!IsAllowCommandletRendering())
	{
		UE_LOG(LogRoseIcons, Error,
			TEXT("[icons3d] rendering is disabled for commandlets — re-run with ")
			TEXT("-AllowCommandletRendering (and WITHOUT -nullrhi)."));
		return 1;
	}

	const FString OutDir = IconOutDir();
	IFileManager::Get().MakeDirectory(*OutDir, true);

	IAssetRegistry& AR = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(
		TEXT("AssetRegistry")).Get();
	AR.SearchAllAssets(/*bSynchronous*/ true);

	UE_LOG(LogRoseIcons, Display, TEXT("[icons3d] %dpx -> %s%s"),
		Size, *OutDir, bForce ? TEXT("  (force)") : TEXT("  (skipping existing)"));

	// -probe=/Game/Some/Path renders the first few assets found there, whatever
	// they are.  Pointed at a TEXTURE folder it isolates the readback from the
	// scene: a texture thumbnail is a blit, no meshes, materials or lights
	// involved, so a black result there means the pixel readback is broken
	// rather than the render.
	if (const FString* Probe = ParamVals.Find(TEXT("probe")))
	{
		TArray<FAssetData> Assets;
		AR.GetAssetsByPath(FName(**Probe), Assets, false);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) {
			return A.AssetName.LexicalLess(B.AssetName); });
		const int32 N = FMath::Min(Assets.Num(), MaxPerSlot);
		UE_LOG(LogRoseIcons, Display, TEXT("[icons3d] PROBE %s (%d of %d assets)"),
			**Probe, N, Assets.Num());
		for (int32 i = 0; i < N; ++i)
		{
			UObject* Asset = Assets[i].GetAsset();
			if (!Asset) continue;
			const FString Out = OutDir / FString::Printf(
				TEXT("probe_%s.png"), *Assets[i].AssetName.ToString());
			UE_LOG(LogRoseIcons, Display, TEXT("  %s [%s] -> %s"),
				*Assets[i].AssetName.ToString(), *Asset->GetClass()->GetName(),
				RenderOne(Asset, Size, Out) ? TEXT("ok") : TEXT("FAILED"));
		}
		return 0;
	}

	int32 GrandDone = 0, GrandSkip = 0, GrandFail = 0;

	for (const FIconSlot& Slot : kIconSlots)
	{
		if (Only.Num() && !Only.Contains(Slot.Slot))
			continue;

		TArray<FAssetData> Assets;
		AR.GetAssetsByPath(FName(Slot.Folder), Assets, /*bRecursive*/ false);
		if (Assets.Num() == 0)
		{
			UE_LOG(LogRoseIcons, Warning, TEXT("[icons3d] %s: nothing under %s"),
				Slot.Slot, Slot.Folder);
			continue;
		}

		// Deterministic order so a chunked run (-max) resumes predictably.
		Assets.Sort([](const FAssetData& A, const FAssetData& B) {
			return A.AssetName.LexicalLess(B.AssetName); });

		// Counts ATTEMPTS, not successes: gating -max on the success count means a
		// slot that fails every render never reaches the limit and grinds through
		// all 2,911 assets instead of the 3 you asked for.
		int32 Attempted = 0;
		int32 Done = 0, Skipped = 0, Failed = 0;
		for (const FAssetData& AD : Assets)
		{
			if (Attempted >= MaxPerSlot)
				break;

			const FString Name = AD.AssetName.ToString();
			if (!Name.StartsWith(Slot.Prefix))
				continue;
			// The off-hand mesh of a dual wield is the same item — one icon only.
			if (Name.EndsWith(TEXT("_off")))
				continue;

			const FString IdPart = Name.RightChop(FCString::Strlen(Slot.Prefix));
			if (!IdPart.IsNumeric())
				continue;

			const FString OutPath = OutDir / FString::Printf(
				TEXT("icon_%s_%s.png"), Slot.Slot, *IdPart);
			if (!bForce && IFileManager::Get().FileExists(*OutPath))
			{
				++Skipped;
				continue;
			}

			++Attempted;
			UObject* Asset = AD.GetAsset();
			if (!Asset)
			{
				++Failed;
				continue;
			}
			if (RenderOne(Asset, Size, OutPath))
				++Done;
			else
				++Failed;

			// Meshes are big and every one stays referenced until collected.
			// Without this the run climbs into many GB and the GPU work queues
			// up behind it (the same TDR risk the bulk mesh imports hit).
			if ((Done % 25) == 0)
				CollectGarbage(RF_NoFlags);
		}

		UE_LOG(LogRoseIcons, Display,
			TEXT("[icons3d] %-8s rendered %4d   skipped %4d   failed %3d"),
			Slot.Slot, Done, Skipped, Failed);
		GrandDone += Done; GrandSkip += Skipped; GrandFail += Failed;
		CollectGarbage(RF_NoFlags);
	}

	UE_LOG(LogRoseIcons, Display,
		TEXT("[icons3d] TOTAL rendered %d, skipped %d, failed %d"),
		GrandDone, GrandSkip, GrandFail);
	return 0;
}
