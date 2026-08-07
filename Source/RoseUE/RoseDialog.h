// ROSE NPC conversation runtime — a faithful port of the client's CEvent
// (src/client/event/cevent.cpp) running the ORIGINAL Lua 4.0 bytecode chunks
// embedded in the CON files (Content/Dialogs/<STEM>.json via
// tools/gen_dialog_data.py; interpreter = Source/RoseUE/lua4).
//
// One FRoseDialogSession = one open conversation: it owns a lua_State with the
// chunk loaded and the QF_/GF_ builtins registered (lua_init.cpp QF_Init
// subset the CON scripts actually call), and walks the menu tree exactly like
// CEvent::Start/Conversation/Click_ITEM/End.  The UI (RoseUIDialog.cpp) just
// renders SayText + Options and forwards clicks.
#pragma once

#include "CoreMinimal.h"

class ARoseCharacter;
class ARoseNpc;
struct lua_State;

// One menu entry (tagSCRIPTITEM).
struct FRoseDialogItem
{
	int32 Type = 0;        // SC_MSG_*: 0 close, 1 next, 2 npc-say, 3/4 select
	int32 Child = -1;      // child menu index
	FString Check;         // Lua gate function ("" = always shown)
	FString Click;         // Lua click function
	FString Text;          // resolved English text (may hold <NAME> tokens)
};

// One parsed CON file (shared, cached by stem).
struct FRoseDialogData
{
	TArray<FString> EventFuncs;             // 16 event-index functions
	FString GateCheck, GateClick;           // messages[0] check/click
	TArray<TArray<FRoseDialogItem>> Menus;
	TArray<uint8> LuaChunk;                 // Lua 4.0 bytecode

	static TSharedPtr<FRoseDialogData> Load(const FString& Stem);
};

class ROSEUE_API FRoseDialogSession : public TSharedFromThis<FRoseDialogSession>
{
public:
	~FRoseDialogSession();

	// Begin a conversation (CEventLIST::Run_EVENT + CEvent::Start).  EventIdx
	// -1 = the normal click conversation; 0..15 fires that event function only
	// (e.g. QUEST_EVENT_ON_DEAD).  Returns null when the gate fails.
	static TSharedPtr<FRoseDialogSession> Start(const FString& ConStem,
		ARoseCharacter* Player, ARoseNpc* Npc, int32 EventIdx = -1);

	// UI state.
	FString NpcName;                        // title (QF_ChangetalkName can override)
	FString SayText;                        // current NPC message
	struct FOption { FString Text; int32 ItemIdx; };
	TArray<FOption> Options;                // click targets (types 0/3/4)
	bool bClosed = false;
	int32 Revision = 0;                     // bumped whenever the state changes

	// CEvent::Click_ITEM for Options[OptionIdx].
	void ClickOption(int32 OptionIdx);
	void Close();

	ARoseCharacter* GetPlayer() const { return Player.Get(); }
	ARoseNpc* GetNpc() const { return Npc.Get(); }

private:
	TSharedPtr<FRoseDialogData> Data;
	lua_State* L = nullptr;
	TWeakObjectPtr<ARoseCharacter> Player;
	TWeakObjectPtr<ARoseNpc> Npc;
	// Items added by the CURRENT Conversation pass, indexed by FOption::ItemIdx.
	TArray<FRoseDialogItem> ClickItems;

	bool StartInternal(int32 EventIdx);
	int32 Conversation(int32 MenuIdx);      // returns the valid-item count
	bool End();                             // gateClick — true = restart at menu 0
	int32 CallIntFunc(const FString& Func); // lua_CallIntFUNC(name, handle)
	FString ParseMessage(const FString& Msg) const;   // <NAME>/<LEVEL>/... tokens
	void RegisterBuiltins();
};
