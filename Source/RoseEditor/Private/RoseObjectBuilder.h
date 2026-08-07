// ZSC object instancing: ROSE meshes/materials/textures -> UE assets, with the
// content-keyed caches that make a zone import cheap.
//
// A zone places thousands of objects drawn from a few hundred unique meshes and
// materials.  Building each instance from scratch is what made the old pipeline
// emit ~2,900 static meshes for JPT01; keying on content collapses that to the
// unique set, which is the single biggest win.
#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"

struct FRoseZSC;
struct FRoseZscMaterial;
class FRosePathResolver;
class UStaticMesh;
class UTexture2D;
class UMaterialInterface;
class UMaterialInstanceConstant;

// FGCObject, not a bare class: a zone or equipment run creates tens of thousands
// of UObjects and holds them in these caches for the whole run.  Raw UObject*
// with nothing rooting them is a use-after-collect waiting to happen the first
// time anything triggers a GC mid-import.
class FRoseAssetCache : public FGCObject
{
public:
	// InMasterPath = null uses the OBJECT master (M_RoseMaster).  The character
	// importer passes M_RoseChar: characters and world geometry do NOT share a
	// master.
	// InMasterPath = null uses the OBJECT master (M_RoseMaster).
	// bInForceTwoSided = true makes every material two-sided regardless of the
	// ZSC flag — required for characters, whose meshes are open shells and whose
	// master (M_RoseChar) is authored two-sided.
	// bInAlphaIsSpecular = true is the NPC policy: PART_NPC.ZSC materials claim
	// alpha/alpha_test, but an NPC's DDS alpha channel carries SPECULAR/glow
	// data, NOT coverage.  Cutting on it punches holes through solid body pixels.
	// tools/build_monsters.py — the pipeline these were imported with before —
	// hardcodes "alpha": False for exactly this reason, and
	// ue5_fix_monster_materials_zsc.py documents it as "alpha_test ignored".
	// Avatars are the opposite: when their alpha flag is on it IS coverage, so
	// this stays off for the equipment cache.
	FRoseAssetCache(const FString& InPackageRoot, FRosePathResolver& InResolver,
		const TCHAR* InMasterPath = nullptr, bool bInForceTwoSided = false,
		bool bInAlphaIsSpecular = false);

	//~ FGCObject
	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;
	virtual FString GetReferencerName() const override { return TEXT("FRoseAssetCache"); }

	// One UStaticMesh per unique ZMS path.  Geometry is mirrored in Y at build
	// time (see RoseMapImporter) so instance transforms need no negative scale.
	//
	// bNeedsCollision=false skips the body setup entirely.  Most placed parts are
	// foliage the ZSC marks shape-NONE, and cooking complex collision for meshes
	// nothing can ever hit is pure cost in memory, build time and package size.
	// The same ZMS can be placed both ways, so a mesh built without collision is
	// UPGRADED in place the first time a colliding placement asks for it — the
	// map importer saves the cache at the end of the run, so the upgrade always
	// lands before the asset is written.
	UStaticMesh* GetMesh(const FString& ZmsPath, bool bNeedsCollision = true);

	// One UTexture2D per unique DDS path.
	UTexture2D* GetTexture(const FString& TexturePath, bool bNeedsAlpha);

	// One material instance per unique (texture, render-state) pair.  ROSE
	// render state is 1:1 from the ZSC (zz_material.cpp::apply):
	//   alpha=0                -> OPAQUE   (alpha_test is inert without blending)
	//   alpha=1, alpha_test=1  -> MASKED   clip = alpha_ref/255
	//   alpha=1, alpha_test=0  -> TRANSLUCENT
	//   blend_type=3 (Lighten) -> ADDITIVE
	//   two_sided ONLY where the ZSC flag says so
	UMaterialInstanceConstant* GetMaterial(const FRoseZscMaterial& Mat);

	int32 NumMeshes() const { return MeshCache.Num(); }
	int32 NumTextures() const { return TextureCache.Num(); }
	int32 NumMaterials() const { return MaterialCache.Num(); }
	int32 NumMissing() const { return Missing.Num(); }

	// Every asset created this run, for one batched save at the end.
	//
	// TObjectPtr, not UObject*: the raw-pointer AddReferencedObject(s) overloads
	// are unsafe under incremental GC (the engine's own deprecation says a
	// program using them "will randomly crash"), and the whole point of this
	// cache being an FGCObject is to survive a GC mid-import.
	TArray<TObjectPtr<UObject>> Created;

	// Save everything in Created and leave the packages CLEAN.
	//
	// Leaving them dirty is not cosmetic: the editor's autosave then re-serialises
	// texture source bulkdata that has already been handed to the saved package,
	// which fails with "Attempting to save bulkdata <guid> with an invalid payload"
	// (FEditorBulkData::TryPayloadValidationForSaving) and pops a save-failed
	// dialog at the user long after the import finished.
	//
	// Returns the number of packages that failed to save.
	int32 SaveCreated();

private:
	FString PackageRoot;
	FRosePathResolver& Resolver;
	TObjectPtr<UMaterialInterface> Master = nullptr;
	bool bForceTwoSided = false;
	bool bAlphaIsSpecular = false;

	TMap<FString, TObjectPtr<UStaticMesh>> MeshCache;
	TMap<FString, TObjectPtr<UTexture2D>> TextureCache;
	TMap<FString, TObjectPtr<UMaterialInstanceConstant>> MaterialCache;
	TSet<FString> Missing;

	// Package-safe name derived from the ROSE path.  NOT unique on its own:
	// different ROSE paths collapse onto the same name (only the last folder and
	// the stem survive), and one texture yields several materials.
	FString AssetNameFor(const FString& Prefix, const FString& RosePath) const;

	// The name actually used for an asset: AssetNameFor + Suffix, made UNIQUE.
	//
	// IdentityKey is the cache key — whatever genuinely distinguishes this asset.
	// The same key always gets the same name back; a different key that wants a
	// taken name gets _2, _3, ...
	//
	// This is load-bearing, not tidiness.  Two assets sharing a package name
	// share a package: the second overwrites the first's settings, and both land
	// in Created, so the same file is saved twice in a row and comes back
	// "Serial size mismatch: Got N, Expected M" on load.  That is how
	// MI_CAP_cap_00300_op corrupted — a one-sided and a two-sided material with
	// the same texture both wanted the "_op" name.
	FString UniqueAssetName(const FString& Prefix, const FString& RosePath,
		const FString& IdentityKey, const FString& Suffix = FString());

	TMap<FString, FString> AssignedNames;   // identity key -> asset name
	TMap<FString, int32> NameUses;          // lowercase asset name -> times taken
};
