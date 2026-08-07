// ROSE HUD — always-on windows: dlginfo (player name/level + HP/MP/EXP
// gauges), dlgmenu (window toggles) and dlgquickbar (skill hotbar).
// Registered by RoseUIHud_Register (called from URoseUIManager::BeginPlay).
//
// Faithful to the classic client's HUD dialogs:
//   src/client/interface/dlgs/cinfodlg.cpp     (dlginfo: HP/MP/EXP gauges)
//   src/client/interface/dlgs/cmenudlg.cpp     (dlgmenu: window-toggle buttons)
//   src/client/interface/dlgs/cquickbardlg.cpp (dlgquickbar: skill/item hotbar)
// The gauge fill/quickbar-cell coordinates come from the converted layout JSON
// (Content/UI/Layouts/dlg{info,menu,quickbar}.json) — the authored positions of
// the classic XML, so this matches the original placement.
#include "RoseUIManager.h"
#include "RoseUIHelpers.h"
#include "RoseCharacter.h"
#include "RoseSkillComponent.h"
#include "RoseSkillTypes.h"

#include "Dom/JsonObject.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

// ─────────────────────────────────────────────────────────────────────────────
//  hud_info content overlay — the three gauge fills + name/level text.
//  The renderer draws the IMAGE background (UI21_INFO_BG) and skips GUAGE nodes;
//  we overlay each gauge's fill sprite, clipped horizontally to its percent.
// ─────────────────────────────────────────────────────────────────────────────
class SRoseHudInfo : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseHudInfo) {}
		SLATE_ARGUMENT(TWeakObjectPtr<URoseUIManager>, UI)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		UIWeak = InArgs._UI;

		TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);

		// Layout gives us each gauge's authored window-space position + size.
		TSharedPtr<FJsonObject> Layout = RoseUI::LoadLayout(TEXT("dlginfo"));

		AddGauge(Canvas, Layout, TEXT("UI21_GUAGE_RED"),    FVector2D(42, 38),
			[this]() { return HPPercent(); });
		AddGauge(Canvas, Layout, TEXT("UI21_GUAGE_BLUE"),   FVector2D(42, 52),
			[this]() { return MPPercent(); });
		AddGauge(Canvas, Layout, TEXT("UI21_GUAGE_YELLOW"), FVector2D(42, 66),
			[this]() { return EXPPercent(); });

		// Summon capacity, below EXP.  ROSE puts it in the endurance/buff panel
		// rather than here, but this panel is where our other gauges live and it
		// keeps the readout next to the bars it behaves like.  Hidden entirely
		// while nothing is summoned, which is what the client does (it only
		// draws the bar when iValue > 0).
		AddGauge(Canvas, Layout, TEXT("UI00_GUAGE_GREEN"), FVector2D(42, 80),
			[this]() { return SummonPercent(); });

		// Name + level text near the top of the BG (y ≈ 8..24 in the authored
		// 240x118 layout).  Bound so Level updates live.
		const FSlateFontInfo Font = FCoreStyle::GetDefaultFontStyle("Regular", 9);
		Canvas->AddSlot()
			.Offset(FMargin(42.f, 8.f, 140.f, 14.f))
			.Alignment(FVector2D(0, 0))
			[
				SNew(STextBlock)
				.Font(Font)
				.ColorAndOpacity(FSlateColor(FLinearColor::White))
				.Text(FText::FromString(TEXT("Rose Dev")))
			];
		Canvas->AddSlot()
			.Offset(FMargin(42.f, 22.f, 140.f, 14.f))
			.Alignment(FVector2D(0, 0))
			[
				SNew(STextBlock)
				.Font(Font)
				.ColorAndOpacity(FSlateColor(FLinearColor::White))
				.Text_Lambda([this]() {
					const int32 Lvl = Char() ? Char()->Level : 1;
					return FText::FromString(FString::Printf(TEXT("Lv %d"), Lvl));
				})
			];

		// Active buff/debuff icons under the portrait (classic dlginfo): fixed
		// slots bound live to the skill component's Buffs — icon = the source
		// skill's icon; tooltip = name + remaining seconds.
		{
			TSharedRef<SHorizontalBox> BuffRow = SNew(SHorizontalBox);
			for (int32 i = 0; i < 10; ++i)
			{
				BuffRow->AddSlot().AutoWidth().Padding(0, 0, 2, 0)
				[
					SNew(SBox).WidthOverride(20.f).HeightOverride(20.f)
					[
						SNew(SImage)
						.Image_Lambda([this, i]() { return BuffIcon(i); })
						.Visibility_Lambda([this, i]() {
							return BuffAt(i) ? EVisibility::Visible : EVisibility::Collapsed; })
						.ToolTipText_Lambda([this, i]() { return BuffTip(i); })
					]
				];
			}
			// Below the whole info panel (authored dlginfo BG is 240x118).
			Canvas->AddSlot()
				.Offset(FMargin(10.f, 122.f, 240.f, 22.f))
				.Alignment(FVector2D(0, 0))
				[ BuffRow ];
		}

		ChildSlot[ Canvas ];
	}

private:
	ARoseCharacter* Char() const
	{
		return UIWeak.IsValid() ? UIWeak->GetRoseCharacter() : nullptr;
	}

	// ── buff icon row ──
	URoseSkillComponent* Skills() const
	{
		ARoseCharacter* C = Char();
		return C ? C->FindComponentByClass<URoseSkillComponent>() : nullptr;
	}
	const FRoseActiveBuff* BuffAt(int32 i) const
	{
		URoseSkillComponent* S = Skills();
		return (S && S->Buffs.IsValidIndex(i)) ? &S->Buffs[i] : nullptr;
	}
	const FSlateBrush* BuffIcon(int32 i)
	{
		const FRoseActiveBuff* B = BuffAt(i);
		URoseSkillComponent* S = Skills();
		if (!B || !S) return nullptr;
		const FRoseSkillRow* Row = S->GetSkillRow(B->SourceSkill);
		const int32 Icon = Row ? Row->IconIdx : 0;
		if (Icon <= 0) return nullptr;
		if (TSharedPtr<FSlateBrush>* Found = BuffBrushCache.Find(Icon))
			return Found->Get();
		TSharedPtr<FSlateBrush> Brush = RoseUI::MakeSkillIconBrush(Icon, 20.f, BuffKeepTextures);
		BuffBrushCache.Add(Icon, Brush);
		return Brush.Get();
	}
	FText BuffTip(int32 i) const
	{
		const FRoseActiveBuff* B = BuffAt(i);
		if (!B) return FText::GetEmpty();
		const float Now = Char() && Char()->GetWorld() ? Char()->GetWorld()->GetTimeSeconds() : 0.f;
		const int32 Left = B->EndTime > 0.f ? FMath::Max(0, FMath::CeilToInt(B->EndTime - Now)) : -1;
		return FText::FromString(Left >= 0
			? FString::Printf(TEXT("Status %d — %ds left"), B->StatusId, Left)
			: FString::Printf(TEXT("Status %d"), B->StatusId));
	}
	TMap<int32, TSharedPtr<FSlateBrush>> BuffBrushCache;
	TArray<TStrongObjectPtr<UTexture2D>> BuffKeepTextures;

	float HPPercent() const
	{
		ARoseCharacter* C = Char();
		if (!C) return 0.f;
		const float Max = (float)FMath::Max(1, C->GetMaxHPStat());
		return FMath::Clamp(C->CurrentHP / Max, 0.f, 1.f);
	}
	float MPPercent() const
	{
		ARoseCharacter* C = Char();
		if (!C) return 0.f;
		const float Max = (float)FMath::Max(1, C->GetMaxMPStat());
		return FMath::Clamp(C->CurrentMP / Max, 0.f, 1.f);
	}
	// SUMMON CAPACITY.  ROSE draws this as `100 * used / max` in the endurance
	// panel (cenduranceproperty.cpp:533) — a budget, not a timer: max is
	// 50 + the AT_PSV_SUMMON_MOB_CNT passive, and each summoned mob costs
	// LIST_NPC col 21.
	float SummonPercent() const
	{
		ARoseCharacter* C = Char();
		if (!C) return 0.f;
		const float Max = (float)FMath::Max(1, C->GetSummonMaxCapacity());
		return FMath::Clamp((float)C->GetSummonUsedCapacity() / Max, 0.f, 1.f);
	}
	float EXPPercent() const
	{
		// TODO: no experience system yet — the EXP gauge stays empty until one
		// exists (Add_EXP / next-level curve, gs_user.cpp).
		return 0.f;
	}

	// A gauge fill = the full-width fill sprite (137x8) placed inside a
	// ClipToBounds SBox whose width is 137*pct — so the sprite is CLIPPED, never
	// scaled (scaling would smear the sprite art).
	void AddGauge(const TSharedRef<SConstraintCanvas>& Canvas,
	              const TSharedPtr<FJsonObject>& Layout,
	              const FString& Gid, FVector2D FallbackPos,
	              TFunction<float()> Percent)
	{
		const float FullW = 137.f;
		const float FullH = 8.f;

		FVector2D Pos = FallbackPos;
		if (Layout.IsValid())
		{
			FVector2D Found;
			if (RoseUI::FindNode(Layout, TEXT("GUAGE"), Gid, &Found).IsValid())
				Pos = Found;
		}

		TSharedPtr<FSlateBrush> Brush = RoseUI::MakeSpriteBrush(Gid, FullW, FullH, KeepTextures);
		if (!Brush.IsValid())
			return;   // sprite not imported — skip this fill (logged by helper)
		Brushes.Add(Brush);

		TFunction<float()> PctFn = MoveTemp(Percent);
		Canvas->AddSlot()
			.Offset(FMargin(Pos.X, Pos.Y, FullW, FullH))
			.Alignment(FVector2D(0, 0))
			[
				SNew(SBox)
				.HeightOverride(FullH)
				.Clipping(EWidgetClipping::ClipToBounds)
				.HAlign(HAlign_Left)
				.WidthOverride(TAttribute<FOptionalSize>::CreateLambda(
					[PctFn, FullW]() { return FOptionalSize(FullW * PctFn()); }))
				[
					// Full-size image; the SBox clip reveals only the filled part.
					SNew(SBox).WidthOverride(FullW).HeightOverride(FullH)
					[ SNew(SImage).Image(Brush.Get()) ]
				]
			];
	}

	TWeakObjectPtr<URoseUIManager> UIWeak;
	TArray<TSharedPtr<FSlateBrush>> Brushes;
	TArray<TStrongObjectPtr<UTexture2D>> KeepTextures;
};

// ─────────────────────────────────────────────────────────────────────────────
//  hud_quickbar content overlay — the 4-slot skill hotbar.
//  Each cell draws the hotbar skill's icon, a bottom-anchored cooldown sweep,
//  and a "1".."4" key label; click casts the slot, tooltip = the skill label.
//  Icon brushes are cached lazily (keyed by skill id) so hotbar changes don't
//  leak brushes and textures stay pinned.
// ─────────────────────────────────────────────────────────────────────────────
class SRoseHudQuickbar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseHudQuickbar) {}
		SLATE_ARGUMENT(TWeakObjectPtr<URoseUIManager>, UI)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		UIWeak = InArgs._UI;

		// Faithful HORIZONTAL slot layout (CQuickBAR::UpdateCSlotPosition,
		// quicktoolbar.cpp:281): 8 slots (HOT_ICONS_PER_PAGE) starting at (40,21),
		// each (W+1) apart, with a 9px divider gap after the 4th slot.
		const float Cell = 40.f;
		const float StartX = 40.f, StartY = 21.f;

		TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
		for (int32 i = 0; i < 8; ++i)
		{
			const float X = StartX + (Cell + 1.f) * i + (i >= 4 ? 9.f : 0.f);
			Canvas->AddSlot()
				.Offset(FMargin(X, StartY, Cell, Cell))
				.Alignment(FVector2D(0, 0))
				[ MakeCell(i, Cell) ];
		}
		ChildSlot[ Canvas ];
	}

private:
	ARoseCharacter* Char() const
	{
		return UIWeak.IsValid() ? UIWeak->GetRoseCharacter() : nullptr;
	}
	URoseSkillComponent* SkillComp() const
	{
		ARoseCharacter* C = Char();
		return C ? C->Skills : nullptr;
	}
	// Current skill id in hotbar slot i (<=0 = empty).
	int32 SlotId(int32 Index) const
	{
		URoseSkillComponent* S = SkillComp();
		if (!S || !S->Hotbar.IsValidIndex(Index))
			return 0;
		return S->Hotbar[Index];
	}

	// Lazily build (and cache) the icon brush for a skill id.  Textures stay
	// pinned in KeepTextures, which this widget owns → they outlive the brush.
	FSlateBrush* IconBrushFor(int32 SkillId, float Size)
	{
		if (SkillId <= 0)
			return nullptr;
		if (TSharedPtr<FSlateBrush>* Cached = IconCache.Find(SkillId))
			return Cached->Get();

		URoseSkillComponent* S = SkillComp();
		const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
		TSharedPtr<FSlateBrush> Brush = Row
			? RoseUI::MakeSkillIconBrush(Row->IconIdx, Size, KeepTextures)
			: nullptr;
		IconCache.Add(SkillId, Brush);   // cache the null too (avoid re-probing)
		return Brush.IsValid() ? Brush.Get() : nullptr;
	}

	TSharedRef<SWidget> MakeCell(int32 Index, float Size)
	{
		return SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(0))
			.ToolTipText_Lambda([this, Index]() {
				const int32 Id = SlotId(Index);
				URoseSkillComponent* S = SkillComp();
				return (Id > 0 && S)
					? FText::FromString(S->SkillLabel(Id))
					: FText::FromString(FString::Printf(TEXT("Hotbar %d — empty"), Index + 1));
			})
			.OnClicked_Lambda([this, Index]() {
				if (URoseSkillComponent* S = SkillComp())
					S->CastHotbar(Index);   // CastHotbar is 0-based
				return FReply::Handled();
			})
			[
				SNew(SBox).WidthOverride(Size).HeightOverride(Size)
				[
					SNew(SOverlay)
					// Skill icon (rebound each frame so hotbar edits show up).
					+ SOverlay::Slot()
					[
						SNew(SImage)
						.Image_Lambda([this, Index, Size]() -> const FSlateBrush* {
							return IconBrushFor(SlotId(Index), Size);
						})
					]
					// Cooldown sweep: dark box anchored to the bottom whose
					// height = Size * remaining/total, clipped to the cell.
					+ SOverlay::Slot()
					.HAlign(HAlign_Fill)
					.VAlign(VAlign_Bottom)
					[
						SNew(SBox)
						.WidthOverride(Size)
						.Clipping(EWidgetClipping::ClipToBounds)
						.HeightOverride(TAttribute<FOptionalSize>::CreateLambda(
							[this, Index, Size]() { return FOptionalSize(Size * CooldownFrac(Index)); }))
						[
							// Engine white brush tinted translucent-dark → a solid
							// sweep that fills bottom-up as the cooldown ticks down.
							SNew(SImage)
							.Image(FCoreStyle::Get().GetBrush("WhiteBrush"))
							.ColorAndOpacity(FSlateColor(FLinearColor(0.f, 0.f, 0.f, 0.6f)))
						]
					]
					// "F1".."F8" key label, top-left.
					+ SOverlay::Slot()
					.HAlign(HAlign_Left)
					.VAlign(VAlign_Top)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor::White))
						.Text(FText::FromString(FString::Printf(TEXT("F%d"), Index + 1)))
					]
				]
			];
	}

	// 0 = ready, 1 = just cast; = remaining / total cooldown of the slot's skill.
	float CooldownFrac(int32 Index) const
	{
		URoseSkillComponent* S = SkillComp();
		const int32 Id = SlotId(Index);
		if (!S || Id <= 0)
			return 0.f;
		const FRoseSkillRow* Row = S->GetSkillRow(Id);
		if (!Row)
			return 0.f;
		const float Total = FMath::Max(0.01f, Row->CooldownSec);
		return FMath::Clamp(S->GetCooldownRemaining(Id) / Total, 0.f, 1.f);
	}

	TWeakObjectPtr<URoseUIManager> UIWeak;
	TMap<int32, TSharedPtr<FSlateBrush>> IconCache;
	TArray<TStrongObjectPtr<UTexture2D>> KeepTextures;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Registration.
// ─────────────────────────────────────────────────────────────────────────────
void RoseUIHud_Register(URoseUIManager& UI)
{
	// RETIRED: the classic hud_info / hud_menu / hud_quickbar windows are replaced
	// by the modern SRoseHUD (RoseHUD.cpp), created by URoseUIManager::BeginPlay.
	// The SRoseHudInfo / SRoseHudQuickbar content widgets above are kept (unused)
	// in case we want the classic look back.
	(void)UI;
}
