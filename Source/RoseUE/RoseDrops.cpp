#include "RoseDrops.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

// ── ROSE item types (datatype.h:439) ────────────────────────────────────────
namespace
{
	enum : int32
	{
		IT_FACE = 1, IT_HELMET = 2, IT_ARMOR = 3, IT_GAUNTLET = 4, IT_BOOTS = 5,
		IT_KNAPSACK = 6, IT_JEWEL = 7, IT_WEAPON = 8, IT_SUBWPN = 9, IT_USE = 10,
		IT_ETC = 11, IT_NATURAL = 12, IT_QUEST = 13, IT_RIDE = 14, IT_MONEY = 31,
	};

	// item_drop.stb rows referenced by NPCs — 50 codes each (DROPITEM_ITEMNO).
	// Loaded once, file-static, from Content/DataTables/item_drops.json.
	const TMap<int32, TArray<int32>>& DropRows()
	{
		static TMap<int32, TArray<int32>> Rows;
		static bool bLoaded = false;
		if (bLoaded)
			return Rows;
		bLoaded = true;

		const FString Path = FPaths::ProjectContentDir() / TEXT("DataTables/item_drops.json");
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *Path))
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoseDrops] missing %s"), *Path);
			return Rows;
		}
		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoseDrops] parse failed %s"), *Path);
			return Rows;
		}
		const TSharedPtr<FJsonObject>* RowsObj;
		if (Root->TryGetObjectField(TEXT("rows"), RowsObj))
		{
			for (const auto& Pair : (*RowsObj)->Values)
			{
				const TArray<TSharedPtr<FJsonValue>>* Arr;
				if (!Pair.Value.IsValid() || !Pair.Value->TryGetArray(Arr))
					continue;
				TArray<int32> Cols;
				Cols.Reserve(Arr->Num());
				for (const TSharedPtr<FJsonValue>& V : *Arr)
					Cols.Add((int32)V->AsNumber());
				Rows.Add(FCString::Atoi(*Pair.Key), MoveTemp(Cols));
			}
		}
		UE_LOG(LogTemp, Log, TEXT("[RoseDrops] loaded %d drop rows"), Rows.Num());
		return Rows;
	}

	bool IsStackable(int32 Type)
	{
		return Type == IT_USE || Type == IT_ETC || Type == IT_NATURAL
			|| Type == IT_QUEST || Type == IT_MONEY;
	}

	// RANDOM(n) in the server = rand()%n (0..n-1); guard n<=0.
	int32 R(int32 N) { return N > 0 ? FMath::RandRange(0, N - 1) : 0; }
}

FString RoseItemTypeToSlot(int32 ItemType)
{
	switch (ItemType)
	{
	case IT_FACE:     return TEXT("faceitem");   // FACE_ITEM (goggles/masks), not the appearance face
	case IT_HELMET:   return TEXT("cap");
	case IT_ARMOR:    return TEXT("body");
	case IT_GAUNTLET: return TEXT("arms");
	case IT_BOOTS:    return TEXT("foot");
	case IT_KNAPSACK: return TEXT("back");
	case IT_JEWEL:    return TEXT("jewel");
	case IT_WEAPON:   return TEXT("weapon");
	case IT_SUBWPN:   return TEXT("subwpn");
	case IT_USE:      return TEXT("consumable");
	case IT_ETC:      return TEXT("gem");        // ITEM_TYPE_ETC/GEM (11)
	case IT_NATURAL:  return TEXT("material");
	case IT_QUEST:    return TEXT("quest");
	case 14:          return TEXT("pat");        // ITEM_TYPE_RIDE_PART (LIST_PAT)
	default:          return TEXT("misc");
	}
}

bool RoseIsEquipSlot(const FString& Slot)
{
	return Slot == TEXT("body") || Slot == TEXT("arms") || Slot == TEXT("foot")
		|| Slot == TEXT("cap") || Slot == TEXT("back") || Slot == TEXT("weapon")
		|| Slot == TEXT("face") || Slot == TEXT("faceitem") || Slot == TEXT("jewel")
		|| Slot == TEXT("subwpn");
}

bool RoseIsStackableSlot(const FString& Slot)
{
	return Slot == TEXT("consumable") || Slot == TEXT("material")
		|| Slot == TEXT("gem") || Slot == TEXT("quest");
}

int32 RoseBagCategory(const FString& Slot)
{
	if (Slot == TEXT("consumable")) return 1;
	if (RoseIsEquipSlot(Slot))      return 0;   // armor / weapon / accessory
	return 2;                                   // material / gem / quest / misc
}

bool RoseRollDrop(int32 MobLevel, int32 PlayerLevel, int32 DropType, int32 DropItem,
	int32 DropMoney, int32 Charm, FRoseDropResult& Out)
{
	Out = FRoseDropResult();

	// World rates (calculation.cpp:16-23): drop 300, money multiplier 100.
	const int32 WorldDrop = 300, WorldDropM = 100, DropRateBonus = 0;

	int32 LevelDiff = PlayerLevel - MobLevel;   // attacker - defender
	if (LevelDiff < 0)
		LevelDiff = 0;
	else if (LevelDiff >= 10)
		return false;

	const int32 iDrop_VAR = (int32)((WorldDrop + DropItem - (1 + R(100))
		- ((LevelDiff + 16) * 3.5f) - 10 + DropRateBonus) * 0.38f);
	if (iDrop_VAR <= 0)
		return false;

	// Money branch.
	if (1 + R(100) <= DropMoney)
	{
		const int32 Money = (MobLevel + 20) * (MobLevel + iDrop_VAR + 40) * WorldDropM / 3200;
		if (Money <= 0)
			return false;
		Out.bIsMoney = true;
		Out.Money = Money;
		return true;
	}

	// Item branch.  The server also has a zone-table fallback when
	// DropItem-(1+RANDOM(100))<0; we only load NPC DropType rows, so always use
	// the mob's own table (documented deviation — no per-zone drop tables here).
	const TArray<int32>* Row = DropRows().Find(DropType);
	if (!Row || Row->Num() == 0)
		return false;

	int32 Idx = (iDrop_VAR > 30) ? R(30) : R(iDrop_VAR);
	if (!Row->IsValidIndex(Idx))
		return false;
	int32 Code = (*Row)[Idx];

	if (Code <= 1000)
	{
		// 1..4 = sub-table pointer → re-read at 26 + code*5 + RANDOM(5).
		if (Code >= 1 && Code <= 4)
		{
			Idx = 26 + Code * 5 + R(5);
			if (!Row->IsValidIndex(Idx))
				return false;
			Code = (*Row)[Idx];
			if (Code <= 1000)
				return false;
		}
		else
			return false;
	}

	Out.ItemType = Code / 1000;
	Out.ItemNo = Code % 1000;

	// Rolled bonus option (GEM_OP) — armor/accessory only (type ≤ KNAPSACK 6),
	// faithful to CCal::Get_DropITEM Default branch (calculation.cpp:208-224).
	// Bonus is a LIST_JEMITEM row id (an OPTION granting named stats), not a
	// magnitude.  Classic: `m_bIsAppraisal = m_nGEM_OP ? 0 : 1` — an item that
	// ROLLED an option drops UNappraised (its stats hidden until appraisal); an
	// item with no option has nothing to appraise.  This was inverted here.
	if (Out.ItemType >= IT_FACE && Out.ItemType <= IT_KNAPSACK)
	{
		const int32 r = 1 + R(100);
		const int32 iItemOp = (int32)(((MobLevel * 0.4f + (DropItem - 35) * 4 + 80 - r + Charm)
			* 24.f / (r + 13)) - 100);
		if (iItemOp > 0)
			Out.Bonus = iItemOp % (MobLevel < 230 ? (MobLevel + 70) : 301);
		Out.bAppraised = (Out.Bonus == 0);
	}

	// Quantity (calculation.cpp:188-196): stackable-not-consumable → level/var
	// formula (cap MAX_DROP_MULTIPLIER=10); consumable → MAX_CONSUMABLE=1; else 1.
	if (IsStackable(Out.ItemType) && Out.ItemType != IT_USE)
	{
		Out.Quantity = 1 + ((MobLevel + 10) / 9 + (1 + R(20)) + DropRateBonus) / (iDrop_VAR + 4);
		Out.Quantity = FMath::Clamp(Out.Quantity, 1, 10);
	}
	else
	{
		Out.Quantity = 1;   // consumables MAX_CONSUMABLE_DROP_MULTIPLIER=1; equipment 1
	}
	return true;
}
