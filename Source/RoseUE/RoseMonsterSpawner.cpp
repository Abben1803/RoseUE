#include "RoseMonsterSpawner.h"

#include "RoseMonster.h"
#include "Engine/World.h"

ARoseMonsterSpawner::ARoseMonsterSpawner()
{
	PrimaryActorTick.bCanEverTick = true;
	// A bare AActor has no root component, so it CANNOT hold a transform —
	// without this every spawner sits at the world origin and the whole zone's
	// mobs pile up at (0,0).
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ARoseMonsterSpawner::BeginPlay()
{
	Super::BeginPlay();

	// The regen point is pure server logic: it SPAWNS actors, and a spawn on a
	// client would create a second, non-replicated copy of every mob in the
	// zone.  Placed-in-level actors run BeginPlay on every machine, hence the
	// explicit authority test (and the tick below is disabled outright).
	if (!HasAuthority())
	{
		SetActorTickEnabled(false);
		return;
	}

	// Hand-placed simple camp → one basic slot.
	if (BasicSpawns.Num() == 0 && NpcId > 0)
	{
		FRoseSpawnEntry E; E.NpcId = NpcId; E.Count = Count;
		BasicSpawns.Add(E);
		if (LimitCount <= 0)
			LimitCount = Count;
	}
	if (LimitCount <= 0)
		for (const FRoseSpawnEntry& E : BasicSpawns)
			LimitCount += FMath::Max(0, E.Count);
	LimitCount = FMath::Max(1, LimitCount);

	// Prime the camp immediately (the server's first Proc fires after one
	// interval; waiting 6-12 s for the world to populate just feels broken).
	ProcRegen();
}

void ARoseMonsterSpawner::Tick(float Dt)
{
	Super::Tick(Dt);
	if (!HasAuthority()) return;
	IntervalTimer += Dt;
	if (IntervalTimer >= FMath::Max(1.f, Interval))
	{
		IntervalTimer = 0.f;
		ProcRegen();
	}
}

void ARoseMonsterSpawner::NotifyDied()
{
	// SubLiveCNT — the interval tick refills, no per-death respawn timer.
	if (LiveCount > 0)
		--LiveCount;
}

// CRegenPOINT::Proc — the tactics-point table decides which slots spawn.
// B = BasicSpawns[0..4], T = TacticSpawns[0..1]; counts <1 are skipped by
// RegenCharacter (so the "count-2" rows can no-op, same as ROSE).
void ARoseMonsterSpawner::ProcRegen()
{
	if (LiveCount >= LimitCount)
	{
		if (CurTactics > 1)
			--CurTactics;
		return;
	}

	const auto B = [&](int32 i) -> const FRoseSpawnEntry* {
		return BasicSpawns.IsValidIndex(i) ? &BasicSpawns[i] : nullptr; };
	const auto T = [&](int32 i) -> const FRoseSpawnEntry* {
		return TacticSpawns.IsValidIndex(i) ? &TacticSpawns[i] : nullptr; };
	const auto Spawn = [&](const FRoseSpawnEntry* E, int32 CountDelta = 0) {
		if (E) RegenCharacter(E->NpcId, E->Count + CountDelta); };

	// 리젠변수 = {(한계몹수*2 - 현재몹수) * 현재전술P * 50} / {한계몹수 * 전술P주기}
	const int32 Var = ((LimitCount * 2 - LiveCount) * CurTactics * 50)
		/ FMath::Max(1, LimitCount * FMath::Max(1, TacticPoints));

	if      (Var <= 10) { CurTactics += 12; Spawn(B(0)); }
	else if (Var <= 15) { CurTactics += 15; Spawn(B(0), -2); Spawn(B(1)); }
	else if (Var <= 25) { CurTactics += 12; Spawn(B(2)); }
	else if (Var <= 30) { CurTactics += 15; Spawn(B(0), -1); Spawn(B(2)); }
	else if (Var <= 40) { CurTactics += 12; Spawn(B(3)); }
	else if (Var <= 50) { CurTactics += 12; Spawn(B(1)); Spawn(B(2), -2); }
	else if (Var <= 65) { CurTactics += 20; Spawn(B(2)); Spawn(B(3), -2); }
	else if (Var <= 73) { CurTactics += 15; Spawn(B(3)); Spawn(B(4)); }
	else if (Var <= 85) { CurTactics += 15; Spawn(B(0)); Spawn(T(0), -1); Spawn(B(4), -2); }
	else if (Var <= 92) { CurTactics = 1;   Spawn(B(1)); Spawn(T(0)); Spawn(T(1)); }
	else                { CurTactics = 7;   Spawn(B(4)); Spawn(T(0), +1); Spawn(T(1)); }

	CurTactics = FMath::Min(CurTactics, 500);
}

FVector ARoseMonsterSpawner::RandomPoint() const
{
	// RegenCharacter: uniform in the ±Range SQUARE (not a disc).
	const FVector Base = GetActorLocation();
	const float X = Base.X - Range + FMath::FRandRange(0.f, Range * 2.f);
	const float Y = Base.Y - Range + FMath::FRandRange(0.f, Range * 2.f);

	// Ground-snap: the IFO point z is unreliable — trace down for the terrain.
	float Z = Base.Z;
	if (UWorld* World = GetWorld())
	{
		FHitResult Hit;
		const FVector From(X, Y, Base.Z + 3000.f), To(X, Y, Base.Z - 5000.f);
		if (World->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility))
			Z = Hit.ImpactPoint.Z;
	}
	return FVector(X, Y, Z + 90.f);   // capsule half height + margin
}

void ARoseMonsterSpawner::RegenCharacter(int32 InNpcId, int32 InCount)
{
	if (InNpcId < 1 || InCount < 1)   // faithful guard (also eats count-2 no-ops)
		return;
	for (int32 i = 0; i < InCount; ++i)
		SpawnOne(InNpcId, RandomPoint());
}

void ARoseMonsterSpawner::SpawnOne(int32 InNpcId, const FVector& At)
{
	UWorld* World = GetWorld();
	if (!World) return;
	// Deferred spawn: NpcId must be set BEFORE BeginPlay runs (it drives the
	// mesh/anims/stats load).
	const FTransform T(FRotator(0.f, FMath::FRandRange(0.f, 360.f), 0.f), At);
	ARoseMonster* M = World->SpawnActorDeferred<ARoseMonster>(
		ARoseMonster::StaticClass(), T, this, nullptr,
		ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn);
	if (M)
	{
		M->NpcId = InNpcId;
		M->bAggressive = bAggressive;
		M->OwnerSpawner = this;
		M->FinishSpawning(T);
		++LiveCount;
		UE_LOG(LogTemp, Log, TEXT("[Rose] %s spawned npc %d at (%.0f, %.0f, %.0f) live=%d/%d"),
			*GetName(), InNpcId, At.X, At.Y, At.Z, LiveCount, LimitCount);
	}
}
