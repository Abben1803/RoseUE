#include "RoseDialog.h"
#include "RoseCharacter.h"
#include "RoseNpc.h"
#include "RoseQuest.h"
#include "RoseSkillComponent.h"
#include "RoseUIChat.h"
#include "RoseUIManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Dom/JsonObject.h"

extern "C"
{
#include "lua4/lua.h"
#include "lua4/lualib.h"
}

DEFINE_LOG_CATEGORY_STATIC(LogRoseDialog, Log, All);

// SC_MSG_* (cevent.cpp:21).
namespace { enum { MsgClose = 0, MsgNext = 1, MsgNpcSay = 2, MsgSelect = 3, MsgJump = 4 }; }

// ─── CON data cache ──────────────────────────────────────────────────────────
TSharedPtr<FRoseDialogData> FRoseDialogData::Load(const FString& Stem)
{
	static TMap<FString, TSharedPtr<FRoseDialogData>> Cache;
	if (TSharedPtr<FRoseDialogData>* Found = Cache.Find(Stem))
		return *Found;

	const FString Path = FPaths::ProjectContentDir() / TEXT("Dialogs") / Stem + TEXT(".json");
	FString Raw;
	TSharedPtr<FRoseDialogData> Data;
	if (FFileHelper::LoadFileToString(Raw, *Path))
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		if (FJsonSerializer::Deserialize(Reader, Root) && Root.IsValid())
		{
			Data = MakeShared<FRoseDialogData>();
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (Root->TryGetArrayField(TEXT("eventFuncs"), Arr))
				for (const auto& V : *Arr)
					Data->EventFuncs.Add(V->AsString());
			Root->TryGetStringField(TEXT("gateCheck"), Data->GateCheck);
			Root->TryGetStringField(TEXT("gateClick"), Data->GateClick);

			if (Root->TryGetArrayField(TEXT("menus"), Arr))
				for (const auto& MenuV : *Arr)
				{
					TArray<FRoseDialogItem>& Menu = Data->Menus.AddDefaulted_GetRef();
					const TArray<TSharedPtr<FJsonValue>>* Items = nullptr;
					if (MenuV->TryGetArray(Items))
						for (const auto& ItV : *Items)
						{
							const TSharedPtr<FJsonObject> O = ItV->AsObject();
							FRoseDialogItem& It = Menu.AddDefaulted_GetRef();
							O->TryGetNumberField(TEXT("type"), It.Type);
							O->TryGetNumberField(TEXT("child"), It.Child);
							O->TryGetStringField(TEXT("check"), It.Check);
							O->TryGetStringField(TEXT("click"), It.Click);
							O->TryGetStringField(TEXT("text"), It.Text);
						}
				}

			FString LuaB64;
			if (Root->TryGetStringField(TEXT("lua"), LuaB64))
				FBase64::Decode(LuaB64, Data->LuaChunk);
		}
	}
	if (!Data.IsValid())
		UE_LOG(LogRoseDialog, Warning, TEXT("dialog %s failed to load (%s)"), *Stem, *Path);
	Cache.Add(Stem, Data);
	return Data;
}

// ─── Lua builtins (the QF_/GF_ surface the 125 CON scripts call) ────────────
// The active session during any Lua call — conversations are modal and the
// game thread owns Lua, so a single static is exactly the client's g_pAVATAR
// coupling in spirit.
static FRoseDialogSession* GSession = nullptr;

namespace
{
	URoseQuestComponent* QuestComp()
	{
		ARoseCharacter* C = GSession ? GSession->GetPlayer() : nullptr;
		return C ? C->FindComponentByClass<URoseQuestComponent>() : nullptr;
	}

	void ChatStub(const TCHAR* What)
	{
		FRoseChatLog::Add(FRoseChatLog::EKind::System,
			FString::Printf(TEXT("[%s] is not available yet."), What));
	}

	// ---- QF_: quests --------------------------------------------------------
	int L_checkQuestCondition(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		const char* Name = lua_tostring(L, 1);
		const bool bOk = Q && Name
			&& Q->CheckQuestTrigger(ANSI_TO_TCHAR(Name), false) == ERoseQuestResult::Success;
		lua_pushnumber(L, bOk ? 1 : 0);
		return 1;
	}
	int L_doQuestTrigger(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		const char* Name = lua_tostring(L, 1);
		const bool bOk = Q && Name
			&& Q->CheckQuestTrigger(ANSI_TO_TCHAR(Name), true) == ERoseQuestResult::Success;
		lua_pushnumber(L, bOk ? 1 : 0);
		return 1;
	}
	int L_findQuest(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		lua_pushnumber(L, Q ? Q->FindQuestSlot((int32)lua_tonumber(L, 1)) : -1);
		return 1;
	}
	int L_getQuestCount(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		int32 N = 0;
		if (Q) for (const FRoseQuestSlot& S : Q->Slots) if (S.Id) N++;
		lua_pushnumber(L, N);
		return 1;
	}
	int L_getQuestID(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		const int32 H = (int32)lua_tonumber(L, 1);
		lua_pushnumber(L, (Q && Q->Slots.IsValidIndex(H)) ? Q->Slots[H].Id : -1);
		return 1;
	}
	int L_appendQuest(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		lua_pushnumber(L, (Q && Q->AppendQuest((int32)lua_tonumber(L, 1)) >= 0) ? 1 : 0);
		return 1;
	}
	int L_deleteQuest(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		if (Q) Q->DeleteQuest((int32)lua_tonumber(L, 1));
		return 0;
	}
	int L_getQuestVar(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		lua_pushnumber(L, Q ? Q->GetQuestVarBySlot((int32)lua_tonumber(L, 1),
			(int32)lua_tonumber(L, 2)) : 0);
		return 1;
	}
	int L_getQuestSwitch(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		lua_pushnumber(L, Q ? Q->GetQuestSwitchBySlot((int32)lua_tonumber(L, 1),
			(int32)lua_tonumber(L, 2)) : 0);
		return 1;
	}
	int L_getEpisodeVAR(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		lua_pushnumber(L, Q ? Q->GetEpisodeVar((int32)lua_tonumber(L, 1)) : 0);
		return 1;
	}
	int L_getJobVAR(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		lua_pushnumber(L, Q ? Q->GetJobVar((int32)lua_tonumber(L, 1)) : 0);
		return 1;
	}
	int L_getPlanetVAR(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		lua_pushnumber(L, Q ? Q->GetPlanetVar((int32)lua_tonumber(L, 1)) : 0);
		return 1;
	}
	int L_getUnionVAR(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		lua_pushnumber(L, Q ? Q->GetUnionVar((int32)lua_tonumber(L, 1)) : 0);
		return 1;
	}
	int L_getUserSwitch(lua_State* L)
	{
		URoseQuestComponent* Q = QuestComp();
		lua_pushnumber(L, Q ? Q->GetUserSwitch((int32)lua_tonumber(L, 1)) : -1);
		return 1;
	}
	int L_getQuestItemQuantity(lua_State* L)
	{
		// (questID, 5-digit type+no) — qf_quest.cpp:364.
		URoseQuestComponent* Q = QuestComp();
		if (!Q) { lua_pushnumber(L, -1); return 1; }
		const int32 QuestId = (int32)lua_tonumber(L, 1);
		const int32 ItemSN = (int32)lua_tonumber(L, 2);
		if (Q->FindQuestSlot(QuestId) < 0) { lua_pushnumber(L, -1); return 1; }
		lua_pushnumber(L, Q->QuestItemQuantity(QuestId, ItemSN));
		return 1;
	}
	int L_getNpcQuestZeroVal(lua_State* L)
	{
		lua_pushnumber(L, 0);   // server NPC object var[0] — no server, always 0
		return 1;
	}
	int L_getEventOwner(lua_State* L)
	{
		lua_pushnumber(L, lua_tonumber(L, 1));   // handle == owner token here
		return 1;
	}

	// ---- GF_: game ----------------------------------------------------------
	int L_getVariable(lua_State* L)
	{
		// gf_system.cpp:16 — SV_ index reads.
		ARoseCharacter* C = GSession ? GSession->GetPlayer() : nullptr;
		URoseSkillComponent* Sk = C ? C->FindComponentByClass<URoseSkillComponent>() : nullptr;
		int32 V = -1;
		if (C)
			switch ((int32)lua_tonumber(L, 1))
			{
			case 0:  V = C->Gender == TEXT("Female") ? 1 : 0; break;  // SV_SEX
			case 2:  V = Sk ? Sk->CurrentJob : 0; break;              // SV_CLASS
			case 6:  V = C->Strength; break;
			case 7:  V = C->Dexterity; break;
			case 8:  V = C->Intelligence; break;
			case 9:  V = C->Concentration; break;
			case 10: V = C->Charm; break;
			case 11: V = C->Sense; break;
			case 13: V = C->Level; break;                             // SV_LEVEL
			default: V = 0; break;   // BIRTH/UNION/RANK/FAME/EXP/POINT
			}
		lua_pushnumber(L, V);
		return 1;
	}
	int L_GetMotionUseFile(lua_State* L) { lua_pushnumber(L, 0); return 1; }
	int L_SetMotion(lua_State* L) { return 0; }
	int L_openStore(lua_State* L)
	{
		// GF_openStore(hID) — open the conversation NPC's store window.
		ARoseCharacter* C = GSession ? GSession->GetPlayer() : nullptr;
		ARoseNpc* Npc = GSession ? GSession->GetNpc() : nullptr;
		if (C && C->UI && Npc && Npc->HasStore())
			C->UI->OpenStore(Npc);
		else
			ChatStub(TEXT("Store"));
		return 0;
	}
	int L_openBank(lua_State* L) { ChatStub(TEXT("Bank")); return 0; }
	int L_openUpgrade(lua_State* L) { ChatStub(TEXT("Upgrade")); return 0; }
	int L_openSeparate(lua_State* L) { ChatStub(TEXT("Disassembly")); return 0; }
	int L_openDeliveryStore(lua_State* L) { ChatStub(TEXT("Delivery store")); return 0; }
	int L_repair(lua_State* L) { ChatStub(TEXT("Repair")); return 0; }
	int L_appraisal(lua_State* L) { ChatStub(TEXT("Appraisal")); return 0; }
	int L_organizeClan(lua_State* L) { ChatStub(TEXT("Clan registration")); return 0; }
	int L_disorganizeClan(lua_State* L) { ChatStub(TEXT("Clan dissolution")); return 0; }
	int L_setRevivePosition(lua_State* L)
	{
		FRoseChatLog::Add(FRoseChatLog::EKind::System, TEXT("Revive position saved."));
		return 0;
	}

	// ---- QF_: conversation control -----------------------------------------
	int L_closeCon(lua_State* L)
	{
		if (GSession) GSession->bClosed = true;
		return 0;
	}
	int L_ChangetalkName(lua_State* L)
	{
		if (GSession && lua_tostring(L, 1))
			GSession->NpcName = ANSI_TO_TCHAR(lua_tostring(L, 1));
		return 0;
	}
	int L_nop(lua_State* L) { return 0; }
}

void FRoseDialogSession::RegisterBuiltins()
{
	lua_baselibopen(L);
	lua_mathlibopen(L);
	lua_strlibopen(L);

	struct { const char* Name; lua_CFunction Fn; } Regs[] = {
		{ "QF_checkQuestCondition", L_checkQuestCondition },
		{ "QF_doQuestTrigger",      L_doQuestTrigger },
		{ "QF_findQuest",           L_findQuest },
		{ "QF_getQuestCount",       L_getQuestCount },
		{ "QF_getQuestID",          L_getQuestID },
		{ "QF_appendQuest",         L_appendQuest },
		{ "QF_deleteQuest",         L_deleteQuest },
		{ "QF_getQuestVar",         L_getQuestVar },
		{ "QF_getQuestSwitch",      L_getQuestSwitch },
		{ "QF_getEpisodeVAR",       L_getEpisodeVAR },
		{ "QF_getJobVAR",           L_getJobVAR },
		{ "QF_getPlanetVAR",        L_getPlanetVAR },
		{ "QF_getUnionVAR",         L_getUnionVAR },
		{ "QF_getUserSwitch",       L_getUserSwitch },
		{ "QF_getQuestItemQuantity", L_getQuestItemQuantity },
		{ "QF_getNpcQuestZeroVal",  L_getNpcQuestZeroVal },
		{ "QF_getEventOwner",       L_getEventOwner },
		{ "QF_gotoCon",             L_nop },
		{ "QF_beginCon",            L_nop },
		{ "QF_closeCon",            L_closeCon },
		{ "QF_ChangetalkImage",     L_nop },
		{ "QF_ChangetalkName",      L_ChangetalkName },
		{ "QF_NpcTalkinterfaceHide", L_nop },
		{ "QF_NpcTalkinterfaceView", L_nop },
		{ "QF_NpcHide",             L_nop },
		{ "QF_NpcView",             L_nop },
		{ "QF_EffectCallSelf",      L_nop },
		{ "QF_EffectCallNpc",       L_nop },
		{ "QF_MotionCallSelf",      L_nop },
		{ "QF_MotionCallNpc",       L_nop },
		{ "QF_CameraworkingSelf",   L_nop },
		{ "QF_CameraworkingNpc",    L_nop },
		{ "QF_CameraworkingPoint",  L_nop },
		{ "GF_getVariable",         L_getVariable },
		{ "GF_GetMotionUseFile",    L_GetMotionUseFile },
		{ "GF_SetMotion",           L_SetMotion },
		{ "GF_openStore",           L_openStore },
		{ "GF_openBank",            L_openBank },
		{ "GF_openUpgrade",         L_openUpgrade },
		{ "GF_openSeparate",        L_openSeparate },
		{ "GF_openDeliveryStore",   L_openDeliveryStore },
		{ "GF_repair",              L_repair },
		{ "GF_appraisal",           L_appraisal },
		{ "GF_organizeClan",        L_organizeClan },
		{ "GF_disorganizeClan",     L_disorganizeClan },
		{ "GF_setRevivePosition",   L_setRevivePosition },
	};
	for (const auto& R : Regs)
		lua_register(L, R.Name, R.Fn);

	// QF_Init's constant globals (lua_init.cpp:25): SV_ var indices +
	// ITEM_TYPE_* (datatype.h:439 — GEM aliases ETC).
	struct { const char* Name; int32 Value; } Vars[] = {
		{ "SV_SEX", 0 }, { "SV_BIRTH", 1 }, { "SV_CLASS", 2 }, { "SV_UNION", 3 },
		{ "SV_RANK", 4 }, { "SV_FAME", 5 }, { "SV_STR", 6 }, { "SV_DEX", 7 },
		{ "SV_INT", 8 }, { "SV_CON", 9 }, { "SV_CHA", 10 }, { "SV_SEN", 11 },
		{ "SV_EXP", 12 }, { "SV_LEVEL", 13 }, { "SV_POINT", 14 },
		{ "ITEM_TYPE_FACE_ITEM", 1 }, { "ITEM_TYPE_HELMET", 2 },
		{ "ITEM_TYPE_ARMOR", 3 }, { "ITEM_TYPE_GAUNTLET", 4 },
		{ "ITEM_TYPE_BOOTS", 5 }, { "ITEM_TYPE_KNAPSACK", 6 },
		{ "ITEM_TYPE_JEWEL", 7 }, { "ITEM_TYPE_WEAPON", 8 },
		{ "ITEM_TYPE_SUBWPN", 9 }, { "ITEM_TYPE_USE", 10 },
		{ "ITEM_TYPE_ETC", 11 }, { "ITEM_TYPE_GEM", 11 },
		{ "ITEM_TYPE_NATURAL", 12 },
	};
	for (const auto& V : Vars)
	{
		lua_pushnumber(L, V.Value);
		lua_setglobal(L, V.Name);
	}
}

// ─── session ─────────────────────────────────────────────────────────────────
FRoseDialogSession::~FRoseDialogSession()
{
	if (L) { lua_close(L); L = nullptr; }
	if (GSession == this) GSession = nullptr;
}

TSharedPtr<FRoseDialogSession> FRoseDialogSession::Start(const FString& ConStem,
	ARoseCharacter* InPlayer, ARoseNpc* InNpc, int32 EventIdx)
{
	TSharedPtr<FRoseDialogData> Data = FRoseDialogData::Load(ConStem);
	if (!Data.IsValid() || Data->Menus.Num() == 0 || Data->LuaChunk.Num() == 0)
		return nullptr;

	TSharedPtr<FRoseDialogSession> S = MakeShared<FRoseDialogSession>();
	S->Data = Data;
	S->Player = InPlayer;
	S->Npc = InNpc;
	if (!S->StartInternal(EventIdx))
		return nullptr;
	return S;
}

bool FRoseDialogSession::StartInternal(int32 EventIdx)
{
	L = lua_open(1024);
	if (!L) return false;

	GSession = this;
	RegisterBuiltins();

	// CEvent::Start — run the chunk (defines the script's functions)...
	const int Err = lua_dobuffer(L, (const char*)Data->LuaChunk.GetData(),
		Data->LuaChunk.Num(), "con");
	if (Err != 0)
	{
		UE_LOG(LogRoseDialog, Error, TEXT("dialog lua chunk error %d"), Err);
		return false;
	}

	// ...then either the indexed event function (on-dead etc.)...
	if (EventIdx >= 0 && EventIdx < Data->EventFuncs.Num())
	{
		if (Data->EventFuncs[EventIdx].IsEmpty())
			return false;
		const bool bOk = CallIntFunc(Data->EventFuncs[EventIdx]) >= 1;
		bClosed = true;   // event functions never open the window
		return bOk;
	}

	// ...or the click conversation: gate check + menu 0.
	if (!Data->GateCheck.IsEmpty() && CallIntFunc(Data->GateCheck) < 1)
		return false;

	if (!Conversation(0))
		return false;

	Revision++;
	return true;
}

int32 FRoseDialogSession::CallIntFunc(const FString& Func)
{
	if (Func.IsEmpty() || !L) return 1;
	GSession = this;

	lua_getglobal(L, TCHAR_TO_ANSI(*Func));
	if (!lua_isfunction(L, -1))
	{
		UE_LOG(LogRoseDialog, Warning, TEXT("dialog lua function '%s' missing"), *Func);
		lua_settop(L, 0);
		return 0;
	}
	lua_pushnumber(L, 1);   // hEvent handle token (lua_CallIntFUNC's ZZ_PARAM_INT this)
	const int Err = lua_call(L, 1, 1);
	if (Err != 0)
	{
		UE_LOG(LogRoseDialog, Warning, TEXT("dialog lua call '%s' error %d"), *Func, Err);
		lua_settop(L, 0);
		return 0;
	}
	const int32 R = lua_isnumber(L, -1) ? (int32)lua_tonumber(L, -1) : 0;
	lua_settop(L, 0);
	return R;
}

int32 FRoseDialogSession::Conversation(int32 MenuIdx)
{
	// CEvent::Conversation — a faithful walk of the menu's items.
	if (MenuIdx < 0 || !Data->Menus.IsValidIndex(MenuIdx))
		return 0;

	int32 ValidCount = 0;
	for (const FRoseDialogItem& It : Data->Menus[MenuIdx])
	{
		if (!It.Check.IsEmpty() && CallIntFunc(It.Check) < 1)
			continue;

		switch (It.Type)
		{
		case MsgClose:
		case MsgSelect:
		case MsgJump:
		{
			const int32 ItemIdx = ClickItems.Add(It);
			Options.Add({ ParseMessage(It.Text), ItemIdx });
			break;
		}
		case MsgNpcSay:
		case MsgNext:
			Options.Reset();               // Del_ClickITEMS + CloseQueryDlg
			SayText = ParseMessage(It.Text);
			Conversation(It.Child);        // typically fills the options
			break;
		}
		ValidCount++;
	}
	return ValidCount;
}

bool FRoseDialogSession::End()
{
	return !Data->GateClick.IsEmpty() && CallIntFunc(Data->GateClick) >= 1;
}

void FRoseDialogSession::ClickOption(int32 OptionIdx)
{
	if (bClosed || !Options.IsValidIndex(OptionIdx))
		return;
	const FRoseDialogItem It = ClickItems[Options[OptionIdx].ItemIdx];

	GSession = this;
	if (!It.Click.IsEmpty())
		CallIntFunc(It.Click);

	if (bClosed) { Revision++; return; }   // QF_closeCon from the click func

	// CEvent::Click_ITEM: descend; a dead end runs End() and maybe restarts.
	if (!Conversation(It.Child))
	{
		if (End() && Conversation(0))
		{
			Revision++;
			return;
		}
		bClosed = true;
	}
	Revision++;
}

void FRoseDialogSession::Close()
{
	bClosed = true;
	Revision++;
}

FString FRoseDialogSession::ParseMessage(const FString& Msg) const
{
	// CEvent::ParseMESSAGE — replace <TOKEN>s with live character values.
	ARoseCharacter* C = Player.Get();
	URoseSkillComponent* Sk = C ? C->FindComponentByClass<URoseSkillComponent>() : nullptr;

	FString Out;
	Out.Reserve(Msg.Len());
	for (int32 i = 0; i < Msg.Len(); )
	{
		if (Msg[i] != TEXT('<')) { Out.AppendChar(Msg[i++]); continue; }
		const int32 EndTok = Msg.Find(TEXT(">"), ESearchCase::CaseSensitive,
			ESearchDir::FromStart, i + 1);
		if (EndTok == INDEX_NONE) { Out.AppendChar(Msg[i++]); continue; }
		const FString Tok = Msg.Mid(i + 1, EndTok - i - 1);
		i = EndTok + 1;

		if (!C) continue;
		if (Tok == TEXT("NAME")) Out += TEXT("Adventurer");
		else if (Tok == TEXT("LEVEL")) Out.AppendInt(C->Level);
		else if (Tok == TEXT("MONEY") || Tok == TEXT("USER_MONEY")) Out.AppendInt(C->GetZuly());
		else if (Tok == TEXT("SEX")) Out += C->Gender == TEXT("Female") ? TEXT("Lady") : TEXT("Sir");
		else if (Tok == TEXT("JOB")) Out.AppendInt(Sk ? Sk->CurrentJob : 0);
		else if (Tok == TEXT("STR")) Out.AppendInt(C->Strength);
		else if (Tok == TEXT("DEX")) Out.AppendInt(C->Dexterity);
		else if (Tok == TEXT("INT")) Out.AppendInt(C->Intelligence);
		else if (Tok == TEXT("CON")) Out.AppendInt(C->Concentration);
		else if (Tok == TEXT("MAXHP")) Out.AppendInt(C->GetMaxHPStat());
		else if (Tok == TEXT("MAXMP")) Out.AppendInt(C->GetMaxMPStat());
		else if (Tok.StartsWith(TEXT("WORLD_RATE")) || Tok.StartsWith(TEXT("TOWN_RATE"))
			|| Tok.StartsWith(TEXT("ITEM_RATE"))) Out += TEXT("100");
		// EXP/POINT/FAME/RANK/UNION/REVIVE_ZONE: no systems yet → blank.
	}
	return Out;
}
