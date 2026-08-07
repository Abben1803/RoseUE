// SRoseOptions — the modern options / system window (opened by Z or the HUD
// hamburger "System", wrapped in SRoseModernWindow by URoseUIManager::
// ToggleOptions).  Tabs: Game / Video / Sound / System.  The Game/Video/Sound
// rows are on/off toggles (local UI state — settings backend not wired yet); the
// System tab has Return-to-Game + Exit-Game actions (faithful to dlgsystem).
#include "RoseUIManager.h"
#include "RoseUIHelpers.h"
#include "RoseUIChat.h"
#include "RoseCharacter.h"
#include "RoseUITheme.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor kRow    = RoseTheme::Row;
	const FLinearColor kTabOn  = RoseTheme::TabOn;
	const FLinearColor kTabOff = RoseTheme::TabOff;
	const FLinearColor kOn     = RoseTheme::Green;
	const FLinearColor kOff    = RoseTheme::Off;
	const FLinearColor kBtn    = RoseTheme::Button;
	const FLinearColor kExit   = RoseTheme::Danger;
	const FLinearColor kAccent = RoseTheme::Accent;
	const FLinearColor kText   = RoseTheme::Text;
	const FLinearColor kTextDim= RoseTheme::TextDim;

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	const TCHAR* kOptTabs[4] = { TEXT("Game"), TEXT("Video"), TEXT("Sound"), TEXT("System") };
}

class SRoseOptions : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseOptions) {}
		SLATE_ARGUMENT(TWeakObjectPtr<URoseUIManager>, Manager)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Manager = InArgs._Manager;

		RowBrush   = MakeShared<FSlateRoundedBoxBrush>(kRow,    6.f);
		TabOnBrush = RoseUI::GlassTab(true);
		TabOffBrush= RoseUI::GlassTab(false);
		if (!TabOnBrush.IsValid())  TabOnBrush = MakeShared<FSlateRoundedBoxBrush>(kTabOn,  8.f, kAccent, 1.f);
		if (!TabOffBrush.IsValid()) TabOffBrush= MakeShared<FSlateRoundedBoxBrush>(kTabOff, 8.f);
		OnBrush    = MakeShared<FSlateRoundedBoxBrush>(kOn,    10.f);
		OffBrush   = MakeShared<FSlateRoundedBoxBrush>(kOff,   10.f);
		BtnBrush   = MakeShared<FSlateRoundedBoxBrush>(kBtn,    8.f, kAccent, 1.f);
		ExitBrush  = MakeShared<FSlateRoundedBoxBrush>(kExit,   8.f);

		// Default toggle states (cosmetic until a settings backend exists).
		Toggles.Add(TEXT("Damage Numbers"), true);
		Toggles.Add(TEXT("Auto-loot"),      true);
		Toggles.Add(TEXT("Camera Shake"),   false);
		Toggles.Add(TEXT("Fullscreen"),     true);
		Toggles.Add(TEXT("VSync"),          true);
		Toggles.Add(TEXT("Bloom"),          true);
		Toggles.Add(TEXT("Music"),          true);
		Toggles.Add(TEXT("Sound Effects"),  true);

		ChildSlot
		[
			SNew(SBox).WidthOverride(356.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ BuildTabs() ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)[ BuildBody() ]
			]
		];
	}

private:
	TSharedRef<SWidget> BuildTabs()
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 i = 0; i < 4; ++i)
			Row->AddSlot().FillWidth(1.f).Padding(i == 0 ? 0 : 4, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0))
				.OnClicked_Lambda([this, i]() { ActiveTab = i; return FReply::Handled(); })
				[
					SNew(SBox).HeightOverride(28.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[ SNew(SImage).Image_Lambda([this, i]() -> const FSlateBrush* {
							return (ActiveTab == i ? TabOnBrush : TabOffBrush).Get(); }) ]
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
						[ SNew(STextBlock).Font(Font(9, true))
						  .ColorAndOpacity_Lambda([this, i]() { return ActiveTab == i ? kText : kTextDim; })
						  .Text(FText::FromString(kOptTabs[i])) ]
					]
				]
			];
		return Row;
	}

	// A label + on/off pill toggle.
	TSharedRef<SWidget> ToggleRow(const FString& Label)
	{
		return SNew(SBox).HeightOverride(30.f).Padding(0, 0, 0, 5)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(RowBrush.Get()) ]
			+ SOverlay::Slot().Padding(10, 0).VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(kText).Text(FText::FromString(Label)) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0))
					.OnClicked_Lambda([this, Label]() { Toggles.FindOrAdd(Label) = !Toggles.FindOrAdd(Label); return FReply::Handled(); })
					[
						SNew(SBox).WidthOverride(46.f).HeightOverride(20.f)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()[ SNew(SImage).Image_Lambda([this, Label]() -> const FSlateBrush* {
								return (Toggles.FindRef(Label) ? OnBrush : OffBrush).Get(); }) ]
							+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
							[ SNew(STextBlock).Font(Font(7, true)).ColorAndOpacity(kText)
							  .Text_Lambda([this, Label]() { return FText::FromString(Toggles.FindRef(Label) ? TEXT("ON") : TEXT("OFF")); }) ]
						]
					]
				]
			]
		];
	}

	TSharedRef<SWidget> ActionButton(const FString& Label, bool bExit, TFunction<void()> OnClick)
	{
		// The glass skin has one plate colour, so Exit keeps its danger signal in
		// the LABEL rather than the plate — losing it entirely would be a
		// usability regression for "Exit Game".
		const FButtonStyle* Glass = RoseUI::GlassButton(RoseUI::EButtonKind::Action);
		const FLinearColor LabelColor = (Glass && bExit) ? RoseTheme::Danger : kText;
		return SNew(SButton)
			.ButtonStyle(Glass ? Glass : &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(FMargin(0))
			.OnClicked_Lambda([OnClick]() { if (OnClick) OnClick(); return FReply::Handled(); })
			[
				SNew(SBox).HeightOverride(34.f).Padding(0, 0, 0, 6)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[ SNew(SImage).Image((bExit ? ExitBrush : BtnBrush).Get())
					  .Visibility(Glass ? EVisibility::Collapsed : EVisibility::HitTestInvisible) ]
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[ SNew(STextBlock).Font(Font(11, true)).ColorAndOpacity(LabelColor)
					  .Text(FText::FromString(Label)) ]
				]
			];
	}

	TSharedRef<SWidget> BuildBody()
	{
		return SNew(SWidgetSwitcher)
			.WidgetIndex_Lambda([this]() { return ActiveTab; })
			+ SWidgetSwitcher::Slot()   // Game
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Damage Numbers")) ]
				+ SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Auto-loot")) ]
				+ SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Camera Shake")) ]
			]
			+ SWidgetSwitcher::Slot()   // Video
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Fullscreen")) ]
				+ SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("VSync")) ]
				+ SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Bloom")) ]
			]
			+ SWidgetSwitcher::Slot()   // Sound
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Music")) ]
				+ SVerticalBox::Slot().AutoHeight()[ ToggleRow(TEXT("Sound Effects")) ]
			]
			+ SWidgetSwitcher::Slot()   // System
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[ ActionButton(TEXT("Return to Game"), false, [this]() { if (Manager.IsValid()) Manager->ToggleOptions(); }) ]
				+ SVerticalBox::Slot().AutoHeight()
				[ ActionButton(TEXT("Exit Game"), true, [this]() { Quit(); }) ]
			];
	}

	void Quit()
	{
		if (Manager.IsValid())
			if (ARoseCharacter* C = Manager->GetRoseCharacter())
				if (UWorld* W = C->GetWorld())
					if (APlayerController* PC = W->GetFirstPlayerController())
						PC->ConsoleCommand(TEXT("quit"));
	}

	TWeakObjectPtr<URoseUIManager> Manager;
	int32 ActiveTab = 0;
	TMap<FString, bool> Toggles;
	TSharedPtr<FSlateBrush> RowBrush, TabOnBrush, TabOffBrush, OnBrush, OffBrush, BtnBrush, ExitBrush;
};

TSharedRef<SWidget> RoseOptions_MakeContent(URoseUIManager& UI)
{
	return SNew(SRoseOptions).Manager(&UI);
}
