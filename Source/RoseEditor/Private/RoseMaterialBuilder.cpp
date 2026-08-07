#include "RoseMaterialBuilder.h"
#include "Materials/MaterialExpressionPower.h"

#include "RoseEditor.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Materials/Material.h"
#include "Materials/MaterialExpressionAdd.h"
#include "Materials/MaterialExpressionAppendVector.h"
#include "Materials/MaterialExpressionConstant.h"
#include "Materials/MaterialExpressionDistance.h"
#include "Materials/MaterialExpressionDotProduct.h"
#include "Materials/MaterialExpressionMax.h"
#include "Materials/MaterialExpressionSkyAtmosphereLightDirection.h"
#include "Materials/MaterialExpressionDivide.h"
#include "Materials/MaterialExpressionOneMinus.h"
#include "Materials/MaterialExpressionSaturate.h"
#include "Materials/MaterialExpressionVertexNormalWS.h"
#include "Materials/MaterialExpressionWorldPosition.h"
#include "Materials/MaterialExpressionLinearInterpolate.h"
#include "Materials/MaterialExpressionNoise.h"
#include "Materials/MaterialExpressionTime.h"
#include "Materials/MaterialExpressionFloor.h"
#include "Materials/MaterialExpressionFrac.h"
#include "Materials/MaterialExpressionSine.h"
#include "Materials/MaterialExpressionAbs.h"
#include "Materials/MaterialExpressionSubtract.h"
#include "Materials/MaterialExpressionComponentMask.h"
#include "Materials/MaterialExpressionConstant2Vector.h"
#include "Materials/MaterialExpressionMultiply.h"
#include "Materials/MaterialExpressionScalarParameter.h"
#include "Materials/MaterialExpressionTextureCoordinate.h"
#include "Materials/MaterialExpressionTextureSampleParameter2D.h"
#include "Materials/MaterialExpressionVectorParameter.h"
#include "MaterialEditingLibrary.h"
#include "Misc/PackageName.h"
#include "SceneTypes.h"          // EMaterialProperty (MP_BaseColor, ...)
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	const TCHAR* kTerrainPkg = TEXT("/Game/Atlas/M_RoseTerrain");
	const TCHAR* kTerrainName = TEXT("M_RoseTerrain");

	// BUMP THIS whenever the graph below changes.
	//
	// Checking only that the parameters exist is not enough: a master built by
	// an OLDER version of this function still passes that check, so the importer
	// keeps using a stale graph and edits here silently never reach the screen.
	// That cost two full build-and-import cycles that appeared to do nothing.
	// The version is stored as a scalar parameter on the material itself.
	constexpr float kMasterVersion = 7.f;
	const TCHAR* kVersionParam = TEXT("RoseMasterVersion");

	template <typename T>
	T* Expr(UMaterial* M, int32 X, int32 Y)
	{
		return Cast<T>(UMaterialEditingLibrary::CreateMaterialExpression(
			M, T::StaticClass(), X, Y));
	}

	// EVERY texture sampler needs a real default.
	//
	// A UMaterialExpressionTextureSampleParameter2D left with a null Texture
	// does not compile into a usable parameter — the material falls back to the
	// engine's grey grid, which is what "the ground has no texture" looks like.
	// It also means SetTextureParameterValueEditorOnly on the instance silently
	// does nothing, because the parameter never existed.
	UTexture* NeutralWhite()
	{
		static UTexture* White = LoadObject<UTexture>(
			nullptr, TEXT("/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"));
		if (!White)
		{
			UE_LOG(LogRoseImport, Error,
				TEXT("WhiteSquareTexture missing — terrain samplers would have no default"));
		}
		return White;
	}

	UMaterialExpressionTextureSampleParameter2D* Sampler(
		UMaterial* M, int32 X, int32 Y, const TCHAR* ParamName)
	{
		auto* S = Expr<UMaterialExpressionTextureSampleParameter2D>(M, X, Y);
		S->ParameterName = ParamName;
		S->Texture = NeutralWhite();
		return S;
	}

	void Connect(UMaterialExpression* From, const TCHAR* FromOut,
		UMaterialExpression* To, const TCHAR* ToIn)
	{
		UMaterialEditingLibrary::ConnectMaterialExpressions(From, FromOut, To, ToIn);
	}

	// TexCoord(index) * Param.RG + Param.BA  ->  atlas sub-rect UVs.
	//
	// The float2s are assembled from the vector parameter's SCALAR pins with
	// AppendVector: a ComponentMask over a VectorParameter's float3 output does
	// not compile ("Not enough components for mask 0011").
	UMaterialExpression* UVChain(UMaterial* M, int32 UVIndex, const TCHAR* ParamName, int32 Y)
	{
		auto* TC = Expr<UMaterialExpressionTextureCoordinate>(M, -1400, Y);
		TC->CoordinateIndex = UVIndex;

		auto* P = Expr<UMaterialExpressionVectorParameter>(M, -1400, Y + 90);
		P->ParameterName = ParamName;
		P->DefaultValue = FLinearColor(1.f, 1.f, 0.f, 0.f);

		auto* Scale = Expr<UMaterialExpressionAppendVector>(M, -1150, Y + 60);
		auto* Offset = Expr<UMaterialExpressionAppendVector>(M, -1150, Y + 160);
		auto* Mul = Expr<UMaterialExpressionMultiply>(M, -960, Y);
		auto* Add = Expr<UMaterialExpressionAdd>(M, -820, Y);

		Connect(P, TEXT("R"), Scale, TEXT("A"));
		Connect(P, TEXT("G"), Scale, TEXT("B"));
		Connect(P, TEXT("B"), Offset, TEXT("A"));
		Connect(P, TEXT("A"), Offset, TEXT("B"));
		Connect(TC, TEXT(""), Mul, TEXT("A"));
		Connect(Scale, TEXT(""), Mul, TEXT("B"));
		Connect(Mul, TEXT(""), Add, TEXT("A"));
		Connect(Offset, TEXT(""), Add, TEXT("B"));
		return Add;
	}
}

UMaterial* RoseMaterials::EnsureTerrainMaster(bool bForceRebuild)
{
	const FString ObjPath = FString::Printf(TEXT("%s.%s"), kTerrainPkg, kTerrainName);
	UMaterial* Existing = LoadObject<UMaterial>(nullptr, *ObjPath);

	// Existence is NOT enough — validate it.
	//
	// A master carrying a sampler with a null texture compiles to the engine's
	// grey grid and, worse, its parameter never registers, so the instance's
	// SetTextureParameterValue silently does nothing and the whole zone renders
	// untextured.  Check that every expected parameter is present AND has a real
	// default; rebuild if not, so a half-written master repairs itself instead
	// of quietly poisoning every import.
	if (Existing && !bForceRebuild)
	{
		bool bValid = true;

		// Is it OUR current graph?  A stale master passes every parameter check
		// below while still rendering the previous version's output.
		float Version = 0.f;
		if (!Existing->GetScalarParameterValue(FHashedMaterialParameterInfo(kVersionParam), Version)
			|| Version != kMasterVersion)
		{
			UE_LOG(LogRoseImport, Log,
				TEXT("%s: version %.0f != %.0f — rebuilding the master"),
				kTerrainName, Version, kMasterVersion);
			bValid = false;
		}

		static const TCHAR* Required[] = { TEXT("BaseColor"), TEXT("TopColor"), TEXT("Lightmap") };
		for (const TCHAR* Name : Required)
		{
			if (!bValid)
				break;
			UTexture* Value = nullptr;
			const FHashedMaterialParameterInfo Info(Name);
			if (!Existing->GetTextureParameterValue(Info, Value) || !Value)
			{
				UE_LOG(LogRoseImport, Warning,
					TEXT("%s: parameter '%s' missing or has no texture — rebuilding the master"),
					kTerrainName, Name);
				bValid = false;
			}
		}
		if (bValid)
			return Existing;
	}

	UPackage* Pkg = CreatePackage(kTerrainPkg);
	Pkg->FullyLoad();

	UMaterial* M = Existing;
	if (!M)
		M = NewObject<UMaterial>(Pkg, kTerrainName, RF_Public | RF_Standalone);

	// In place — never delete+recreate the asset under the same name.
	UMaterialEditingLibrary::DeleteAllMaterialExpressions(M);

	M->BlendMode = BLEND_Opaque;
	M->TwoSided = true;

	// LIT.  UE owns the lighting for the whole game, ground included.
	//
	// ROSE's own terrain shader is unlit (terrain.psh writes
	// lerp(bottom,top,top.a) * lightmap straight out) and this was built that
	// way for a while, but an unlit ground CANNOT RECEIVE SHADOWS — objects
	// cast them and nothing catches them, which is the flat, shadowless look.
	//
	// Two separate things blew the ground out white on the way here, and both
	// were the LIGHTMAP wiring, never the shading model:
	//   1. additive (`albedo + albedo*light*5`) — an unlit-only construct that
	//      stacks on UE's sun;
	//   2. removed entirely — which sounds safe and is not, because ROSE authors
	//      ground textures bright precisely so this map can multiply them down.
	// The lightmap below is now ROSE's own `mul_x2`, which is correct for both.
	M->SetShadingModel(MSM_DefaultLit);

	UMaterialExpression* BotUV = UVChain(M, 0, TEXT("UVTransform"), 0);
	UMaterialExpression* TopUV = UVChain(M, 1, TEXT("TopUVTransform"), 420);

	auto* Bot = Sampler(M, -600, 0, TEXT("BaseColor"));
	Connect(BotUV, TEXT(""), Bot, TEXT("UVs"));

	auto* Top = Sampler(M, -600, 420, TEXT("TopColor"));
	Connect(TopUV, TEXT(""), Top, TEXT("UVs"));

	// rgb = lerp(bottom.rgb, top.rgb, top.a)   — terrain.psh line 16.
	auto* Lerp = Expr<UMaterialExpressionLinearInterpolate>(M, -280, 120);
	Connect(Bot, TEXT("RGB"), Lerp, TEXT("A"));
	Connect(Top, TEXT("RGB"), Lerp, TEXT("B"));
	Connect(Top, TEXT("A"), Lerp, TEXT("Alpha"));

	// albedo * lerp(1, lightmap*2, LightmapScale)
	//
	// THE LIGHTMAP MUST BE APPLIED, and this is why: ROSE's ground textures are
	// authored deliberately BRIGHT because the engine multiplies them back down.
	// JPT01's plaza tiles measure RGB ~(188,173,150) — 0.74 albedo, far lighter
	// than any real stone.  Lit with no lightmap, that ground can only render
	// near-white, which is exactly the "the ground is bright" report.  Turning
	// the lightmap off to avoid double-shading against UE's shadows removed the
	// one thing holding that albedo down.
	//
	// The form is ROSE's own (terrain.psh `mul_x2 r0.rgb, r0, t2`), NOT additive
	// — `albedo + albedo*light*5` is an unlit-only construct and stacking it on
	// UE's sun is what blew the ground out the first time lit was tried.
	//
	// Written as a lerp from WHITE so a zone with NO lightmap is exactly albedo
	// rather than black.  The importer sets 1 where it binds a real lightmap.
	auto* LmTC = Expr<UMaterialExpressionTextureCoordinate>(M, -1400, 860);
	LmTC->CoordinateIndex = 2;

	auto* Lm = Sampler(M, -600, 860, TEXT("Lightmap"));
	Connect(LmTC, TEXT(""), Lm, TEXT("UVs"));

	auto* LmScale = Expr<UMaterialExpressionScalarParameter>(M, -600, 1000);
	LmScale->ParameterName = TEXT("LightmapScale");
	LmScale->DefaultValue = 0.f;

	auto* One = Expr<UMaterialExpressionConstant>(M, -600, 1080);
	One->R = 1.f;

	// lightmap * 2 — ROSE's `mul_x2 r0.rgb, r0, t2` (terrain.psh line 17).
	//
	// mul_x2 is a D3D8/9 pixel-shader modifier that doubles the result, so a
	// mid-grey 0.5 lightmap is NEUTRAL, not half brightness.  This is also why
	// the old Python master defaulted its lightmap sampler to GreyTexture:
	// 0.5 * 2 = 1.
	auto* Two = Expr<UMaterialExpressionConstant>(M, -600, 1160);
	Two->R = 2.f;

	auto* LmX2 = Expr<UMaterialExpressionMultiply>(M, -520, 940);
	Connect(Lm, TEXT("RGB"), LmX2, TEXT("A"));
	Connect(Two, TEXT(""), LmX2, TEXT("B"));

	auto* LmFactor = Expr<UMaterialExpressionLinearInterpolate>(M, -420, 900);
	Connect(One, TEXT(""), LmFactor, TEXT("A"));
	Connect(LmX2, TEXT(""), LmFactor, TEXT("B"));
	Connect(LmScale, TEXT(""), LmFactor, TEXT("Alpha"));

	auto* Lit = Expr<UMaterialExpressionMultiply>(M, -280, 240);
	Connect(Lerp, TEXT(""), Lit, TEXT("A"));
	Connect(LmFactor, TEXT(""), Lit, TEXT("B"));

	auto* Tint = Expr<UMaterialExpressionVectorParameter>(M, -280, 340);
	Tint->ParameterName = TEXT("Tint");
	Tint->DefaultValue = FLinearColor::White;

	auto* TintMul = Expr<UMaterialExpressionMultiply>(M, -100, 160);
	Connect(Lit, TEXT(""), TintMul, TEXT("A"));
	Connect(Tint, TEXT(""), TintMul, TEXT("B"));

	// Version stamp, so a later change to this function forces a rebuild instead
	// of silently leaving the old graph in place.  Unconnected on purpose — it
	// exists only to be read back by the check above.
	auto* Ver = Expr<UMaterialExpressionScalarParameter>(M, -1400, 1300);
	Ver->ParameterName = kVersionParam;
	Ver->DefaultValue = kMasterVersion;

	UMaterialEditingLibrary::ConnectMaterialProperty(TintMul, TEXT(""), MP_BaseColor);

	// Matte. Ground is not glossy, and UE's defaults (roughness 0.5, specular
	// 0.5) put a sheen across every hill once a real sun is on it.
	auto* Rough = Expr<UMaterialExpressionConstant>(M, -100, 420);
	Rough->R = 1.f;
	UMaterialEditingLibrary::ConnectMaterialProperty(Rough, TEXT(""), MP_Roughness);

	auto* Spec = Expr<UMaterialExpressionConstant>(M, -100, 500);
	Spec->R = 0.f;
	UMaterialEditingLibrary::ConnectMaterialProperty(Spec, TEXT(""), MP_Specular);

	UMaterialEditingLibrary::RecompileMaterial(M);

	FAssetRegistryModule::AssetCreated(M);
	Pkg->MarkPackageDirty();

	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename = FPackageName::LongPackageNameToFilename(
		kTerrainPkg, FPackageName::GetAssetPackageExtension());
	if (UPackage::SavePackage(Pkg, M, *Filename, Args))
		Pkg->SetDirtyFlag(false);
	else
		UE_LOG(LogRoseImport, Error, TEXT("failed to save %s"), kTerrainPkg);

	UE_LOG(LogRoseImport, Log, TEXT("built %s (opaque, 3 samplers)"), kTerrainName);
	return M;
}

// ═══════════════════════════════════════════════════════════════════════════
//  M_RoseChar — the AVATAR master
// ═══════════════════════════════════════════════════════════════════════════
namespace
{
	// Under /Game/Rose with everything else the native importer owns.
	//
	// It used to live at /Game/Characters/Materials/M_RoseChar — inside the
	// LEGACY content tree.  Deleting that tree (which is the point of the native
	// path) takes the character master with it, and every character MI then
	// parents to a missing asset and renders dark.  Nothing the native importer
	// produces may depend on /Game/Characters.
	const TCHAR* kCharPkg = TEXT("/Game/Rose/Characters/M_RoseChar");
	const TCHAR* kCharName = TEXT("M_RoseChar");

	// Bump when the graph below changes.  Same reasoning as the terrain master:
	// a master built by an older version passes every parameter check while
	// still rendering the previous version's output.
	constexpr float kCharVersion = 7.f;   // 7 = RoseLightScale/RoseAmbient (6 = vertex light)
}

UMaterial* RoseMaterials::EnsureCharacterMaster(bool bForceRebuild)
{
	const FString ObjPath = FString::Printf(TEXT("%s.%s"), kCharPkg, kCharName);
	UMaterial* Existing = LoadObject<UMaterial>(nullptr, *ObjPath);

	if (Existing && !bForceRebuild)
	{
		float Version = 0.f;
		if (Existing->GetScalarParameterValue(FHashedMaterialParameterInfo(kVersionParam), Version)
			&& Version == kCharVersion)
		{
			return Existing;
		}
		UE_LOG(LogRoseImport, Log,
			TEXT("%s: version %.0f != %.0f — rebuilding the character master"),
			kCharName, Version, kCharVersion);
	}

	UPackage* Pkg = CreatePackage(kCharPkg);
	Pkg->FullyLoad();

	UMaterial* M = Existing;
	if (!M)
		M = NewObject<UMaterial>(Pkg, kCharName, RF_Public | RF_Standalone);

	// In place — never delete+recreate under the same name.
	UMaterialEditingLibrary::DeleteAllMaterialExpressions(M);

	// MASKED + TWO-SIDED by default.
	//
	// ROSE avatar meshes are open shells, not closed solids: one-sided culls the
	// backfaces and the character reads as see-through.  Instances still carry
	// their own BasePropertyOverrides (the ZSC decides masked vs opaque per
	// material), but the master's own defaults have to be the safe ones.
	M->BlendMode = BLEND_Masked;
	M->TwoSided = true;

	// UNLIT, and this is the point of the material.
	//
	// ROSE's character textures are hand-painted with their lighting already in
	// the diffuse — highlights, shading, ambient occlusion, all baked.  Running
	// them through PBR lighting applies it a SECOND time, which is what made the
	// characters look muddy in a lit scene while looking perfect in the mesh
	// editor's bright studio preview.
	//

	// Build every character material on "Universal Render Pipeline/Unlit" with nothing but _BaseMap.
	// Unlit in UE outputs through EMISSIVE, so the albedo goes there, not into
	// BaseColor.
	M->SetShadingModel(MSM_Unlit);

	// Skeletal usage, explicitly.  A master without it renders every skeletal
	// mesh using it as the engine's default grey material — no import error,
	// just a wrong character.
	M->bUsedWithSkeletalMesh = true;

	auto* Tex = Sampler(M, -600, 0, TEXT("BaseColor"));

	// SAMPLERTYPE_Color, explicitly.
	//
	// The parameter's sampler type is otherwise inferred from the sampler's
	// DEFAULT texture (a white 1x1), not from the sRGB albedo the instance binds
	// later.  A mismatch there decodes the texture wrongly and the whole
	// character renders dark while every instance-level check still passes.
	Tex->SamplerType = SAMPLERTYPE_Color;

	// ── refine glow ──────────────────────────────────────────────────────────
	//
	// The original fixed-pipeline effect (ZZ_GLOW_TEXTURE), transcribed from the
	// reference client's RefineGlow shader:
	//
	//     glow  = pow(albedo, GlowPower) * RefineColor * RefineIntensity
	//     final = albedo + glow
	//
	// Raising the albedo to a power before tinting is what makes the effect sit
	// on the BRIGHT parts of the texture rather than washing the whole item —
	// a plain multiply would tint the dark areas just as much.
	//
	// RefineIntensity defaults to 0, so an unrefined item is byte-identical to
	// no glow at all; the per-item instance raises it and picks the colour from
	// the refine table (which already carries one per grade: +8 = 250,180,120,
	// +9 = 255,255,140, ...).
	auto* GlowPower = Expr<UMaterialExpressionScalarParameter>(M, -900, 200);
	GlowPower->ParameterName = TEXT("GlowPower");
	GlowPower->DefaultValue = 1.f;

	auto* RefineColor = Expr<UMaterialExpressionVectorParameter>(M, -900, 300);
	RefineColor->ParameterName = TEXT("RefineColor");
	RefineColor->DefaultValue = FLinearColor::White;

	auto* RefineIntensity = Expr<UMaterialExpressionScalarParameter>(M, -900, 400);
	RefineIntensity->ParameterName = TEXT("RefineIntensity");
	RefineIntensity->DefaultValue = 0.f;      // 0 = no glow

	auto* Pow = Expr<UMaterialExpressionPower>(M, -600, 250);
	Pow->Base.Connect(0, Tex);
	Pow->Exponent.Connect(0, GlowPower);

	auto* GlowTint = Expr<UMaterialExpressionMultiply>(M, -420, 250);
	GlowTint->A.Connect(0, Pow);
	GlowTint->B.Connect(0, RefineColor);

	auto* Glow = Expr<UMaterialExpressionMultiply>(M, -280, 250);
	Glow->A.Connect(0, GlowTint);
	Glow->B.Connect(0, RefineIntensity);

	// ── ROSE vertex lighting ────────────────────────────────────────────────
	//
	// 1:1 with the reference client's ROSE/RefineGlow (Neo Refine Shader.shader):
	//
	//     light   = max(dot(N, L), 0) + 0.5      <- the original ROSE term
	//     diffuse = tex * _Color * light
	//     glow    = pow(tex, GlowPower) * _RefineColor * _RefineIntensity
	//     final   = diffuse + glow
	//
	// The glow half already matched; this is the half that was missing.  Going
	// unlit dropped ROSE's own lambert-plus-ambient term entirely, so every
	// character rendered at flat full albedo with no form to it.  Adding it back
	// is not "lighting the character twice": the shading model stays UNLIT and
	// this is ROSE's own arithmetic reproduced inside the emissive, exactly as
	//
	// The +0.5 is the ambient floor, so an unlit face never goes below half
	// brightness — which is why ROSE characters read as bright and flat rather
	// than dark on their shadowed side.
	//
	// Light direction comes from the sky atmosphere's light 0 (the sun) rather
	// than a parameter pushed from C++: no per-frame work, no actor coupling, and
	// it tracks the time-of-day sun the zone lighting already sets up.
	auto* SunDir = Expr<UMaterialExpressionSkyAtmosphereLightDirection>(M, -900, -260);
	SunDir->LightIndex = 0;

	auto* NormalForLight = Expr<UMaterialExpressionVertexNormalWS>(M, -900, -180);

	auto* NdotL = Expr<UMaterialExpressionDotProduct>(M, -700, -220);
	NdotL->A.Connect(0, NormalForLight);
	NdotL->B.Connect(0, SunDir);

	auto* Zero = Expr<UMaterialExpressionConstant>(M, -700, -120);
	Zero->R = 0.f;

	auto* NdotLClamped = Expr<UMaterialExpressionMax>(M, -560, -200);
	NdotLClamped->A.Connect(0, NdotL);
	NdotLClamped->B.Connect(0, Zero);

	//
	//     light = max(dot(N,L),0) * RoseLightScale + RoseAmbient
	//
	//   RoseLightScale 1, RoseAmbient 0.5  -> ROSE/RefineGlow exactly
	//   RoseLightScale 0, RoseAmbient 1.0  -> Universal Render Pipeline/Unlit
	//                                          exactly (flat albedo)
	//
	//
	// Raising RoseAmbient alone does NOT reproduce flat: with the NdotL term
	// still summed in, an ambient of 1 gives 1..2 — the lit side comes out at
	// double brightness.  Killing the scale is what flattens it.
	auto* LightScale = Expr<UMaterialExpressionScalarParameter>(M, -700, -20);
	LightScale->ParameterName = TEXT("RoseLightScale");
	LightScale->DefaultValue = 0.f;      // 0 = flat, matches URP/Unlit

	auto* Ambient = Expr<UMaterialExpressionScalarParameter>(M, -700, -60);
	Ambient->ParameterName = TEXT("RoseAmbient");
	Ambient->DefaultValue = 1.f;         // 1 = full albedo when LightScale is 0

	auto* LightScaled = Expr<UMaterialExpressionMultiply>(M, -560, -140);
	LightScaled->A.Connect(0, NdotLClamped);
	LightScaled->B.Connect(0, LightScale);

	auto* LightTerm = Expr<UMaterialExpressionAdd>(M, -430, -160);
	LightTerm->A.Connect(0, LightScaled);
	LightTerm->B.Connect(0, Ambient);

	// _Color in the  shader — a per-instance tint, white by default.
	auto* Tint = Expr<UMaterialExpressionVectorParameter>(M, -700, 20);
	Tint->ParameterName = TEXT("RoseTint");
	Tint->DefaultValue = FLinearColor::White;

	auto* Tinted = Expr<UMaterialExpressionMultiply>(M, -430, 40);
	Tinted->A.Connect(0, Tex);
	Tinted->B.Connect(0, Tint);

	auto* Diffuse = Expr<UMaterialExpressionMultiply>(M, -280, 60);
	Diffuse->A.Connect(0, Tinted);
	Diffuse->B.Connect(0, LightTerm);

	auto* Emissive = Expr<UMaterialExpressionAdd>(M, -140, 100);
	Emissive->A.Connect(0, Diffuse);
	Emissive->B.Connect(0, Glow);

	// ── body shape: chest bulge ──────────────────────────────────────────────
	//
	// A world-position offset, NOT a morph target.  Morphs are the natural tool
	// for this and they cannot be used: the character renders as ONE mesh built
	// by USkeletalMergingLibrary::MergeMeshes, and neither SkeletalMeshMerge.cpp
	// nor SkeletalMergingLibrary.cpp mentions morph targets anywhere — the merge
	// drops them, so a morph authored on the body part is gone before it draws.
	// Bone scaling is out too: FEMALE.ZMD has 21 bones and no chest-specific
	// one; b1_chest parents the neck and both clavicles, so scaling it inflates
	// the whole upper body and the arms with it.
	//
	// WPO survives the merge because it lives in the MATERIAL, which the merge
	// carries through as a slot.
	//
	// ChestCenter is WORLD space and is pushed every frame from the b1_chest
	// bone (ARoseCharacter), so the region follows the animation instead of
	// staying put while the body moves through it.  Vertices swell along their
	// own normal, which reads as a body shape rather than the balloon a radial
	// push from a point would give.
	//
	//     falloff = saturate(1 - distance(WorldPos, ChestCenter) / ChestRadius)
	//     WPO     = VertexNormalWS * falloff * ChestBulge
	//
	// ChestBulge defaults to 0, so an untouched character is byte-identical to
	// no offset at all.
	auto* ChestCenter = Expr<UMaterialExpressionVectorParameter>(M, -900, 600);
	ChestCenter->ParameterName = TEXT("ChestCenter");
	ChestCenter->DefaultValue = FLinearColor(0.f, 0.f, 0.f, 0.f);

	auto* ChestRadius = Expr<UMaterialExpressionScalarParameter>(M, -900, 700);
	ChestRadius->ParameterName = TEXT("ChestRadius");
	ChestRadius->DefaultValue = 18.f;      // cm; roughly the chest region

	auto* ChestBulge = Expr<UMaterialExpressionScalarParameter>(M, -900, 800);
	ChestBulge->ParameterName = TEXT("ChestBulge");
	ChestBulge->DefaultValue = 0.f;        // 0 = untouched

	auto* WorldPos = Expr<UMaterialExpressionWorldPosition>(M, -900, 500);

	auto* Dist = Expr<UMaterialExpressionDistance>(M, -700, 550);
	Dist->A.Connect(0, WorldPos);
	Dist->B.Connect(0, ChestCenter);

	auto* DistOverR = Expr<UMaterialExpressionDivide>(M, -560, 600);
	DistOverR->A.Connect(0, Dist);
	DistOverR->B.Connect(0, ChestRadius);

	auto* Inv = Expr<UMaterialExpressionOneMinus>(M, -430, 600);
	Inv->Input.Connect(0, DistOverR);

	auto* Falloff = Expr<UMaterialExpressionSaturate>(M, -320, 600);
	Falloff->Input.Connect(0, Inv);

	auto* Amount = Expr<UMaterialExpressionMultiply>(M, -220, 650);
	Amount->A.Connect(0, Falloff);
	Amount->B.Connect(0, ChestBulge);

	auto* NormalWS = Expr<UMaterialExpressionVertexNormalWS>(M, -320, 750);

	auto* Offset = Expr<UMaterialExpressionMultiply>(M, -120, 700);
	Offset->A.Connect(0, NormalWS);
	Offset->B.Connect(0, Amount);

	UMaterialEditingLibrary::ConnectMaterialProperty(Offset, TEXT(""), MP_WorldPositionOffset);

	UMaterialEditingLibrary::ConnectMaterialProperty(Emissive, TEXT(""), MP_EmissiveColor);
	// Texture alpha drives the cutout; the instance's clip value decides where.
	UMaterialEditingLibrary::ConnectMaterialProperty(Tex, TEXT("A"), MP_OpacityMask);

	// MATTE, and NOT metallic — this is the whole point of rebuilding it.
	//
	// The version ue5_import_modular.py produced wired only BaseColor and the
	// opacity mask, so roughness and specular fell back to UE's 0.5/0.5.  On
	// ROSE's flat hand-painted diffuse that is a shiny plastic sheen, and the
	// specular response under a dim sun is what made the character look dark.
	// M_RoseMaster pins 1.0/0.0 and world geometry looks right; the avatar
	// master needs the same.  Metallic is pinned at 0 rather than left to
	// default, because a metallic surface with nothing to reflect renders black.
	auto* Rough = Expr<UMaterialExpressionConstant>(M, -300, 300);
	Rough->R = 1.f;
	UMaterialEditingLibrary::ConnectMaterialProperty(Rough, TEXT(""), MP_Roughness);

	auto* Spec = Expr<UMaterialExpressionConstant>(M, -300, 380);
	Spec->R = 0.f;
	UMaterialEditingLibrary::ConnectMaterialProperty(Spec, TEXT(""), MP_Specular);

	auto* Metal = Expr<UMaterialExpressionConstant>(M, -300, 460);
	Metal->R = 0.f;
	UMaterialEditingLibrary::ConnectMaterialProperty(Metal, TEXT(""), MP_Metallic);

	// Version stamp, read back by the check above.  Unconnected on purpose.
	auto* Ver = Expr<UMaterialExpressionScalarParameter>(M, -900, 600);
	Ver->ParameterName = kVersionParam;
	Ver->DefaultValue = kCharVersion;

	UMaterialEditingLibrary::RecompileMaterial(M);

	FAssetRegistryModule::AssetCreated(M);
	Pkg->MarkPackageDirty();

	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename = FPackageName::LongPackageNameToFilename(
		kCharPkg, FPackageName::GetAssetPackageExtension());
	if (UPackage::SavePackage(Pkg, M, *Filename, Args))
		Pkg->SetDirtyFlag(false);
	else
		UE_LOG(LogRoseImport, Error, TEXT("failed to save %s"), kCharPkg);

	UE_LOG(LogRoseImport, Log,
		TEXT("built %s (masked, two-sided, roughness 1 / specular 0 / metallic 0)"),
		kCharName);
	return M;
}

namespace
{
	const TCHAR* kSkyPkg = TEXT("/Game/Rose/Sky/M_RoseSky");
	const TCHAR* kSkyName = TEXT("M_RoseSky");
	constexpr float kSkyVersion = 1.f;
}

UMaterial* RoseMaterials::EnsureSkyMaster(bool bForceRebuild)
{
	const FString ObjPath = FString::Printf(TEXT("%s.%s"), kSkyPkg, kSkyName);
	UMaterial* Existing = LoadObject<UMaterial>(nullptr, *ObjPath);

	if (Existing && !bForceRebuild)
	{
		float Version = 0.f;
		if (Existing->GetScalarParameterValue(FHashedMaterialParameterInfo(kVersionParam), Version)
			&& Version == kSkyVersion)
		{
			return Existing;
		}
		UE_LOG(LogRoseImport, Log,
			TEXT("%s: version %.0f != %.0f — rebuilding the sky master"),
			kSkyName, Version, kSkyVersion);
	}

	UPackage* Pkg = CreatePackage(kSkyPkg);
	Pkg->FullyLoad();

	UMaterial* M = Existing;
	if (!M)
		M = NewObject<UMaterial>(Pkg, kSkyName, RF_Public | RF_Standalone);

	// In place — never delete+recreate under the same name.
	UMaterialEditingLibrary::DeleteAllMaterialExpressions(M);

	M->BlendMode = BLEND_Opaque;

	// TWO-SIDED because the camera sits INSIDE the dome.  The ZMS winding faces
	// outward, so a one-sided sky is invisible from where the player stands.
	M->TwoSided = true;

	// UNLIT: the sky texture is the final colour, exactly as ROSE draws it.
	M->SetShadingModel(MSM_Unlit);

	// bIsSky is the UE equivalent of CSkyDOME's ::setReceiveFog(m_hSKY, 0).
	// Without it, exponential height fog is composited over the dome and the
	// upper sky washes out to the fog colour — the "flat grey band" look.
	M->bIsSky = true;

	auto* Day = Sampler(M, -700, -120, TEXT("DayTex"));
	Day->SamplerType = SAMPLERTYPE_Color;

	auto* Night = Sampler(M, -700, 160, TEXT("NightTex"));
	Night->SamplerType = SAMPLERTYPE_Color;

	// 0 = day, 1 = night.  CSkyDOME drives this with setSkyMaterialBlendRatio;
	// the runtime dome actor drives it from the zone's day-cycle clock.
	auto* Blend = Expr<UMaterialExpressionScalarParameter>(M, -700, 40);
	Blend->ParameterName = TEXT("SkyBlend");
	Blend->DefaultValue = 0.f;

	auto* Lerp = Expr<UMaterialExpressionLinearInterpolate>(M, -400, 0);
	Lerp->A.Connect(0, Day);
	Lerp->B.Connect(0, Night);
	Lerp->Alpha.Connect(0, Blend);

	// A brightness trim, so a zone can be tuned without touching the texture.
	auto* Tint = Expr<UMaterialExpressionVectorParameter>(M, -700, 300);
	Tint->ParameterName = TEXT("SkyTint");
	Tint->DefaultValue = FLinearColor::White;

	auto* Tinted = Expr<UMaterialExpressionMultiply>(M, -240, 40);
	Tinted->A.Connect(0, Lerp);
	Tinted->B.Connect(0, Tint);

	// Unlit outputs through EMISSIVE.
	UMaterialEditingLibrary::ConnectMaterialProperty(Tinted, TEXT(""), MP_EmissiveColor);

	auto* Ver = Expr<UMaterialExpressionScalarParameter>(M, -900, 500);
	Ver->ParameterName = kVersionParam;
	Ver->DefaultValue = kSkyVersion;

	UMaterialEditingLibrary::RecompileMaterial(M);

	FAssetRegistryModule::AssetCreated(M);
	Pkg->MarkPackageDirty();

	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename = FPackageName::LongPackageNameToFilename(
		kSkyPkg, FPackageName::GetAssetPackageExtension());
	if (UPackage::SavePackage(Pkg, M, *Filename, Args))
		Pkg->SetDirtyFlag(false);
	else
		UE_LOG(LogRoseImport, Error, TEXT("failed to save %s"), kSkyPkg);

	UE_LOG(LogRoseImport, Log,
		TEXT("built %s (unlit, two-sided, bIsSky, day/night lerp)"), kSkyName);
	return M;
}

namespace
{
	const TCHAR* kPrecipPkg = TEXT("/Game/Rose/Sky/M_RosePrecip");
	const TCHAR* kPrecipName = TEXT("M_RosePrecip");
	constexpr float kPrecipVersion = 2.f;   // 2 = per-column streaks (1 = noise blobs)
}

UMaterial* RoseMaterials::EnsurePrecipMaster(bool bForceRebuild)
{
	const FString ObjPath = FString::Printf(TEXT("%s.%s"), kPrecipPkg, kPrecipName);
	UMaterial* Existing = LoadObject<UMaterial>(nullptr, *ObjPath);

	if (Existing && !bForceRebuild)
	{
		float Version = 0.f;
		if (Existing->GetScalarParameterValue(FHashedMaterialParameterInfo(kVersionParam), Version)
			&& Version == kPrecipVersion)
		{
			return Existing;
		}
	}

	UPackage* Pkg = CreatePackage(kPrecipPkg);
	Pkg->FullyLoad();

	UMaterial* M = Existing;
	if (!M)
		M = NewObject<UMaterial>(Pkg, kPrecipName, RF_Public | RF_Standalone);

	UMaterialEditingLibrary::DeleteAllMaterialExpressions(M);

	M->BlendMode = BLEND_Translucent;
	M->TwoSided = true;
	M->SetShadingModel(MSM_Unlit);
	M->bUsedWithStaticLighting = false;

	// PER-COLUMN STREAKS, not noise.
	//
	// The first version sampled a stretched Noise node and it read as drifting
	// fog, because noise produces round blobs no matter how hard it is
	// thresholded — there is nothing in it that is long and thin and vertical.
	//
	// Rain needs discrete falling DROPS, so the UV is diced into columns and
	// each column gets its own drop:
	//
	//   col   = floor(u * Density)        which column this pixel is in
	//   fu    = frac (u * Density)        position across that column
	//   rnd   = frac(sin(col * 12.9898) * 43758.5453)      per-column offset
	//   v     = frac(v * Density/Stretch + time*Fall + rnd)
	//   drop  = pow(v, Sharpness)         bright head fading upward = a streak
	//   line  = 1 - saturate(|fu-0.5| * Thickness)          thin vertical band
	//   alpha = drop * line * Precipitation
	//
	// The per-column random offset is what stops every drop falling in lockstep
	// (which reads as a moving grid, not rain), and dividing the vertical scale
	// by Stretch smears the drop without also changing the spacing between them.
	auto* Precip = Expr<UMaterialExpressionScalarParameter>(M, -1600, 0);
	Precip->ParameterName = TEXT("Precipitation");
	Precip->DefaultValue = 0.f;

	auto* Speed = Expr<UMaterialExpressionScalarParameter>(M, -1600, 80);
	Speed->ParameterName = TEXT("FallSpeed");
	Speed->DefaultValue = 1.8f;

	auto* Stretch = Expr<UMaterialExpressionScalarParameter>(M, -1600, 160);
	Stretch->ParameterName = TEXT("Stretch");
	Stretch->DefaultValue = 16.f;

	auto* Density = Expr<UMaterialExpressionScalarParameter>(M, -1600, 240);
	Density->ParameterName = TEXT("Density");
	Density->DefaultValue = 26.f;

	auto* Sharp = Expr<UMaterialExpressionScalarParameter>(M, -1600, 320);
	Sharp->ParameterName = TEXT("Sharpness");
	Sharp->DefaultValue = 7.f;

	auto* Thick = Expr<UMaterialExpressionScalarParameter>(M, -1600, 400);
	Thick->ParameterName = TEXT("Thickness");
	Thick->DefaultValue = 22.f;      // higher = thinner streaks

	auto* Colour = Expr<UMaterialExpressionVectorParameter>(M, -1600, 480);
	Colour->ParameterName = TEXT("PrecipColor");
	Colour->DefaultValue = FLinearColor(0.75f, 0.82f, 0.95f);

	auto* TC = Expr<UMaterialExpressionTextureCoordinate>(M, -1400, -140);
	TC->CoordinateIndex = 0;

	auto* U = Expr<UMaterialExpressionComponentMask>(M, -1200, -180);
	U->R = true; U->G = false; U->B = false; U->A = false;
	U->Input.Connect(0, TC);

	auto* V = Expr<UMaterialExpressionComponentMask>(M, -1200, -100);
	V->R = false; V->G = true; V->B = false; V->A = false;
	V->Input.Connect(0, TC);

	// Columns across, from Density.
	auto* Ucols = Expr<UMaterialExpressionMultiply>(M, -1040, -180);
	Ucols->A.Connect(0, U);
	Ucols->B.Connect(0, Density);

	auto* Col = Expr<UMaterialExpressionFloor>(M, -880, -240);
	Col->Input.Connect(0, Ucols);

	auto* Fu = Expr<UMaterialExpressionFrac>(M, -880, -140);
	Fu->Input.Connect(0, Ucols);

	// Per-column pseudo-random offset: frac(sin(col * 12.9898) * 43758.5453).
	auto* HashA = Expr<UMaterialExpressionConstant>(M, -880, -340);
	HashA->R = 12.9898f;

	auto* HashB = Expr<UMaterialExpressionConstant>(M, -880, -400);
	HashB->R = 43758.5453f;

	auto* ColMul = Expr<UMaterialExpressionMultiply>(M, -720, -300);
	ColMul->A.Connect(0, Col);
	ColMul->B.Connect(0, HashA);

	auto* ColSin = Expr<UMaterialExpressionSine>(M, -580, -300);
	ColSin->Input.Connect(0, ColMul);

	auto* ColBig = Expr<UMaterialExpressionMultiply>(M, -440, -300);
	ColBig->A.Connect(0, ColSin);
	ColBig->B.Connect(0, HashB);

	auto* Rnd = Expr<UMaterialExpressionFrac>(M, -300, -300);
	Rnd->Input.Connect(0, ColBig);

	// Vertical: v * (Density / Stretch) + time * FallSpeed + rnd, wrapped.
	auto* VScale = Expr<UMaterialExpressionDivide>(M, -1200, 200);
	VScale->A.Connect(0, Density);
	VScale->B.Connect(0, Stretch);

	auto* Vrows = Expr<UMaterialExpressionMultiply>(M, -1040, 60);
	Vrows->A.Connect(0, V);
	Vrows->B.Connect(0, VScale);

	auto* Time = Expr<UMaterialExpressionTime>(M, -1200, 300);

	auto* Fall = Expr<UMaterialExpressionMultiply>(M, -1040, 300);
	Fall->A.Connect(0, Time);
	Fall->B.Connect(0, Speed);

	auto* VplusT = Expr<UMaterialExpressionAdd>(M, -880, 120);
	VplusT->A.Connect(0, Vrows);
	VplusT->B.Connect(0, Fall);

	auto* VplusR = Expr<UMaterialExpressionAdd>(M, -720, 120);
	VplusR->A.Connect(0, VplusT);
	VplusR->B.Connect(0, Rnd);

	auto* Vv = Expr<UMaterialExpressionFrac>(M, -580, 120);
	Vv->Input.Connect(0, VplusR);

	auto* Drop = Expr<UMaterialExpressionPower>(M, -440, 120);
	Drop->Base.Connect(0, Vv);
	Drop->Exponent.Connect(0, Sharp);

	// Horizontal: a thin band centred in each column.
	auto* Half = Expr<UMaterialExpressionConstant>(M, -880, -60);
	Half->R = 0.5f;

	auto* Centred = Expr<UMaterialExpressionSubtract>(M, -720, -100);
	Centred->A.Connect(0, Fu);
	Centred->B.Connect(0, Half);

	auto* Dist = Expr<UMaterialExpressionAbs>(M, -580, -100);
	Dist->Input.Connect(0, Centred);

	auto* DistT = Expr<UMaterialExpressionMultiply>(M, -440, -100);
	DistT->A.Connect(0, Dist);
	DistT->B.Connect(0, Thick);

	auto* Inv = Expr<UMaterialExpressionOneMinus>(M, -300, -100);
	Inv->Input.Connect(0, DistT);

	auto* Line = Expr<UMaterialExpressionSaturate>(M, -180, -100);
	Line->Input.Connect(0, Inv);

	auto* Streak = Expr<UMaterialExpressionMultiply>(M, -40, 20);
	Streak->A.Connect(0, Drop);
	Streak->B.Connect(0, Line);

	auto* Opacity = Expr<UMaterialExpressionMultiply>(M, 120, 120);
	Opacity->A.Connect(0, Streak);
	Opacity->B.Connect(0, Precip);

	auto* Emissive = Expr<UMaterialExpressionMultiply>(M, 120, -80);
	Emissive->A.Connect(0, Colour);
	Emissive->B.Connect(0, Streak);

	UMaterialEditingLibrary::ConnectMaterialProperty(Emissive, TEXT(""), MP_EmissiveColor);
	UMaterialEditingLibrary::ConnectMaterialProperty(Opacity, TEXT(""), MP_Opacity);

	auto* Ver = Expr<UMaterialExpressionScalarParameter>(M, -1600, 560);
	Ver->ParameterName = kVersionParam;
	Ver->DefaultValue = kPrecipVersion;

	UMaterialEditingLibrary::RecompileMaterial(M);

	FAssetRegistryModule::AssetCreated(M);
	Pkg->MarkPackageDirty();

	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename = FPackageName::LongPackageNameToFilename(
		kPrecipPkg, FPackageName::GetAssetPackageExtension());
	if (UPackage::SavePackage(Pkg, M, *Filename, Args))
		Pkg->SetDirtyFlag(false);
	else
		UE_LOG(LogRoseImport, Error, TEXT("failed to save %s"), kPrecipPkg);

	UE_LOG(LogRoseImport, Log, TEXT("built %s (per-column procedural streaks)"), kPrecipName);
	return M;
}

namespace
{
	const TCHAR* kParticlePkg = TEXT("/Game/Rose/Sky/M_RoseParticle");
	const TCHAR* kParticleName = TEXT("M_RoseParticle");
	constexpr float kParticleVersion = 1.f;
}

UMaterial* RoseMaterials::EnsureParticleMaster(bool bForceRebuild)
{
	const FString ObjPath = FString::Printf(TEXT("%s.%s"), kParticlePkg, kParticleName);
	UMaterial* Existing = LoadObject<UMaterial>(nullptr, *ObjPath);

	if (Existing && !bForceRebuild)
	{
		float Version = 0.f;
		if (Existing->GetScalarParameterValue(FHashedMaterialParameterInfo(kVersionParam), Version)
			&& Version == kParticleVersion)
		{
			return Existing;
		}
	}

	UPackage* Pkg = CreatePackage(kParticlePkg);
	Pkg->FullyLoad();

	UMaterial* M = Existing;
	if (!M)
		M = NewObject<UMaterial>(Pkg, kParticleName, RF_Public | RF_Standalone);

	UMaterialEditingLibrary::DeleteAllMaterialExpressions(M);

	M->BlendMode = BLEND_Translucent;
	M->TwoSided = true;
	M->SetShadingModel(MSM_Unlit);

	// Instanced static meshes need this or every instance draws with the
	// engine's default material — the same trap as bUsedWithSkeletalMesh.
	M->bUsedWithInstancedStaticMeshes = true;

	// alpha = 1 - saturate(|uv - 0.5| * 2 * Softness), a round dot on the plane.
	// The INSTANCE decides the shape: rain scales the quad tall so the dot
	// stretches into a streak, snow keeps it square so it stays a flake.
	auto* TC = Expr<UMaterialExpressionTextureCoordinate>(M, -900, 0);
	TC->CoordinateIndex = 0;

	auto* Half = Expr<UMaterialExpressionConstant2Vector>(M, -900, 140);
	Half->R = 0.5f;
	Half->G = 0.5f;

	auto* Centred = Expr<UMaterialExpressionSubtract>(M, -740, 40);
	Centred->A.Connect(0, TC);
	Centred->B.Connect(0, Half);

	auto* Dist = Expr<UMaterialExpressionDistance>(M, -600, 40);
	Dist->A.Connect(0, Centred);
	auto* Origin = Expr<UMaterialExpressionConstant2Vector>(M, -740, 160);
	Origin->R = 0.f;
	Origin->G = 0.f;
	Dist->B.Connect(0, Origin);

	auto* Soft = Expr<UMaterialExpressionScalarParameter>(M, -900, 240);
	Soft->ParameterName = TEXT("Softness");
	Soft->DefaultValue = 2.2f;

	auto* Scaled = Expr<UMaterialExpressionMultiply>(M, -440, 40);
	Scaled->A.Connect(0, Dist);
	Scaled->B.Connect(0, Soft);

	auto* Inv = Expr<UMaterialExpressionOneMinus>(M, -300, 40);
	Inv->Input.Connect(0, Scaled);

	auto* Mask = Expr<UMaterialExpressionSaturate>(M, -180, 40);
	Mask->Input.Connect(0, Inv);

	auto* Opacity = Expr<UMaterialExpressionScalarParameter>(M, -900, 320);
	Opacity->ParameterName = TEXT("Opacity");
	Opacity->DefaultValue = 1.f;

	auto* FinalA = Expr<UMaterialExpressionMultiply>(M, -40, 100);
	FinalA->A.Connect(0, Mask);
	FinalA->B.Connect(0, Opacity);

	auto* Colour = Expr<UMaterialExpressionVectorParameter>(M, -900, 400);
	Colour->ParameterName = TEXT("PrecipColor");
	Colour->DefaultValue = FLinearColor(0.85f, 0.90f, 1.f);

	auto* Emissive = Expr<UMaterialExpressionMultiply>(M, -40, -40);
	Emissive->A.Connect(0, Colour);
	Emissive->B.Connect(0, Mask);

	UMaterialEditingLibrary::ConnectMaterialProperty(Emissive, TEXT(""), MP_EmissiveColor);
	UMaterialEditingLibrary::ConnectMaterialProperty(FinalA, TEXT(""), MP_Opacity);

	auto* Ver = Expr<UMaterialExpressionScalarParameter>(M, -900, 480);
	Ver->ParameterName = kVersionParam;
	Ver->DefaultValue = kParticleVersion;

	UMaterialEditingLibrary::RecompileMaterial(M);

	FAssetRegistryModule::AssetCreated(M);
	Pkg->MarkPackageDirty();

	FSavePackageArgs Args;
	Args.TopLevelFlags = RF_Public | RF_Standalone;
	const FString Filename = FPackageName::LongPackageNameToFilename(
		kParticlePkg, FPackageName::GetAssetPackageExtension());
	if (UPackage::SavePackage(Pkg, M, *Filename, Args))
		Pkg->SetDirtyFlag(false);
	else
		UE_LOG(LogRoseImport, Error, TEXT("failed to save %s"), kParticlePkg);

	UE_LOG(LogRoseImport, Log, TEXT("built %s (instanced particle sprite)"), kParticleName);
	return M;
}
