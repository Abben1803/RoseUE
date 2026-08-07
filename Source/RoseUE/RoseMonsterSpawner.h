// ROSE monster spawn point (REGEN) — a faithful transcription of the server's
// CRegenPOINT (src/sho_gameserver/src/common/cregenarea.cpp): five "basic"
// slots + two "tactic" slots, an interval tick, a live-count limit, and the
// tactics-point table that decides WHICH slots spawn each tick.  Placed from
// the zone IFO MonsterSpawn blocks by tools/ue5_import_mob_spawns.py, or by
// hand (set NpcId/Count for a simple one-mob camp).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "RoseMonsterSpawner.generated.h"

// One slot of the basic/tactic list (tagREGENMOB).
USTRUCT(BlueprintType)
struct FRoseSpawnEntry
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 NpcId = 0;
	UPROPERTY(EditAnywhere, BlueprintReadWrite) int32 Count = 0;
};

UCLASS()
class ROSEUE_API ARoseMonsterSpawner : public AActor
{
	GENERATED_BODY()

public:
	ARoseMonsterSpawner();
	virtual void Tick(float DeltaSeconds) override;

	// ── Simple one-mob camp (hand-placed / test camps): used only when
	//    BasicSpawns is empty — becomes BasicSpawns=[(NpcId,Count)], Limit=Count.
	UPROPERTY(EditAnywhere, Category = "Rose|Spawner") int32 NpcId = 1;
	UPROPERTY(EditAnywhere, Category = "Rose|Spawner") int32 Count = 3;

	// ── REGEN data (IFO MonsterSpawn payload) ────────────────────────────────
	UPROPERTY(EditAnywhere, Category = "Rose|Regen") TArray<FRoseSpawnEntry> BasicSpawns;
	UPROPERTY(EditAnywhere, Category = "Rose|Regen") TArray<FRoseSpawnEntry> TacticSpawns;
	UPROPERTY(EditAnywhere, Category = "Rose|Regen") float Interval = 6.f;     // seconds
	UPROPERTY(EditAnywhere, Category = "Rose|Regen") int32 LimitCount = 0;     // 0 = sum of basic counts
	UPROPERTY(EditAnywhere, Category = "Rose|Regen") float Range = 400.f;      // cm (IFO meters ×100)
	UPROPERTY(EditAnywhere, Category = "Rose|Regen") int32 TacticPoints = 100; // 전술P 주기

	UPROPERTY(EditAnywhere, Category = "Rose|Spawner") bool bAggressive = true;

	// Called by a dying monster (server SubLiveCNT).
	void NotifyDied();

protected:
	virtual void BeginPlay() override;

	// One regen tick — the CRegenPOINT::Proc tactics table.
	void ProcRegen();
	// Spawn Count monsters of NpcId at random points in the ±Range square
	// (RegenCharacter: skips id<1 or count<1).
	void RegenCharacter(int32 InNpcId, int32 InCount);
	void SpawnOne(int32 InNpcId, const FVector& At);
	// Random point in the square ±Range around the spawner, snapped to the
	// ground by a downward trace (IFO points carry no reliable z).
	FVector RandomPoint() const;

	int32 LiveCount = 0;
	int32 CurTactics = 1;      // 전술 포인트 (Reset() → 1)
	float IntervalTimer = 0.f;
};
