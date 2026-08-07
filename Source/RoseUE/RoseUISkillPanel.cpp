// SRoseSkillPanel — the MODERN skill window (custom Slate, HUD-styled rounded
// panels), replacing the classic dlgskill for the always-on UI.  Opened from the
// HUD hamburger "Skill" button and the K key (URoseUIManager::ToggleSkillPanel).
//
// Three tabs mirror the client's SKILL_TAB_TYPE paging (verified against the
// data + dlgskill's three TABBUTTONs): **1 = Active** (castable action skills),
// **2 = Passive** (SkillType-15 permanents), **0/else = Basic** (the common
// everyone-has skills).  Each tab lists the LEARNED skills of that page; a row is
// icon + "Name (Lv:N)" + a "+" level-up button (enabled when CanLearn the next
// level, deducting SkillPoints via URoseSkillComponent::LearnSkill).  Click the
// icon → assign to the first free hotbar slot; Shift+click → cast.
//
// Data/logic is the same URoseSkillComponent path as SRoseSkillList
// (RoseUISkills.cpp) — only the presentation is the new rounded look.
#include "RoseUIManager.h"
#include "RoseUIHelpers.h"
#include "RoseUITheme.h"
#include "RoseUIDrag.h"
#include "RoseCharacter.h"
#include "RoseSkillComponent.h"
#include "RoseSkillTypes.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	// Palette — centralised dark-grey theme (RoseUITheme.h).
	const FLinearColor kPanel   = RoseTheme::Panel;
	const FLinearColor kPanelHi = RoseTheme::PanelHi;
	const FLinearColor kRow     = RoseTheme::Row;
	const FLinearColor kTabOn   = RoseTheme::TabOn;
	const FLinearColor kTabOff  = RoseTheme::TabOff;
	const FLinearColor kAccent  = RoseTheme::Accent;
	const FLinearColor kGreen   = RoseTheme::Green;
	const FLinearColor kDim     = RoseTheme::Dim;
	const FLinearColor kText    = RoseTheme::Text;
	const FLinearColor kTextDim = RoseTheme::TextDim;

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	// SKILL_TAB_TYPE → tab index (0 Active / 1 Passive / 2 Basic).  Tab==1 Active,
	// Tab==2 Passive, everything else Basic — same rule as SRoseSkillList.
	int32 TabIndexForSkill(int32 Tab) { return (Tab == 1) ? 0 : (Tab == 2) ? 1 : 2; }

	const TCHAR* kTabLabels[3] = { TEXT("Active"), TEXT("Passive"), TEXT("Basic") };
}

class SRoseSkillPanel : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseSkillPanel) {}
		SLATE_ARGUMENT(TWeakObjectPtr<URoseUIManager>, UI)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		UIWeak = InArgs._UI;

		RowBrush   = MakeShared<FSlateRoundedBoxBrush>(kRow,     8.f);
		IconFrame  = MakeShared<FSlateRoundedBoxBrush>(kPanelHi,  6.f, kAccent, 1.f);
		TabOnBrush = RoseUI::GlassTab(true);
		TabOffBrush= RoseUI::GlassTab(false);
		if (!TabOnBrush.IsValid())  TabOnBrush = MakeShared<FSlateRoundedBoxBrush>(kTabOn,  8.f, kAccent, 1.f);
		if (!TabOffBrush.IsValid()) TabOffBrush= MakeShared<FSlateRoundedBoxBrush>(kTabOff, 8.f);
		PlusOn     = MakeShared<FSlateRoundedBoxBrush>(kGreen,    6.f);
		PlusOff    = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.14f,0.15f,0.18f,0.9f), 6.f);

		// Content only — the draggable frame/title/close come from SRoseModernWindow.
		ChildSlot
		[
			SNew(SBox).WidthOverride(360.f).HeightOverride(440.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ BuildHeader() ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[ BuildTabs() ]
				+ SVerticalBox::Slot().FillHeight(1.f).Padding(0, 8, 0, 0)
				[
					SAssignNew(ListScroll, SScrollBox).Orientation(Orient_Vertical)
				]
			]
		];
		Rebuild();
	}

	// Rebuild when the learned set (SkillRevision) or the active tab changes.
	virtual void Tick(const FGeometry& G, const double T, const float D) override
	{
		SCompoundWidget::Tick(G, T, D);
		URoseSkillComponent* S = Skills();
		const int32 Rev = S ? S->SkillRevision : 0;
		if (Rev != LastRevision || ActiveTab != LastTab)
		{
			LastRevision = Rev;
			LastTab = ActiveTab;
			Rebuild();
		}
	}

private:
	ARoseCharacter* Char() const { return UIWeak.IsValid() ? UIWeak->GetRoseCharacter() : nullptr; }
	URoseSkillComponent* Skills() const { ARoseCharacter* C = Char(); return C ? C->Skills : nullptr; }

	// ── Header: skill-points readout (title/close come from the window chrome) ─
	TSharedRef<SWidget> BuildHeader()
	{
		return SNew(SBox).HeightOverride(18.f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[ SNew(STextBlock).Font(Font(10, true)).ColorAndOpacity(kAccent)
			  .Text_Lambda([this]() {
				URoseSkillComponent* S = Skills();
				return FText::FromString(FString::Printf(TEXT("SP: %d"), S ? S->SkillPoints : 0)); }) ]
		];
	}

	// ── Tabs: Active / Passive / Basic ────────────────────────────────────────
	TSharedRef<SWidget> BuildTabs()
	{
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 i = 0; i < 3; ++i)
		{
			Row->AddSlot().FillWidth(1.f).Padding(i == 0 ? 0 : 4, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0))
				.OnClicked_Lambda([this, i]() { ActiveTab = i; return FReply::Handled(); })
				[
					SNew(SBox).HeightOverride(30.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()
						[ SNew(SImage).Image_Lambda([this, i]() -> const FSlateBrush* {
							return (ActiveTab == i ? TabOnBrush : TabOffBrush).Get(); }) ]
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
						[ SNew(STextBlock).Font(Font(10, true))
						  .ColorAndOpacity_Lambda([this, i]() { return ActiveTab == i ? kText : kTextDim; })
						  .Text(FText::FromString(kTabLabels[i])) ]
					]
				]
			];
		}
		return Row;
	}

	// ── Skill list (learned skills on the active tab) ─────────────────────────
	FSlateBrush* IconBrushFor(int32 SkillId, float Size)
	{
		if (SkillId <= 0) return nullptr;
		if (TSharedPtr<FSlateBrush>* C = IconCache.Find(SkillId)) return C->IsValid() ? C->Get() : nullptr;
		URoseSkillComponent* S = Skills();
		const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
		TSharedPtr<FSlateBrush> B = Row ? RoseUI::MakeSkillIconBrush(Row->IconIdx, Size, KeepTextures) : nullptr;
		IconCache.Add(SkillId, B);
		return B.IsValid() ? B.Get() : nullptr;
	}

	int32 GetBase(int32 SkillId) const
	{
		const FRoseSkillRow* Row = Skills() ? Skills()->GetSkillRow(SkillId) : nullptr;
		return Row ? Row->BaseSkillId : SkillId;
	}
	// Next-level row of a skill's line; -1 when maxed (line-aware — see
	// URoseSkillComponent::NextLevelIdOfLine).
	int32 NextLevelId(int32 SkillId) const
	{
		URoseSkillComponent* S = Skills();
		return S ? S->NextLevelIdOfLine(GetBase(SkillId)) : -1;
	}
	bool CanLevel(int32 SkillId) const
	{
		URoseSkillComponent* S = Skills();
		const int32 Next = NextLevelId(SkillId);
		FString R;
		return S && Next > 0 && S->CanLearn(Next, R);
	}

	// Learned row ids on the active tab, sorted (one per learned line).
	TArray<int32> ActiveTabSkills() const
	{
		TArray<int32> Ids;
		URoseSkillComponent* S = Skills();
		if (!S) return Ids;
		for (const TPair<int32, int32>& P : S->Learned)
		{
			if (P.Value <= 0) continue;
			const FRoseSkillRow* Row = S->GetSkillRow(P.Value);
			if (Row && TabIndexForSkill(Row->Tab) == ActiveTab)
				Ids.Add(P.Value);
		}
		Ids.Sort();
		return Ids;
	}

	void Rebuild()
	{
		if (!ListScroll.IsValid()) return;
		ListScroll->ClearChildren();
		const TArray<int32> Ids = ActiveTabSkills();
		if (Ids.Num() == 0)
		{
			ListScroll->AddSlot().Padding(4, 20)
			[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(kTextDim)
			  .Justification(ETextJustify::Center)
			  .Text(FText::FromString(TEXT("No skills learned on this tab.\nOpen the skill tree (X) to learn skills."))) ];
			return;
		}
		for (int32 Id : Ids)
			ListScroll->AddSlot().Padding(0, 0, 0, 5)[ MakeRow(Id) ];
	}

	// One skill row: icon + "Name (Lv:N)" + cost/cooldown line + "+" level-up.
	TSharedRef<SWidget> MakeRow(int32 SkillId)
	{
		return SNew(SBox).HeightOverride(54.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(RowBrush.Get()) ]
			+ SOverlay::Slot().Padding(6.f)
			[
				SNew(SHorizontalBox)
				// Icon: click → hotbar, shift+click → cast, DRAG → drop on a hotbar slot.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SRoseSkillDragBox)
					.ToolTipText_Lambda([this, SkillId]() { return RowTooltip(SkillId); })
					.OnClick_Lambda([this, SkillId]() { AssignHotbar(SkillId); })
					.OnShiftClick_Lambda([this, SkillId]() { CastCurrent(SkillId); })
					.OnBeginDrag_Lambda([this, SkillId]() -> TSharedPtr<FRoseSkillDragOp> {
						URoseSkillComponent* S = Skills();
						if (!S) return nullptr;
						const int32 Cur = S->LearnedIdOfLine(GetBase(SkillId));
						const FRoseSkillRow* Row = (Cur > 0) ? S->GetSkillRow(Cur) : nullptr;
						// Passives can't go on the hotbar.
						if (Cur <= 0 || (Row && TabIndexForSkill(Row->Tab) == 1)) return nullptr;
						TSharedRef<FRoseSkillDragOp> Op = MakeShared<FRoseSkillDragOp>();
						Op->SkillId = Cur;
						Op->Icon = IconBrushFor(SkillId, 42.f);
						return Op;
					})
					[
						SNew(SBox).WidthOverride(42.f).HeightOverride(42.f)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()[ SNew(SImage).Image(IconFrame.Get()) ]
							+ SOverlay::Slot().Padding(2.f)
							[ SNew(SImage).Image_Lambda([this, SkillId]() -> const FSlateBrush* {
								return IconBrushFor(SkillId, 38.f); }) ]
						]
					]
				]
				// Name + cost/cooldown line.
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8, 0, 0, 0)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[ SNew(STextBlock).Font(Font(11, true)).ColorAndOpacity(kText)
					  .Text_Lambda([this, SkillId]() { return FText::FromString(NameWithLevel(SkillId)); }) ]
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
					[ SNew(STextBlock).Font(Font(8)).ColorAndOpacity(kTextDim)
					  .Text_Lambda([this, SkillId]() { return FText::FromString(CostLine(SkillId)); }) ]
				]
				// "+" level-up button (green when levelable, dim otherwise).
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0))
					.IsEnabled_Lambda([this, SkillId]() { return CanLevel(SkillId); })
					.ToolTipText_Lambda([this, SkillId]() {
						URoseSkillComponent* S = Skills();
						const FRoseSkillRow* Next = S ? S->GetSkillRow(NextLevelId(SkillId)) : nullptr;
						if (!Next) return FText::FromString(TEXT("Max level"));
						FString R;
						return FText::FromString(S->CanLearn(NextLevelId(SkillId), R)
							? FString::Printf(TEXT("Level up (%d SP)"), Next->PointCost) : R);
					})
					.OnClicked_Lambda([this, SkillId]() {
						if (URoseSkillComponent* S = Skills()) S->LearnSkill(NextLevelId(SkillId));
						return FReply::Handled();
					})
					[
						SNew(SBox).WidthOverride(30.f).HeightOverride(30.f)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()[ SNew(SImage).Image_Lambda([this, SkillId]() -> const FSlateBrush* {
								return (CanLevel(SkillId) ? PlusOn : PlusOff).Get(); }) ]
							+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
							[ SNew(STextBlock).Font(Font(14, true))
							  .ColorAndOpacity_Lambda([this, SkillId]() { return CanLevel(SkillId) ? kText : kDim; })
							  .Text(FText::FromString(TEXT("+"))) ]
						]
					]
				]
			]
		];
	}

	FString NameWithLevel(int32 SkillId) const
	{
		URoseSkillComponent* S = Skills();
		const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
		if (!Row) return FString();
		const int32 Lv = S->LearnedLevelOfLine(Row->BaseSkillId);
		const FString Nm = Row->DisplayName.IsEmpty() ? Row->SkillName : Row->DisplayName;
		return FString::Printf(TEXT("%s (Lv:%d)"), *Nm, FMath::Max(1, Lv));
	}

	FString CostLine(int32 SkillId) const
	{
		URoseSkillComponent* S = Skills();
		const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
		if (!Row) return FString();
		TArray<FString> Parts;
		if (Row->UseAbility1 == 17 && Row->UseAmount1 > 0)   // AT_MP
			Parts.Add(FString::Printf(TEXT("MP %d"), Row->UseAmount1));
		if (Row->CooldownSec > 0.f)
			Parts.Add(FString::Printf(TEXT("CD %.1fs"), Row->CooldownSec));
		if (ActiveTab == 1)   // Passive tab
			Parts.Add(TEXT("Passive"));
		return FString::Join(Parts, TEXT("   "));
	}

	FText RowTooltip(int32 SkillId) const
	{
		URoseSkillComponent* S = Skills();
		const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
		if (!Row) return FText::GetEmpty();
		FString Tip = Row->DisplayName.IsEmpty() ? Row->SkillName : Row->DisplayName;
		if (!Row->Description.IsEmpty()) Tip += TEXT("\n") + Row->Description;
		Tip += (ActiveTab == 1)   // passives can't cast/hotbar
			? FString(TEXT("\n(passive — always active)"))
			: FString(TEXT("\nclick: to hotbar   shift+click: cast"));
		return FText::FromString(Tip);
	}

	// Assign the skill's current learned level to the first free hotbar slot.
	void AssignHotbar(int32 SkillId)
	{
		URoseSkillComponent* S = Skills();
		if (!S) return;
		const int32 Cur = S->LearnedIdOfLine(GetBase(SkillId));
		if (Cur <= 0) return;
		const FRoseSkillRow* Row = S->GetSkillRow(Cur);
		if (Row && TabIndexForSkill(Row->Tab) == 1) return;   // passives have no hotbar action
		int32 Free = INDEX_NONE;
		for (int32 i = 0; i < S->Hotbar.Num(); ++i)
			if (S->Hotbar[i] <= 0) { Free = i; break; }
		S->SetHotbarSlot(Free != INDEX_NONE ? Free : 0, Cur);
	}

	// Cast the skill's current learned level (shift+click).
	void CastCurrent(int32 SkillId)
	{
		URoseSkillComponent* S = Skills();
		if (!S) return;
		const int32 Cur = S->LearnedIdOfLine(GetBase(SkillId));
		if (Cur > 0) S->CastSkill(Cur);
	}

	TWeakObjectPtr<URoseUIManager> UIWeak;
	TSharedPtr<SScrollBox> ListScroll;
	TMap<int32, TSharedPtr<FSlateBrush>> IconCache;
	TArray<TStrongObjectPtr<UTexture2D>> KeepTextures;
	TSharedPtr<FSlateBrush> RowBrush, IconFrame,
		TabOnBrush, TabOffBrush, PlusOn, PlusOff;
	int32 ActiveTab = 0;      // 0 Active / 1 Passive / 2 Basic
	int32 LastTab = -1;
	int32 LastRevision = -1;
};

TSharedRef<SWidget> RoseSkillPanel_Make(URoseUIManager& UI)
{
	return SNew(SRoseSkillPanel).UI(&UI);
}
