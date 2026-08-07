// Equipment import: the rigid (non-skinned) slots.
//
//   weapon    WEAPON/LIST_WEAPON.ZSC      gender-neutral
//   subwpn    WEAPON/LIST_SUBWPN.ZSC      shields / off-hand
//   back      AVATAR/LIST_BACK.ZSC        wings / capes, gender-neutral
//   pat       PAT/LIST_PAT.ZSC            cart / castle-gear / mount parts
//   field     ITEM/LIST_FIELDITEM.ZSC     dropped-item ground models
//
// THE STB->ZSC BINDING (CLAUDE.md, the master key): every item is (slot, id),
// and the LIST_<type>.STB row id indexes LIST_<type>.ZSC.objects DIRECTLY.  So
// no STB read is needed to BUILD the meshes — the ZSC object index IS the item
// id.  The STB is only needed for names/stats, which already live in DataTables.
//
// One UStaticMesh per item, with each ZSC part baked in as its own material
// section: the runtime loads a single mesh per slot (ARoseCharacter::
// LoadWeaponStatic and friends), so parts cannot be separate actors the way
// they are for map objects.
//
// Skinned slots (body/arms/foot/cap/face/hair/faceitem) are NOT here — those
// merge onto the character skeleton and need the skeletal importer.
#pragma once

#include "CoreMinimal.h"

struct FRoseEquipImportOptions
{
	FString AssetRoot;
	bool bWeapons = true;
	bool bSubWeapons = true;
	bool bBack = true;
	bool bPat = true;
	// ITEM/LIST_FIELDITEM.ZSC — the model a dropped item shows on the ground.
	bool bFieldItems = true;
	// 0 = every item in the pack.  Small values are for a quick smoke test.
	int32 MaxItemsPerPack = 0;
	// Skip items whose asset already exists, so a run can be resumed.
	bool bSkipExisting = false;
};

struct FRoseEquipPackResult
{
	FString Kind;
	int32 ObjectsInPack = 0;
	int32 Built = 0;
	int32 Empty = 0;         // ZSC objects with no parts — normal, not an error
	int32 Failed = 0;
	int32 Skipped = 0;
	int64 Triangles = 0;
};

struct FRoseEquipImportResult
{
	bool bSuccess = false;
	TArray<FRoseEquipPackResult> Packs;
	int32 UniqueTextures = 0;
	int32 UniqueMaterials = 0;
	int32 MissingAssets = 0;
	// Self-check: fraction of triangles whose face normal (UE's reverse cross
	// product) agrees with the ZMS vertex normal.  A low number means the
	// winding is inverted and everything will render inside-out.
	double NormalAgreement = 0.0;
	double SecondsTotal = 0.0;
};

bool RoseImportEquipment(const FRoseEquipImportOptions& Options, FRoseEquipImportResult& Result);
