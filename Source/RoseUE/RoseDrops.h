// RoseDrops — faithful monster drop roll, transcribed from CCal::Get_DropITEM
// (src/common/calculation.cpp:117) against the real ITEM_DROP.STB (exported to
// Content/DataTables/item_drops.json by tools/gen_drop_table.py).
//
// One roll per kill yields EITHER money OR one item (or nothing), exactly like
// the server: gated by iDrop_VAR, then a money-chance branch, then a table
// lookup with the 1..4 sub-table redirect.
#pragma once

#include "CoreMinimal.h"

// Result of a drop roll.  bIsMoney → Money is the zuly amount; otherwise
// (ItemType, ItemNo) is the ROSE item (type*1000+itemno decoded) with Quantity.
struct FRoseDropResult
{
	bool  bIsMoney = false;
	int32 Money = 0;
	int32 ItemType = 0;    // ROSE ITEM_TYPE_* (1 face .. 8 weapon .. 10 use ..)
	int32 ItemNo = 0;      // row id within that type's list
	int32 Quantity = 1;
	int32 Bonus = 0;       // rolled GEM_OP option magnitude (armor/accessory; 0 = none)
	bool  bAppraised = true;
};

// Roll a drop for a killed monster.  Returns false = nothing dropped.
//   MobLevel/PlayerLevel — for the level-difference gate + amounts
//   DropType   — NPC_DROP_TYPE (ITEM_DROP.STB row)
//   DropItem   — NPC_DROP_ITEM (drop rate/quality)
//   DropMoney  — NPC_DROP_MONEY (money-drop chance 1-100)
//   Charm      — player Charm (affects item option roll only; harmless if 0)
bool RoseRollDrop(int32 MobLevel, int32 PlayerLevel, int32 DropType, int32 DropItem,
	int32 DropMoney, int32 Charm, FRoseDropResult& Out);

// ROSE item type → our inventory slot key ("cap","body","arms","foot","back",
// "weapon","faceitem","jewel","subwpn","consumable","material","quest","misc").
FString RoseItemTypeToSlot(int32 ItemType);

// Bag category tab a slot belongs to: 0 = Equip (armor/weapon/accessory —
// unique, never stacks), 1 = Consumable, 2 = Etc (materials/gems/quest/misc).
int32 RoseBagCategory(const FString& Slot);

// True for slots that stack in the bag (consumables / materials / gems / quest);
// equipment is always a unique instance.
bool RoseIsStackableSlot(const FString& Slot);

// True for equippable slots (armor / weapon / accessory / faceitem / subwpn).
bool RoseIsEquipSlot(const FString& Slot);
