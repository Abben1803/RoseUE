#include "RoseGameMode.h"

#include "RoseBackend.h"
#include "RoseCharacter.h"
#include "RoseGameInstance.h"
#include "RoseMobHUD.h"
#include "Dom/JsonObject.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"

DEFINE_LOG_CATEGORY_STATIC(LogRoseGameMode, Log, All);

ARoseGameMode::ARoseGameMode()
{
	DefaultPawnClass = ARoseCharacter::StaticClass();
	HUDClass = ARoseMobHUD::StaticClass();   // monster overhead name + HP plates
}

void ARoseGameMode::BeginPlay()
{
	Super::BeginPlay();

	// Periodic write-back so a server crash costs minutes, not a session.
	if (UGameInstance* GI = GetGameInstance())
		if (URoseBackend* B = GI->GetSubsystem<URoseBackend>())
			if (B->IsConfigured() && CheckpointSeconds > 0.f)
				GetWorldTimerManager().SetTimer(CheckpointTimer, this,
					&ARoseGameMode::CheckpointAll, CheckpointSeconds, true);
}

void ARoseGameMode::EndPlay(const EEndPlayReason::Type Reason)
{
	GetWorldTimerManager().ClearTimer(CheckpointTimer);
	Super::EndPlay(Reason);
}

FString ARoseGameMode::InitNewPlayer(APlayerController* NewPlayerController,
	const FUniqueNetIdRepl& UniqueId, const FString& Options, const FString& Portal)
{
	const FString Error = Super::InitNewPlayer(NewPlayerController, UniqueId, Options, Portal);
	if (!Error.IsEmpty() || !NewPlayerController)
		return Error;

	UGameInstance* GI = GetGameInstance();
	URoseBackend* Backend = GI ? GI->GetSubsystem<URoseBackend>() : nullptr;
	if (!Backend || !Backend->IsConfigured())
		return Error;    // fully local: no accounts, no persistence

	// The connect URL is the only thing the client gets to say about identity,
	// and all it can say is "here is a ticket" — an opaque one-shot token it
	// could not have forged.  WHICH character that means is decided by the
	// backend in PostLogin, never here and never by the client.
	URosePlayerAccount* Account = NewObject<URosePlayerAccount>(NewPlayerController);
	Account->RegisterComponent();
	Account->Ticket = UGameplayStatics::ParseOption(Options, TEXT("ticket"));

	if (Account->Ticket.IsEmpty())
		UE_LOG(LogRoseGameMode, Warning,
			TEXT("connection with no ticket — playable, but nothing will be saved"));

	return Error;
}

void ARoseGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);

	UGameInstance* GI = GetGameInstance();
	URoseBackend* Backend = GI ? GI->GetSubsystem<URoseBackend>() : nullptr;
	URosePlayerAccount* Account =
		NewPlayer ? NewPlayer->FindComponentByClass<URosePlayerAccount>() : nullptr;
	if (!Backend || !Backend->IsConfigured() || !Account || Account->Ticket.IsEmpty())
		return;

	TWeakObjectPtr<APlayerController> WeakPC(NewPlayer);
	TWeakObjectPtr<URosePlayerAccount> WeakAcc(Account);

	// redeem → load → apply.  Every leg is async and any of them can outlive
	// the connection, hence the weak pointers.
	Backend->RedeemTicket(Account->Ticket,
		FRoseBackendRedeem::CreateLambda(
			[this, Backend, WeakPC, WeakAcc](bool bOk, const FString& Err, int32 CharId, const FString& Name)
			{
				URosePlayerAccount* Acc = WeakAcc.Get();
				if (!Acc || !WeakPC.IsValid())
					return;    // client left mid-handshake
				if (!bOk)
				{
					// Refuse rather than let an unauthenticated connection play
					// as an unsaved character — that path silently loses work.
					UE_LOG(LogRoseGameMode, Error, TEXT("ticket redeem failed: %s"), *Err);
					if (APlayerController* PC = WeakPC.Get())
						PC->ClientWasKicked(FText::FromString(
							TEXT("Could not verify your character. Please log in again.")));
					return;
				}

				Acc->CharacterId = CharId;
				Acc->CharacterName = Name;
				UE_LOG(LogRoseGameMode, Log, TEXT("connection authenticated as '%s' (character %d)"),
					*Name, CharId);

				Backend->LoadState(CharId,
					FRoseBackendState::CreateLambda(
						[this, WeakPC, WeakAcc](bool bLoaded, const FString& LoadErr, int32 Revision,
							TSharedPtr<FJsonObject> State)
						{
							URosePlayerAccount* A = WeakAcc.Get();
							if (!A || !WeakPC.IsValid())
								return;
							if (!bLoaded)
							{
								// Do NOT fall through to a default character: it
								// would be saved over the real one at logout.
								UE_LOG(LogRoseGameMode, Error,
									TEXT("state load failed for character %d: %s"),
									A->CharacterId, *LoadErr);
								A->CharacterId = 0;
								if (APlayerController* PC = WeakPC.Get())
									PC->ClientWasKicked(FText::FromString(
										TEXT("Could not load your character. Please try again.")));
								return;
							}
							A->Revision = Revision;
							A->PendingState = State;
							ApplyPendingState(WeakPC.Get());
						}));
			}));
}

void ARoseGameMode::ApplyPendingState(APlayerController* PC)
{
	URosePlayerAccount* Acc = PC ? PC->FindComponentByClass<URosePlayerAccount>() : nullptr;
	if (!Acc || Acc->bStateApplied || !Acc->PendingState.IsValid())
		return;

	ARoseCharacter* Char = Cast<ARoseCharacter>(PC->GetPawn());
	if (!Char)
	{
		// The pawn has not spawned yet.  Retry shortly rather than racing it —
		// the state is already parked on the account component.
		FTimerHandle Retry;
		TWeakObjectPtr<APlayerController> WeakPC(PC);
		GetWorldTimerManager().SetTimer(Retry,
			FTimerDelegate::CreateLambda([this, WeakPC] { ApplyPendingState(WeakPC.Get()); }),
			0.1f, false);
		return;
	}

	URoseGameInstance* GI = Cast<URoseGameInstance>(GetGameInstance());
	if (!GI)
		return;

	// A brand-new character carries `seeded`: the backend has the look chosen at
	// Character Select and NOTHING else, because the starting kit is game data
	// and belongs here, not in the database.  Restoring that as if it were a
	// full state wipes the loadout BeginPlay just built and stores a naked
	// character with 0 HP — so stamp the look on instead, and save immediately
	// so revision 1 is a real character.
	bool bSeeded = false;
	Acc->PendingState->TryGetBoolField(TEXT("seeded"), bSeeded);

	if (bSeeded)
	{
		FString LookGender;
		Acc->PendingState->TryGetStringField(TEXT("gender"), LookGender);
		int32 Hair = 0, Face = 0;
		const TSharedPtr<FJsonObject>* Eq = nullptr;
		if (Acc->PendingState->TryGetObjectField(TEXT("equipped"), Eq))
		{
			(*Eq)->TryGetNumberField(TEXT("hair"), Hair);
			(*Eq)->TryGetNumberField(TEXT("face"), Face);
		}
		Char->ApplyLook(LookGender, Hair, Face);

		Acc->bStateApplied = true;
		Acc->PendingState.Reset();
		UE_LOG(LogRoseGameMode, Log,
			TEXT("character %d ('%s') is new — starting kit kept, look applied"),
			Acc->CharacterId, *Acc->CharacterName);
		SaveAccount(PC, /*bRelease*/ false);
		return;
	}

	// FromJson fills the snapshot, Restore() pushes it onto the character — the
	// exact same path a cross-zone warp uses, so a login and a warp can never
	// disagree about what a character is.
	GI->FromJson(Acc->PendingState);
	GI->Restore(Char);
	Char->PushAppearance();     // tell every other client what this player looks like

	Acc->bStateApplied = true;
	Acc->PendingState.Reset();
	UE_LOG(LogRoseGameMode, Log, TEXT("character %d ('%s') state applied at revision %d"),
		Acc->CharacterId, *Acc->CharacterName, Acc->Revision);
}

void ARoseGameMode::SaveAccount(APlayerController* PC, bool bRelease)
{
	URosePlayerAccount* Acc = PC ? PC->FindComponentByClass<URosePlayerAccount>() : nullptr;
	// CharacterId 0 = this connection never authenticated.  Saving it would
	// write a default character over somebody's real one.
	if (!Acc || Acc->CharacterId == 0 || !Acc->bStateApplied)
		return;

	ARoseCharacter* Char = Cast<ARoseCharacter>(PC->GetPawn());
	UGameInstance* GameInst = GetGameInstance();
	URoseBackend* Backend = GameInst ? GameInst->GetSubsystem<URoseBackend>() : nullptr;
	URoseGameInstance* GI = Cast<URoseGameInstance>(GameInst);
	if (!Char || !Backend || !GI)
		return;

	GI->Capture(Char);
	const FString Zone = GetWorld()->GetMapName().Replace(TEXT("UEDPIE_0_"), TEXT(""));
	TSharedPtr<FJsonObject> State = GI->ToJson(Zone, Char->GetActorLocation());

	const int32 CharId = Acc->CharacterId;
	TWeakObjectPtr<URosePlayerAccount> WeakAcc(Acc);
	Backend->SaveState(CharId, Acc->Revision, State, bRelease,
		FRoseBackendResult::CreateLambda(
			[CharId, WeakAcc](bool bOk, const FString& Err)
			{
				if (!bOk)
				{
					// A 409 means someone else wrote this character while we
					// held it.  Never retry blindly — that is exactly how a
					// stale inventory overwrites a good one.
					UE_LOG(LogRoseGameMode, Error, TEXT("save failed for character %d: %s"),
						CharId, *Err);
					return;
				}
				if (URosePlayerAccount* A = WeakAcc.Get())
					++A->Revision;   // matches the revision the service just stored
				UE_LOG(LogRoseGameMode, Verbose, TEXT("character %d saved"), CharId);
			}));
}

void ARoseGameMode::CheckpointAll()
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		SaveAccount(It->Get(), /*bRelease*/ false);
}

void ARoseGameMode::Logout(AController* Exiting)
{
	// Release on the way out so the character can be entered again immediately;
	// without it the backend's `held_by` would block the next login until the
	// row was cleared by hand.
	SaveAccount(Cast<APlayerController>(Exiting), /*bRelease*/ true);
	Super::Logout(Exiting);
}
