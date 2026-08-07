// ROSE skills UI — dlgskill (learned skills + hotbar assignment) and the skill
// TREES (learn / level up skills spending skill points), wired to
// URoseSkillComponent.  Registered by RoseUISkills_Register (from
// URoseUIManager::BeginPlay).
//
// Faithful to the classic client:
//   src/client/interface/dlgs/cskilldlg.cpp     (dlgskill: learned-skill pages)
//   src/client/interface/dlgs/skilltreedlg.cpp  (skill tree: node layout + icons)
// The tree node layout comes from the converted XML node trees
// (Content/UI/Layouts/skilltree_{soldier,muse,howker,dealer}.json) — each SKILL
// node carries INDEX (base skill row = SKILL_1LEV_INDEX), OFFSETX/OFFSETY (window-
// relative px), an optional LEVEL (default 1) and LIMITLEVEL.  The client resolves
// a node to the concrete skill LEVEL row via `skillindex + level - 1`
// (CSkillTreeDlg::MakeIcon, skilltreedlg.cpp:205-223) — consecutive levels are
// consecutive LIST_SKILL row ids — which is exactly our skills.csv row Id.  The
// frame art (SKILL_TREE_DLG_*) is the single dlgskilltree layout; the per-job node
// layout is overlaid on top, mirroring the client (one dialog + a job-selected XML,
// skilltreedlg.cpp:116-136).
#include "RoseUIManager.h"
#include "RoseUIWindow.h"        // SRoseUIWindow::IsTabActive (tab-filtered skill list)
#include "RoseUIHelpers.h"
#include "RoseUITheme.h"
#include "RoseUIDrag.h"          // SRoseSkillDragBox (tree nodes drag to the quickbar)
#include "RoseCharacter.h"
#include "RoseSkillComponent.h"
#include "RoseSkillTypes.h"

#include "Dom/JsonObject.h"
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

// ─────────────────────────────────────────────────────────────────────────────
//  Shared helpers.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	// The four job trees, keyed by the client's Get_JOB()/100 bucket
	// (skilltreedlg.cpp:117-133): 1→soldier, 2→muse, 3→howker, 4→dealer.
	struct FJobTree { const TCHAR* Id; const TCHAR* Layout; int32 Bucket; };
	const FJobTree GJobTrees[] = {
		{ TEXT("skilltree_soldier"), TEXT("skilltree_soldier"), 1 },
		{ TEXT("skilltree_muse"),    TEXT("skilltree_muse"),    2 },
		{ TEXT("skilltree_howker"),  TEXT("skilltree_howker"),  3 },
		{ TEXT("skilltree_dealer"),  TEXT("skilltree_dealer"),  4 },
	};

	// Job (AT_CLASS) → job bucket.  111/121/122 Soldier line → 1; 2xx Muse → 2;
	// 3xx Hawker → 3; 4xx Dealer → 4; 0 (Visitor) defaults to Soldier, matching
	// the client's default-to-1 fall-through when no tree is selected.
	int32 JobBucket(int32 Job)
	{
		if (Job >= 200 && Job < 300) return 2;
		if (Job >= 300 && Job < 400) return 3;
		if (Job >= 400 && Job < 500) return 4;
		return 1;   // Soldier line (111/121/122) and Visitor(0)
	}

	const TCHAR* LayoutForBucket(int32 Bucket)
	{
		for (const FJobTree& T : GJobTrees)
			if (T.Bucket == Bucket)
				return T.Layout;
		return GJobTrees[0].Layout;
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  dlgskill content overlay — the LEARNED skills, one icon cell per skill.
//  ROSE pages learned skills by SKILL_TAB_TYPE (Basic/Active/Passive tabs); we
//  follow the layout's tabs (IsTabActive) and only show the skills whose row Tab
//  matches the active page.  Click → assign to the first free hotbar slot;
//  Shift+click → cast.  Layout cells come from the dlgskill listbox slots if the
//  layout authors them, else a simple grid.
// ─────────────────────────────────────────────────────────────────────────────
class SRoseSkillList : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseSkillList) {}
		SLATE_ARGUMENT(TWeakObjectPtr<URoseUIManager>, UI)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		UIWeak = InArgs._UI;
		// Faithful list region: the dlgskill ZLISTBOX at ABS (0,65), ~190x310,
		// 44px rows (CSkillListItem::SetHeight(44)).  A vertical SScrollBox fills
		// it top-down and rebuilds when the learned set or active tab changes.
		ChildSlot
		[
			SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot()
			.Offset(FMargin(6.f, 65.f, 196.f, 310.f))
			.Alignment(FVector2D(0, 0))
			[
				SNew(SBox).WidthOverride(196.f).HeightOverride(310.f)
				.Clipping(EWidgetClipping::ClipToBounds)
				[ SAssignNew(ListScroll, SScrollBox).Orientation(Orient_Vertical) ]
			]
		];
		Rebuild();
	}

	// Rebuild when the learned set (SkillRevision) or the active tab changes.
	virtual void Tick(const FGeometry& G, const double T, const float D) override
	{
		SCompoundWidget::Tick(G, T, D);
		URoseSkillComponent* S = SkillComp();
		const int32 Rev = S ? S->SkillRevision : 0;
		const FString Tab = ActiveTabTag();
		if (Rev != LastRevision || Tab != LastTab)
		{
			LastRevision = Rev;
			LastTab = Tab;
			Rebuild();
		}
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

	// Cached icon brush per skill id (textures pinned in KeepTextures which this
	// widget owns, so they outlive every brush).
	FSlateBrush* IconBrushFor(int32 SkillId, float Size)
	{
		if (SkillId <= 0)
			return nullptr;
		if (TSharedPtr<FSlateBrush>* Cached = IconCache.Find(SkillId))
			return Cached->IsValid() ? Cached->Get() : nullptr;
		URoseSkillComponent* S = SkillComp();
		const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
		TSharedPtr<FSlateBrush> Brush = Row
			? RoseUI::MakeSkillIconBrush(Row->IconIdx, Size, KeepTextures)
			: nullptr;
		IconCache.Add(SkillId, Brush);
		return Brush.IsValid() ? Brush.Get() : nullptr;
	}

	// A hotbar slot is free when it holds no skill (-1 / <=0).
	void AssignToHotbar(int32 SkillId)
	{
		URoseSkillComponent* S = SkillComp();
		if (!S || SkillId <= 0)
			return;
		int32 Free = INDEX_NONE;
		for (int32 i = 0; i < S->Hotbar.Num(); ++i)
			if (S->Hotbar[i] <= 0) { Free = i; break; }
		S->SetHotbarSlot(Free != INDEX_NONE ? Free : 0, SkillId);   // replace slot 0 when full
	}

	// dlgskill tab tag for the current row's SKILL_TAB_TYPE (skills.csv `Tab`,
	// SKILL_TAB_TYPE = LIST_SKILL col 4).  Verified against the data + dlgskill's
	// three TABBUTTONs (UI09_BTN_{ACTIVE,PASSIVE,BASIC}_NORMAL): **1 = Active**
	// (all castable action skills), **2 = Passive** (all SkillType-15), **0/else
	// = Basic** (the common everyone-has skills).  We match by IsTabActive.
	static const TCHAR* TabTagFor(int32 Tab)
	{
		return (Tab == 1) ? TEXT("ACTIVE")
			: (Tab == 2) ? TEXT("PASSIVE") : TEXT("BASIC");
	}

	// The window's currently-active tab tag; dlgskill opens on ACTIVE (first tab).
	FString ActiveTabTag() const
	{
		TSharedPtr<SRoseUIWindow> Win = UIWeak.IsValid() ? UIWeak->GetWindow(TEXT("skill")) : nullptr;
		if (Win.IsValid())
			for (const TCHAR* T : { TEXT("BASIC"), TEXT("ACTIVE"), TEXT("PASSIVE") })
				if (Win->IsTabActive(T))
					return T;
		return TEXT("ACTIVE");
	}

	// Every skill LINE this character's job can learn — learned or not.
	//
	// The window used to list only LEARNED skills, paged into the layout's
	// Active/Passive/Basic tabs.  That cannot show a tree: a skill you have not
	// learned yet was invisible, so there was nowhere to learn it from.  One
	// list of lines, gated by job, is the tree.
	//
	// Keyed on the LEVEL-1 row of each line (SkillLevel == 1).  Row ids run
	// consecutively ACROSS lines, so a line is identified by its base row, never
	// by arithmetic on an id.
	TArray<int32> JobTreeLines() const
	{
		TArray<int32> Lines;
		URoseSkillComponent* S = SkillComp();
		UDataTable* T = S->SkillTable;
		if (!S || !T) return Lines;

		for (const FName& RN : T->GetRowNames())
		{
			const FRoseSkillRow* Row = T->FindRow<FRoseSkillRow>(RN, TEXT("tree"));
			// <= 1, not == 1.  ROSE's BASIC skills (Sit 11, Pick Up 12, Jump 13,
			// Drive Cart 17/23, Drive Castle Gear 24, Ride Request 25) are stored
			// at SkillLevel 0, so an == 1 test drops every one of them.
			if (!Row || Row->SkillLevel > 1)
				continue;
			// Unnamed rows are table padding, not content.
			if (Row->DisplayName.IsEmpty() && Row->SkillName.IsEmpty())
				continue;
			if (!S->JobInClassSet(Row->RequiredClassSet))
				continue;
			Lines.AddUnique(Row->BaseSkillId > 0 ? Row->BaseSkillId : Row->Id);
		}
		Lines.Sort();
		return Lines;
	}

	int32 GetBase(int32 SkillId) const
	{
		const FRoseSkillRow* Row = SkillComp() ? SkillComp()->GetSkillRow(SkillId) : nullptr;
		return Row ? Row->BaseSkillId : SkillId;
	}
	// Next-level row id of a skill's line; -1 when maxed (line-aware — see
	// URoseSkillComponent::NextLevelIdOfLine).
	int32 NextLevelId(int32 SkillId) const
	{
		URoseSkillComponent* S = SkillComp();
		return S ? S->NextLevelIdOfLine(GetBase(SkillId)) : -1;
	}
	// True when the skill can still level up (next level exists + affordable).
	bool CanLevel(int32 SkillId) const
	{
		URoseSkillComponent* S = SkillComp();
		const int32 Next = NextLevelId(SkillId);
		FString R;
		return S && Next > 0 && S->CanLearn(Next, R);
	}

	// Rebuild the row list for the active tab.
	void Rebuild()
	{
		if (!ListScroll.IsValid())
			return;
		ListScroll->ClearChildren();
		for (int32 Id : JobTreeLines())
			ListScroll->AddSlot()[ MakeRow(Id) ];
	}

	// One 44px skill row: icon (17,4) + "Name(Lv:N)" (50,5) + "+" level-up button
	// (171,24), faithful to CSkillListItem (cskilllistitem.cpp).
	TSharedRef<SWidget> MakeRow(int32 SkillId)
	{
		if (!PlusNormal.IsValid())
		{
			PlusNormal  = RoseUI::MakeSpriteBrush(TEXT("UI09_BTN_PLUS_NORMAL"),  17, 17, KeepTextures);
			PlusDisable = RoseUI::MakeSpriteBrush(TEXT("UI09_BTN_PLUS_DISABLE"), 17, 17, KeepTextures);
		}
		return SNew(SBox).WidthOverride(190.f).HeightOverride(44.f)
		[
			SNew(SConstraintCanvas)
			// Icon (17,4), 38px — click → first free hotbar slot; shift+click cast.
			+ SConstraintCanvas::Slot()
			.Offset(FMargin(17.f, 4.f, 38.f, 38.f)).Alignment(FVector2D(0, 0))
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0))
				.ToolTipText_Lambda([this, SkillId]() {
					URoseSkillComponent* S = SkillComp();
					const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
					if (!Row) return FText::GetEmpty();
					FString Tip = (Row->DisplayName.IsEmpty() ? Row->SkillName : Row->DisplayName);
					if (!Row->Description.IsEmpty()) Tip += TEXT("\n") + Row->Description;
					Tip += TEXT("\nclick: to hotbar   shift+click: cast");
					return FText::FromString(Tip);
				})
				.OnClicked_Lambda([this, SkillId]() {
					URoseSkillComponent* S = SkillComp();
					if (S)
					{
						const int32 Cur = S->LearnedIdOfLine(GetBase(SkillId));
						// Unlearned: nothing to hotbar or cast.  Without this the
						// tree's unlearned rows pushed -1 into a hotbar slot.
						if (Cur <= 0)
							return FReply::Handled();
						if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
							S->CastSkill(Cur);
						else
						{
							int32 Free = INDEX_NONE;
							for (int32 i = 0; i < S->Hotbar.Num(); ++i)
								if (S->Hotbar[i] <= 0) { Free = i; break; }
							S->SetHotbarSlot(Free != INDEX_NONE ? Free : 0, Cur);
						}
					}
					return FReply::Handled();
				})
				[ SNew(SImage).Image_Lambda([this, SkillId]() -> const FSlateBrush* {
					return IconBrushFor(SkillId, 38.f); }) ]
			]
			// "Name (Lv:N)" at (50,5).
			+ SConstraintCanvas::Slot()
			.Offset(FMargin(50.f, 5.f, 118.f, 18.f)).Alignment(FVector2D(0, 0))
			[
				SNew(STextBlock)
				.Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				.ColorAndOpacity(FSlateColor(FLinearColor(0.95f, 0.92f, 0.8f)))
				.Text_Lambda([this, SkillId]() {
					URoseSkillComponent* S = SkillComp();
					const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
					if (!Row) return FText::GetEmpty();
					const int32 Lv = S->LearnedLevelOfLine(Row->BaseSkillId);
					const FString Nm = Row->DisplayName.IsEmpty() ? Row->SkillName : Row->DisplayName;
					// Lv < 0 means the line is UNLEARNED.  Clamping to 1 (what this
					// did) claimed you already had it.
					return FText::FromString(Lv < 0
						? FString::Printf(TEXT("%s (not learned)"), *Nm)
						: FString::Printf(TEXT("%s (Lv:%d)"), *Nm, Lv));
				})
			]
			// "+" level-up button at (171,24): NORMAL (green) when levelable,
			// DISABLE (grey) otherwise; click levels the skill up.
			+ SConstraintCanvas::Slot()
			.Offset(FMargin(171.f, 24.f, 17.f, 17.f)).Alignment(FVector2D(0, 0))
			[
				SNew(SButton)
				.ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0))
				.IsEnabled_Lambda([this, SkillId]() { return CanLevel(SkillId); })
				.ToolTipText_Lambda([this, SkillId]() {
					URoseSkillComponent* S = SkillComp();
					const FRoseSkillRow* Next = S ? S->GetSkillRow(NextLevelId(SkillId)) : nullptr;
					if (!Next) return FText::FromString(TEXT("Max level"));
					FString R;
					return FText::FromString(S->CanLearn(NextLevelId(SkillId), R)
						? FString::Printf(TEXT("Level up (%d SP)"), Next->PointCost) : R);
				})
				.OnClicked_Lambda([this, SkillId]() {
					if (URoseSkillComponent* S = SkillComp())
						S->LearnSkill(NextLevelId(SkillId));
					return FReply::Handled();
				})
				[ SNew(SImage).Image_Lambda([this, SkillId]() -> const FSlateBrush* {
					return (CanLevel(SkillId) ? PlusNormal : PlusDisable).Get(); }) ]
			]
		];
	}

	bool RowMatchesActiveTab(const FRoseSkillRow* Row) const
	{
		if (!UIWeak.IsValid())
			return true;
		TSharedPtr<SRoseUIWindow> Win = UIWeak->GetWindow(TEXT("skill"));
		if (!Win.IsValid())
			return true;
		const TCHAR* TabTag = TabTagFor(Row ? Row->Tab : 0);
		// If none of our known tags is active (layout without tabs), show all.
		if (!Win->IsTabActive(TEXT("BASIC")) && !Win->IsTabActive(TEXT("ACTIVE"))
			&& !Win->IsTabActive(TEXT("PASSIVE")))
			return true;
		return Win->IsTabActive(TabTag);
	}

	// The learned skills, as (row id) in a stable order (by row id).
	TArray<int32> LearnedIds() const
	{
		TArray<int32> Ids;
		if (URoseSkillComponent* S = SkillComp())
			for (const TPair<int32, int32>& P : S->Learned)
				if (P.Value > 0)
					Ids.Add(P.Value);
		Ids.Sort();
		return Ids;
	}

	TSharedRef<SWidget> BuildGrid()
	{
		// dlgskill's content area starts under the caption/top art (~y=66) and is
		// 223 wide.  Lay learned skills out as a 40px grid of 5 columns; each cell
		// is filtered per-frame by the active tab, so tab switches re-page without
		// a rebuild.  Positions are stable per learned-skill index.
		const float Cell = 42.f;
		const float IconSz = 32.f;
		const FVector2D Origin(14.f, 72.f);
		const int32 Cols = 5;
		const int32 MaxCells = 40;   // 8 rows — the dlgskill list capacity

		TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
		for (int32 i = 0; i < MaxCells; ++i)
		{
			const int32 Col = i % Cols;
			const int32 Rowi = i / Cols;
			const float X = Origin.X + Col * Cell;
			const float Y = Origin.Y + Rowi * Cell;
			Canvas->AddSlot()
				.Offset(FMargin(X, Y, Cell, Cell))
				.Alignment(FVector2D(0, 0))
				[ MakeCell(i, IconSz) ];
		}
		return Canvas;
	}

	// Skill id shown in visible-cell #Index (after tab filtering), 0 if none.
	int32 CellSkillId(int32 Index) const
	{
		const TArray<int32> Ids = LearnedIds();
		int32 Visible = 0;
		for (int32 Id : Ids)
		{
			const FRoseSkillRow* Row = SkillComp() ? SkillComp()->GetSkillRow(Id) : nullptr;
			if (!RowMatchesActiveTab(Row))
				continue;
			if (Visible == Index)
				return Id;
			++Visible;
		}
		return 0;
	}

	TSharedRef<SWidget> MakeCell(int32 Index, float IconSz)
	{
		return SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.ContentPadding(FMargin(0))
			.Visibility_Lambda([this, Index]() {
				return CellSkillId(Index) > 0 ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.ToolTipText_Lambda([this, Index]() {
				const int32 Id = CellSkillId(Index);
				URoseSkillComponent* S = SkillComp();
				const FRoseSkillRow* Row = (S && Id > 0) ? S->GetSkillRow(Id) : nullptr;
				if (!Row)
					return FText::GetEmpty();
				FString Tip = Row->DisplayName.IsEmpty() ? Row->SkillName : Row->DisplayName;
				Tip += FString::Printf(TEXT(" (Lv %d)"), Row->SkillLevel);
				if (!Row->Description.IsEmpty())
					Tip += TEXT("\n") + Row->Description;
				if (Row->UseAbility1 == 17 && Row->UseAmount1 > 0)   // AT_MP
					Tip += FString::Printf(TEXT("\nMP %d"), Row->UseAmount1);
				if (Row->CooldownSec > 0.f)
					Tip += FString::Printf(TEXT("\nCooldown %.1fs"), Row->CooldownSec);
				Tip += TEXT("\nclick: to hotbar   shift+click: cast");
				return FText::FromString(Tip);
			})
			.OnClicked_Lambda([this, Index]() {
				const int32 Id = CellSkillId(Index);
				URoseSkillComponent* S = SkillComp();
				if (S && Id > 0)
				{
					if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
						S->CastSkill(Id);
					else
						AssignToHotbar(Id);
				}
				return FReply::Handled();
			})
			[
				SNew(SBox).WidthOverride(IconSz + 8.f).HeightOverride(IconSz + 8.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					.HAlign(HAlign_Center).VAlign(VAlign_Top)
					[
						SNew(SBox).WidthOverride(IconSz).HeightOverride(IconSz)
						[
							SNew(SImage)
							.Image_Lambda([this, Index, IconSz]() -> const FSlateBrush* {
								return IconBrushFor(CellSkillId(Index), IconSz);
							})
						]
					]
					// Skill level, bottom-right.
					+ SOverlay::Slot()
					.HAlign(HAlign_Right).VAlign(VAlign_Bottom)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 8))
						.ColorAndOpacity(FSlateColor(FLinearColor::White))
						.Text_Lambda([this, Index]() {
							const int32 Id = CellSkillId(Index);
							const FRoseSkillRow* Row = (SkillComp() && Id > 0)
								? SkillComp()->GetSkillRow(Id) : nullptr;
							return Row ? FText::FromString(FString::Printf(TEXT("%d"), Row->SkillLevel))
							           : FText::GetEmpty();
						})
					]
				]
			];
	}

	TWeakObjectPtr<URoseUIManager> UIWeak;
	TSharedPtr<SScrollBox> ListScroll;
	TMap<int32, TSharedPtr<FSlateBrush>> IconCache;
	TArray<TStrongObjectPtr<UTexture2D>> KeepTextures;
	TSharedPtr<FSlateBrush> PlusNormal, PlusDisable;
	int32 LastRevision = -1;
	FString LastTab;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Skill-tree content — OUR OWN layout (the client's absolute-offset node art
//  made every base's children overlap; this replaces it entirely).  The authored
//  node tree (Content/UI/Layouts/skilltree_<job>.json) is still the data source
//  — it defines WHICH skills are in the job's tree and their prerequisite edges
//  — but the presentation is a clean scrolling list: one card per skill LINE,
//  the base skill at the left and its descendants chained to the right
//  (base › child › grandchild; branches stack vertically).  Every line is
//  always visible — no master-detail click-to-reveal.
//
//  Node states (live-bound, no rebuild needed):
//    accent frame + full icon + "Lv N"  — learned (click levels it further)
//    green frame  + full icon + "+"     — learnable now (click to learn)
//    dark frame   + dimmed icon         — locked (tooltip says why)
//  The header shows remaining SkillPoints; tooltips carry name/description/
//  cost/MP/cooldown + the exact CanLearn reason when locked.
// ─────────────────────────────────────────────────────────────────────────────
class SRoseSkillTree : public SCompoundWidget
{
	// One authored node: a skill row id + its prerequisite children.
	struct FNode
	{
		int32 SkillId = 0;
		TArray<TSharedPtr<FNode>> Kids;
	};

public:
	SLATE_BEGIN_ARGS(SRoseSkillTree) : _Bucket(1) {}
		SLATE_ARGUMENT(TWeakObjectPtr<URoseUIManager>, UI)
		SLATE_ARGUMENT(FString, Layout)
		SLATE_ARGUMENT(int32, Bucket)      // 1 Soldier / 2 Muse / 3 Hawker / 4 Dealer line
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		UIWeak = InArgs._UI;
		LayoutName = InArgs._Layout;
		Bucket = FMath::Clamp(InArgs._Bucket, 1, 4);

		CardBrush      = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::Row, 10.f);
		HeaderBrush    = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::PanelHi, 10.f);
		FrameLearned   = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::PanelHi, 8.f, RoseTheme::Accent, 2.f);
		FrameLearnable = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::PanelHi, 8.f, RoseTheme::Green, 2.f);
		FrameLocked    = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.10f, 0.10f, 0.12f, 0.95f), 8.f,
			FLinearColor(0.f, 0.f, 0.f, 0.6f), 1.f);

		// Parse the authored tree: top-level SKILL nodes are the base skills,
		// their descendants that base's prerequisite chain (skilltreedlg.cpp).
		TArray<TSharedPtr<FNode>> Bases;
		if (TSharedPtr<FJsonObject> Root = RoseUI::LoadLayout(LayoutName))
		{
			const TArray<TSharedPtr<FJsonValue>>* Kids = nullptr;
			if (Root->TryGetArrayField(TEXT("children"), Kids))
				for (const TSharedPtr<FJsonValue>& K : *Kids)
					if (TSharedPtr<FNode> N = ParseNode(K.IsValid() ? K->AsObject() : nullptr))
						Bases.Add(N);
		}

		// Note: missing skill families are authored INTO the layout JSONs (e.g.
		// skilltree_muse.json's buffs section — the client XML omitted the whole
		// support/blessing family), so the layout stays the single source.

		TSharedRef<SScrollBox> List = SNew(SScrollBox).Orientation(Orient_Vertical);
		for (const TSharedPtr<FNode>& B : Bases)
			List->AddSlot().Padding(0, 0, 0, 6)[ MakeLineCard(B.ToSharedRef()) ];

		ChildSlot
		[
			SNew(SBox).WidthOverride(640.f).HeightOverride(560.f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ BuildHeader() ]
				+ SVerticalBox::Slot().FillHeight(1.f).Padding(0, 8, 0, 0)[ List ]
			]
		];
	}

private:
	// index + level - 1 = the concrete skill LEVEL row (skilltreedlg.cpp:219).
	TSharedPtr<FNode> ParseNode(const TSharedPtr<FJsonObject>& Obj)
	{
		if (!Obj.IsValid() || !Obj->HasField(TEXT("index")))
			return nullptr;
		const int32 Index = (int32)RoseUI::Num(Obj, TEXT("index"));
		const int32 Level = FMath::Max(1, (int32)RoseUI::Num(Obj, TEXT("level"), 1.f));
		TSharedPtr<FNode> N = MakeShared<FNode>();
		N->SkillId = Index + Level - 1;
		if (N->SkillId <= 0)
			return nullptr;
		const TArray<TSharedPtr<FJsonValue>>* Kids = nullptr;
		if (Obj->TryGetArrayField(TEXT("children"), Kids))
			for (const TSharedPtr<FJsonValue>& K : *Kids)
				if (TSharedPtr<FNode> C = ParseNode(K.IsValid() ? K->AsObject() : nullptr))
					N->Kids.Add(C);
		return N;
	}

	ARoseCharacter* Char() const
	{
		return UIWeak.IsValid() ? UIWeak->GetRoseCharacter() : nullptr;
	}
	URoseSkillComponent* SkillComp() const
	{
		ARoseCharacter* C = Char();
		return C ? C->Skills : nullptr;
	}

	// ── Header: legend + live SP readout ──────────────────────────────────────
	TSharedRef<SWidget> BuildHeader()
	{
		return SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(HeaderBrush.Get()) ]
			+ SOverlay::Slot().Padding(10.f, 7.f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
				[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
				  .ColorAndOpacity(RoseTheme::TextDim)
				  .Text(FText::FromString(TEXT("Click a skill to learn or level it  —  green = learnable now"))) ]
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
				  .ColorAndOpacity(FSlateColor(FLinearColor(1.f, 0.95f, 0.6f)))
				  .Text_Lambda([this]() {
					URoseSkillComponent* S = SkillComp();
					return FText::FromString(FString::Printf(TEXT("Skill Points: %d"), S ? S->SkillPoints : 0)); }) ]
			];
	}

	// ── One card per skill line: name column + the prerequisite chain ─────────
	TSharedRef<SWidget> MakeLineCard(const TSharedRef<FNode>& Base)
	{
		return SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(CardBrush.Get()) ]
			+ SOverlay::Slot().Padding(10.f, 8.f)
			[
				SNew(SHorizontalBox)
				// Line name — fixed left column so the cards read as a table.
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[
					SNew(SBox).WidthOverride(130.f)
					[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					  .ColorAndOpacity(RoseTheme::Text).AutoWrapText(true)
					  .Text(FText::FromString(LineName(Base->SkillId))) ]
				]
				// The chain, horizontally scrollable when a line runs deep.
				+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(6, 0, 0, 0)
				[
					SNew(SScrollBox).Orientation(Orient_Horizontal)
					.ScrollBarVisibility(EVisibility::Collapsed)
					+ SScrollBox::Slot()[ MakeChain(Base) ]
				]
			];
	}

	FString LineName(int32 SkillId) const
	{
		URoseSkillComponent* S = SkillComp();
		const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
		if (!Row) return FString::Printf(TEXT("skill %d"), SkillId);
		return Row->DisplayName.IsEmpty() ? Row->SkillName : Row->DisplayName;
	}

	// node › (children stacked vertically, each chaining further right).
	TSharedRef<SWidget> MakeChain(const TSharedRef<FNode>& Node)
	{
		TSharedRef<SHorizontalBox> H = SNew(SHorizontalBox);
		H->AddSlot().AutoWidth().VAlign(VAlign_Center)[ MakeNodeCard(Node->SkillId) ];
		if (Node->Kids.Num() > 0)
		{
			H->AddSlot().AutoWidth().VAlign(VAlign_Center).Padding(3, 0)
			[ SNew(STextBlock).Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
			  .ColorAndOpacity(RoseTheme::Dim).Text(FText::FromString(TEXT("›"))) ];
			TSharedRef<SVerticalBox> V = SNew(SVerticalBox);
			for (const TSharedPtr<FNode>& K : Node->Kids)
				V->AddSlot().AutoHeight().Padding(0, 2)[ MakeChain(K.ToSharedRef()) ];
			H->AddSlot().AutoWidth().VAlign(VAlign_Center)[ V ];
		}
		return H;
	}

	FSlateBrush* IconBrushFor(int32 SkillId, float Size)
	{
		if (SkillId <= 0)
			return nullptr;
		if (TSharedPtr<FSlateBrush>* Cached = IconCache.Find(SkillId))
			return Cached->IsValid() ? Cached->Get() : nullptr;
		URoseSkillComponent* S = SkillComp();
		const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
		TSharedPtr<FSlateBrush> Brush = Row
			? RoseUI::MakeSkillIconBrush(Row->IconIdx, Size, KeepTextures)
			: nullptr;
		IconCache.Add(SkillId, Brush);
		return Brush.IsValid() ? Brush.Get() : nullptr;
	}

	// A tree node is "owned" when its LINE is learned to at least this node's
	// level.  (NOT IsLearned() — that wants the exact current row id, so leveling
	// a line past a node's level would wrongly grey the lower node, which read as
	// the skill being "removed" on level-up.)
	bool IsNodeLearned(int32 SkillId) const
	{
		URoseSkillComponent* S = SkillComp();
		const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
		return Row && S->LearnedLevelOfLine(Row->BaseSkillId) >= Row->SkillLevel;
	}

	// The row id a click on this node would learn: the line's next level, or
	// -1 when the line is maxed (line-aware — NextLevelIdOfLine).
	int32 NextLearnId(int32 SkillId) const
	{
		URoseSkillComponent* S = SkillComp();
		const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
		const int32 Base = Row && Row->BaseSkillId ? Row->BaseSkillId : SkillId;
		return S ? S->NextLevelIdOfLine(Base) : -1;
	}
	// Unlearned node that CanLearn right now → green frame + "+".
	bool IsNodeLearnable(int32 SkillId) const
	{
		if (IsNodeLearned(SkillId)) return false;
		URoseSkillComponent* S = SkillComp();
		FString R;
		return S && S->CanLearn(SkillId, R);
	}

	TSharedRef<SWidget> MakeNodeCard(int32 SkillId)
	{
		const float NodeSz = 46.f;
		// Drag box, not a button: click = learn/level, drag a LEARNED active
		// skill onto the quickbar to assign it.
		return SNew(SRoseSkillDragBox)
			.OnBeginDrag_Lambda([this, SkillId]() -> TSharedPtr<FRoseSkillDragOp> {
				URoseSkillComponent* S = SkillComp();
				const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
				if (!Row) return nullptr;
				const int32 Base = Row->BaseSkillId ? Row->BaseSkillId : SkillId;
				const int32 Cur = S->LearnedIdOfLine(Base);
				const FRoseSkillRow* CurRow = (Cur > 0) ? S->GetSkillRow(Cur) : nullptr;
				// Only learned, castable (non-passive) skills go on the bar.
				if (!CurRow || CurRow->SkillType == 15) return nullptr;
				TSharedRef<FRoseSkillDragOp> Op = MakeShared<FRoseSkillDragOp>();
				Op->SkillId = Cur;
				Op->Icon = IconBrushFor(SkillId, 40.f);
				return Op;
			})
			.ToolTipText_Lambda([this, SkillId]() {
				URoseSkillComponent* S = SkillComp();
				const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
				if (!Row)
					return FText::GetEmpty();
				FString Tip = Row->DisplayName.IsEmpty() ? Row->SkillName : Row->DisplayName;
				Tip += FString::Printf(TEXT(" (Lv %d)"), Row->SkillLevel);
				if (!Row->Description.IsEmpty())
					Tip += TEXT("\n") + Row->Description;
				Tip += FString::Printf(TEXT("\nCost: %d SP"), Row->PointCost);
				if (Row->UseAbility1 == 17 && Row->UseAmount1 > 0)   // AT_MP
					Tip += FString::Printf(TEXT("   MP: %d"), Row->UseAmount1);
				if (Row->CooldownSec > 0.f)
					Tip += FString::Printf(TEXT("   CD: %.1fs"), Row->CooldownSec);
				if (IsNodeLearned(SkillId))
					Tip += NextLearnId(SkillId) > 0
						? TEXT("\nLearned — click to level up   drag: to quickbar")
						: TEXT("\nLearned — max level   drag: to quickbar");
				else
				{
					FString Reason;
					const bool bCan = S ? S->CanLearn(SkillId, Reason) : false;
					Tip += TEXT("\n") + (bCan ? FString(TEXT("Click to learn")) : Reason);
				}
				return FText::FromString(Tip);
			})
			// Click = learn the line's next level (LearnSkill checks points/
			// prereqs and messages the exact reason on failure); no-op when maxed.
			.OnClick_Lambda([this, SkillId]() {
				const int32 Next = NextLearnId(SkillId);
				if (Next > 0)
					if (URoseSkillComponent* S = SkillComp())
						S->LearnSkill(Next);
			})
			[
				SNew(SBox).WidthOverride(NodeSz).HeightOverride(NodeSz)
				[
					SNew(SOverlay)
					// State frame: learned accent / learnable green / locked dark.
					+ SOverlay::Slot()[ SNew(SImage).Image_Lambda([this, SkillId]() -> const FSlateBrush* {
						return IsNodeLearned(SkillId) ? FrameLearned.Get()
							: IsNodeLearnable(SkillId) ? FrameLearnable.Get() : FrameLocked.Get(); }) ]
					+ SOverlay::Slot().Padding(3.f)
					[
						SNew(SImage)
						.Image_Lambda([this, SkillId]() -> const FSlateBrush* {
							return IconBrushFor(SkillId, 40.f);
						})
						.ColorAndOpacity_Lambda([this, SkillId]() {
							return (IsNodeLearned(SkillId) || IsNodeLearnable(SkillId))
								? FLinearColor::White
								: FLinearColor(0.35f, 0.35f, 0.35f);
						})
					]
					// "Lv N" (learned) or "+" (learnable), bottom-right.
					+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(2.f)
					[
						SNew(STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
						.Visibility(EVisibility::HitTestInvisible)
						.ColorAndOpacity_Lambda([this, SkillId]() {
							return FSlateColor(IsNodeLearned(SkillId)
								? FLinearColor::White : RoseTheme::Green); })
						.Text_Lambda([this, SkillId]() {
							URoseSkillComponent* S = SkillComp();
							const FRoseSkillRow* Row = S ? S->GetSkillRow(SkillId) : nullptr;
							if (!Row) return FText::GetEmpty();
							if (IsNodeLearned(SkillId))
								return FText::FromString(FString::Printf(TEXT("Lv%d"),
									FMath::Max(1, S->LearnedLevelOfLine(Row->BaseSkillId))));
							return IsNodeLearnable(SkillId)
								? FText::FromString(TEXT("+")) : FText::GetEmpty();
						})
					]
				]
			];
	}

	TWeakObjectPtr<URoseUIManager> UIWeak;
	FString LayoutName;
	int32 Bucket = 1;        // class line (1 Soldier / 2 Muse / 3 Hawker / 4 Dealer)
	TMap<int32, TSharedPtr<FSlateBrush>> IconCache;
	TArray<TStrongObjectPtr<UTexture2D>> KeepTextures;
	TSharedPtr<FSlateBrush> CardBrush, HeaderBrush,
		FrameLearned, FrameLearnable, FrameLocked;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Modern skill-tree factories (used by URoseUIManager::ToggleSkillTree — the X
//  key / hamburger open the tree as a modern draggable window instead of the
//  classic dlgskilltree frame).  The job bucket picks the node layout + title.
// ─────────────────────────────────────────────────────────────────────────────
static int32 SkillTreeBucketFor(URoseUIManager& UI)
{
	if (ARoseCharacter* C = UI.GetRoseCharacter())
		if (C->Skills)
			return JobBucket(C->Skills->CurrentJob);
	return 1;
}

TSharedRef<SWidget> RoseSkillTree_MakeContent(URoseUIManager& UI)
{
	const int32 Bucket = SkillTreeBucketFor(UI);
	return SNew(SRoseSkillTree).UI(&UI).Layout(FString(LayoutForBucket(Bucket))).Bucket(Bucket);
}

FString RoseSkillTree_Title(URoseUIManager& UI)
{
	static const TCHAR* Names[5] = { TEXT("Skill Tree"), TEXT("Soldier"),
		TEXT("Muse"), TEXT("Hawker"), TEXT("Dealer") };
	return FString::Printf(TEXT("Skill Tree  —  %s"), Names[FMath::Clamp(SkillTreeBucketFor(UI), 1, 4)]);
}

// ─────────────────────────────────────────────────────────────────────────────
//  Registration.
// ─────────────────────────────────────────────────────────────────────────────
void RoseUISkills_Register(URoseUIManager& UI)
{
	// 1) "skill" — the classic dlgskill (learned skills + hotbar assign).  The K
	//    key + hamburger "Skill" button now open the MODERN skill window
	//    (RoseUISkillPanel.cpp, URoseUIManager::ToggleSkillPanel); this classic
	//    dialog stays registered so `RoseUI dlgskill` still opens it as a bare
	//    window, but it no longer owns a toggle key.
	{
		FRoseUIWindowDef Def;
		Def.Dialog = TEXT("dlgskill");
		Def.Anchor = ERoseUIAnchor::TopLeft;
		Def.Offset = FVector2D(620, 120);
		Def.ZOrder = 9;
		Def.BuildContent = [](URoseUIManager& M) -> TSharedPtr<SWidget> {
			return SNew(SRoseSkillList).UI(&M);
		};
		UI.RegisterWindow(TEXT("skill"), MoveTemp(Def));
	}

	// The skill TREE (X key / hamburger "Tree") now opens as a MODERN draggable
	// window via URoseUIManager::ToggleSkillTree (RoseSkillTree_MakeContent picks
	// the job's node layout).  The classic dlgskilltree windows + the X-keyed
	// dispatcher are retired; `RoseUI dlgskilltree` still shows the bare frame.
}
