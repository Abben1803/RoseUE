// A ROSE monster/NPC: the imported npc_<id> skeletal mesh + CHR animations,
// stats from the npcs DataTable, and a ROSE-style mob AI state machine
// (idle → wander → aggro-chase → attack → leash-return; hit react + die).
// Everything is data-driven by NpcId — no per-monster code.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "RoseMonster.generated.h"

class UAnimSequence;
class UDataTable;
class ARoseMonsterSpawner;
class URoseMonsterAIComponent;
struct FRoseNpcRow;

UCLASS()
class ROSEUE_API ARoseMonster : public ACharacter
{
	GENERATED_BODY()

public:
	ARoseMonster();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// LIST_NPC row / CHR model index (e.g. 1 = Mini-Jelly Bean).
	// Replicated: it is the ONLY thing a client needs to build the right model,
	// name, sounds and animations — every other stat is server-side.  It arrives
	// with the actor's initial property bunch, before the client's BeginPlay.
	UPROPERTY(EditAnywhere, Replicated, Category = "Rose|Monster") int32 NpcId = 1;

	// Set when this monster is a player's SUMMON rather than a wild spawn.
	// It holds capacity on the owner (ROSE budgets summons by cost, not count),
	// so the owner must be told when this dies or the budget leaks.
	UPROPERTY() TObjectPtr<class ARoseCharacter> SummonOwner = nullptr;

	// AI tuning.  ROSE drives these from per-mob AIP scripts (FILE_AI.STB);
	// these defaults model the common field-mob behaviour.
	UPROPERTY(EditAnywhere, Category = "Rose|AI") float WanderRadius = 600.f;
	UPROPERTY(EditAnywhere, Category = "Rose|AI") float AggroRange = 700.f;   // 0 = passive until hit
	UPROPERTY(EditAnywhere, Category = "Rose|AI") float LeashRange = 2500.f;  // from spawn point
	UPROPERTY(EditAnywhere, Category = "Rose|AI") bool  bAggressive = true;

	UPROPERTY(EditAnywhere, Category = "Rose|Data") UDataTable* NpcTable = nullptr;

	// ── Combat interface (mirrors ARoseTargetDummy so the player's melee path
	//    treats dummies and monsters the same) ────────────────────────────────
	void ApplyHit(float Damage, const FVector& FromDir, bool bCritical = false);
	void ShowMiss();
	bool IsDead() const { return State == EState::Dead; }

	// Overhead-plate readouts (ARoseMobHUD draws name + HP bar in screen space).
	const FString& GetDisplayName() const { return DisplayName; }
	float GetHP() const { return HP; }
	float GetMaxHP() const { return MaxHP; }
	bool IsAggroed() const { return State == EState::Chase || State == EState::Attack; }

	// Defender stats for the attacker's ROSE combat roll.
	int32 Level = 1;
	int32 GiveExp = 0;     // NPC_GIVE_EXP (col 17) — XP awarded to the killer
	int32 DropType = 0;    // NPC DropType — ITEM_DROP.STB row
	int32 DropMoney = 0;   // NPC DropMoney — money-drop chance 1-100
	int32 DropItem = 0;    // NPC DropItem — drop rate / quality
	int32 Avoid = 0;
	int32 Defense = 0;
	int32 Resist = 0;   // vs magic-type skill damage (SKILL_DAMAGE_TYPE 2)
	// LIST_HITSOUND material column (0 flesh / 1 hard / 2 metal) — the attacker
	// looks this up to pick the impact sound (HIT_SOUND(weaponType, material)).
	int32 HitMaterial = 0;
	// On-death quest trigger (NPC_DESC) — Die fires it on the killer's quest
	// component (CObjMOB::Do_DeadEvent).
	FString QuestTrigger;
	// Store tabs (NPC_SELL_TAB0-3 → stores table rows; 0 = none).  Town NPCs
	// (ARoseNpc) with any tab get a working store via GF_openStore.
	int32 SellTabs[4] = { 0, 0, 0, 0 };
	bool HasStore() const { return SellTabs[0] || SellTabs[1] || SellTabs[2] || SellTabs[3]; }

	// Set by the spawner so it can respawn this monster after death.
	TWeakObjectPtr<ARoseMonsterSpawner> OwnerSpawner;

	// ── AIP-driven AI (classic FILE_AI scripts; URoseMonsterAIComponent) ──────
	// The interpreter steers the FSM below through these; the FSM stays the
	// locomotion/combat engine.
	int32 AiType = 0;                                 // FILE_AI.STB row (LIST_NPC col 16)
	void AIStop();                                    // AIACT_00
	void AIWander(bool bAroundSpawn, float Dist, bool bRun);   // AIACT_03/04
	void AIFlee(float Dist);                          // AIACT_08/16
	void AIAggroPlayer();                             // target-selection actions
	// AIACT_24: cast SkillId (skills DataTable row) at Target; bSelf = self-cast.
	void AICastSkill(int32 SkillId, int32 MotionId, AActor* Target, bool bSelf);

protected:
	enum class EState : uint8 { Idle, Wander, Chase, Attack, Return, Dead };
	EState State = EState::Idle;

	// ── Replication ─────────────────────────────────────────────────────────
	// Monsters live entirely on the server: AI, combat rolls, loot and death all
	// run there and only the RESULT goes out.  Locomotion needs nothing on the
	// wire — CharacterMovement replicates the transform, and the client picks
	// stop/walk/run from the resulting velocity (TickRemoteAnim), exactly like
	// the player proxies.  What does have to replicate:
	//   NpcId   — which model/row to build (above)
	//   HP/MaxHP— the overhead plate ARoseMobHUD draws
	//   RepState— so the death animation and collision-off happen client-side
	UPROPERTY(ReplicatedUsing = OnRep_MobState) uint8 RepState = 0;
	UFUNCTION() void OnRep_MobState();
	// Client-only: drive the locomotion clip from the replicated velocity.
	void TickRemoteAnim(float Dt);

	// Table stats (loaded in BeginPlay from NpcId's row).
	UPROPERTY(Replicated) float MaxHP = 10.f;
	UPROPERTY(Replicated) float HP = 10.f;
	float WalkSpeed = 200.f, RunSpeed = 400.f, AttackRange = 190.f;
	int32 AttackPower = 1, HitRate = 50, AtkSpeedStat = 100;
	bool bMagicDamage = false;          // NPC_IS_MAGIC_DAMAGE → rolls vs Resist
	int32 AttackSoundId = 0;            // FILE_SOUND rows (RoseSoundData.h)
	int32 DieSoundId = 0;
	FString DisplayName;

	// CHR animations (MOB_ANI slots).
	UPROPERTY() UAnimSequence* AnimStop = nullptr;
	UPROPERTY() UAnimSequence* AnimMove = nullptr;
	UPROPERTY() UAnimSequence* AnimRun = nullptr;
	UPROPERTY() UAnimSequence* AnimAttack = nullptr;
	UPROPERTY() UAnimSequence* AnimHit = nullptr;
	UPROPERTY() UAnimSequence* AnimDie = nullptr;
	UAnimSequence* CurrentAnim = nullptr;
	void Play(UAnimSequence* Anim, bool bLoop, float Rate = 1.f);

	bool bInitialized = false;
	bool Initialize();               // load row + mesh + anims; false → no such npc

	// Whether this actor runs its AIP script (ARoseNpc opts out — town NPCs
	// have no combat AI tick, so scripted movement would strand them).
	virtual bool UsesAip() const { return true; }

	// AIP interpreter (only when the npc row has AiType > 0 with parsed data).
	UPROPERTY() URoseMonsterAIComponent* AIComp = nullptr;
	// Held weapon (LIST_NPC RWeapon → WeaponsStatic mesh on the CHR hand dummy's
	// parent bone; socket data from npc_weapon_sockets.json).
	UPROPERTY() class UStaticMeshComponent* WeaponComp = nullptr;
	void AttachNpcWeapon(int32 RWeaponId);
	// One in-flight skill cast (applied at the motion's contact point).
	int32 PendingSkillId = -1;
	float PendingSkillTimer = 0.f;
	bool  bPendingSkillSelf = false;
	TWeakObjectPtr<AActor> PendingSkillTarget;
	void ApplyPendingSkill();

	// ── AI ────────────────────────────────────────────────────────────────────
	FVector SpawnLocation = FVector::ZeroVector;
	FVector WanderTarget = FVector::ZeroVector;
	float   StateTimer = 0.f;        // idle wait / attack cooldown / corpse linger
	float   AttackHitTimer = -1.f;   // >0: pending damage at the swing's contact point
	float   HitStunTimer = 0.f;      // hit-react freeze
	class ARoseCharacter* FindPlayer() const;
	virtual void TickAI(float Dt);   // ARoseNpc overrides with a no-op (no combat AI)
	void EnterIdle();
	void EnterWander();
	void EnterChase();
	void EnterAttack(class ARoseCharacter* Player);
	void EnterReturn();
	void Die(const FVector& FromDir);
	void FacePlayer(const AActor* Player);
};
