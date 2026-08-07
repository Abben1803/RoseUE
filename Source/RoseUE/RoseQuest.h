// ROSE quest engine — a faithful client-side port of the QSD trigger system
// (src/common/shared/io_quest.{h,cpp}: CQuestDATA::CheckQUEST +
// CQuestTRIGGER::Proc and the F_QSTCOND/F_QSTREWD tables) running against
// Content/Quests/quests.json (tools/gen_quest_data.py).
//
// Player quest state mirrors tagQuestData (cuserdata.h:212): 10 quest slots
// (CQUEST: id + 10 vars + 32 switches + 6 quest items + expiry), episode/job/
// planet/union vars, and 512 global switches.  Conversation scripts drive it
// through QF_checkQuestCondition / QF_doQuestTrigger (see RoseDialog.cpp);
// monster kills through the NPC row's STL quest key (CObjMOB::Do_DeadEvent).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/DataTable.h"
#include "RoseQuest.generated.h"

class ARoseCharacter;
class FJsonObject;

// quests.csv row (gen_quest_data.py) — the journal strings for a LIST_QUEST id.
USTRUCT(BlueprintType)
struct FRoseQuestRow : public FTableRowBase
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest") int32 Id = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest") FString DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest") FString Description;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest") FString StartMsg;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest") FString EndMsg;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest") int32 TimeLimit = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest") int32 OwnerType = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest") int32 Icon = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Quest") FString StlKey;
};

// One quest-inventory entry (tagBaseITEM): ItemSN = type*1000 + no.
USTRUCT()
struct FRoseQuestItem
{
	GENERATED_BODY()
	UPROPERTY() int32 ItemSN = 0;
	UPROPERTY() int32 Quantity = 0;
};

// One of the 10 quest slots (CQUEST, cquest.h): 10 vars, 32 switches, 6 items.
USTRUCT()
struct FRoseQuestSlot
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;                    // LIST_QUEST row (0 = free slot)
	UPROPERTY() double ExpireAt = 0.0;           // world seconds (0 = no limit)
	UPROPERTY() TArray<int32> Vars;              // QUEST_VAR_PER_QUEST = 10
	UPROPERTY() int32 Switches = 0;              // QUEST_SWITCH_PER_QUEST = 32 bits
	UPROPERTY() TArray<FRoseQuestItem> Items;    // QUEST_ITEM_PER_QUEST = 6 max

	void Reset()
	{
		Id = 0; ExpireAt = 0.0; Switches = 0;
		Vars.Init(0, 10);
		Items.Reset();
	}
};

// CheckQUEST result (eQST_RESULT).
enum class ERoseQuestResult : uint8 { Invalid, Success, Failed };

UCLASS(ClassGroup = (Rose), meta = (BlueprintSpawnableComponent))
class ROSEUE_API URoseQuestComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	URoseQuestComponent();
	virtual void BeginPlay() override;

	// ── tagQuestData state ────────────────────────────────────────────────
	UPROPERTY() TArray<FRoseQuestSlot> Slots;    // QUEST_PER_PLAYER = 10
	UPROPERTY() TArray<int32> EpisodeVars;       // 5
	UPROPERTY() TArray<int32> JobVars;           // 3
	UPROPERTY() TArray<int32> PlanetVars;        // 7
	UPROPERTY() TArray<int32> UnionVars;         // 10
	UPROPERTY() TArray<int32> UnionPoints;       // AT_UNION_POINT1..10 (81..90)
	TBitArray<> Switches;                        // QUEST_SWITCH_CNT = 512

	// Bumped on any quest-state change so the journal UI can refresh lazily.
	UPROPERTY() int32 QuestRevision = 0;

	// quests.csv journal table (auto-loaded like the item tables).
	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* QuestTable = nullptr;
	const FRoseQuestRow* GetQuestRow(int32 QuestId) const;

	// ── The engine (CQuestDATA::CheckQUEST) ──────────────────────────────
	// Walks the named trigger: all conditions must pass, then rewards run
	// (bDoReward=false is the conversation's "would this trigger fire?" gate —
	// action rewards are skipped exactly like the client's early-outs).
	// Follows both chains: reward-9 jumps and checkNext fall-through.
	ERoseQuestResult CheckQuestTrigger(const FString& TriggerName, bool bDoReward);

	// ── Quest slot helpers (CUserDATA::Quest_*) ───────────────────────────
	int32 FindQuestSlot(int32 QuestId) const;        // -1 when not registered
	int32 AppendQuest(int32 QuestId);                // Quest_Append: reuse or first free
	bool  DeleteQuest(int32 QuestId);
	int32 QuestItemQuantity(int32 QuestId, int32 ItemSN) const;

	// ── Lua/QF_ accessors ─────────────────────────────────────────────────
	int32 GetUserSwitch(int32 SwitchNo) const;
	int32 GetEpisodeVar(int32 VarNo) const;
	int32 GetJobVar(int32 VarNo) const;
	int32 GetPlanetVar(int32 VarNo) const;
	int32 GetUnionVar(int32 VarNo) const;
	int32 GetQuestVarBySlot(int32 SlotIdx, int32 VarNo) const;
	int32 GetQuestSwitchBySlot(int32 SlotIdx, int32 SwitchNo) const;

	// Console diagnostics.
	FString DescribeState() const;

private:
	ARoseCharacter* OwnerChar() const;

	// Per-check context (tQST_PARAM): the selected quest slot survives across
	// the whole trigger chain until a reward-9 jump resets it.
	struct FParam
	{
		int32 SlotIdx = -1;          // selected quest slot (-1 = none)
		FString NextTrigger;         // reward-9 jump target
	};

	bool ProcTrigger(const TSharedPtr<FJsonObject>& Trigger, FParam& Param, bool bDoReward);
	bool CheckCondition(const TSharedPtr<FJsonObject>& Cond, FParam& Param);
	bool ApplyReward(const TSharedPtr<FJsonObject>& Rewd, FParam& Param, bool bDoReward);

	// STR_QUEST_DATA var access by varType (Get/Set_QuestVAR).
	static constexpr int32 VarReadFailed = INT32_MIN;
	int32 GetQuestVar(const FParam& Param, int32 VarType, int32 VarNo) const;
	void  SetQuestVar(FParam& Param, int32 VarType, int32 VarNo, int32 Value);

	// Check_QuestOP comparison (0 ==, 1 >, 2 >=, 3 <, 4 <=, 10 !=).
	static bool CheckOp(int32 Op, int64 Left, int64 Right);

	// Quest-relevant ability read/write (Check_UserVAR / Reward_ABILITY).
	int32 GetAbility(int32 AbilType) const;
	void  AddAbility(int32 AbilType, int32 Value, bool bSet);

	// CCal::Get_RewardVALUE (calculation.cpp:78) — reward-5 equations.
	int32 RewardValue(int32 Equation, int32 Base, int32 DupCnt) const;

	// Item plumbing: itemSN = type*1000 + no → bag slot key + id.
	bool HasItemCheck(const TSharedPtr<FJsonObject>& ItemCheck, FParam& Param) const;
	bool GiveOrTakeItem(int32 ItemSN, int32 Count, bool bGive, FParam& Param);

	void Msg(const FString& Text) const;
};
