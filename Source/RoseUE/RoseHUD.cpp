// SRoseHUD — the modern always-on HUD (iROSE-mobile styled, PC controls: no
// on-screen joystick).  Built fresh in Slate (rounded panels/bars) rather than
// from the classic XML dialogs, reusing our skill/gauge assets.  Replaces the
// classic hud_info/hud_menu/hud_quickbar windows; the hamburger menu still opens
// the existing detail windows (inventory/skills/character/quest/system) and the
// dlgchat + dlgminimap windows stay for chat + the corner map.
//
// Layout:
//   top-left    — portrait + HP/MP/EXP rounded bars + level badge
//   left        — hamburger button → expandable grid of window toggles
//   top-right   — zone name + clock + quest tracker
//   bottom-center — 12-slot skill quickbar (keys 1-9, 0, -, =)
#include "RoseUIManager.h"
#include "RoseUIWindow.h"
#include "RoseUIHelpers.h"
#include "RoseUITheme.h"
#include "RoseUIDrag.h"
#include "RoseCharacter.h"
#include "RoseQuest.h"
#include "RoseSkillComponent.h"
#include "RoseSkillTypes.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// Palette — centralised dark-grey theme (RoseUITheme.h).
	const FLinearColor kPanel   = RoseTheme::Panel;
	const FLinearColor kPanelHi = RoseTheme::PanelHi;
	const FLinearColor kTrack   = RoseTheme::Track;
	const FLinearColor kHP      = RoseTheme::HP;
	const FLinearColor kMP      = RoseTheme::MP;
	const FLinearColor kEXP     = RoseTheme::EXP;
	const FLinearColor kAccent  = RoseTheme::Accent;
	const FLinearColor kText    = RoseTheme::Text;

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}
}

class SRoseHUD : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseHUD) {}
		SLATE_ARGUMENT(TWeakObjectPtr<URoseUIManager>, UI)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		UIWeak = InArgs._UI;

		// Persistent rounded brushes (kept for the widget's lifetime).
		PanelBrush    = MakeShared<FSlateRoundedBoxBrush>(kPanel,   10.f, FLinearColor(kAccent.R, kAccent.G, kAccent.B, 0.5f), 1.f);
		// Gauges: glass art if present, flat rounded bars otherwise.  Gauge()
		// clips the fill by width, so any brush works here unchanged.
		// Each bar carries its OWN track (dlginfo.xml BGID) — HP's and MP's are
		// different sprites, so there is no one shared track under the glass skin.
		HPFill  = RoseUI::GlassGauge(RoseUI::EGaugeKind::HP);
		MPFill  = RoseUI::GlassGauge(RoseUI::EGaugeKind::MP);
		EXPFill = RoseUI::GlassGauge(RoseUI::EGaugeKind::EXP);
		HPTrack  = RoseUI::GlassGauge(RoseUI::EGaugeKind::HP,  true);
		MPTrack  = RoseUI::GlassGauge(RoseUI::EGaugeKind::MP,  true);
		EXPTrack = RoseUI::GlassGauge(RoseUI::EGaugeKind::EXP, true);
		TrackBrush = MakeShared<FSlateRoundedBoxBrush>(kTrack, 6.f);
		if (!HPFill.IsValid())   HPFill   = MakeShared<FSlateRoundedBoxBrush>(kHP,  6.f);
		if (!MPFill.IsValid())   MPFill   = MakeShared<FSlateRoundedBoxBrush>(kMP,  6.f);
		if (!EXPFill.IsValid())  EXPFill  = MakeShared<FSlateRoundedBoxBrush>(kEXP, 4.f);
		if (!HPTrack.IsValid())  HPTrack  = TrackBrush;
		if (!MPTrack.IsValid())  MPTrack  = TrackBrush;
		if (!EXPTrack.IsValid()) EXPTrack = TrackBrush;
		// The skill-cooldown scrim needs a PLAIN rectangle — it used to borrow
		// TrackBrush, which is now a shaped gauge sprite with its own highlight.
		CooldownBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0, 0, 0, 0.55f), 4.f);
		BadgeBrush    = MakeShared<FSlateRoundedBoxBrush>(kPanelHi, 22.f, kAccent, 2.f);
		PortraitBrush = MakeShared<FSlateRoundedBoxBrush>(kPanelHi, 30.f, kAccent, 2.f);
		SlotBrush     = RoseUI::GlassPanel(RoseUI::EPanelKind::Inset);
		if (!SlotBrush.IsValid())
			SlotBrush = MakeShared<FSlateRoundedBoxBrush>(kPanelHi, 8.f, FLinearColor(kAccent.R, kAccent.G, kAccent.B, 0.5f), 1.f);
		MenuBtnBrush  = MakeShared<FSlateRoundedBoxBrush>(kPanelHi,  8.f, kAccent, 1.f);

		ChildSlot
		[
			SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot().Anchors(FAnchors(0,0)).Alignment(FVector2D(0,0))
			  .AutoSize(true).Offset(FMargin(8, 8, 0, 0))       [ BuildCharacterPanel() ]
			+ SConstraintCanvas::Slot().Anchors(FAnchors(0,0)).Alignment(FVector2D(0,0))
			  .AutoSize(true).Offset(FMargin(8, 96, 0, 0))      [ BuildMenu() ]
			+ SConstraintCanvas::Slot().Anchors(FAnchors(1,0)).Alignment(FVector2D(1,0))
			  .AutoSize(true).Offset(FMargin(8, 150, 0, 0))     [ BuildQuestTracker() ]
			+ SConstraintCanvas::Slot().Anchors(FAnchors(0.5f,1)).Alignment(FVector2D(0.5f,1))
			  .AutoSize(true).Offset(FMargin(0, 0, 0, 8))       [ BuildQuickBar() ]
		];
	}

private:
	ARoseCharacter* Char() const { return UIWeak.IsValid() ? UIWeak->GetRoseCharacter() : nullptr; }
	URoseSkillComponent* Skills() const { ARoseCharacter* C = Char(); return C ? C->Skills : nullptr; }

	// ── A rounded gauge: track + fill clipped to percent + centered value ──────
	TSharedRef<SWidget> Gauge(TSharedPtr<FSlateBrush> Fill, TSharedPtr<FSlateBrush> Track,
	                          float W, float H,
	                          TFunction<float()> Pct, TFunction<FText()> Label)
	{
		return SNew(SBox).WidthOverride(W).HeightOverride(H)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(Track.Get()) ]
			+ SOverlay::Slot().HAlign(HAlign_Left)
			[
				SNew(SBox).HeightOverride(H).Clipping(EWidgetClipping::ClipToBounds)
				.WidthOverride(TAttribute<FOptionalSize>::CreateLambda(
					[W, Pct]() { return FOptionalSize(FMath::Clamp(Pct(), 0.f, 1.f) * W); }))
				[ SNew(SImage).Image(Fill.Get()) ]
			]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[ SNew(STextBlock).Font(Font(8)).ColorAndOpacity(kText)
			  .Text_Lambda([Label]() { return Label(); }) ]
		];
	}

	// ── Player panel: the client's own dlginfo (210x100) ─────────────────────
	//
	// Name, HP, MP, EXP, level/job and weight, all at ROSE's own coordinates
	// with its own gauge art.  The layout defines FIVE gauges -- the two extra
	// are PAT_GAUGE and SUMMON_GAUGE, which the client reserves for the mount
	// fuel and summon capacity bars.
	TSharedRef<SWidget> BuildCharacterPanel()
	{
		return SNew(SBox).WidthOverride(210.f).HeightOverride(100.f)
		[
			SNew(SRoseUIWindow)
			.Dialog(TEXT("dlginfo"))
			.OnGaugeValue(FRoseUIGaugeValue::CreateSP(this, &SRoseHUD::InfoGauge))
			.OnTextValue(FRoseUITextValue::CreateSP(this, &SRoseHUD::InfoText))
		];
	}

	/** 0..1 fill for each of dlginfo's gauges, by its XML NAME. */
	float InfoGauge(const FRoseUIKey& Key) const
	{
		ARoseCharacter* C = Char();
		if (!C)
			return 0.f;
		if (Key.Is(TEXT("HP")))
			return C->CurrentHP / FMath::Max(1.f, (float)C->GetMaxHPStat());
		if (Key.Is(TEXT("MP")))
			return C->CurrentMP / FMath::Max(1.f, (float)C->GetMaxMPStat());
		if (Key.Is(TEXT("EXP_MINI")))
			return C->GetExpFraction();
		if (Key.Is(TEXT("SUMMON_GAUGE")))
		{
			const int32 Max = C->GetSummonMaxCapacity();
			return Max > 0 ? (float)C->GetSummonUsedCapacity() / (float)Max : 0.f;
		}
		if (Key.Is(TEXT("PAT_GAUGE")))
			return C->MaxFuel > 0.f ? C->Fuel / C->MaxFuel : 0.f;
		return 0.f;
	}

	/** Every string dlginfo asks for, by NAME.  The gauges' own text is the
	 *  "current/max" pair; the *_PERCENT fields are separate right-aligned
	 *  labels sharing the same rect, which is how ROSE shows "100%" beside it. */
	FText InfoText(const FRoseUIKey& Key, int32 Row) const
	{
		ARoseCharacter* C = Char();
		if (!C)
			return FText::GetEmpty();

		auto Pair = [](float Cur, int32 Max) {
			return FText::FromString(FString::Printf(TEXT("%d/%d"), (int32)Cur, Max)); };
		auto Pct = [](float F) {
			return FText::FromString(FString::Printf(TEXT("%d%%"),
				FMath::RoundToInt(FMath::Clamp(F, 0.f, 1.f) * 100.f))); };

		if (Key.Is(TEXT("CAPTION")))    return FText::FromString(C->GetDisplayName());
		if (Key.Is(TEXT("HP")))         return Pair(C->CurrentHP, C->GetMaxHPStat());
		if (Key.Is(TEXT("MP")))         return Pair(C->CurrentMP, C->GetMaxMPStat());
		if (Key.Is(TEXT("HP_PERCENT")))
			return Pct(C->CurrentHP / FMath::Max(1.f, (float)C->GetMaxHPStat()));
		if (Key.Is(TEXT("MP_PERCENT")))
			return Pct(C->CurrentMP / FMath::Max(1.f, (float)C->GetMaxMPStat()));
		if (Key.Is(TEXT("EXP_MINI")))   return Pct(C->GetExpFraction());
		if (Key.Is(TEXT("INFO_LEV_VALUE")))  return FText::AsNumber(C->Level);
		if (Key.Is(TEXT("INFO_JOB_VALUE")))  return FText::FromString(C->GetJobName());
		if (Key.Is(TEXT("INFO_WEIGHT")))     return FText::FromString(TEXT("Weight"));
		if (Key.Is(TEXT("INFO_WEIGHTAGE_VALUE")))
			return Pct(C->GetCarriedWeightFraction());
		return FText::GetEmpty();
	}

	TSharedRef<SWidget> BuildCharacterPanelLegacy()
	{
		return SNew(SBox).WidthOverride(300.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(PanelBrush.Get()) ]
			+ SOverlay::Slot().Padding(6.f)
			[
				SNew(SHorizontalBox)
				// Portrait + level badge.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SOverlay)
					// Rounded frame.
					+ SOverlay::Slot()
					[ SNew(SBox).WidthOverride(60.f).HeightOverride(60.f)
					  [ SNew(SImage).Image(PortraitBrush.Get()) ] ]
					// Live face render-target (falls back to the frame until ready).
					+ SOverlay::Slot().Padding(3.f)
					[ SNew(SBox).WidthOverride(54.f).HeightOverride(54.f)
					  .Clipping(EWidgetClipping::ClipToBounds)
					  [ SNew(SImage).Image_Lambda([this]() -> const FSlateBrush* {
						if (!PortraitLive.IsValid())
						{
							ARoseCharacter* C = Char();
							if (C && C->GetPortraitRT())
							{
								PortraitLive = MakeShared<FSlateBrush>();
								PortraitLive->SetResourceObject(C->GetPortraitRT());
								PortraitLive->ImageSize = FVector2D(54.f, 54.f);
							}
						}
						return PortraitLive.IsValid() ? PortraitLive.Get() : PortraitBrush.Get();
					  }) ] ]
					// Level badge, bottom-left of the portrait.
					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom)
					[ SNew(SBox).WidthOverride(26.f).HeightOverride(26.f)
					  [ SNew(SOverlay)
					    + SOverlay::Slot()[ SNew(SImage).Image(BadgeBrush.Get()) ]
					    + SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					    [ SNew(STextBlock).Font(Font(10, true)).ColorAndOpacity(kText)
					      .Text_Lambda([this]() { return FText::AsNumber(Char() ? Char()->Level : 1); }) ] ] ]
				]
				// Name + gauges.
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[ SNew(STextBlock).Font(Font(11, true)).ColorAndOpacity(kText)
					  .Text(FText::FromString(TEXT("Rose Dev"))) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
					[ Gauge(HPFill, HPTrack, 210, 15,
						[this]() { ARoseCharacter* C = Char(); return C ? C->CurrentHP / FMath::Max(1.f, (float)C->GetMaxHPStat()) : 0.f; },
						[this]() { ARoseCharacter* C = Char(); return C ? FText::FromString(FString::Printf(TEXT("%d/%d"), (int)C->CurrentHP, C->GetMaxHPStat())) : FText(); }) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[ Gauge(MPFill, MPTrack, 210, 15,
						[this]() { ARoseCharacter* C = Char(); return C ? C->CurrentMP / FMath::Max(1.f, (float)C->GetMaxMPStat()) : 0.f; },
						[this]() { ARoseCharacter* C = Char(); return C ? FText::FromString(FString::Printf(TEXT("%d/%d"), (int)C->CurrentMP, C->GetMaxMPStat())) : FText(); }) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[ Gauge(EXPFill, EXPTrack, 210, 8,
						[this]() { ARoseCharacter* C = Char(); return C ? C->GetExpFraction() : 0.f; },
						[this]() { ARoseCharacter* C = Char(); return C ? FText::FromString(
							FString::Printf(TEXT("EXP %.1f%%"), C->GetExpFraction() * 100.f)) : FText(); }) ]
				]
			]
		];
	}

	// ── Hamburger menu → expandable grid of window toggles ────────────────────
	void ToggleMenu() { bMenuOpen = !bMenuOpen; }
	void Do(TFunction<void()> Fn) { if (Fn) Fn(); bMenuOpen = false; }

	TSharedRef<SWidget> MenuItem(const FString& Label, TFunction<void()> OnClick)
	{
		// Glass plate as the button STYLE so hover/pressed art plays; the flat
		// skin keeps its static rounded box behind the label.
		const FButtonStyle* Glass = RoseUI::GlassButton(RoseUI::EButtonKind::Action);
		return SNew(SButton)
			.ButtonStyle(Glass ? Glass : &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(FMargin(0))
			.OnClicked_Lambda([this, OnClick]() { Do(OnClick); return FReply::Handled(); })
			[
				SNew(SBox).WidthOverride(78.f).HeightOverride(30.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ SNew(SImage).Image(MenuBtnBrush.Get())
					                    .Visibility(Glass ? EVisibility::Collapsed
					                                      : EVisibility::HitTestInvisible) ]
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[ SNew(STextBlock).Font(Font(9)).ColorAndOpacity(kText)
					  .Text(FText::FromString(Label)) ]
				]
			];
	}

	TSharedRef<SWidget> BuildMenu()
	{
		TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(3.f));
		auto Add = [&](int32 R, int32 C, const FString& L, TFunction<void()> Fn)
		{ Grid->AddSlot(C, R)[ MenuItem(L, Fn) ]; };
		Add(0, 0, TEXT("Char"),   [this]() { if (Char()) Char()->ToggleCharacterSheet(); });
		Add(0, 1, TEXT("Bag"),    [this]() { if (Char()) Char()->ToggleInventory(); });
		Add(1, 0, TEXT("Skill"),  [this]() { if (UIWeak.IsValid()) UIWeak->ToggleSkillPanel(); });
		Add(1, 1, TEXT("Tree"),   [this]() { if (UIWeak.IsValid()) UIWeak->ToggleSkillTree(); });
		Add(2, 0, TEXT("Quest"),  [this]() { if (UIWeak.IsValid()) UIWeak->ToggleQuestJournal(); });
		Add(2, 1, TEXT("Map"),    [this]() { if (UIWeak.IsValid()) UIWeak->ToggleMinimap(); });
		Add(3, 0, TEXT("Chat"),   [this]() { if (UIWeak.IsValid()) UIWeak->FocusChat(); });
		Add(3, 1, TEXT("System"), [this]() { if (UIWeak.IsValid()) UIWeak->ToggleOptions(); });

		return SNew(SVerticalBox)
			// Hamburger button.
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SButton)
				.ButtonStyle(RoseUI::GlassButton(RoseUI::EButtonKind::Small)
				             ? RoseUI::GlassButton(RoseUI::EButtonKind::Small)
				             : &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
				.ContentPadding(FMargin(0))
				.OnClicked_Lambda([this]() { ToggleMenu(); return FReply::Handled(); })
				[
					SNew(SBox).WidthOverride(44.f).HeightOverride(38.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[ SNew(SImage).Image(MenuBtnBrush.Get())
						  .Visibility(RoseUI::GlassButton(RoseUI::EButtonKind::Small)
						              ? EVisibility::Collapsed : EVisibility::HitTestInvisible) ]
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
						[ SNew(STextBlock).Font(Font(16, true)).ColorAndOpacity(kAccent)
						  .Text(FText::FromString(FString::Chr(0x2630))) ]   // hamburger glyph
					]
				]
			]
			// The grid, shown only when open.
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[
				SNew(SBox)
				.Visibility_Lambda([this]() { return bMenuOpen ? EVisibility::Visible : EVisibility::Collapsed; })
				[ Grid ]
			];
	}

	// ── Quest tracker + zone/clock (top-right) ────────────────────────────────
	TSharedRef<SWidget> BuildQuestTracker()
	{
		return SNew(SBox).WidthOverride(220.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(PanelBrush.Get()) ]
			+ SOverlay::Slot().Padding(8.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()
				[ SNew(STextBlock).Font(Font(11, true)).ColorAndOpacity(kEXP)
				  .Text_Lambda([this]() {
					UWorld* W = Char() ? Char()->GetWorld() : nullptr;
					FString Map = W ? W->GetMapName() : FString();
					int32 U; if (Map.StartsWith(TEXT("UEDPIE_")) && Map.FindChar('_', U)) Map = Map.RightChop(U + 1);
					Map.RemoveFromStart(TEXT("UEDPIE_0_")); Map.RemoveFromStart(TEXT("L_"));
					return FText::FromString(Map.IsEmpty() ? TEXT("ROSE") : Map);
				  }) ]
				// Live tracker: active quest names from the quest component.
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
				[ SNew(STextBlock).Font(Font(9)).ColorAndOpacity(kText).AutoWrapText(true)
				  .Text_Lambda([this]() {
					URoseQuestComponent* Q = Char() ? Char()->Quests : nullptr;
					FString Out;
					int32 Shown = 0;
					if (Q)
						for (const FRoseQuestSlot& Slot : Q->Slots)
						{
							if (!Slot.Id) continue;
							if (Shown == 3) { Out += TEXT("\n…"); break; }
							const FRoseQuestRow* Row = Q->GetQuestRow(Slot.Id);
							const FString Name = (Row && !Row->DisplayName.IsEmpty())
								? Row->DisplayName : FString::Printf(TEXT("Quest %d"), Slot.Id);
							if (Shown++) Out += TEXT("\n");
							Out += TEXT("• ") + Name;
						}
					return FText::FromString(Shown ? Out : TEXT("No active quest"));
				  }) ]
			]
		];
	}

	// ── Quickbar (bottom-center): 12 square slots cast by keys 1-9, 0, -, = ───
	// Each slot: icon (drag a skill from the skill window onto it to assign),
	// bottom-up cooldown sweep, key label top-left, remaining-seconds readout
	// while cooling.  Click casts.
	// Bar 1 shows the bare key, bar 2 the Alt+ form.  This used to be a flat
	// 12-entry table, so every slot on the second bar rendered "?".
	static FString SlotKeyLabel(int32 Slot)
	{
		return URoseSkillComponent::HotbarKeyLabel(Slot);
	}

	FSlateBrush* SkillIcon(int32 Slot)
	{
		URoseSkillComponent* S = Skills();
		const int32 Id = (S && S->Hotbar.IsValidIndex(Slot)) ? S->Hotbar[Slot] : -1;
		if (Id <= 0) return nullptr;
		if (TSharedPtr<FSlateBrush>* C = IconCache.Find(Id)) return C->IsValid() ? C->Get() : nullptr;
		const FRoseSkillRow* Row = S->GetSkillRow(Id);
		TSharedPtr<FSlateBrush> B = Row ? RoseUI::MakeSkillIconBrush(Row->IconIdx, 52, KeepTextures) : nullptr;
		IconCache.Add(Id, B);
		return B.IsValid() ? B.Get() : nullptr;
	}

	// Seconds left on the slot's cooldown (0 = ready / empty).
	float SlotCooldown(int32 Slot) const
	{
		URoseSkillComponent* S = Skills();
		const int32 Id = (S && S->Hotbar.IsValidIndex(Slot)) ? S->Hotbar[Slot] : -1;
		return (S && Id > 0) ? S->GetCooldownRemaining(Id) : 0.f;
	}

	TSharedRef<SWidget> SkillButton(int32 Slot, float Size)
	{
		// Drop zone: a skill from the skill window lands here; a drag from
		// another hotbar slot SWAPS the two slots.  The cell itself is a drag
		// source (rearrange), click = cast, right-click = clear.
		return SNew(SRoseSkillDropZone)
			.OnDropSkill_Lambda([this, Slot](int32 SkillId, int32 FromSlot) {
				URoseSkillComponent* S = Skills();
				if (!S) return;
				if (FromSlot >= 0 && FromSlot != Slot)
				{
					const int32 Here = S->Hotbar.IsValidIndex(Slot) ? S->Hotbar[Slot] : -1;
					S->SetHotbarSlot(FromSlot, Here);
				}
				if (FromSlot != Slot)
					S->SetHotbarSlot(Slot, SkillId);
			})
			[
			SNew(SRoseSkillDragBox)
			.ToolTipText_Lambda([this, Slot]() {
				URoseSkillComponent* S = Skills();
				const int32 Id = (S && S->Hotbar.IsValidIndex(Slot)) ? S->Hotbar[Slot] : -1;
				// Name the BAR and its key: with two stacked bars it is not obvious
				// which row a slot is on, nor that the second lives on Alt.
				const FString Where = FString::Printf(TEXT("Bar %d  ·  %s"),
					URoseSkillComponent::HotbarBarOf(Slot),
					*URoseSkillComponent::HotbarKeyLabel(Slot));
				return (S && Id > 0)
					? FText::FromString(S->SkillLabel(Id) + TEXT("\n") + Where
						+ TEXT("\nclick: cast   drag: move   right-click: clear"))
					: FText::FromString(Where + TEXT(" — drag a skill here"));
			})
			.OnClick_Lambda([this, Slot]() { if (Skills()) Skills()->CastHotbar(Slot); })
			.OnRightClick_Lambda([this, Slot]() {
				URoseSkillComponent* S = Skills();
				if (S && S->Hotbar.IsValidIndex(Slot) && S->Hotbar[Slot] > 0)
					S->SetHotbarSlot(Slot, -1);
			})
			.OnBeginDrag_Lambda([this, Slot]() -> TSharedPtr<FRoseSkillDragOp> {
				URoseSkillComponent* S = Skills();
				const int32 Id = (S && S->Hotbar.IsValidIndex(Slot)) ? S->Hotbar[Slot] : -1;
				if (Id <= 0) return nullptr;
				TSharedRef<FRoseSkillDragOp> Op = MakeShared<FRoseSkillDragOp>();
				Op->SkillId = Id;
				Op->FromHotbarSlot = Slot;
				Op->Icon = SkillIcon(Slot);
				return Op;
			})
			[
				SNew(SBox).WidthOverride(Size).HeightOverride(Size)
				[
					SNew(SOverlay)
					// NO slot backing here: dlgquickbar already draws it
					// (GEN_DECO20 + the GEN_DECO21 separators).  Ours sat on top
					// of the client's art and read as a second, older UI.
					+ SOverlay::Slot().Padding(1.f)
					[ SNew(SImage).Image_Lambda([this, Slot]() -> const FSlateBrush* { return SkillIcon(Slot); }) ]
					// Cooldown darken overlay (fills from bottom as it ticks).
					+ SOverlay::Slot().VAlign(VAlign_Bottom)
					[ SNew(SBox).WidthOverride(Size).Clipping(EWidgetClipping::ClipToBounds)
					  .HeightOverride(TAttribute<FOptionalSize>::CreateLambda([this, Slot, Size]() {
						URoseSkillComponent* S = Skills();
						const int32 Id = (S && S->Hotbar.IsValidIndex(Slot)) ? S->Hotbar[Slot] : -1;
						const FRoseSkillRow* Row = (S && Id > 0) ? S->GetSkillRow(Id) : nullptr;
						const float F = (Row && Row->CooldownSec > 0.f)
							? S->GetCooldownRemaining(Id) / Row->CooldownSec : 0.f;
						return FOptionalSize(Size * FMath::Clamp(F, 0.f, 1.f)); }))
					  [ SNew(SImage).Image(CooldownBrush.Get()) ] ]
					// Remaining seconds, centred while the skill cools.
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[ SNew(STextBlock).Font(Font(11, true)).ColorAndOpacity(FLinearColor(1.f, 0.9f, 0.5f))
					  .Visibility_Lambda([this, Slot]() {
						return SlotCooldown(Slot) > 0.05f ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					  .Text_Lambda([this, Slot]() {
						return FText::FromString(FString::Printf(TEXT("%.0f"), FMath::CeilToFloat(SlotCooldown(Slot)))); }) ]
					// Key label, top-left.
					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(3.f, 1.f)
					[ SNew(STextBlock).Font(Font(9, true)).ColorAndOpacity(kAccent)
					  .Text(FText::FromString(SlotKeyLabel(Slot))) ]
				]
			]
			];   // close the SRoseSkillDropZone content
	}

	// ── Quick bar: the client's own dlgquickbar (530x70) ─────────────────────
	//
	// ROSE draws the bar in two layers and our old hand-built panel showed
	// neither: the HORIZONTAL pane is always visible (slot backings +
	// separators only), while HORIZONTAL_HOVER — the window frame, the rotate
	// button on the left and the page control on the right — appears ONLY while
	// the cursor is over the bar.  That is exactly the hovered/unhovered pair
	// in the client.  The VERTICAL panes are the rotated form, hidden until bar
	// rotation is implemented.
	//
	// Two bars are stacked, so the whole thing is built twice; slot icons are
	// overlaid on the authored icon anchors (41px pitch from HOR_ICON_POS).
	TSharedRef<SWidget> BuildQuickBar()
	{
		TSharedRef<SVerticalBox> Bars = SNew(SVerticalBox);
		for (int32 Bar = 0; Bar < URoseSkillComponent::HotbarBars; ++Bar)
			Bars->AddSlot().AutoHeight().Padding(0.f, Bar == 0 ? 0.f : 2.f, 0.f, 0.f)
				[ BuildQuickBarRow(Bar) ];
		return Bars;
	}

	TSharedRef<SWidget> BuildQuickBarRow(int32 Bar)
	{
		// The slot widgets go INSIDE the layout window (its Content overlay),
		// not beside it.  A sibling canvas does not inherit the window's drag
		// transform, so dragging the bar moved the frame and left the skill
		// icons and their key labels behind.
		TSharedRef<SConstraintCanvas> Slots = SNew(SConstraintCanvas);
		Slots->SetVisibility(EVisibility::SelfHitTestInvisible);

		TSharedPtr<SRoseUIWindow> Layout;
		TSharedRef<SWidget> Row =
			SNew(SBox).WidthOverride(530.f).HeightOverride(70.f)
			[
				SAssignNew(Layout, SRoseUIWindow)
				.Dialog(TEXT("dlgquickbar"))
				.Content(Slots)
			];

		QuickBarLayouts.Add(Layout);
		const int32 Index = QuickBarLayouts.Num() - 1;

		// Horizontal form only for now; the hover chrome follows the cursor.
		Layout->SetPaneVisibility(TEXT("VERTICAL"),       EVisibility::Collapsed);
		Layout->SetPaneVisibility(TEXT("VERTICAL_HOVER"), EVisibility::Collapsed);
		Layout->SetPaneVisibility(TEXT("VERTICAL_FUNC"),  EVisibility::Collapsed);
		Layout->SetPaneVisibility(TEXT("HORIZONTAL_HOVER"),
			TAttribute<EVisibility>::CreateLambda([this, Index]() {
				return (QuickBarLayouts.IsValidIndex(Index)
				        && QuickBarLayouts[Index].IsValid()
				        && QuickBarLayouts[Index]->IsHovered())
					? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed; }));

		// Position from the SLOT BACKINGS themselves (GEN_DECO20, one per slot,
		// in order) rather than a single anchor: they are 25x25 markers centred
		// in a 41x41 cell, so inflating each by 8 gives the cell exactly.  The
		// VERTICAL pane also draws 12, hence taking only the first row.
		const TArray<FSlateRect>* Backings = Layout->GetSpriteRects(TEXT("GEN_DECO20"));
		for (int32 i = 0; i < URoseSkillComponent::SlotsPerBar; ++i)
		{
			const int32 Slot = Bar * URoseSkillComponent::SlotsPerBar + i;
			FSlateRect Cell(16.f + i * 41.f, 15.f, 57.f + i * 41.f, 56.f);
			if (Backings && Backings->IsValidIndex(i))
			{
				const FSlateRect& B = (*Backings)[i];
				Cell = FSlateRect(B.Left - 8.f, B.Top - 8.f, B.Right + 8.f, B.Bottom + 8.f);
			}
			Slots->AddSlot()
				.Offset(FMargin(Cell.Left, Cell.Top, Cell.GetSize().X, Cell.GetSize().Y))
				.Alignment(FVector2D(0, 0))
				[ SkillButton(Slot, Cell.GetSize().X) ];
		}
		return Row;
	}

	// One layout per stacked bar; the hover binding looks itself up by index.
	TArray<TSharedPtr<SRoseUIWindow>> QuickBarLayouts;

	TWeakObjectPtr<URoseUIManager> UIWeak;
	bool bMenuOpen = false;
	TSharedPtr<FSlateBrush> PortraitLive;   // brush over the character's live RT
	TMap<int32, TSharedPtr<FSlateBrush>> IconCache;
	TArray<TStrongObjectPtr<UTexture2D>> KeepTextures;
	TSharedPtr<FSlateBrush> PanelBrush, TrackBrush, HPFill, MPFill, EXPFill,
	                        HPTrack, MPTrack, EXPTrack, CooldownBrush,
		BadgeBrush, PortraitBrush, SlotBrush, MenuBtnBrush;
};

TSharedRef<SWidget> RoseHUD_Make(URoseUIManager& UI)
{
	return SNew(SRoseHUD).UI(&UI);
}
