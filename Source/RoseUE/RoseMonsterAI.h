// URoseMonsterAIComponent — the classic ROSE AIP interpreter (v1 subset).
//
// Each monster's LIST_NPC AiType selects a FILE_AI.STB row = one .AIP script,
// pre-parsed by tools/gen_ai_tables.py into Content/DataTables/ai_patterns.json
// (loaded once, shared).  A script = 6 trigger patterns (CREATED, STOP,
// ATTACKMOVE, DAMAGED, KILL, DEAD); each pattern = events; an event = AND of
// conditions -> if all pass, run ALL its actions and stop the pattern
// (CAI_PATTERN::Check semantics, src/common/shared/cai_file.cpp).
//
// This is where monster SKILL USAGE lives: action op 25 (AIACT_24) = "cast
// skill nSkill with motion nMotion at self/target/found-char" — routed to
// ARoseMonster::AICastSkill.  Movement/aggro actions steer the monster's
// existing FSM (the FSM stays the locomotion engine); unsupported opcodes are
// logged once and skipped.  Distances in the JSON are already cm (x100).
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "RoseMonsterAI.generated.h"

class ARoseMonster;
class ARoseCharacter;

// The 6 classic trigger patterns (cai_file.h AI_PATTERN_*).
enum class EAipTrigger : uint8 { Created = 0, Stop = 1, AttackMove = 2, Damaged = 3, Kill = 4, Dead = 5 };

struct FAipRec
{
	int32 Op = 0;                 // low-byte opcode (1-based dispatch slot)
	TMap<FName, int32> F;         // numeric fields (dist/pct/skill/...)
	FString Text;                 // say/shout/trigger text
};

struct FAipEvent
{
	FString Name;
	TArray<FAipRec> Conds;
	TArray<FAipRec> Acts;
};

struct FAipData
{
	int32 IdleSec = 5;            // STOP re-check interval (header iSecond)
	int32 DamagedPct = 0;         // DAMAGED gate % when already fighting
	TArray<FAipEvent> Patterns[6];
};

UCLASS()
class ROSEUE_API URoseMonsterAIComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// Bind to an AI id (FILE_AI.STB row).  Returns false when the id has no data.
	bool Setup(int32 InAiId);

	void FireTrigger(EAipTrigger T);

	// Trigger helpers driven from the monster's state ticks.
	void TickIdle(float Dt);      // throttled STOP
	void TickChase(float Dt);     // throttled ATTACKMOVE
	void OnDamaged();             // %-gated DAMAGED (only when already fighting)

private:
	int32 AiId = 0;
	const FAipData* Data = nullptr;
	float StopAccum = 0.f;
	float ChaseAccum = 0.f;

	// The char selected by a select-condition (AICOND_02/08) this evaluation —
	// AIACT_24 btTarget==0 casts at it.  Player or allied monster.
	TWeakObjectPtr<AActor> FindActor;

	ARoseMonster* Mob() const;
	ARoseCharacter* Player() const;

	bool EvalConds(const FAipEvent& Ev);
	void RunActs(const FAipEvent& Ev);
	bool EvalCond(const FAipRec& R);
	void RunAct(const FAipRec& R);

	// The shared ai_patterns.json table (loaded once).
	static const FAipData* FindAi(int32 Id);
};
