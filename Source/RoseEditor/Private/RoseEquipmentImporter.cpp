#include "RoseEquipmentImporter.h"

#include "RoseDds.h"
#include "RoseEditor.h"
#include "RoseObjectBuilder.h"
#include "RoseObjectFormats.h"
#include "RosePathResolver.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/StaticMesh.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceConstant.h"
#include "MeshDescription.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "PhysicsEngine/BodySetup.h"
#include "StaticMeshAttributes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"

namespace
{
	struct FEquipPack
	{
		const TCHAR* Kind;        // "weapon", "subwpn", "back", "pat"
		const TCHAR* ZscPath;     // relative to 3DDATA
		const TCHAR* GameFolder;  // /Game/Rose/Equipment/<folder>

		// Bake the ZSC per-part transform into the vertices?
		//
		// ON for weapon/subwpn, because that transform IS the grip orientation.
		// ROSE stores how a weapon sits in the hand per ITEM, on the ZSC part
		// (rose_avatar.py: weapons are the slots that opt into `use_part_xform`
		// precisely because "the ZSC part carries a real grip rotation that
		// orients the mesh in the hand").  A dagger and a bow do not share one
		// orientation, so discarding it and hand-tuning a single global
		// GripRotR — what build_weapons_static.py forced — can only ever be
		// approximately right, and has to be re-derived whenever the rig space
		// changes.  Baked here, orientation comes from the data and the runtime
		// grip is identity.
		//
		// ON for pat/field: free-standing world models whose parts must compose
		// correctly relative to each other.
		//
		// OFF for back, which genuinely has no per-part transform — its
		// placement is one shared offset (BackLoc/BackRot) for every back item.
		bool bBakePartTransform;

		// Split one ZSC object into main-hand and off-hand meshes when its
		// parts sit on different hand dummies.  WEAPONS only: a cart or a
		// field item legitimately has parts on many dummies and must stay one
		// mesh.
		bool bSplitByHand = false;
	};

	// NOTE the shipped filename LIST_FACEIEM.ZSC is misspelled; it is a skinned
	// slot and lives in the skeletal importer, listed here only as a warning to
	// whoever adds it.
	const FEquipPack kPacks[] =
	{
		{ TEXT("weapon"), TEXT("WEAPON/LIST_WEAPON.ZSC"), TEXT("Weapons"),  true,  /*split*/ true  },
		{ TEXT("subwpn"), TEXT("WEAPON/LIST_SUBWPN.ZSC"), TEXT("SubWpn"),   true  },
		{ TEXT("back"),   TEXT("AVATAR/LIST_BACK.ZSC"),   TEXT("Back"),     false },
		{ TEXT("pat"),    TEXT("PAT/LIST_PAT.ZSC"),       TEXT("Pat"),      true  },
		// Ground-drop models: what a dropped item looks like lying in the
		// world.  ARoseGroundItem currently spawns an engine cube.
		{ TEXT("field"),  TEXT("ITEM/LIST_FIELDITEM.ZSC"), TEXT("FieldItem"), true },
	};

	const TCHAR* kEquipRoot = TEXT("/Game/Rose/Equipment");

	// FullyLoad at creation, NEVER just before SavePackage.  SavePackage refuses
	// a package that is not fully loaded, but doing it late pulls the on-disk
	// exports in on top of the object already built and the bulkdata then fails
	// validation with "invalid payload" — fatally, at the end of a long run.
	UPackage* MakeWritablePackage(const FString& PkgName)
	{
		UPackage* Pkg = CreatePackage(*PkgName);
		if (Pkg)
			Pkg->FullyLoad();
		return Pkg;
	}

	// Same row-vector compose as the map importer.  Equipment part transforms
	// are relative to the item's own origin (the grip / attach point), so they
	// are baked straight into the vertices.
	FMatrix EquipCompose(const FVector3f& Pos, const FQuat4f& Q, const FVector3f& InScale)
	{
		// A handful of ZSC parts carry a zero (or near-zero) scale.  Taken
		// literally that collapses the part to a point and the item renders as
		// nothing; ROSE treats those as unit scale, so do the same.
		FVector3f Scale = InScale;
		if (FMath::Abs(Scale.X) < 1e-6f || FMath::Abs(Scale.Y) < 1e-6f || FMath::Abs(Scale.Z) < 1e-6f)
			Scale = FVector3f(1.f, 1.f, 1.f);

		// ROSE leaves many parts as the ZERO quaternion, which means "no
		// rotation" — the R** terms below already yield identity for it, but the
		// normalise must not turn it into NaN.
		float X = Q.X, Y = Q.Y, Z = Q.Z, W = Q.W;
		const float N = FMath::Sqrt(X * X + Y * Y + Z * Z + W * W);
		if (N > KINDA_SMALL_NUMBER) { X /= N; Y /= N; Z /= N; W /= N; }

		const double R00 = 1 - 2 * (Y * Y + Z * Z), R01 = 2 * (X * Y - Z * W), R02 = 2 * (X * Z + Y * W);
		const double R10 = 2 * (X * Y + Z * W), R11 = 1 - 2 * (X * X + Z * Z), R12 = 2 * (Y * Z - X * W);
		const double R20 = 2 * (X * Z - Y * W), R21 = 2 * (Y * Z + X * W), R22 = 1 - 2 * (X * X + Y * Y);

		FMatrix M = FMatrix::Identity;
		M.M[0][0] = R00 * Scale.X; M.M[0][1] = R10 * Scale.X; M.M[0][2] = R20 * Scale.X;
		M.M[1][0] = R01 * Scale.Y; M.M[1][1] = R11 * Scale.Y; M.M[1][2] = R21 * Scale.Y;
		M.M[2][0] = R02 * Scale.Z; M.M[2][1] = R12 * Scale.Z; M.M[2][2] = R22 * Scale.Z;
		M.M[3][0] = Pos.X; M.M[3][1] = Pos.Y; M.M[3][2] = Pos.Z;
		return M;
	}
}

bool RoseImportEquipment(const FRoseEquipImportOptions& Options, FRoseEquipImportResult& Result)
{
	const double TimeStart = FPlatformTime::Seconds();

	FRosePathResolver Resolver(Options.AssetRoot);
	// Textures and materials are shared across every pack: a weapon and a
	// shield frequently reference the same sheet.
	//
	// Parented to M_RoseChar, NOT the world-object master.
	//
	// Weapons, sub-weapons, back items and PATs are avatar art, not world
	// geometry: same unlit ROSE shading, and — the reason this changed — the
	// same REFINE chain.  M_RoseMaster has no RefineColor/RefineIntensity, so a
	// refined weapon could not glow the way refined armour does and had to fall
	// back to an additive M_RoseRefineGlow overlay that looks different.  One
	// master means one glow.
	FRoseAssetCache Cache(kEquipRoot, Resolver,
		TEXT("/Game/Rose/Characters/M_RoseChar.M_RoseChar"));

	int64 TotalTris = 0;
	int64 AgreeTris = 0;

	for (const FEquipPack& Pack : kPacks)
	{
		const bool bWanted =
			(FCString::Strcmp(Pack.Kind, TEXT("weapon")) == 0 && Options.bWeapons) ||
			(FCString::Strcmp(Pack.Kind, TEXT("subwpn")) == 0 && Options.bSubWeapons) ||
			(FCString::Strcmp(Pack.Kind, TEXT("back")) == 0 && Options.bBack) ||
			(FCString::Strcmp(Pack.Kind, TEXT("pat")) == 0 && Options.bPat) ||
			(FCString::Strcmp(Pack.Kind, TEXT("field")) == 0 && Options.bFieldItems);
		if (!bWanted)
			continue;

		const FString ZscFull = Resolver.Resolve(Pack.ZscPath);
		FRoseZSC Zsc;
		if (ZscFull.IsEmpty() || !Zsc.Load(ZscFull))
		{
			UE_LOG(LogRoseImport, Error, TEXT("%s: cannot read %s"), Pack.Kind, Pack.ZscPath);
			continue;
		}

		FRoseEquipPackResult PackResult;
		PackResult.Kind = Pack.Kind;
		PackResult.ObjectsInPack = Zsc.Objects.Num();

		const int32 Limit = Options.MaxItemsPerPack > 0
			? FMath::Min(Options.MaxItemsPerPack, Zsc.Objects.Num())
			: Zsc.Objects.Num();

		UE_LOG(LogRoseImport, Log, TEXT("%s: %d objects in %s"),
			Pack.Kind, Zsc.Objects.Num(), *FPaths::GetCleanFilename(ZscFull));

		for (int32 Id = 0; Id < Limit; ++Id)
		{
			const TArray<FRoseZscPart>& Parts = Zsc.Objects[Id].Parts;
			// Empty ZSC objects are normal: the STB has id gaps and unused rows.
			if (Parts.Num() == 0)
			{
				++PackResult.Empty;
				continue;
			}

			// ── DUAL WIELD: one ZSC object, TWO meshes ───────────────────────
			//
			// A katar or dual-sword object carries both blades as parts on
			// DIFFERENT hand dummies.  Baking them into a single mesh and then
			// attaching that mesh to the right hand draws both blades there —
			// the weapon looks doubled.  The runtime already asks for a second
			// asset (weapon_<id>_off) for the off hand, so emit one.
			//
			// The main hand is the LOWEST dummy index present; anything on a
			// different dummy goes to the off-hand mesh.
			int32 MainDummy = MAX_int32;
			bool bMultiDummy = false;
			for (const FRoseZscPart& P : Parts)
			{
				if (P.DummyIdx < 0)
					continue;
				if (MainDummy != MAX_int32 && P.DummyIdx != MainDummy)
					bMultiDummy = true;
				MainDummy = FMath::Min(MainDummy, P.DummyIdx);
			}
			const bool bDualSplit = Pack.bSplitByHand && bMultiDummy;

			for (int32 Variant = 0; Variant < (bDualSplit ? 2 : 1); ++Variant)
			{
			const bool bOffHand = (Variant == 1);
			const FString AssetName = FString::Printf(TEXT("SM_%s_%d%s"),
				Pack.Kind, Id, bOffHand ? TEXT("_off") : TEXT(""));
			const FString PkgName = FString::Printf(TEXT("%s/%s/%s"),
				kEquipRoot, Pack.GameFolder, *AssetName);

			if (Options.bSkipExisting && FPackageName::DoesPackageExist(PkgName))
			{
				++PackResult.Skipped;
				continue;
			}

			FMeshDescription MeshDesc;
			FStaticMeshAttributes Attributes(MeshDesc);
			Attributes.Register();

			TVertexAttributesRef<FVector3f> Positions = Attributes.GetVertexPositions();
			TVertexInstanceAttributesRef<FVector3f> Normals = Attributes.GetVertexInstanceNormals();
			TVertexInstanceAttributesRef<FVector2f> UVs = Attributes.GetVertexInstanceUVs();
			UVs.SetNumChannels(1);

			TArray<UMaterialInstanceConstant*> SectionMaterials;
			bool bAnyGeometry = false;

			for (int32 p = 0; p < Parts.Num(); ++p)
			{
				const FRoseZscPart& Part = Parts[p];
				if (!Zsc.MeshFiles.IsValidIndex(Part.MeshId))
					continue;
				// Each variant takes only its own hand's parts.
				if (bDualSplit && ((Part.DummyIdx == MainDummy) == bOffHand))
					continue;

				const FString ZmsFull = Resolver.Resolve(Zsc.MeshFiles[Part.MeshId]);
				FRoseZMS Zms;
				if (ZmsFull.IsEmpty() || !Zms.Load(ZmsFull))
					continue;

				// ZMS v7/v8 are METRES -> x100 to ROSE centimetres; v5/v6 are
				// already world units.  Same rule as the map objects.
				const float VertexScale = (Zms.Version >= 7) ? 100.f : 1.f;
				// Held slots keep raw vertices — see FEquipPack::bBakePartTransform.
				const FMatrix PartMatrix = Pack.bBakePartTransform
					? EquipCompose(Part.Position, Part.Rotation, Part.Scale)
					: FMatrix::Identity;

				const FPolygonGroupID Group = MeshDesc.CreatePolygonGroup();
				Attributes.GetPolygonGroupMaterialSlotNames()[Group] =
					FName(*FString::Printf(TEXT("Mat%d"), p));

				UMaterialInstanceConstant* PartMat = nullptr;
				if (Zsc.Materials.IsValidIndex(Part.MaterialId))
					PartMat = Cache.GetMaterial(Zsc.Materials[Part.MaterialId]);
				SectionMaterials.Add(PartMat);

				TArray<FVertexInstanceID> Instances;
				Instances.SetNumUninitialized(Zms.Positions.Num());

				for (int32 v = 0; v < Zms.Positions.Num(); ++v)
				{
					const FVector3f Raw = Zms.Positions[v] * VertexScale;
					// One UStaticMesh per item, so multi-part items compose here
					// rather than as separate components — but only for the packs
					// that want it (pat/field); held gear stays origin-authored.
					const FVector Local = PartMatrix.TransformPosition(FVector(Raw));

					const FVertexID Vertex = MeshDesc.CreateVertex();
					// No axis change and no mirror: mapforge's ROSE->glTF BASIS
					// (det -1) and Interchange's glTF->UE conversion (det -1)
					// cancel, so the existing weapon assets are simply ROSE
					// vertices x100 — and every grip transform in
					// DefaultGame.ini is hand-tuned against exactly that.
					Positions[Vertex] = FVector3f(Local);

					const FVertexInstanceID Instance = MeshDesc.CreateVertexInstance(Vertex);
					Instances[v] = Instance;

					if (Zms.Normals.IsValidIndex(v))
					{
						const FVector4 N4 = PartMatrix.TransformVector(FVector(Zms.Normals[v]));
						Normals[Instance] = FVector3f(FVector(N4).GetSafeNormal());
					}
					UVs.Set(Instance, 0,
						Zms.UV0.IsValidIndex(v) ? Zms.UV0[v] : FVector2f::ZeroVector);
				}

				for (const FIntVector& Face : Zms.Faces)
				{
					if (!Instances.IsValidIndex(Face.X) ||
						!Instances.IsValidIndex(Face.Y) ||
						!Instances.IsValidIndex(Face.Z))
						continue;
					// DEGENERATE faces (two indices equal) exist in shipped ZMS
					// data.  FMeshDescription::CreateTriangle ASSERTS on them
					// (`!bExists` in MeshElementIndexer) because the collapsed
					// edge is already registered — bRemoveDegenerates runs far
					// too late to help.
					if (Face.X == Face.Y || Face.Y == Face.Z || Face.X == Face.Z)
						continue;

					// No mirror here (unlike map objects), so the winding is
					// REVERSED relative to the ZMS to satisfy UE's reverse
					// cross-product convention.  The self-check below reports
					// whether that held.
					MeshDesc.CreateTriangle(Group,
						{ Instances[Face.X], Instances[Face.Z], Instances[Face.Y] });

					// Self-check against UE's own face-normal formula.
					const FVector3f P0 = Positions[MeshDesc.GetVertexInstanceVertex(Instances[Face.X])];
					const FVector3f P1 = Positions[MeshDesc.GetVertexInstanceVertex(Instances[Face.Z])];
					const FVector3f P2 = Positions[MeshDesc.GetVertexInstanceVertex(Instances[Face.Y])];
					const FVector3f FaceN = FVector3f::CrossProduct(P2 - P0, P1 - P0);
					if (Zms.Normals.IsValidIndex(Face.X))
					{
						++TotalTris;
						if (FVector3f::DotProduct(FaceN, Normals[Instances[Face.X]]) > 0.f)
							++AgreeTris;
					}
					bAnyGeometry = true;
				}
			}

			if (!bAnyGeometry)
			{
				++PackResult.Failed;
				continue;
			}

			UPackage* Pkg = MakeWritablePackage(PkgName);
			// Re-use, do not NewObject over a live name: that renames the
			// incumbent and leaves a stale second mesh in the package.
			UStaticMesh* Mesh = FindObject<UStaticMesh>(Pkg, *AssetName);
			const bool bNewMesh = (Mesh == nullptr);
			if (bNewMesh)
			{
				Mesh = NewObject<UStaticMesh>(Pkg, *AssetName, RF_Public | RF_Standalone);
				Mesh->InitResources();
			}
			else
			{
				// RE-IMPORT over a live asset.
				//
				// InitResources() on a mesh that already has render resources
				// double-initialises them; PreEditChange is the engine's own way
				// to tear them down first and waits on the release fence.  Without
				// it the async build worker touches resources being rebuilt under
				// it — EXCEPTION_ACCESS_VIOLATION in "Background Worker #27".
				Mesh->PreEditChange(nullptr);
			}
			Mesh->SetLightingGuid();

			// AddSourceModel and the material array both APPEND, so a re-import
			// stacks another LOD and another slot set onto the previous run's.
			// The 9th LOD trips check(LODIndex < MAX_STATIC_MESH_LODS) inside
			// Build() — an assert that fires several imports after the cause.
			Mesh->SetNumSourceModels(0);
			Mesh->GetStaticMaterials().Empty();

			// Section info is keyed by (LOD, section) and neither reset above
			// touches it.  A leftover entry describing a section the rebuilt mesh
			// no longer has is read during Build() and indexes past the end:
			//   Assertion failed: (Index >= 0) & (Index < ArrayNum)  Array.h:1339
			// which is what killed the second import run at SM_subwpn_108 — an
			// object with one ordinary part and nothing wrong with its data.
			Mesh->GetSectionInfoMap().Clear();
			Mesh->GetOriginalSectionInfoMap().Clear();

			for (int32 s = 0; s < SectionMaterials.Num(); ++s)
			{
				const FName SlotName(*FString::Printf(TEXT("Mat%d"), s));

				// NEVER put a null material in the array.
				//
				// A part can carry a sentinel material id — QQ-iROSE's
				// LIST_BACK.ZSC object 818 has 65535 against 1161 materials — and
				// the IsValidIndex guard above correctly leaves PartMat null.  The
				// null then reaches the async build worker, which dereferences it:
				// EXCEPTION_ACCESS_VIOLATION in a Background Worker thread, taking
				// the whole import down after 1170 weapons had already succeeded.
				// One bad row in the data must not cost the entire run.
				UMaterialInterface* SlotMat = SectionMaterials[s];
				if (!SlotMat)
				{
					SlotMat = UMaterial::GetDefaultMaterial(MD_Surface);
					UE_LOG(LogRoseImport, Warning,
						TEXT("%s section %d has no material (bad ZSC material id) "
						     "— using the engine default"), *AssetName, s);
				}
				Mesh->GetStaticMaterials().Add(
					FStaticMaterial(SlotMat, SlotName, SlotName));
			}

			FStaticMeshSourceModel& SrcModel = Mesh->AddSourceModel();
			SrcModel.BuildSettings.bRecomputeNormals = false;
			SrcModel.BuildSettings.bRecomputeTangents = true;
			SrcModel.BuildSettings.bUseMikkTSpace = true;
			SrcModel.BuildSettings.bGenerateLightmapUVs = false;
			SrcModel.BuildSettings.bRemoveDegenerates = true;

			FMeshDescription* Target = Mesh->CreateMeshDescription(0);
			*Target = MoveTemp(MeshDesc);
			Mesh->CommitMeshDescription(0);

			Mesh->GetNaniteSettings().bEnabled = false;
			Mesh->Build(false);
			Mesh->PostEditChange();

			FAssetRegistryModule::AssetCreated(Mesh);
			Pkg->MarkPackageDirty();

			FSavePackageArgs SaveArgs;
			SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
			if (UPackage::SavePackage(Pkg, Mesh,
				*FPackageName::LongPackageNameToFilename(
					PkgName, FPackageName::GetAssetPackageExtension()),
				SaveArgs))
			{
				Pkg->SetDirtyFlag(false);
			}
			else
			{
				UE_LOG(LogRoseImport, Error, TEXT("failed to save %s"), *PkgName);
			}

			if (Mesh->GetRenderData() && Mesh->GetRenderData()->LODResources.Num() > 0)
				PackResult.Triangles += Mesh->GetRenderData()->LODResources[0].GetNumTriangles();

			++PackResult.Built;
			}   // variant (main hand / off hand)
		}

		UE_LOG(LogRoseImport, Log,
			TEXT("%s: built %d, empty %d, skipped %d, failed %d"),
			Pack.Kind, PackResult.Built, PackResult.Empty,
			PackResult.Skipped, PackResult.Failed);

		Result.Packs.Add(PackResult);
	}

	// Save the shared textures/materials once, at the end, leaving the packages
	// clean so the editor's autosave never revisits them.
	if (const int32 SaveFailures = Cache.SaveCreated())
	{
		UE_LOG(LogRoseImport, Error,
			TEXT("%d shared asset package(s) failed to save"), SaveFailures);
	}

	Result.UniqueTextures = Cache.NumTextures();
	Result.UniqueMaterials = Cache.NumMaterials();
	Result.MissingAssets = Cache.NumMissing();
	Result.NormalAgreement = TotalTris > 0 ? (double)AgreeTris / (double)TotalTris : 0.0;
	Result.SecondsTotal = FPlatformTime::Seconds() - TimeStart;
	Result.bSuccess = Result.Packs.Num() > 0;

	UE_LOG(LogRoseImport, Log,
		TEXT("equipment: %d textures / %d materials / %d missing, winding agreement %.1f%%"),
		Result.UniqueTextures, Result.UniqueMaterials, Result.MissingAssets,
		Result.NormalAgreement * 100.0);

	return Result.bSuccess;
}
