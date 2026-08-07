// Project game mode: spawns the ROSE character as the default pawn in every
// level (required for cross-zone warping — levels no longer need a hand-
// placed character).  Set as GlobalDefaultGameMode in DefaultEngine.ini.
//
// It is also the server's persistence boundary.  A connecting client carries a
// one-shot `?ticket=` issued by the backend; the GameMode redeems it to learn
// WHICH character this connection is (the client never says), loads that
// character's state, and saves it back on logout and on a periodic checkpoint.
// With no backend configured none of that runs and the game behaves exactly as
// it does offline.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RoseGameMode.generated.h"

class URosePlayerAccount;

UCLASS()
class ROSEUE_API ARoseGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ARoseGameMode();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController,
		const FUniqueNetIdRepl& UniqueId, const FString& Options,
		const FString& Portal = TEXT("")) override;
	virtual void PostLogin(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	// How often a still-connected player's state is written back.  A crash then
	// costs at most this much play time; every save is also a backend round
	// trip, so this trades durability against load.
	UPROPERTY(EditDefaultsOnly, Category = "Rose|Backend") float CheckpointSeconds = 120.f;

protected:
	// Save one connection's character.  bRelease clears the backend's `held_by`
	// so the character can be entered again — true on logout, false on a
	// checkpoint.
	void SaveAccount(APlayerController* PC, bool bRelease);
	// Push loaded state onto a controller's pawn (once its pawn exists).
	void ApplyPendingState(APlayerController* PC);

	FTimerHandle CheckpointTimer;
	void CheckpointAll();
};
