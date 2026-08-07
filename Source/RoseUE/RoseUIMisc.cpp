// ROSE misc windows — the MODERN quest journal (Q, SRoseModernWindow content),
// dlgsystem (working exit), dlgoption and a set of plain keyless shells
// (party/clan/help) so the HUD menu can toggle them by id.  Every other
// converted layout is reachable via the `RoseUI <dialog>` console command
// already.  Registered by RoseUIMisc_Register.
//
// dlgsystem authority: src/client/interface/dlgs (the in-game system menu) —
// EXIT GAME quits, CONTINUE closes, "goto select avatar" returns to selection.
#include "RoseUIManager.h"
#include "RoseUIWindow.h"
#include "RoseUIChat.h"          // FRoseChatLog (created by the chat agent; coded to its API)
#include "RoseCharacter.h"
#include "RoseDrops.h"           // RoseItemTypeToSlot (quest-item names)
#include "RoseQuest.h"           // live quest journal (Q window)
#include "RoseUITheme.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"

namespace
{
	// A centered single-line message overlay for the placeholder windows.
	TSharedPtr<SWidget> MakeCenteredText(const FString& Msg)
	{
		return SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot()
			.Anchors(FAnchors(0.5f, 0.5f))
			.Alignment(FVector2D(0.5f, 0.5f))
			.AutoSize(true)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Msg))
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
				.ColorAndOpacity(FSlateColor(FLinearColor::White))
				.AutoWrapText(true)
			];
	}

	// Live quest journal (MODERN — content for SRoseModernWindow, opened by
	// URoseUIManager::ToggleQuestJournal): one rounded row per active quest
	// slot with name + wrapped description; rebuilds whenever
	// URoseQuestComponent::QuestRevision changes.
	// The client's own dlgquest (350x484).  Two list boxes drive it: id 20 is
	// the quest LIST (selectable) and id 30 the selected quest's DESCRIPTION,
	// which the client wraps into fixed-width rows -- so we wrap it the same
	// way rather than handing it one long string.
	class SRoseQuestJournal : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRoseQuestJournal) {}
			SLATE_ARGUMENT(TWeakObjectPtr<URoseQuestComponent>, Quests)
			SLATE_ARGUMENT(TWeakObjectPtr<URoseUIManager>, Manager)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Quests = InArgs._Quests;
			Manager = InArgs._Manager;

			ChildSlot
			[
				SNew(SBox).WidthOverride(350.f).HeightOverride(484.f)
				[
					SAssignNew(Layout, SRoseUIWindow)
					.Dialog(TEXT("dlgquest"))
					.OnTextValue(FRoseUITextValue::CreateSP(this, &SRoseQuestJournal::QuestText))
					.OnRowCount(FRoseUICountValue::CreateSP(this, &SRoseQuestJournal::RowCount))
					.OnActivate(FOnRoseUIActivate::CreateSP(this, &SRoseQuestJournal::Activate))
					.OnClose(FSimpleDelegate::CreateLambda([this]() {
						if (Manager.IsValid())
							Manager->CloseModernWindow(TEXT("questjournal")); }))
				]
			];
			Refresh();
		}

		virtual void Tick(const FGeometry& G, const double T, const float Dt) override
		{
			SCompoundWidget::Tick(G, T, Dt);
			if (Quests.IsValid() && Quests->QuestRevision != SeenRevision)
				Refresh();
		}

	private:
		TWeakObjectPtr<URoseQuestComponent> Quests;
		TWeakObjectPtr<URoseUIManager> Manager;
		TSharedPtr<SRoseUIWindow> Layout;
		int32 SeenRevision = -1;
		int32 Selected = 0;                 // index into Active
		TArray<int32> Active;               // quest slot ids, in slot order
		TArray<FString> DescLines;          // wrapped description of Selected

		// dlgquest's control ids.
		static constexpr int32 kQuestList = 20;
		static constexpr int32 kDescList  = 30;
		// LISTBOX 30 is 235 wide at CHARWIDTH 6 -> ~39 characters per line.
		static constexpr int32 kDescCols  = 39;

		URoseQuestComponent* Q() const { return Quests.Get(); }

		const FRoseQuestRow* RowFor(int32 Index) const
		{
			URoseQuestComponent* C = Q();
			return (C && Active.IsValidIndex(Index)) ? C->GetQuestRow(Active[Index]) : nullptr;
		}

		FString NameFor(int32 Index) const
		{
			const FRoseQuestRow* R = RowFor(Index);
			if (R && !R->DisplayName.IsEmpty())
				return R->DisplayName;
			return Active.IsValidIndex(Index)
				? FString::Printf(TEXT("Quest %d"), Active[Index]) : FString();
		}

		void Refresh()
		{
			URoseQuestComponent* C = Q();
			SeenRevision = C ? C->QuestRevision : -1;

			Active.Reset();
			if (C)
				for (const FRoseQuestSlot& Slot : C->Slots)
					if (Slot.Id)
						Active.Add(Slot.Id);

			Selected = Active.IsValidIndex(Selected) ? Selected : 0;
			RewrapDescription();
		}

		/** Greedy word wrap — the client wraps the description into listbox rows
		 *  of CHARWIDTH cells, so a single auto-wrapping block would not line up
		 *  with the authored row pitch. */
		void RewrapDescription()
		{
			DescLines.Reset();
			const FRoseQuestRow* R = RowFor(Selected);
			if (!R)
				return;

			TArray<FString> Paragraphs;
			R->Description.ParseIntoArray(Paragraphs, TEXT("\n"), false);
			for (const FString& Para : Paragraphs)
			{
				if (Para.IsEmpty()) { DescLines.Add(FString()); continue; }
				TArray<FString> Words;
				Para.ParseIntoArray(Words, TEXT(" "), true);
				FString Line;
				for (const FString& W : Words)
				{
					if (!Line.IsEmpty() && Line.Len() + 1 + W.Len() > kDescCols)
					{
						DescLines.Add(Line);
						Line.Reset();
					}
					if (!Line.IsEmpty())
						Line += TEXT(" ");
					Line += W;
				}
				if (!Line.IsEmpty())
					DescLines.Add(Line);
			}
		}

		int32 RowCount(const FRoseUIKey& Key) const
		{
			if (Key.Id == kQuestList) return Active.Num();
			if (Key.Id == kDescList)  return DescLines.Num();
			return 0;
		}

		FText QuestText(const FRoseUIKey& Key, int32 Row) const
		{
			// List rows.
			if (Key.Id == kQuestList && Row >= 0)
				return Active.IsValidIndex(Row)
					? FText::FromString(NameFor(Row)) : FText::GetEmpty();
			if (Key.Id == kDescList && Row >= 0)
				return DescLines.IsValidIndex(Row)
					? FText::FromString(DescLines[Row]) : FText::GetEmpty();

			// Named fields.
			if (Key.Is(TEXT("CAPTION")))            return FText::FromString(TEXT("Quest"));
			if (Key.Is(TEXT("QUESTS_TXT")))         return FText::FromString(TEXT("Quests"));
			if (Key.Is(TEXT("DAILY_QUESTS_TXT")))   return FText::FromString(TEXT("Daily Quests"));
			if (Key.Is(TEXT("QUESTS_VALUE")))
				return FText::FromString(FString::Printf(TEXT("%d / 10"), Active.Num()));
			if (Key.Is(TEXT("DAILY_QUESTS_VALUE")))
				return FText::FromString(TEXT("0 / 5"));     // no daily quests yet
			if (Key.Is(TEXT("QUEST_NAME")))         return FText::FromString(NameFor(Selected));
			if (Key.Is(TEXT("TIME_VALUE")))         return FText::GetEmpty();
			if (Key.Is(TEXT("ABANDONQUEST_BTN")))   return FText::FromString(TEXT("Abandon Quest"));
			return FText::GetEmpty();
		}

		void Activate(const FRoseUIKey& Key, int32 Row)
		{
			if (Key.Id == kQuestList && Active.IsValidIndex(Row))
			{
				Selected = Row;
				RewrapDescription();
			}
		}
	};
}

// Modern quest journal content — wrapped in SRoseModernWindow by
// URoseUIManager::ToggleQuestJournal (Q key / HUD "Quest" button).
TSharedRef<SWidget> RoseQuestJournal_MakeContent(URoseUIManager& UI)
{
	ARoseCharacter* C = UI.GetRoseCharacter();
	return SNew(SRoseQuestJournal).Quests(C ? C->Quests : nullptr).Manager(&UI);
}

void RoseUIMisc_Register(URoseUIManager& UI)
{
	// quest (Q) is a MODERN window now — URoseUIManager::ToggleQuestJournal
	// (bound on the character + HUD "Quest" button); the classic dlgquest
	// layout stays reachable via `RoseUI dlgquest` only.

	// ── system (Z) — the in-game system menu; working exit/continue ──────────
	// dlgsystem.json buttons (label → wiring):
	//   "CONTINUE"           → close the system window (return to game)
	//   "goto select avatar" → not implemented (chat notice)
	//   "EXIT GAME"          → quit the game (PC ConsoleCommand "quit")
	//   "close"              → handled by SRoseUIWindow (close button)
	{
		// Z + the HUD "System" button now open the MODERN options window
		// (RoseUIOptions.cpp, URoseUIManager::ToggleOptions).  The classic
		// dlgsystem stays registered (reachable via `RoseUI dlgsystem`) but
		// without a toggle key so it doesn't double-bind Z.
		FRoseUIWindowDef Def;
		Def.Dialog = TEXT("dlgsystem");
		Def.Anchor = ERoseUIAnchor::Center;
		Def.Offset = FVector2D(0, 0);
		Def.OnButton = [](URoseUIManager& Mgr, const FString& Label)
		{
			// "option" button (if present) → toggle the options window.
			if (Label.Contains(TEXT("OPTION")))
			{
				Mgr.Toggle(TEXT("option"));
				return;
			}
			// CONTINUE / return-to-game → just close the system menu.
			if (Label.Contains(TEXT("CONTINUE")) || Label.Contains(TEXT("RETURN")))
			{
				Mgr.Close(TEXT("system"));
				return;
			}
			// EXIT / QUIT → quit the game via the player controller console.
			if (Label.Contains(TEXT("EXIT")) || Label.Contains(TEXT("QUIT")))
			{
				if (ARoseCharacter* C = Mgr.GetRoseCharacter())
					if (UWorld* W = C->GetWorld())
						if (APlayerController* PC = W->GetFirstPlayerController())
							PC->ConsoleCommand(TEXT("quit"));
				return;
			}
			// Everything else (select avatar / restart / …) → not implemented.
			FRoseChatLog::Add(FRoseChatLog::EKind::System,
				FString::Printf(TEXT("%s: not implemented"), *Label));
		};
		UI.RegisterWindow(TEXT("system"), MoveTemp(Def));
	}

	// ── option — shell; layout renders, only the close button is wired ───────
	// RADIOBUTTON/CHECKBOX nodes in dlgoption.json are cosmetic for now (the
	// SRoseUIWindow renderer draws their sprites; no live settings binding yet).
	{
		FRoseUIWindowDef Def;
		Def.Dialog = TEXT("dlgoption");
		Def.Offset = FVector2D(300, 100);
		// No ToggleKey — opened by the system menu / HUD menu by id.
		// No OnButton — the layout's close button closes via SRoseUIWindow.
		UI.RegisterWindow(TEXT("option"), MoveTemp(Def));
	}

	// ── keyless plain shells so the HUD menu can toggle by id ────────────────
	auto RegisterShell = [&UI](const TCHAR* Id, const TCHAR* Dialog)
	{
		FRoseUIWindowDef Def;
		Def.Dialog = Dialog;
		Def.Offset = FVector2D(260, 140);
		UI.RegisterWindow(FName(Id), MoveTemp(Def));
	};
	RegisterShell(TEXT("party"), TEXT("dlgparty"));
	RegisterShell(TEXT("clan"),  TEXT("dlgclan"));
	// No "help" shell: dlghelp is CLASSIC-only — the glass client has no
	// dlghelp.xml, so registering it pointed at a stale classic layout whose
	// UI07_/UI14_ sprites do not exist in the glass atlas (39 dead refs).
}
