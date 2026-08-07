#include "RoseObjectBuilder.h"

#include "RoseDds.h"
#include "RoseEditor.h"
#include "RoseObjectFormats.h"
#include "RosePathResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Engine/Texture2D.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Materials/MaterialInterface.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "TextureCompiler.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	// M_RoseMaster: BaseColor (texture), UVTransform (vector), Tint (vector),
	// texture alpha wired to both OpacityMask and Opacity.  Blend mode and
	// two-sidedness come from per-instance BasePropertyOverrides, exactly as
	// tools/ue5_refit_map_mats.py does it.
	const TCHAR* kObjectMaster = TEXT("/Game/Atlas/M_RoseMaster.M_RoseMaster");

	FString SanitiseName(const FString& In)
	{
		FString Out;
		Out.Reserve(In.Len());
		for (TCHAR C : In)
		{
			if (FChar::IsAlnum(C) || C == TEXT('_'))
				Out.AppendChar(C);
			else
				Out.AppendChar(TEXT('_'));
		}
		return Out;
	}

	// Get a package ready to be written into.
	//
	// FullyLoad MUST happen here — right after CreatePackage and BEFORE anything
	// in the package is created or touched.  Two reasons, and they pull the same
	// way:
	//
	//  * SavePackage refuses a package that is not fully loaded, because saving
	//    would drop the exports that were never read in.  A package already on
	//    disk is only lazily loaded, which is why an import runs clean headlessly
	//    (every package new) and then fails on EVERY asset in the editor.
	//
	//  * Doing it LATE is worse than not doing it.  FullyLoad just before saving
	//    pulls the on-disk exports in on top of the object we already built, so
	//    the texture's bulkdata straddles two versions of itself and dies with
	//    "Attempting to save bulkdata <guid> with an invalid payload" — fatal,
	//    at the very end of a long run.
	UPackage* MakeWritablePackage(const FString& PkgName)
	{
		UPackage* Pkg = CreatePackage(*PkgName);
		if (Pkg)
			Pkg->FullyLoad();
		return Pkg;
	}

	// Re-use the object already living under this name instead of NewObject-ing
	// over the top of it.
	//
	// NewObject with a name that is already taken renames the incumbent out of the
	// way and leaves it in the package, still RF_Standalone — so the package ends
	// up holding a second, stale copy whose bulkdata points at file offsets we are
	// about to overwrite.  That is one way to get an unsaveable package, and it
	// happens the moment two importer runs touch a shared texture (every "Import
	// All" does).  Re-initialising the existing object keeps one object per name.
	template <typename T>
	T* FindOrCreateAsset(UPackage* Pkg, const FString& AssetName)
	{
		if (T* Existing = FindObject<T>(Pkg, *AssetName))
			return Existing;
		return NewObject<T>(Pkg, *AssetName, RF_Public | RF_Standalone);
	}
}

void FRoseAssetCache::AddReferencedObjects(FReferenceCollector& Collector)
{
	// All TObjectPtr — the raw UObject* overloads are unsafe under incremental GC.
	Collector.AddReferencedObjects(Created);
	Collector.AddReferencedObject(Master);
	Collector.AddReferencedObjects(MeshCache);
	Collector.AddReferencedObjects(TextureCache);
	Collector.AddReferencedObjects(MaterialCache);
}

int32 FRoseAssetCache::SaveCreated()
{
	// Textures built this run may still be compiling.  Saving a texture whose
	// async build is in flight is what the engine's own PreSave guards against;
	// flush first so every payload is settled before it hits a linker.
	FTextureCompilingManager::Get().FinishAllCompilation();

	int32 Failed = 0;
	// Save each PACKAGE at most once.  Re-saving a package we just wrote is not
	// merely wasteful — it is how packages come back "Serial size mismatch: Got
	// N, Expected M" and assert in FLinkerLoad on the next open.
	TSet<UPackage*> Saved;

	for (UObject* Asset : Created)
	{
		if (!Asset)
			continue;
		UPackage* Pkg = Asset->GetOutermost();
		if (!Pkg)
			continue;
		bool bAlready = false;
		Saved.Add(Pkg, &bAlready);
		if (bAlready)
			continue;

		const FString Filename = FPackageName::LongPackageNameToFilename(
			Pkg->GetName(), FPackageName::GetAssetPackageExtension());

		FSavePackageArgs Args;
		Args.TopLevelFlags = RF_Public | RF_Standalone;

		if (UPackage::SavePackage(Pkg, Asset, *Filename, Args))
		{
			// SavePackage clears this itself on the normal path, but be explicit:
			// a package left dirty is one the editor's autosave will later try to
			// re-serialise, and that is where the "invalid payload" ensure fires.
			Pkg->SetDirtyFlag(false);
		}
		else
		{
			++Failed;
			UE_LOG(LogRoseImport, Error, TEXT("failed to save %s"), *Pkg->GetName());
		}
	}

	// Everything is on disk now, so drop the save list and collect.
	//
	// This does NOT unload the assets — they are RF_Standalone, and
	// GARBAGE_COLLECTION_KEEPFLAGS is exactly RF_Standalone in the editor, so
	// they stay resident for the session.  That is what an in-editor import
	// means and is not something to fight.  What this DOES reclaim is the
	// transient mountain a run leaves behind it: mesh descriptions, the
	// FSkeletalMeshImportData copies, decoded DDS buffers and every intermediate
	// object the builders handed to the collector.
	//
	// The content caches are deliberately NOT cleared: each importer reads
	// NumTextures()/NumMaterials() off them AFTER this returns, and they only
	// hold pointers to assets that survive GC anyway, so emptying them would
	// zero the run's report while freeing nothing worth having.
	Created.Empty();
	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);

	return Failed;
}

FRoseAssetCache::FRoseAssetCache(const FString& InPackageRoot, FRosePathResolver& InResolver,
	const TCHAR* InMasterPath, bool bInForceTwoSided, bool bInAlphaIsSpecular)
	: PackageRoot(InPackageRoot)
	, Resolver(InResolver)
	, bForceTwoSided(bInForceTwoSided)
	, bAlphaIsSpecular(bInAlphaIsSpecular)
{
	// Characters and world objects do NOT share a master.  Avatar parts parent to
	// M_RoseChar — the master the working character pipeline used
	// (tools/ue5_import_modular.py) — while world geometry uses M_RoseMaster.
	const TCHAR* Path = InMasterPath ? InMasterPath : kObjectMaster;

	Master = LoadObject<UMaterialInterface>(nullptr, Path);
	if (!Master)
	{
		UE_LOG(LogRoseImport, Error, TEXT("master material missing: %s"), Path);
	}
	else
	{
		UE_LOG(LogRoseImport, Log, TEXT("material master: %s"), Path);
	}
}

FString FRoseAssetCache::AssetNameFor(const FString& Prefix, const FString& RosePath) const
{
	// Base names collide across folders (every zone has a "TREE01.ZMS"), so the
	// name carries the last folder too.
	FString Norm = RosePath;
	Norm.ReplaceInline(TEXT("\\"), TEXT("/"));
	TArray<FString> Parts;
	Norm.ParseIntoArray(Parts, TEXT("/"), true);

	FString Stem = Parts.Num() ? FPaths::GetBaseFilename(Parts.Last()) : TEXT("unnamed");
	FString Folder = Parts.Num() >= 2 ? Parts[Parts.Num() - 2] : TEXT("");

	return SanitiseName(FString::Printf(TEXT("%s_%s_%s"), *Prefix, *Folder, *Stem));
}

FString FRoseAssetCache::UniqueAssetName(const FString& Prefix, const FString& RosePath,
	const FString& IdentityKey, const FString& Suffix)
{
	if (const FString* Already = AssignedNames.Find(IdentityKey))
		return *Already;

	FString Base = AssetNameFor(Prefix, RosePath);
	if (!Suffix.IsEmpty())
		Base += TEXT("_") + Suffix;

	FString Name = Base;
	int32& Uses = NameUses.FindOrAdd(Base.ToLower());
	if (Uses > 0)
		Name = FString::Printf(TEXT("%s_%d"), *Base, Uses + 1);
	++Uses;

	AssignedNames.Add(IdentityKey, Name);
	return Name;
}

UStaticMesh* FRoseAssetCache::GetMesh(const FString& ZmsPath, bool bNeedsCollision)
{
	const FString Key = ZmsPath.ToLower();
	if (TObjectPtr<UStaticMesh>* Found = MeshCache.Find(Key))
	{
		// Upgrade a cached no-collision mesh the first time something that DOES
		// collide asks for it.  Without this, whichever placement happened to be
		// visited first would decide collision for every other user of the mesh.
		UStaticMesh* Cached = *Found;
		if (bNeedsCollision && Cached && !Cached->GetBodySetup())
		{
			Cached->CreateBodySetup();
			Cached->GetBodySetup()->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
			Cached->PostEditChange();
		}
		return Cached;
	}

	const FString FullPath = Resolver.Resolve(ZmsPath);
	if (FullPath.IsEmpty())
	{
		if (!Missing.Contains(Key))
		{
			Missing.Add(Key);
			UE_LOG(LogRoseImport, Warning, TEXT("ZMS not found: %s"), *ZmsPath);
		}
		MeshCache.Add(Key, nullptr);
		return nullptr;
	}

	FRoseZMS Zms;
	if (!Zms.Load(FullPath))
	{
		Missing.Add(Key);
		MeshCache.Add(Key, nullptr);
		return nullptr;
	}

	FMeshDescription MeshDesc;
	FStaticMeshAttributes Attributes(MeshDesc);
	Attributes.Register();

	TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
	TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
	TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
	const int32 UVChannels = Zms.HasUV1() ? 2 : 1;
	UVs.SetNumChannels(UVChannels);

	const FPolygonGroupID PolyGroup = MeshDesc.CreatePolygonGroup();
	Attributes.GetPolygonGroupMaterialSlotNames()[PolyGroup] = FName(TEXT("Mat"));

	// ZMS VERTEX UNITS DEPEND ON THE VERSION.
	//   v7/v8 store METRES  -> x100 to reach ROSE world centimetres
	//   v5/v6 store world units already -> x1
	// (mapforge/export_map.py::zms_geometry, `scale = 100.0 if version >= 7`.)
	// Skipping this leaves every v7/v8 building 1/100 of its size: the town
	// renders as bare floors with a few specks on it, which reads as "objects
	// missing" rather than "objects tiny".
	const float VertexScale = (Zms.Version >= 7) ? 100.f : 1.f;

	const int32 NumVerts = Zms.Positions.Num();
	MeshDesc.ReserveNewVertices(NumVerts);
	MeshDesc.ReserveNewVertexInstances(NumVerts);
	MeshDesc.ReserveNewTriangles(Zms.Faces.Num());

	TArray<FVertexInstanceID> Instances;
	Instances.SetNumUninitialized(NumVerts);

	for (int32 i = 0; i < NumVerts; ++i)
	{
		// Y is mirrored HERE, once per unique mesh, so every instance transform
		// stays a plain rotation + POSITIVE scale.  (ROSE world Y grows north,
		// UE's grows south — see RoseMapImporter.)
		const FVector3f P = Zms.Positions[i] * VertexScale;
		const FVertexID Vertex = MeshDesc.CreateVertex();
		Positions[Vertex] = FVector3f(P.X, -P.Y, P.Z);

		const FVertexInstanceID Instance = MeshDesc.CreateVertexInstance(Vertex);
		Instances[i] = Instance;

		if (Zms.Normals.IsValidIndex(i))
		{
			const FVector3f N = Zms.Normals[i];
			Normals[Instance] = FVector3f(N.X, -N.Y, N.Z);
		}
		// ROSE and UE both use a top-left UV origin — do NOT flip V.
		UVs.Set(Instance, 0, Zms.UV0.IsValidIndex(i) ? Zms.UV0[i] : FVector2f::ZeroVector);
		if (UVChannels > 1)
			UVs.Set(Instance, 1, Zms.UV1.IsValidIndex(i) ? Zms.UV1[i] : FVector2f::ZeroVector);
	}

	for (const FIntVector& Face : Zms.Faces)
	{
		if (!Instances.IsValidIndex(Face.X) || !Instances.IsValidIndex(Face.Y) || !Instances.IsValidIndex(Face.Z))
			continue;
		// Degenerate faces (two indices equal) ship in real ZMS data and make
		// FMeshDescription::CreateTriangle ASSERT (`!bExists` in
		// MeshElementIndexer) — bRemoveDegenerates runs far too late to help.
		if (Face.X == Face.Y || Face.Y == Face.Z || Face.X == Face.Z)
			continue;
		// ZMS winding is kept AS-IS.  Mirroring Y reverses triangle
		// orientation, but UE computes the face normal with the REVERSE cross
		// product (Cross(P2-P0, P1-P0) — see StaticMeshOperations.cpp), which
		// reverses it again.  The two cancel; flipping here as well turns every
		// object inside-out.
		MeshDesc.CreateTriangle(PolyGroup,
			{ Instances[Face.X], Instances[Face.Y], Instances[Face.Z] });
	}

	const FString AssetName = UniqueAssetName(TEXT("SM"), ZmsPath, Key);
	const FString PkgName = FString::Printf(TEXT("%s/Meshes/%s"), *PackageRoot, *AssetName);
	UPackage* Pkg = MakeWritablePackage(PkgName);

	UStaticMesh* Mesh = FindOrCreateAsset<UStaticMesh>(Pkg, AssetName);
	Mesh->InitResources();
	Mesh->SetLightingGuid();

	// RESET before filling — this mesh may be one we are re-importing over.
	//
	// AddSourceModel APPENDS.  On a re-import the asset already carries the
	// previous run's LOD, so each pass adds another, and the 9th trips
	// `check(LODIndex < MAX_STATIC_MESH_LODS)` in FStaticMeshLODGroup::GetSettings
	// during Build() — a hard assert, several imports after the mistake.  The
	// material array accumulates the same way.
	Mesh->SetNumSourceModels(0);
	Mesh->GetStaticMaterials().Empty();

	// One slot; the material is a per-instance component override, so a mesh
	// used with several textures is still built once.
	Mesh->GetStaticMaterials().Add(FStaticMaterial(nullptr, TEXT("Mat"), TEXT("Mat")));

	FStaticMeshSourceModel& SrcModel = Mesh->AddSourceModel();
	SrcModel.BuildSettings.bRecomputeNormals = !Zms.HasNormals();
	SrcModel.BuildSettings.bRecomputeTangents = true;
	SrcModel.BuildSettings.bUseMikkTSpace = true;
	SrcModel.BuildSettings.bGenerateLightmapUVs = false;
	SrcModel.BuildSettings.bRemoveDegenerates = true;

	FMeshDescription* Target = Mesh->CreateMeshDescription(0);
	*Target = MoveTemp(MeshDesc);
	Mesh->CommitMeshDescription(0);

	Mesh->GetNaniteSettings().bEnabled = false;
	Mesh->Build(false);
	// No body setup at all when nothing can ever hit this — see GetMesh's
	// contract.  Most placed parts are ZSC shape-NONE foliage.
	if (bNeedsCollision)
	{
		Mesh->CreateBodySetup();
		Mesh->GetBodySetup()->CollisionTraceFlag = ECollisionTraceFlag::CTF_UseComplexAsSimple;
	}
	Mesh->PostEditChange();

	FAssetRegistryModule::AssetCreated(Mesh);
	Pkg->MarkPackageDirty();
	Created.Add(Mesh);

	MeshCache.Add(Key, Mesh);
	return Mesh;
}

UTexture2D* FRoseAssetCache::GetTexture(const FString& TexturePath, bool bNeedsAlpha)
{
	const FString Key = TexturePath.ToLower();
	if (TObjectPtr<UTexture2D>* Found = TextureCache.Find(Key))
		return *Found;

	const FString FullPath = Resolver.Resolve(TexturePath);
	FRoseImage Img;
	if (FullPath.IsEmpty() || !RoseLoadDDS(FullPath, Img) || !Img.IsValid())
	{
		if (!Missing.Contains(Key))
		{
			Missing.Add(Key);
			UE_LOG(LogRoseImport, Warning, TEXT("texture not found: %s"), *TexturePath);
		}
		TextureCache.Add(Key, nullptr);
		return nullptr;
	}

	const FString AssetName = UniqueAssetName(TEXT("T"), TexturePath, Key);
	const FString PkgName = FString::Printf(TEXT("%s/Textures/%s"), *PackageRoot, *AssetName);
	UPackage* Pkg = MakeWritablePackage(PkgName);

	UTexture2D* Tex = FindOrCreateAsset<UTexture2D>(Pkg, AssetName);

	// FTextureSource::Init clones exactly SizeX*SizeY*BytesPerPixel out of this
	// pointer, so a short buffer is a heap overread that silently bakes garbage
	// into the asset.  RoseLoadDDS should never return one; refuse if it does.
	const int64 Expected = (int64)Img.Width * Img.Height * 4;
	if (Img.Pixels.Num() < Expected)
	{
		UE_LOG(LogRoseImport, Error,
			TEXT("texture %s: decoded %d bytes, need %lld for %dx%d BGRA8"),
			*TexturePath, Img.Pixels.Num(), Expected, Img.Width, Img.Height);
		TextureCache.Add(Key, nullptr);
		return nullptr;
	}

	Tex->Source.Init(Img.Width, Img.Height, 1, 1, TSF_BGRA8, Img.Pixels.GetData());
	Tex->SRGB = true;
	// Masked/translucent materials need the alpha kept; TC_Default drops to DXT1
	// (no alpha) when the source looks opaque, which silently breaks cutouts.
	Tex->CompressionSettings = TC_Default;
	Tex->CompressionNoAlpha = !bNeedsAlpha;
	Tex->MipGenSettings = TMGS_FromTextureGroup;
	Tex->LODGroup = TEXTUREGROUP_World;
	// PostEditChange calls UpdateResource itself; calling both kicks off two
	// async builds for every texture.
	Tex->PostEditChange();

	FAssetRegistryModule::AssetCreated(Tex);
	Pkg->MarkPackageDirty();
	Created.Add(Tex);

	TextureCache.Add(Key, Tex);
	return Tex;
}

UMaterialInstanceConstant* FRoseAssetCache::GetMaterial(const FRoseZscMaterial& Mat)
{
	if (!Master)
		return nullptr;

	// The render state is part of the identity: the same texture drawn opaque
	// and drawn masked are two different materials.
	const FString Key = FString::Printf(TEXT("%s|a%d|t%d|r%d|2s%d|b%d|av%.3f"),
		*Mat.TexturePath.ToLower(), Mat.bAlpha ? 1 : 0, Mat.AlphaTest, Mat.AlphaRef,
		Mat.bTwoSided ? 1 : 0, Mat.BlendType, Mat.AlphaValue)
		// ZWrite is part of the IDENTITY because it selects the blend mode below.
		// Leave it out and two materials that differ only in zwrite collapse onto
		// one asset, and whichever was built first decides for both.
		+ FString::Printf(TEXT("|zw%d"), Mat.ZWrite);

	if (TObjectPtr<UMaterialInstanceConstant>* Found = MaterialCache.Find(Key))
		return *Found;

	const bool bAdditive = Mat.BlendType == 3;   // Lighten

	// ZWRITE decides translucent vs masked — not alpha_test alone.
	//
	// ROSE alpha-blends WITH DEPTH WRITE: zz_material::apply_shared_property does
	// enable_alpha_blend(...) and then enable_zwrite(s_state.zwrite), and zwrite
	// is 1 on essentially every character material (checked: all 153 alpha-blended
	// LIST_WBODY materials and all 168 in LIST_WCAP are zwrite=1, alpha_value=1).
	// So in ROSE they occlude normally and the alpha only softens edges.
	//
	// UE's BLEND_Translucent does NOT write depth.  Importing those as translucent
	// therefore lets you see straight through the character into its own interior
	// — the "see-through body" artifact.  A depth-writing alpha-blend has no exact
	// UE equivalent; MASKED is the faithful one, because it writes depth, occludes,
	// and still cuts on the texture's alpha.
	//
	// Genuinely see-through surfaces (zwrite=0) stay translucent, which is what
	// that flag is actually for.
	// ALPHA_TEST decides masked — NOT the alpha flag, and NOT zwrite.
	//
	// These are the five flags tools/build_armor_glb.py scrapes out of the ZSC
	// (alpha, alpha_test, alpha_ref, two_sided, blend_type) and applies to the
	// equipment, which is the pipeline that renders characters correctly.
	// zwrite is deliberately absent: it says whether the surface writes depth,
	// not how it blends.
	//
	// Requiring bAlpha here was a real bug.  Measured in the Arua ZSCs:
	//     hair2_00300  alpha=0 alpha_test=1 alpha_ref=128
	//     face2_01000  alpha=0 alpha_test=1 alpha_ref=128
	// ROSE alpha-TESTS those, but with the bAlpha gate they came out OPAQUE, so
	// the cutout that carves hair into strands was ignored and the part rendered
	// as a solid blob — the "giant spiky hair".
	//
	// And routing alpha-blended materials to masked because zwrite was on (it is
	// on for essentially every character material) turned every soft blend into
	// a hard cutout.  A depth-writing alpha blend has no exact UE equivalent;
	// translucency is the honest one, and the see-through-body artifact that
	// motivated the zwrite rule belongs to sort order, not to the blend mode.
	// EXACTLY tools/ue5_fix_item_materials_zsc.py::apply_flags — the mapping the
	// working pipeline uses.  Do not "improve" it:
	//
	//     blend_type == 3        -> ADDITIVE
	//     alpha && alpha_test    -> MASKED at alpha_ref/255
	//     alpha                  -> MASKED at 0.33
	//     else                   -> OPAQUE
	//
	// Two things about it are counter-intuitive and both are deliberate:
	//
	//  * Masking needs alpha AND alpha_test.  When the alpha FLAG is off the
	//    DDS alpha channel carries SPECULAR data, not transparency, so cutting
	//    on it punches holes using specular values.  hair2_00300 and
	//    face2_01000 are alpha=0 alpha_test=1 and are therefore OPAQUE.
	//
	//  * NOTHING is translucent.  Substrate renders BLEND_Translucent
	//    black/dithered here, and it does not write depth, so alpha-only
	//    surfaces are masked at a low clip instead — that keeps the cutout
	//    shape, sorts correctly, and is what stops the see-through body parts.
	//
	// zwrite is NOT part of this decision.
	// bAlphaIsSpecular (NPCs) forces OPAQUE, because the channel a mask would cut
	// on is not coverage — see the constructor comment in RoseObjectBuilder.h.
	// Additive still wins: blend_type 3 is a real blend, not an alpha decision.
	const bool bMasked = !bAdditive && Mat.bAlpha && !bAlphaIsSpecular;
	const bool bMaskedByTest = bMasked && Mat.AlphaTest != 0;
	const bool bTranslucent = false;

	UTexture2D* Tex = GetTexture(Mat.TexturePath, Mat.bAlpha);

	// The suffix names the blend mode, plus two-sidedness because it is the
	// commonest reason two materials share one texture.  Anything the suffix
	// still cannot express (alpha ref, alpha value, other blend types) is caught
	// by UniqueAssetName, which keys on the full render-state Key.
	FString Suffix = bAdditive ? TEXT("add") : bTranslucent ? TEXT("tr")
		: bMasked ? TEXT("msk") : TEXT("op");
	if (Mat.bTwoSided)
		Suffix += TEXT("_2s");

	const FString AssetName = UniqueAssetName(TEXT("MI"), Mat.TexturePath, Key, Suffix);
	const FString PkgName = FString::Printf(TEXT("%s/Materials/%s"), *PackageRoot, *AssetName);

	UPackage* Pkg = MakeWritablePackage(PkgName);
	UMaterialInstanceConstant* MIC = FindOrCreateAsset<UMaterialInstanceConstant>(Pkg, AssetName);
	MIC->SetParentEditorOnly(Master);

	if (Tex)
		MIC->SetTextureParameterValueEditorOnly(FMaterialParameterInfo(TEXT("BaseColor")), Tex);
	// UVTransform identity: object UVs are already the mesh's own, not an atlas.
	MIC->SetVectorParameterValueEditorOnly(
		FMaterialParameterInfo(TEXT("UVTransform")), FLinearColor(1, 1, 0, 0));

	// zz_material.cpp::apply, 1:1.
	FMaterialInstanceBasePropertyOverrides& Ovr = MIC->BasePropertyOverrides;
	Ovr.bOverride_BlendMode = true;
	Ovr.BlendMode = bAdditive ? BLEND_Additive
		: bTranslucent ? BLEND_Translucent
		: bMasked ? BLEND_Masked
		: BLEND_Opaque;

	// Two-sided: the ZSC flag, OR forced on for characters.
	//
	// WORLD GEOMETRY takes the ZSC flag exactly.  Forcing everything two-sided
	// there was the "one side of every structure is black" bug: backfaces ROSE
	// culls got drawn with a shading normal pointing away from the light.
	//
	// CHARACTERS are the opposite case.  M_RoseChar is authored TWO-SIDED masked
	// (tools/ue5_import_modular.py), and most avatar parts carry 2side=0 in the
	// ZSC — so taking the flag literally overrides the master back to one-sided
	// and culls the backfaces, which reads as SEE-THROUGH body parts.  ROSE's
	// avatar meshes are open shells, not closed solids, so they need both faces.
	Ovr.bOverride_TwoSided = true;
	Ovr.TwoSided = Mat.bTwoSided || bForceTwoSided;

	if (bMasked)
	{
		Ovr.bOverride_OpacityMaskClipValue = true;
		// alpha_test on  -> alpha_ref IS ROSE's threshold (128 -> 0.502)
		// alpha_test off -> 0.33, the map-decal policy from apply_flags: keep the
		//                   cutout shape without eating soft-edged texels.
		Ovr.OpacityMaskClipValue = bMaskedByTest
			? FMath::Clamp(Mat.AlphaRef / 255.f, 0.01f, 0.99f)
			: 0.33f;
	}

	MIC->PostEditChange();
	FAssetRegistryModule::AssetCreated(MIC);
	Pkg->MarkPackageDirty();
	Created.Add(MIC);

	MaterialCache.Add(Key, MIC);
	return MIC;
}
