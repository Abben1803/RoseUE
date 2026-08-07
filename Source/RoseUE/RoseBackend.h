// URoseBackend — the game's client to the ROSE backend service (accounts,
// character roster, character persistence).  See backend/README.md.
//
// The backend is NOT a game server: it holds no world state and runs no
// simulation.  It owns what must outlive a zone-server process.
//
// TWO CALLERS, TWO CREDENTIALS — this subsystem exists on both sides and each
// only uses its half:
//
//   the CLIENT  sends `Authorization: Bearer <session token>` and may only
//               touch its own roster.  It can never read or write game state,
//               so a modified client has nothing to gain here.
//   the SERVER  sends `X-Service-Key: <shared secret>` and is the only thing
//               that can load or save a character.
//
// The service key is read from the ROSE_SERVICE_KEY environment variable, never
// from a config file — a packaged client must not be able to contain it.
//
// EVERYTHING IS OPTIONAL.  With no BackendUrl configured every call short-
// circuits and the game behaves exactly as it does today (local save game +
// URoseGameInstance snapshots), so single-player and offline development are
// unaffected.
#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Interfaces/IHttpRequest.h"
#include "RoseBackend.generated.h"

class ARoseCharacter;

// One roster entry from GET /api/v1/characters — appearance only, no game state.
USTRUCT()
struct FRoseBackendCharacter
{
	GENERATED_BODY()

	UPROPERTY() int32 Id = 0;
	UPROPERTY() FString Name;
	UPROPERTY() FString Gender = TEXT("Female");
	// Classic hair ids stop at 78; 110 was Arua-only (bald character).
	UPROPERTY() int32 Hair = 1;
	UPROPERTY() int32 Face = 1;
	UPROPERTY() int32 Level = 1;
	UPROPERTY() int32 Job = 0;
	UPROPERTY() FString Zone;
};

// Generic completion: bSuccess plus a human-readable error for the UI/chat log.
DECLARE_DELEGATE_TwoParams(FRoseBackendResult, bool /*bSuccess*/, const FString& /*Error*/);
DECLARE_DELEGATE_ThreeParams(FRoseBackendRoster, bool, const FString&, const TArray<FRoseBackendCharacter>&);
// EnterWorld: ticket + the zone server to connect to.
DECLARE_DELEGATE_FourParams(FRoseBackendTicket, bool, const FString&, const FString& /*Ticket*/, const FString& /*ServerAddress*/);
// LoadState: the revision to send back on save + the raw state object.
DECLARE_DELEGATE_FourParams(FRoseBackendState, bool, const FString&, int32 /*Revision*/, TSharedPtr<FJsonObject> /*State*/);
// RedeemTicket: which character this connection is authorised to be.
DECLARE_DELEGATE_FourParams(FRoseBackendRedeem, bool, const FString&, int32 /*CharacterId*/, const FString& /*Name*/);

UCLASS(config = Game)
class ROSEUE_API URoseBackend : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// Base URL, e.g. "http://127.0.0.1:8080".  Empty = no backend; every call
	// fails fast and the caller falls back to local behaviour.  Set in
	// DefaultGame.ini under [/Script/RoseUE.RoseBackend], or with -rosebackend=
	// on the command line.
	UPROPERTY(config) FString BackendUrl;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;

	bool IsConfigured() const { return !BackendUrl.IsEmpty(); }
	bool IsLoggedIn() const { return !SessionToken.IsEmpty(); }
	int32 GetCharacterId() const { return CharacterId; }
	const FString& GetCharacterName() const { return CharacterName; }

	// ── client side (session token) ─────────────────────────────────────────
	void Register(const FString& Username, const FString& Password, FRoseBackendResult OnDone);
	void Login(const FString& Username, const FString& Password, FRoseBackendResult OnDone);
	void Logout();
	void FetchRoster(FRoseBackendRoster OnDone);
	void CreateCharacter(const FString& Name, const FString& Gender, int32 Hair, int32 Face,
		FRoseBackendResult OnDone);
	void DeleteCharacter(int32 Id, FRoseBackendResult OnDone);
	// Ask for a one-shot world ticket, then connect to the returned address with
	// "?ticket=<...>".  The client never names a character to the zone server.
	void EnterWorld(int32 Id, FRoseBackendTicket OnDone);

	// ── server side (service key) ───────────────────────────────────────────
	// Exchange the ticket a connecting client supplied for the character it
	// actually authorises.  The zone server never trusts a client-sent id.
	void RedeemTicket(const FString& Ticket, FRoseBackendRedeem OnDone);
	void LoadState(int32 Id, FRoseBackendState OnDone);
	// Revision must be the one LoadState returned: the service rejects a stale
	// save (409) rather than clobbering a newer one — the duplication guard.
	void SaveState(int32 Id, int32 Revision, const TSharedPtr<FJsonObject>& State,
		bool bRelease, FRoseBackendResult OnDone);
	// Zone travel: after saving with bRelease, get a ticket into the next zone
	// to hand to the client for its reconnect.
	void ZoneHandoff(int32 Id, const FString& DestZone, FRoseBackendTicket OnDone);

	// Set by RedeemTicket so the zone server knows which row to save back to.
	void SetHeldCharacter(int32 Id, const FString& Name, int32 Revision);
	int32 GetStateRevision() const { return StateRevision; }
	void SetStateRevision(int32 R) { StateRevision = R; }

private:
	FString SessionToken;     // client only
	FString ServiceKey;       // server only (from ROSE_SERVICE_KEY)
	int32 CharacterId = 0;
	FString CharacterName;
	int32 StateRevision = -1;

	// bService picks the credential; body may be empty for GET/DELETE.
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> MakeRequest(
		const FString& Verb, const FString& Path, bool bService,
		const FString& Body = FString()) const;

	// Shared response handling: turns transport failures and non-2xx bodies
	// into one readable error string.
	static bool ParseResponse(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnected,
		TSharedPtr<FJsonObject>& OutJson, FString& OutError);
};

// Which backend character a CONNECTION is holding.
//
// This cannot live on URoseBackend: that is a GameInstance subsystem, i.e. ONE
// per process, and a zone server holds many players at once.  The GameMode
// attaches one of these to each PlayerController at login, so every connection
// carries its own character id and save revision.
UCLASS()
class ROSEUE_API URosePlayerAccount : public UActorComponent
{
	GENERATED_BODY()

public:
	// Straight off the connect URL ("?ticket=..."), before it has been redeemed.
	FString Ticket;

	// Filled in by the redeem response.  CharacterId 0 = not authenticated;
	// such a connection is playable but NEVER saved (see the note in
	// ARoseGameMode::Logout).
	int32 CharacterId = 0;
	FString CharacterName;
	// The revision LoadState returned.  Sent back on every save so the service
	// can reject a stale write instead of clobbering a newer one.
	int32 Revision = -1;

	bool bStateApplied = false;
	// The loaded state, parked here when it arrives before the pawn exists.
	TSharedPtr<FJsonObject> PendingState;
};
