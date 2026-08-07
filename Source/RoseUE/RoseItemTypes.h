// DataTable row structs for ROSE item tables (generated CSVs in RoseUE/DataTables).
// Property names must match the CSV column headers exactly; the CSV "Name" column
// is the row key (e.g. "weapon_2").  Data-driven: add items by editing the CSV /
// re-running tools/gen_item_tables.py — never by editing code.
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "RoseItemTypes.generated.h"

// weapons.csv (gender-neutral)
USTRUCT(BlueprintType)
struct FRoseWeaponRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Id = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Type = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   MotionType = 0;   // → TYPE_MOTION column (anim set)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString MotionName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Hand;             // Right / Left / Right+Left
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   AttackPower = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) float   AttackSpeed = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Range = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Quality = 0;      // ITEM_QUALITY (STB col 8) → HitRate quality*0.6
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   IconIdx = 0;      // ITEM_ICON_NO (STB col 9) → /Game/UI/Icons/icon_<idx>
	// Store pricing (CEconomy::Get_ItemBuy/SellPRICE): ITEM_BASE_PRICE (col 5)
	// + ITEM_PRICE_RATE (col 6, the fluctuation weight).
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BasePrice = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   PriceRate = 0;
	// ROSE combat sounds (RoseSoundData.h): swing-start FILE_SOUND row and the
	// LIST_HITSOUND row (TYPE); the defender's HitMaterial picks the column.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   AtkStartSound = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   AtkHitSoundType = 0;
	// WEAPON_BULLET_EFFECT (STB col 38): LIST_EFFECT row for ranged weapons —
	// bullet/hit .EFTs + bullet speed + fire/hit sounds (RoseWeaponEffectData.h).
	// 0 = melee.  AtkFireSound = WEAPON_ATK_FIRE_SOUND (FILE_SOUND row, col 41).
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BulletEffect = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   AtkFireSound = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   ReqStat1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   ReqAmount1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   ReqStat2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   ReqAmount2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BonusStat1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BonusAmount1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BonusStat2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BonusAmount2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Durability = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Weight = 0;         // ITEM_WEIGHT col 7 (tenths)
	// Ground-drop model: a row into LIST_FIELDITEM.ZSC, imported as
	// /Game/Rose/Equipment/FieldItem/SM_field_<id>.  What the item looks like
	// lying in the world (ARoseGroundItem).  Deliberately coarse in ROSE — every
	// back item shares one model — so it is NOT usable as an inventory icon.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   FieldItemId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Mesh;             // UE asset path
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString TextureSrc;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString StlKey;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString DisplayName;      // hand-authored
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Description;      // hand-authored

    UPROPERTY(EditAnywhere, BlueprintReadOnly)
    TSoftObjectPtr<USoundBase> HitSound;
};

// body.csv / arms.csv / foot.csv / cap.csv (per-gender meshes).  Superset of fields;
// slot-specific columns (MoveSpeed=foot, HairSetting*=cap) default when absent.
USTRUCT(BlueprintType)
struct FRoseArmorRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Id = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Type = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Defense = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   MagicResist = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   IconIdx = 0;       // ITEM_ICON_NO (STB col 9) → /Game/UI/Icons/icon_<idx>
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Quality = 0;       // ITEM_QUALITY (STB col 8)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BasePrice = 0;     // ITEM_BASE_PRICE (col 5)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   PriceRate = 0;     // ITEM_PRICE_RATE (col 6)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   MoveSpeed = 0;      // foot
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   HairSettingF = 0;   // cap
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   HairSettingM = 0;   // cap
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   ReqStat1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   ReqAmount1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   ReqStat2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   ReqAmount2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BonusStat1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BonusAmount1 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BonusStat2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BonusAmount2 = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Durability = 0;
	// ITEM_WEIGHT (STB col 7), tenths of a unit → display Weight/10.0.  0 until
	// the DataTables are reimported from the weight-bearing CSVs.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Weight = 0;
	/** Ground-drop model — see FRoseWeaponRow::FieldItemId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   FieldItemId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString MeshFemale;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString MeshMale;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString StlKey;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Description;
};

// consumable.csv / gem.csv / material.csv — non-equipment items (USEITEM / JEMITEM
// / NATURAL).  No model/stats needed for the bag: just id + icon + name/desc.
USTRUCT(BlueprintType)
struct FRoseSimpleItemRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Id = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   IconIdx = 0;      // ITEM_ICON_NO (STB col 9)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Subtype = 0;      // ITEM_TYPE (col 4) — essential-goods check
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Quality = 0;      // ITEM_QUALITY (STB col 8)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   BasePrice = 0;    // ITEM_BASE_PRICE (col 5)
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   PriceRate = 0;    // ITEM_PRICE_RATE (col 6)

	// ── Use-item effect block (LIST_USEITEM only; zero on the other tables) ──
	// Columns verified against rose/io/stb.h USEITEM_* macros AND the Arua
	// header names, which agree.  See gen_item_tables.py.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Weight = 0;       // ITEM_WEIGHT (col 7), tenths
	/** Ground-drop model — see FRoseWeaponRow::FieldItemId. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   FieldItemId = 0;
	// USEITEM_NEED_DATA_TYPE/VALUE (17/18): using the item requires
	// GetAbilityValue(NeedAbility) >= NeedValue (gs_user.cpp:940).
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedAbility = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   NeedValue = 0;
	// USEITEM_ADD_DATA_TYPE/VALUE (19/20): the ability granted and how much.
	// 16 = AT_HP, 17 = AT_MP, 76 = AT_STAMINA, 30 = AT_EXP, 40 = AT_MONEY.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   AddAbility = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   AddValue = 0;
	// USEITME_STATUS_STB (24): non-zero = an over-time potion.  StatusType is
	// the LIST_STATUS eING_TYPE (1 = HP, 2 = MP) and StatusPerSec its "Per 1
	// Second" amount, so AddValue is the TOTAL and AddValue/StatusPerSec the
	// duration in seconds.  Zero = the effect lands instantly.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   StatusId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   StatusType = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   StatusPerSec = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   UseEffect = 0;    // LIST_EFFECT row
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   UseSound = 0;     // FILE_SOUND row
	// USEITEM_COOLTIME_TYPE/DELAY (25/26): shared cooldown group + seconds.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   CooldownType = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   CooldownSec = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   ScriptIdx = 0;    // USEITEM_SCRIPT (21)
	// STR_ITEMTYPE.STL name for Subtype — the classic tooltip's "Type:" line.
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString TypeName;

	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString StlKey;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Description;
};

// stores.csv — one NPC store tab (LIST_SELL.STB row): display name + the item
// SNs (type*1000+no) it sells, semicolon-joined in slot order.
USTRUCT(BlueprintType)
struct FRoseStoreRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly) int32   Id = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly) FString Items;
};
