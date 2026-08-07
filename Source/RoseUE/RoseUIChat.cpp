// ROSE chat — dlgchat with a scrollable message log + input editbox, backed by
// a process-global log (FRoseChatLog) any system can append to.  Registered by
// RoseUIChat_Register.
//
// Authority: the classic chat dialog is CChatDLG in
// src/client/interface/dlgs/chattingdlg.{h,cpp} — its layout (IID_EDITBOX=15,
// the ALL/WHISPER/… listboxes, the FILTER button) is the dlgchat.json we render.
// We collapse its six per-channel listboxes into one scroll log for the port.
// Colours approximate CChatDLG's palette (src/client/interface/it_mgr.cpp:120-129:
// c_dwChatColorAll=white, c_dwChatColorSystem=pale, c_dwChatColorError/Notice=
// red-ish, c_dwChatColorWhisper=greenish-pink).
#include "RoseUIChat.h"
#include "RoseUIManager.h"
#include "RoseUIHelpers.h"
#include "RoseUITheme.h"
#include "RoseCharacter.h"

#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"

// ── FRoseChatLog: game-thread-only ring buffer ──────────────────────────────
namespace
{
	constexpr int32 kMaxChatLines = 200;

	// File-static state — process-global, game thread only (no locking).
	TArray<FRoseChatLog::FLine>& ChatLines()
	{
		static TArray<FRoseChatLog::FLine> Lines;
		return Lines;
	}
	int32 GChatRevision = 0;
}

FLinearColor FRoseChatLog::ColorFor(EKind Kind)
{
	// FColor is (R,G,B,A); values from it_mgr.cpp's D3DCOLOR_ARGB constants.
	switch (Kind)
	{
	case EKind::Say:     return FLinearColor(FColor(255, 255, 255)); // c_dwChatColorAll
	case EKind::System:  return FLinearColor(FColor(255, 224, 229)); // c_dwChatColorSystem (pale)
	case EKind::Combat:  return FLinearColor(FColor(255, 120, 100)); // ~c_dwChatColorError/Notice (red-ish)
	case EKind::Whisper: return FLinearColor(FColor(255, 160, 210)); // pink (whisper accent)
	default:             return FLinearColor::White;
	}
}

void FRoseChatLog::Add(EKind Kind, const FString& Text)
{
	FLine L;
	L.Kind = Kind;
	L.Text = Text;
	L.Color = ColorFor(Kind);
	TArray<FLine>& Lines = ChatLines();
	Lines.Add(MoveTemp(L));
	if (Lines.Num() > kMaxChatLines)
		Lines.RemoveAt(0, Lines.Num() - kMaxChatLines, EAllowShrinking::No);
	++GChatRevision;
}

int32 FRoseChatLog::Revision()
{
	return GChatRevision;
}

void FRoseChatLog::GetLines(TArray<FLine>& Out)
{
	Out = ChatLines();
}

// ── SRoseChat: a modern always-on chat panel (rounded, channel tabs + log +
//    input), anchored bottom-left by the manager.  Not the classic dlgchat frame.
namespace
{
	// The ROSE channel tabs (screenshot order); each filters the log.  Only
	// All/System/Whisper have message kinds today — Trade/Party/Clan/Ally are
	// wired for when those systems land (they filter to an as-yet-unused kind).
	struct FChatTab { const TCHAR* Label; int32 Filter; };   // Filter -1 = all, else EKind
	const FChatTab kChatTabs[] = {
		{ TEXT("All"),     -1 },
		{ TEXT("Whisper"), (int32)FRoseChatLog::EKind::Whisper },
		{ TEXT("Trade"),   100 },   // no kind yet
		{ TEXT("Party"),   101 },
		{ TEXT("Clan"),    102 },
		{ TEXT("Ally"),    103 },
	};

	// File-static handle to the live input box so the manager (Enter key) can
	// focus it without exposing the widget class.
	TWeakPtr<SEditableTextBox> GChatInput;

	class SRoseChat : public SCompoundWidget
	{
	public:
		SLATE_BEGIN_ARGS(SRoseChat) {}
			SLATE_ARGUMENT(TWeakObjectPtr<URoseUIManager>, Manager)
		SLATE_END_ARGS()

		void Construct(const FArguments& InArgs)
		{
			Manager = InArgs._Manager;

			PanelBrush = RoseUI::GlassPanel(RoseUI::EPanelKind::Window);
			if (!PanelBrush.IsValid())
				PanelBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(RoseTheme::Panel.R, RoseTheme::Panel.G, RoseTheme::Panel.B, 0.80f), 10.f,
					FLinearColor(RoseTheme::Accent.R, RoseTheme::Accent.G, RoseTheme::Accent.B, 0.3f), 1.f);
			InputBg    = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::Track, 6.f);
			TabOnBrush = RoseUI::GlassTab(true);
			TabOffBrush= RoseUI::GlassTab(false);
			if (!TabOnBrush.IsValid())  TabOnBrush = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::TabOn, 6.f);
			if (!TabOffBrush.IsValid()) TabOffBrush= MakeShared<FSlateRoundedBoxBrush>(RoseTheme::TabOff, 6.f);

			ChildSlot
			[
				SNew(SBox).WidthOverride(430.f).HeightOverride(210.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ SNew(SImage).Image(PanelBrush.Get()) ]
					+ SOverlay::Slot().Padding(6.f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight()[ BuildTabs() ]
						+ SVerticalBox::Slot().FillHeight(1.f).Padding(0, 4, 0, 0)
						[ SAssignNew(LogScroll, SScrollBox).Orientation(Orient_Vertical) ]
						+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
						[
							SNew(SBox).HeightOverride(24.f)
							[
								SNew(SOverlay)
								+ SOverlay::Slot()[ SNew(SImage).Image(InputBg.Get()) ]
								+ SOverlay::Slot().Padding(4, 0)
								[
									SAssignNew(InputBox, SEditableTextBox)
									.HintText(NSLOCTEXT("RoseChat", "Hint", "Enter to chat, /cmd for console"))
									.OnTextCommitted(this, &SRoseChat::OnCommit)
								]
							]
						]
					]
				]
			];

			GChatInput = InputBox;
			RebuildLog();
		}

		virtual void Tick(const FGeometry& Geo, const double Time, const float Delta) override
		{
			SCompoundWidget::Tick(Geo, Time, Delta);
			const int32 Rev = FRoseChatLog::Revision();
			if (Rev != LastRevision || ActiveTab != LastTab)
			{
				RebuildLog();
				if (LogScroll.IsValid())
					LogScroll->ScrollToEnd();
			}
		}

	private:
		TSharedRef<SWidget> BuildTabs()
		{
			TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
			for (int32 i = 0; i < UE_ARRAY_COUNT(kChatTabs); ++i)
				Row->AddSlot().FillWidth(1.f).Padding(i == 0 ? 0 : 3, 0, 0, 0)
				[
					SNew(SButton)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0))
					.OnClicked_Lambda([this, i]() { ActiveTab = i; return FReply::Handled(); })
					[
						SNew(SBox).HeightOverride(20.f)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()[ SNew(SImage).Image_Lambda([this, i]() -> const FSlateBrush* {
								return (ActiveTab == i ? TabOnBrush : TabOffBrush).Get(); }) ]
							+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
							[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
							  .ColorAndOpacity_Lambda([this, i]() {
								return ActiveTab == i ? FLinearColor(0.96f,0.95f,0.9f) : FLinearColor(0.65f,0.67f,0.72f); })
							  .Text(FText::FromString(kChatTabs[i].Label)) ]
						]
					]
				];
			return Row;
		}

		void RebuildLog()
		{
			LastRevision = FRoseChatLog::Revision();
			LastTab = ActiveTab;
			if (!LogScroll.IsValid())
				return;
			LogScroll->ClearChildren();

			TArray<FRoseChatLog::FLine> Lines;
			FRoseChatLog::GetLines(Lines);
			const int32 Filter = kChatTabs[FMath::Clamp(ActiveTab, 0, (int32)UE_ARRAY_COUNT(kChatTabs) - 1)].Filter;

			FSlateFontInfo LineFont = FCoreStyle::GetDefaultFontStyle("Regular", 9);
			for (const FRoseChatLog::FLine& L : Lines)
			{
				if (Filter != -1 && (int32)L.Kind != Filter)
					continue;
				LogScroll->AddSlot().Padding(FMargin(2, 0, 2, 1))
				[
					SNew(STextBlock).Text(FText::FromString(L.Text))
					.Font(LineFont).ColorAndOpacity(FSlateColor(L.Color)).AutoWrapText(true)
				];
			}
			LogScroll->ScrollToEnd();
		}

		void OnCommit(const FText& Text, ETextCommit::Type CommitType)
		{
			if (CommitType != ETextCommit::OnEnter)
				return;
			const FString Line = Text.ToString().TrimStartAndEnd();
			if (!Line.IsEmpty())
			{
				if (Line.StartsWith(TEXT("/")))
				{
					const FString Cmd = Line.RightChop(1);
					if (Manager.IsValid())
						if (ARoseCharacter* C = Manager->GetRoseCharacter())
							if (UWorld* W = C->GetWorld())
								if (APlayerController* PC = W->GetFirstPlayerController())
									PC->ConsoleCommand(Cmd);
				}
				else
				{
					FRoseChatLog::Add(FRoseChatLog::EKind::Say,
						FString::Printf(TEXT("Rose Dev: %s"), *Line));
				}
			}
			if (InputBox.IsValid())
				InputBox->SetText(FText::GetEmpty());
			// Return control to the game (Enter opens chat again).
			FSlateApplication::Get().SetAllUserFocusToGameViewport();
		}

		TWeakObjectPtr<URoseUIManager> Manager;
		TSharedPtr<SScrollBox> LogScroll;
		TSharedPtr<SEditableTextBox> InputBox;
		int32 LastRevision = -1;
		int32 ActiveTab = 0;
		int32 LastTab = -1;
		TSharedPtr<FSlateBrush> PanelBrush, InputBg, TabOnBrush, TabOffBrush;
	};
}

// Focus the chat input (bound to Enter on the character).
void RoseChat_FocusInput()
{
	if (TSharedPtr<SEditableTextBox> Box = GChatInput.Pin())
		FSlateApplication::Get().SetKeyboardFocus(Box);
}

TSharedRef<SWidget> RoseChat_Make(URoseUIManager& UI)
{
	return SNew(SRoseChat).Manager(&UI);
}

// ── Registration ────────────────────────────────────────────────────────────
void RoseUIChat_Register(URoseUIManager& UI)
{
	// Seed the log with a welcome + key reference + console hint.  The modern
	// chat panel itself is created as an always-on overlay by the UI manager
	// (URoseUIManager::BeginPlay) — no classic dlgchat window is registered.
	FRoseChatLog::Add(FRoseChatLog::EKind::System, TEXT("Welcome to ROSE (UE5 port)"));
	FRoseChatLog::Add(FRoseChatLog::EKind::System,
		TEXT("I inventory · C character · K skills · X skill tree · M minimap · Q quest · Enter chat"));
	FRoseChatLog::Add(FRoseChatLog::EKind::System,
		TEXT("Type /<command> for console (e.g. /RoseSetJob 111)"));
}
