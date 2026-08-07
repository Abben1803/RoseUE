#include "RoseBackend.h"

#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

DEFINE_LOG_CATEGORY_STATIC(LogRoseBackend, Log, All);

static FString JsonToString(const TSharedPtr<FJsonObject>& Obj)
{
	FString Out;
	TSharedRef<TJsonWriter<>> W = TJsonWriterFactory<>::Create(&Out);
	FJsonSerializer::Serialize(Obj.ToSharedRef(), W);
	return Out;
}

void URoseBackend::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// -rosebackend=http://host:port overrides the ini (handy for pointing a
	// packaged client at a test server without repacking).
	FString CmdUrl;
	if (FParse::Value(FCommandLine::Get(), TEXT("rosebackend="), CmdUrl) && !CmdUrl.IsEmpty())
		BackendUrl = CmdUrl;
	BackendUrl.RemoveFromEnd(TEXT("/"));

	// The service key comes from the ENVIRONMENT, never from config: a packaged
	// client must not be able to contain it, and config files ship.
	ServiceKey = FPlatformMisc::GetEnvironmentVariable(TEXT("ROSE_SERVICE_KEY"));

	if (!IsConfigured())
	{
		UE_LOG(LogRoseBackend, Log,
			TEXT("no BackendUrl configured — running fully local (save game + GameInstance snapshots)"));
		return;
	}
	UE_LOG(LogRoseBackend, Log, TEXT("backend %s (service key %s)"),
		*BackendUrl, ServiceKey.IsEmpty() ? TEXT("absent — client role") : TEXT("present — server role"));
}

TSharedRef<IHttpRequest, ESPMode::ThreadSafe> URoseBackend::MakeRequest(
	const FString& Verb, const FString& Path, bool bService, const FString& Body) const
{
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> Req = FHttpModule::Get().CreateRequest();
	Req->SetURL(BackendUrl + Path);
	Req->SetVerb(Verb);
	Req->SetHeader(TEXT("Content-Type"), TEXT("application/json"));

	if (bService)
		Req->SetHeader(TEXT("X-Service-Key"), ServiceKey);
	else if (!SessionToken.IsEmpty())
		Req->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + SessionToken);

	if (!Body.IsEmpty())
		Req->SetContentAsString(Body);
	return Req;
}

bool URoseBackend::ParseResponse(FHttpRequestPtr Req, FHttpResponsePtr Resp, bool bConnected,
	TSharedPtr<FJsonObject>& OutJson, FString& OutError)
{
	if (!bConnected || !Resp.IsValid())
	{
		OutError = TEXT("cannot reach the backend");
		return false;
	}

	const int32 Code = Resp->GetResponseCode();
	const FString Content = Resp->GetContentAsString();

	// 204 and other empty-bodied successes are still successes.
	if (Code >= 200 && Code < 300)
	{
		if (Content.IsEmpty())
			return true;
		TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Content);
		// A list response (the roster) is not an object; callers that expect one
		// re-read the content themselves, so a parse miss here is not fatal.
		FJsonSerializer::Deserialize(R, OutJson);
		return true;
	}

	// FastAPI puts the message in {"detail": "..."}.
	TSharedPtr<FJsonObject> Err;
	TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Content);
	if (FJsonSerializer::Deserialize(R, Err) && Err.IsValid() && Err->HasField(TEXT("detail")))
		OutError = Err->GetStringField(TEXT("detail"));
	else
		OutError = Content.IsEmpty() ? FString::Printf(TEXT("HTTP %d"), Code) : Content;
	OutError = FString::Printf(TEXT("%s (HTTP %d)"), *OutError, Code);
	return false;
}

// ── client side ─────────────────────────────────────────────────────────────

void URoseBackend::Register(const FString& Username, const FString& Password, FRoseBackendResult OnDone)
{
	if (!IsConfigured()) { OnDone.ExecuteIfBound(false, TEXT("no backend configured")); return; }

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("username"), Username);
	Body->SetStringField(TEXT("password"), Password);

	auto Req = MakeRequest(TEXT("POST"), TEXT("/api/v1/auth/register"), false, JsonToString(Body));
	Req->OnProcessRequestComplete().BindLambda(
		[OnDone](FHttpRequestPtr R, FHttpResponsePtr P, bool bOk)
		{
			TSharedPtr<FJsonObject> Json; FString Err;
			const bool bGood = ParseResponse(R, P, bOk, Json, Err);
			OnDone.ExecuteIfBound(bGood, Err);
		});
	Req->ProcessRequest();
}

void URoseBackend::Login(const FString& Username, const FString& Password, FRoseBackendResult OnDone)
{
	if (!IsConfigured()) { OnDone.ExecuteIfBound(false, TEXT("no backend configured")); return; }

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("username"), Username);
	Body->SetStringField(TEXT("password"), Password);

	auto Req = MakeRequest(TEXT("POST"), TEXT("/api/v1/auth/login"), false, JsonToString(Body));
	Req->OnProcessRequestComplete().BindLambda(
		[this, OnDone](FHttpRequestPtr R, FHttpResponsePtr P, bool bOk)
		{
			TSharedPtr<FJsonObject> Json; FString Err;
			if (!ParseResponse(R, P, bOk, Json, Err) || !Json.IsValid())
			{
				OnDone.ExecuteIfBound(false, Err.IsEmpty() ? TEXT("malformed login response") : Err);
				return;
			}
			// The token lives in memory only — never written to disk.  A stolen
			// save game must not be a stolen account.
			SessionToken = Json->GetStringField(TEXT("session_token"));
			UE_LOG(LogRoseBackend, Log, TEXT("logged in as %s"),
				*Json->GetStringField(TEXT("username")));
			OnDone.ExecuteIfBound(true, FString());
		});
	Req->ProcessRequest();
}

void URoseBackend::Logout()
{
	if (!IsConfigured() || SessionToken.IsEmpty())
		return;
	auto Req = MakeRequest(TEXT("POST"), TEXT("/api/v1/auth/logout"), false);
	Req->ProcessRequest();     // fire and forget; the token is dropped either way
	SessionToken.Reset();
}

void URoseBackend::FetchRoster(FRoseBackendRoster OnDone)
{
	if (!IsConfigured()) { OnDone.ExecuteIfBound(false, TEXT("no backend configured"), {}); return; }

	auto Req = MakeRequest(TEXT("GET"), TEXT("/api/v1/characters"), false);
	Req->OnProcessRequestComplete().BindLambda(
		[OnDone](FHttpRequestPtr R, FHttpResponsePtr P, bool bOk)
		{
			TSharedPtr<FJsonObject> Ignored; FString Err;
			if (!ParseResponse(R, P, bOk, Ignored, Err))
			{
				OnDone.ExecuteIfBound(false, Err, {});
				return;
			}
			// The roster is a JSON ARRAY, so re-read it as one.
			TArray<TSharedPtr<FJsonValue>> Arr;
			TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(P->GetContentAsString());
			if (!FJsonSerializer::Deserialize(Reader, Arr))
			{
				OnDone.ExecuteIfBound(false, TEXT("malformed roster"), {});
				return;
			}

			TArray<FRoseBackendCharacter> Out;
			for (const TSharedPtr<FJsonValue>& V : Arr)
			{
				const TSharedPtr<FJsonObject>* O;
				if (!V->TryGetObject(O)) continue;
				FRoseBackendCharacter C;
				C.Id     = (*O)->GetIntegerField(TEXT("id"));
				C.Name   = (*O)->GetStringField(TEXT("name"));
				C.Gender = (*O)->GetStringField(TEXT("gender"));
				C.Hair   = (*O)->GetIntegerField(TEXT("hair"));
				C.Face   = (*O)->GetIntegerField(TEXT("face"));
				C.Level  = (*O)->GetIntegerField(TEXT("level"));
				C.Job    = (*O)->GetIntegerField(TEXT("job"));
				(*O)->TryGetStringField(TEXT("zone"), C.Zone);
				Out.Add(C);
			}
			OnDone.ExecuteIfBound(true, FString(), Out);
		});
	Req->ProcessRequest();
}

void URoseBackend::CreateCharacter(const FString& Name, const FString& Gender, int32 Hair,
	int32 Face, FRoseBackendResult OnDone)
{
	if (!IsConfigured()) { OnDone.ExecuteIfBound(false, TEXT("no backend configured")); return; }

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("name"), Name);
	Body->SetStringField(TEXT("gender"), Gender);
	Body->SetNumberField(TEXT("hair"), Hair);
	Body->SetNumberField(TEXT("face"), Face);

	auto Req = MakeRequest(TEXT("POST"), TEXT("/api/v1/characters"), false, JsonToString(Body));
	Req->OnProcessRequestComplete().BindLambda(
		[OnDone](FHttpRequestPtr R, FHttpResponsePtr P, bool bOk)
		{
			TSharedPtr<FJsonObject> Json; FString Err;
			OnDone.ExecuteIfBound(ParseResponse(R, P, bOk, Json, Err), Err);
		});
	Req->ProcessRequest();
}

void URoseBackend::DeleteCharacter(int32 Id, FRoseBackendResult OnDone)
{
	if (!IsConfigured()) { OnDone.ExecuteIfBound(false, TEXT("no backend configured")); return; }

	auto Req = MakeRequest(TEXT("DELETE"), FString::Printf(TEXT("/api/v1/characters/%d"), Id), false);
	Req->OnProcessRequestComplete().BindLambda(
		[OnDone](FHttpRequestPtr R, FHttpResponsePtr P, bool bOk)
		{
			TSharedPtr<FJsonObject> Json; FString Err;
			OnDone.ExecuteIfBound(ParseResponse(R, P, bOk, Json, Err), Err);
		});
	Req->ProcessRequest();
}

void URoseBackend::EnterWorld(int32 Id, FRoseBackendTicket OnDone)
{
	if (!IsConfigured()) { OnDone.ExecuteIfBound(false, TEXT("no backend configured"), {}, {}); return; }

	auto Req = MakeRequest(TEXT("POST"), FString::Printf(TEXT("/api/v1/characters/%d/enter"), Id), false);
	Req->OnProcessRequestComplete().BindLambda(
		[OnDone](FHttpRequestPtr R, FHttpResponsePtr P, bool bOk)
		{
			TSharedPtr<FJsonObject> Json; FString Err;
			if (!ParseResponse(R, P, bOk, Json, Err) || !Json.IsValid())
			{
				OnDone.ExecuteIfBound(false, Err.IsEmpty() ? TEXT("malformed ticket response") : Err, {}, {});
				return;
			}
			OnDone.ExecuteIfBound(true, FString(),
				Json->GetStringField(TEXT("ticket")),
				Json->GetStringField(TEXT("server_address")));
		});
	Req->ProcessRequest();
}

// ── server side ─────────────────────────────────────────────────────────────

void URoseBackend::RedeemTicket(const FString& Ticket, FRoseBackendRedeem OnDone)
{
	if (!IsConfigured()) { OnDone.ExecuteIfBound(false, TEXT("no backend configured"), 0, {}); return; }
	if (ServiceKey.IsEmpty())
	{
		// Loud, because the alternative is a zone server that silently accepts
		// everyone as an unsaved guest.
		UE_LOG(LogRoseBackend, Error,
			TEXT("ROSE_SERVICE_KEY is not set — this server cannot load or save characters"));
		OnDone.ExecuteIfBound(false, TEXT("server has no service key"), 0, {});
		return;
	}

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("ticket"), Ticket);
	Body->SetStringField(TEXT("server_id"), FPlatformProcess::ComputerName());

	auto Req = MakeRequest(TEXT("POST"), TEXT("/internal/v1/tickets/redeem"), true, JsonToString(Body));
	Req->OnProcessRequestComplete().BindLambda(
		[OnDone](FHttpRequestPtr R, FHttpResponsePtr P, bool bOk)
		{
			TSharedPtr<FJsonObject> Json; FString Err;
			if (!ParseResponse(R, P, bOk, Json, Err) || !Json.IsValid())
			{
				OnDone.ExecuteIfBound(false, Err.IsEmpty() ? TEXT("malformed redeem response") : Err, 0, {});
				return;
			}
			OnDone.ExecuteIfBound(true, FString(),
				Json->GetIntegerField(TEXT("character_id")),
				Json->GetStringField(TEXT("name")));
		});
	Req->ProcessRequest();
}

void URoseBackend::LoadState(int32 Id, FRoseBackendState OnDone)
{
	if (!IsConfigured()) { OnDone.ExecuteIfBound(false, TEXT("no backend configured"), -1, nullptr); return; }

	auto Req = MakeRequest(TEXT("GET"),
		FString::Printf(TEXT("/internal/v1/characters/%d/state"), Id), true);
	Req->OnProcessRequestComplete().BindLambda(
		[OnDone](FHttpRequestPtr R, FHttpResponsePtr P, bool bOk)
		{
			TSharedPtr<FJsonObject> Json; FString Err;
			if (!ParseResponse(R, P, bOk, Json, Err) || !Json.IsValid())
			{
				OnDone.ExecuteIfBound(false, Err.IsEmpty() ? TEXT("malformed state response") : Err, -1, nullptr);
				return;
			}
			const TSharedPtr<FJsonObject>* State = nullptr;
			Json->TryGetObjectField(TEXT("state"), State);
			OnDone.ExecuteIfBound(true, FString(),
				Json->GetIntegerField(TEXT("revision")),
				State ? *State : nullptr);
		});
	Req->ProcessRequest();
}

void URoseBackend::SaveState(int32 Id, int32 Revision, const TSharedPtr<FJsonObject>& State,
	bool bRelease, FRoseBackendResult OnDone)
{
	if (!IsConfigured()) { OnDone.ExecuteIfBound(false, TEXT("no backend configured")); return; }
	if (!State.IsValid()) { OnDone.ExecuteIfBound(false, TEXT("no state to save")); return; }

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetNumberField(TEXT("revision"), Revision);
	Body->SetObjectField(TEXT("state"), State);
	Body->SetBoolField(TEXT("release"), bRelease);

	auto Req = MakeRequest(TEXT("PUT"),
		FString::Printf(TEXT("/internal/v1/characters/%d/state"), Id), true, JsonToString(Body));
	Req->OnProcessRequestComplete().BindLambda(
		[this, OnDone](FHttpRequestPtr R, FHttpResponsePtr P, bool bOk)
		{
			TSharedPtr<FJsonObject> Json; FString Err;
			if (!ParseResponse(R, P, bOk, Json, Err))
			{
				// A 409 here means someone else wrote this character while we
				// held it.  Never retry blindly — that is how a stale inventory
				// overwrites a good one.
				UE_LOG(LogRoseBackend, Error, TEXT("save failed: %s"), *Err);
				OnDone.ExecuteIfBound(false, Err);
				return;
			}
			if (Json.IsValid())
				StateRevision = Json->GetIntegerField(TEXT("revision"));
			OnDone.ExecuteIfBound(true, FString());
		});
	Req->ProcessRequest();
}

void URoseBackend::ZoneHandoff(int32 Id, const FString& DestZone, FRoseBackendTicket OnDone)
{
	if (!IsConfigured()) { OnDone.ExecuteIfBound(false, TEXT("no backend configured"), {}, {}); return; }

	TSharedPtr<FJsonObject> Body = MakeShared<FJsonObject>();
	Body->SetStringField(TEXT("dest_zone"), DestZone);
	Body->SetStringField(TEXT("server_id"), FPlatformProcess::ComputerName());

	auto Req = MakeRequest(TEXT("POST"),
		FString::Printf(TEXT("/internal/v1/characters/%d/handoff"), Id), true, JsonToString(Body));
	Req->OnProcessRequestComplete().BindLambda(
		[OnDone](FHttpRequestPtr R, FHttpResponsePtr P, bool bOk)
		{
			TSharedPtr<FJsonObject> Json; FString Err;
			if (!ParseResponse(R, P, bOk, Json, Err) || !Json.IsValid())
			{
				OnDone.ExecuteIfBound(false, Err.IsEmpty() ? TEXT("malformed handoff response") : Err, {}, {});
				return;
			}
			OnDone.ExecuteIfBound(true, FString(),
				Json->GetStringField(TEXT("ticket")),
				Json->GetStringField(TEXT("server_address")));
		});
	Req->ProcessRequest();
}

void URoseBackend::SetHeldCharacter(int32 Id, const FString& Name, int32 Revision)
{
	CharacterId = Id;
	CharacterName = Name;
	StateRevision = Revision;
}
