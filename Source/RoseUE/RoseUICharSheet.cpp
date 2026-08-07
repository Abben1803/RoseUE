// SRoseCharSheet — the modern character sheet content (opened by C / the HUD
// hamburger "Char", wrapped in SRoseModernWindow by ARoseCharacter::
// ToggleCharacterSheet).  Styled after the reference iROSE sheet: HP/MP/Exp bars
// on top, a left column of base stats (STR/DEX/INT/CON/CHARM/SENSE) each with a
// raise "+" and its point cost, a right column of derived stats
// (Attack/Defence/Magic Def/Accuracy/Critical/Dodge/A.Spd/M.Spd), and an SP
// footer.  Reads everything live from ARoseCharacter; the "+" calls RaiseStat.
#include "RoseCharacter.h"
#include "RoseExpData.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "RoseUIWindow.h"
#include "RoseSkillComponent.h"
#include "RoseUITheme.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor kTrack  = RoseTheme::Track;
	const FLinearColor kRow    = RoseTheme::Row;
	const FLinearColor kHP     = RoseTheme::HP;
	const FLinearColor kMP     = RoseTheme::MP;
	const FLinearColor kEXP    = RoseTheme::EXP;
	const FLinearColor kGreen  = RoseTheme::Green;
	const FLinearColor kAccent = RoseTheme::Accent;
	const FLinearColor kText   = RoseTheme::Text;
	const FLinearColor kTextDim= RoseTheme::TextDim;
	const FLinearColor kBadge  = RoseTheme::PanelHi;

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}
}

class SRoseCharSheet : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseCharSheet) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARoseCharacter>, Char)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		CharWeak = InArgs._Char;

		TrackBrush = MakeShared<FSlateRoundedBoxBrush>(kTrack, 6.f);
		HPFill     = MakeShared<FSlateRoundedBoxBrush>(kHP,    6.f);
		MPFill     = MakeShared<FSlateRoundedBoxBrush>(kMP,    6.f);
		EXPFill    = MakeShared<FSlateRoundedBoxBrush>(kEXP,   4.f);
		RowBrush   = MakeShared<FSlateRoundedBoxBrush>(kRow,   6.f);
		BadgeBrush = MakeShared<FSlateRoundedBoxBrush>(kBadge, 5.f);
		PlusBrush  = MakeShared<FSlateRoundedBoxBrush>(kGreen, 5.f);

		// The client's own dlgavata (269x329): both tabs, all the gauges and
		// every label/value at ROSE's coordinates.  Only the six "+" buttons are
		// ours, overlaid on the authored GEN_BTN04 rects -- they carry no name
		// in the XML, so the sprite is what identifies them, and their ORDER is
		// the stat order (STR, DEX, INT, CON, CRM, SEN).
		Overlay = SNew(SConstraintCanvas)
			.Visibility(EVisibility::SelfHitTestInvisible);

		ChildSlot
		[
			// Sized by the layout's own frame (dlgavata's root height is stale).
			// Live layer INSIDE the window (Content), not beside it — a sibling
			// overlay does not inherit the window's drag transform and reads as
			// a stray second copy floating over the world.
			SNew(SBox).WidthOverride(269.f).HeightOverride(416.f)
			[
				SAssignNew(Layout, SRoseUIWindow)
				.Dialog(TEXT("dlgavata"))
				.InitialTab(TEXT("CHARACTER_TBTN"))
				.Content(Overlay)
				.OnGaugeValue(FRoseUIGaugeValue::CreateSP(this, &SRoseCharSheet::SheetGauge))
				.OnTextValue(FRoseUITextValue::CreateSP(this, &SRoseCharSheet::SheetText))
				.OnClose(FSimpleDelegate::CreateLambda([this]() {
					if (ARoseCharacter* C = Char()) C->ToggleCharacterSheet(); }))
			]
		];

		BuildPlusButtons();
	}

	/** The six stat-raise buttons, at the layout's own GEN_BTN04 rects. */
	void BuildPlusButtons()
	{
		if (!Layout.IsValid() || !Overlay.IsValid())
			return;
		static const TCHAR* kStats[] = { TEXT("STR"), TEXT("DEX"), TEXT("INT"),
		                                 TEXT("CON"), TEXT("CHA"), TEXT("SEN") };
		const TArray<FSlateRect>* Rects = Layout->GetSpriteRects(TEXT("GEN_BTN04"));
		if (!Rects)
			return;
		for (int32 i = 0; i < Rects->Num() && i < UE_ARRAY_COUNT(kStats); ++i)
		{
			const FSlateRect& R = (*Rects)[i];
			const FString Stat = kStats[i];
			Overlay->AddSlot()
				.Offset(FMargin(R.Left, R.Top, R.GetSize().X, R.GetSize().Y))
				.Alignment(FVector2D(0, 0))
				[
					SNew(SButton)
					.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
					.ContentPadding(FMargin(0))
					.ToolTipText(FText::FromString(FString::Printf(TEXT("Raise %s"), *Stat)))
					.OnClicked_Lambda([this, Stat]() {
						if (ARoseCharacter* C = Char()) C->RaiseStat(Stat);
						return FReply::Handled(); })
				];
		}
	}

	int32 StatOf(const FRoseUIKey& Key) const
	{
		ARoseCharacter* C = Char();
		if (!C) return 0;
		if (Key.Name.StartsWith(TEXT("STR"))) return C->Strength;
		if (Key.Name.StartsWith(TEXT("DEX"))) return C->Dexterity;
		if (Key.Name.StartsWith(TEXT("INT"))) return C->Intelligence;
		if (Key.Name.StartsWith(TEXT("CON"))) return C->Concentration;
		if (Key.Name.StartsWith(TEXT("CRM"))) return C->Charm;
		if (Key.Name.StartsWith(TEXT("SEN"))) return C->Sense;
		return 0;
	}

	float SheetGauge(const FRoseUIKey& Key) const
	{
		ARoseCharacter* C = Char();
		if (!C) return 0.f;
		if (Key.Is(TEXT("HP_GUAGE")))
			return C->CurrentHP / FMath::Max(1.f, (float)C->GetMaxHPStat());
		if (Key.Is(TEXT("MP_GUAGE")))
			return C->CurrentMP / FMath::Max(1.f, (float)C->GetMaxMPStat());
		if (Key.Is(TEXT("EXP_GUAGE")))
			return C->GetExpFraction();
		return 0.f;
	}

	/** Every string dlgavata asks for.  The *_TXT names are static labels the
	 *  client supplies in code; *_VALUE and the bare stat names are live. */
	FText SheetText(const FRoseUIKey& Key, int32 Row) const
	{
		ARoseCharacter* C = Char();
		if (!C)
			return FText::GetEmpty();

		// Static labels.
		if (Key.Is(TEXT("CAPTION")))  return FText::FromString(C->GetDisplayName());
		if (Key.Is(TEXT("CLAN_TXT"))) return FText::FromString(TEXT("Clan"));
		if (Key.Is(TEXT("LV_TXT")))   return FText::FromString(TEXT("LVL"));
		if (Key.Is(TEXT("JOB_TXT")))  return FText::FromString(TEXT("Job"));
		if (Key.Is(TEXT("EXP")))      return FText::FromString(TEXT("EXP"));
		if (Key.Is(TEXT("HP")))       return FText::FromString(TEXT("HP"));
		if (Key.Is(TEXT("MP")))       return FText::FromString(TEXT("MP"));
		if (Key.Is(TEXT("CHARACTER_TBTN"))) return FText::FromString(TEXT("Character"));
		if (Key.Is(TEXT("CURRENCY_TBTN")))  return FText::FromString(TEXT("Currency"));

		// Header values.
		if (Key.Is(TEXT("CLAN_NAME"))) return FText::GetEmpty();   // no clan system yet
		if (Key.Is(TEXT("LV")))        return FText::AsNumber(C->Level);
		if (Key.Is(TEXT("JOB")))       return FText::FromString(C->GetJobName());

		// Gauge readouts.
		auto Pair = [](float Cur, int32 Max) {
			return FText::FromString(FString::Printf(TEXT("%d/%d"), (int32)Cur, Max)); };
		if (Key.Is(TEXT("HP_GUAGE")))  return Pair(C->CurrentHP, C->GetMaxHPStat());
		if (Key.Is(TEXT("MP_GUAGE")))  return Pair(C->CurrentMP, C->GetMaxMPStat());
		if (Key.Is(TEXT("EXP_GUAGE")))
			// current / needed-for-this-level: the same pair GetExpFraction
			// divides (RoseExp::NeedExp(Level)).
			return FText::FromString(FString::Printf(TEXT("%lld/%lld"),
				(long long)C->GetExp(), (long long)RoseExp::NeedExp(C->Level)));

		// Base stats: NAME is the label, NAME_VALUE the number, NAME_POINT the
		// cost to raise it (ROSE charges floor(value/5), min 1).
		static const TCHAR* kStatNames[] = { TEXT("STR"), TEXT("DEX"), TEXT("INT"),
		                                     TEXT("CON"), TEXT("CRM"), TEXT("SEN") };
		for (const TCHAR* N : kStatNames)
		{
			if (Key.Is(N))
			{
				// The layout keys Charm as CRM; ROSE labels it CHA.
				return FText::FromString(FString(N).Equals(TEXT("CRM"))
					? TEXT("CHA") : N);
			}
			if (Key.Name.Equals(FString(N) + TEXT("_VALUE"), ESearchCase::IgnoreCase))
				return FText::AsNumber(StatOf(Key));
			if (Key.Name.Equals(FString(N) + TEXT("_POINT"), ESearchCase::IgnoreCase))
				return FText::AsNumber(FMath::Max(1, StatOf(Key) / 5));
		}

		// Derived block, right-hand column.
		if (Key.Is(TEXT("ATK")))       return FText::FromString(TEXT("Attack"));
		if (Key.Is(TEXT("DEF")))       return FText::FromString(TEXT("Defense"));
		if (Key.Is(TEXT("RES")))       return FText::FromString(TEXT("Magic Def"));
		if (Key.Is(TEXT("HIT")))       return FText::FromString(TEXT("Accuracy"));
		if (Key.Is(TEXT("CRI")))       return FText::FromString(TEXT("Critical"));
		if (Key.Is(TEXT("AVO")))       return FText::FromString(TEXT("Dodge"));
		if (Key.Is(TEXT("ATS")))       return FText::FromString(TEXT("ASPD"));
		if (Key.Is(TEXT("MOV")))       return FText::FromString(TEXT("MSPD"));
		if (Key.Is(TEXT("ATK_VALUE"))) return FText::AsNumber(C->GetAttackPowerStat());
		if (Key.Is(TEXT("DEF_VALUE"))) return FText::AsNumber(C->GetDefenseStat());
		if (Key.Is(TEXT("RES_VALUE"))) return FText::AsNumber(C->GetResistStat());
		if (Key.Is(TEXT("HIT_VALUE"))) return FText::AsNumber(C->GetHitStat());
		if (Key.Is(TEXT("CRI_VALUE"))) return FText::AsNumber(C->GetCritStat());
		if (Key.Is(TEXT("AVO_VALUE"))) return FText::AsNumber(C->GetAvoidStat());
		if (Key.Is(TEXT("ATS_VALUE")))
			return FText::FromString(FString::Printf(TEXT("%.2f/s"),
				C->GetAttackSpeedStat() / 100.f));
		if (Key.Is(TEXT("MOV_VALUE"))) return FText::AsNumber((int32)C->GetRunSpeedStat());
		if (Key.Is(TEXT("POINT")))     return FText::AsNumber(C->Skills ? C->Skills->SkillPoints : 0);

		// Currency tab: fixed rows of icon / name / value.
		if (Key.Name.StartsWith(TEXT("CURRENCY_NAME_")))
		{
			static const TCHAR* kNames[] = { TEXT("Zulie"), TEXT("Honor"), TEXT("Valor"),
			                                 TEXT("Premium"), TEXT("Kill Points"),
			                                 TEXT("Event Points") };
			const int32 i = FCString::Atoi(*Key.Name.RightChop(14));
			return (i >= 0 && i < UE_ARRAY_COUNT(kNames))
				? FText::FromString(kNames[i]) : FText::GetEmpty();
		}
		if (Key.Name.StartsWith(TEXT("CURRENCY_VALUE_")))
		{
			const int32 i = FCString::Atoi(*Key.Name.RightChop(15));
			// Only Zulie is a real currency so far; the rest read 0 rather than
			// being hidden, which is what the client does for unearned ones.
			return i == 0 ? FText::AsNumber(C->GetZuly()) : FText::AsNumber(0);
		}
		return FText::GetEmpty();
	}

private:
	ARoseCharacter* Char() const { return CharWeak.Get(); }

	// A labeled bar: name + rounded track/fill (fixed width) + centered "cur / max".
	TSharedRef<SWidget> Bar(const FString& Label, TSharedPtr<FSlateBrush> Fill,
	                        TFunction<float()> Pct, TFunction<FString()> Value)
	{
		const float BarW = 274.f;
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ SNew(SBox).WidthOverride(30.f)
			  [ SNew(STextBlock).Font(Font(10, true)).ColorAndOpacity(kTextDim)
			    .Text(FText::FromString(Label)) ] ]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SBox).WidthOverride(BarW).HeightOverride(18.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ SNew(SImage).Image(TrackBrush.Get()) ]
					+ SOverlay::Slot().HAlign(HAlign_Left)
					[
						SNew(SBox).HeightOverride(18.f).Clipping(EWidgetClipping::ClipToBounds)
						.WidthOverride(TAttribute<FOptionalSize>::CreateLambda(
							[Pct, BarW]() { return FOptionalSize(FMath::Clamp(Pct(), 0.f, 1.f) * BarW); }))
						[ SNew(SImage).Image(Fill.Get()) ]
					]
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[ SNew(STextBlock).Font(Font(9, true)).ColorAndOpacity(kText)
					  .Text_Lambda([Value]() { return FText::FromString(Value()); }) ]
				]
			];
	}

	// Base-stat row: [cost] [+] NAME  value.
	TSharedRef<SWidget> StatRow(const FString& Name, TFunction<int32()> Get)
	{
		return SNew(SBox).HeightOverride(24.f).Padding(0, 0, 0, 3)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(RowBrush.Get()) ]
			+ SOverlay::Slot().Padding(4, 0).VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				// Point cost badge (ROSE: raising a stat costs floor(value/5)).
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ SNew(SBox).WidthOverride(22.f)
				  [ SNew(STextBlock).Font(Font(8)).ColorAndOpacity(kTextDim)
				    .Justification(ETextJustify::Center)
				    .Text_Lambda([Get]() { return FText::AsNumber(FMath::Max(1, Get() / 5)); }) ] ]
				// "+" raise button.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(2, 0)
				[
					SNew(SButton)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0))
					.ToolTipText(FText::FromString(FString::Printf(TEXT("Raise %s"), *Name)))
					.OnClicked_Lambda([this, Name]() { if (ARoseCharacter* C = Char()) C->RaiseStat(Name); return FReply::Handled(); })
					[ SNew(SBox).WidthOverride(16.f).HeightOverride(16.f)
					  [ SNew(SOverlay)
					    + SOverlay::Slot()[ SNew(SImage).Image(PlusBrush.Get()) ]
					    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					    [ SNew(STextBlock).Font(Font(11, true)).ColorAndOpacity(kText)
					      .Text(FText::FromString(TEXT("+"))) ] ] ]
				]
				// Name.
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(4, 0, 0, 0)
				[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(kText)
				  .Text(FText::FromString(Name)) ]
				// Value.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ SNew(STextBlock).Font(Font(10, true)).ColorAndOpacity(kText)
				  .Text_Lambda([Get]() { return FText::AsNumber(Get()); }) ]
			]
		];
	}

	TSharedRef<SWidget> BuildBaseColumn()
	{
		return SNew(SBox).WidthOverride(140.f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ StatRow(TEXT("STR"),   [this]() { ARoseCharacter* C = Char(); return C ? C->Strength : 0; }) ]
			+ SVerticalBox::Slot().AutoHeight()[ StatRow(TEXT("DEX"),   [this]() { ARoseCharacter* C = Char(); return C ? C->Dexterity : 0; }) ]
			+ SVerticalBox::Slot().AutoHeight()[ StatRow(TEXT("INT"),   [this]() { ARoseCharacter* C = Char(); return C ? C->Intelligence : 0; }) ]
			+ SVerticalBox::Slot().AutoHeight()[ StatRow(TEXT("CON"),   [this]() { ARoseCharacter* C = Char(); return C ? C->Concentration : 0; }) ]
			+ SVerticalBox::Slot().AutoHeight()[ StatRow(TEXT("CHARM"), [this]() { ARoseCharacter* C = Char(); return C ? C->Charm : 0; }) ]
			+ SVerticalBox::Slot().AutoHeight()[ StatRow(TEXT("SENSE"), [this]() { ARoseCharacter* C = Char(); return C ? C->Sense : 0; }) ]
		];
	}

	// Derived row: NAME .... value (as text so we can format A.Spd / M.Spd).
	TSharedRef<SWidget> DerivedRow(const FString& Name, TFunction<FString()> Get)
	{
		return SNew(SBox).HeightOverride(24.f).Padding(0, 0, 0, 3)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
			[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(kTextDim)
			  .Text(FText::FromString(Name)) ]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ SNew(STextBlock).Font(Font(10, true)).ColorAndOpacity(kText)
			  .Text_Lambda([Get]() { return FText::FromString(Get()); }) ]
		];
	}

	TSharedRef<SWidget> BuildDerivedColumn()
	{
		auto N = [](TFunction<int32()> F) { return [F]() { return FString::FromInt(F()); }; };
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ DerivedRow(TEXT("Attack"),    N([this]() { ARoseCharacter* C = Char(); return C ? C->GetAttackPowerStat() : 0; })) ]
			+ SVerticalBox::Slot().AutoHeight()[ DerivedRow(TEXT("Defence"),   N([this]() { ARoseCharacter* C = Char(); return C ? C->GetDefenseStat() : 0; })) ]
			+ SVerticalBox::Slot().AutoHeight()[ DerivedRow(TEXT("Magic Def"), N([this]() { ARoseCharacter* C = Char(); return C ? C->GetResistStat() : 0; })) ]
			+ SVerticalBox::Slot().AutoHeight()[ DerivedRow(TEXT("Accuracy"),  N([this]() { ARoseCharacter* C = Char(); return C ? C->GetHitStat() : 0; })) ]
			+ SVerticalBox::Slot().AutoHeight()[ DerivedRow(TEXT("Critical"),  N([this]() { ARoseCharacter* C = Char(); return C ? C->GetCritStat() : 0; })) ]
			+ SVerticalBox::Slot().AutoHeight()[ DerivedRow(TEXT("Dodge"),     N([this]() { ARoseCharacter* C = Char(); return C ? C->GetAvoidStat() : 0; })) ]
			+ SVerticalBox::Slot().AutoHeight()[ DerivedRow(TEXT("A.Spd"),
				[this]() { ARoseCharacter* C = Char(); return C ? FString::Printf(TEXT("%.2f/s"), C->GetAttackSpeedStat() / 100.f) : FString(); }) ]
			+ SVerticalBox::Slot().AutoHeight()[ DerivedRow(TEXT("M.Spd"),
				[this]() { ARoseCharacter* C = Char(); return C ? FString::FromInt((int32)(C->GetRunSpeedStat() * C->GetSpeedMultiplier())) : FString(); }) ]
			;
	}

	TSharedPtr<SRoseUIWindow>     Layout;    // the client's dlgavata
	TSharedPtr<SConstraintCanvas> Overlay;   // our "+" buttons over it

	TWeakObjectPtr<ARoseCharacter> CharWeak;
	TSharedPtr<FSlateBrush> TrackBrush, HPFill, MPFill, EXPFill, RowBrush, BadgeBrush, PlusBrush;
};

TSharedRef<SWidget> RoseCharSheet_MakeContent(ARoseCharacter& Char)
{
	return SNew(SRoseCharSheet).Char(&Char);
}
