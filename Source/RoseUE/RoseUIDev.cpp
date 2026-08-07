// SRoseDevSpawn — DEV item spawner window (F10 / `RoseDev` console command,
// wrapped in SRoseModernWindow by URoseUIManager::ToggleDevSpawn).
//
// Category pills on top (Armor, Head, PAT, ...) filter a scrollable item list;
// the search box refines within the category. Click a row to add one to the
// bag, Shift+click = 10. Console equivalent: RoseGive <slot> <id> [count].
//
// Each row is icon | id | name.  The icon is the item's real ITEM_ICON_NO sprite
// (RoseUI::MakeIconBrush, the same path the inventory uses) and carries the
// item description as its tooltip; the row highlights under the cursor.
//
// The list is an SListView, NOT an SScrollBox, and that is load-bearing rather
// than stylistic.  MakeIconBrush resolves through LoadObject, a BLOCKING
// game-thread package load, and SScrollBox does not virtualise — it constructs
// every child up front.  So opening a category built ~400 rows and fired ~400
// synchronous loads back to back, freezing the whole game for seconds (10 of
// the 13 categories are big enough to hit that cap: cap 951, body 859,
// back 812, foot 778, consumable 769, weapons 738, arms 728, gem 447, pat 409).
// SListView calls OnGenerateRow only for rows that are actually on screen, so
// the cost drops to the dozen or so visible icons and the rest stream in as you
// scroll.  That is also why there is no longer a display cap: virtualising
// makes showing all 7,448 items cheaper than the old truncated list.
#include "RoseCharacter.h"
#include "RoseUIHelpers.h"
#include "RoseUIManager.h"
#include "RoseUITheme.h"

#include "Brushes/SlateNoResource.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateTypes.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

namespace
{
	struct FDevCat { const TCHAR* Label; const TCHAR* Slot; };
	// One pill per item table ("" = All).
	const FDevCat kDevCats[] = {
		{ TEXT("All"),     TEXT("")           },
		{ TEXT("Weapon"),  TEXT("weapon")     },
		{ TEXT("Sub"),     TEXT("subwpn")     },
		{ TEXT("Armor"),   TEXT("body")       },
		{ TEXT("Head"),    TEXT("cap")        },
		{ TEXT("Gloves"),  TEXT("arms")       },
		{ TEXT("Boots"),   TEXT("foot")       },
		{ TEXT("Back"),    TEXT("back")       },
		{ TEXT("Face"),    TEXT("faceitem")   },
		{ TEXT("Jewel"),   TEXT("jewel")      },
		{ TEXT("PAT"),     TEXT("pat")        },
		{ TEXT("Use"),     TEXT("consumable") },
		{ TEXT("Gem"),     TEXT("gem")        },
		{ TEXT("Mat"),     TEXT("material")   },
	};

	/** One row of the list.
	 *
	 *  Name is resolved ONCE at window open, not per rebuild: the filter box
	 *  rebuilds on every keystroke, and looking the name up per item per
	 *  keystroke would be 7,448 DataTable hits a character. */
	struct FDevRow
	{
		FString Slot;
		int32   Id = 0;
		FString Name;
		FString Haystack;      // "<id> <name>", what the filter matches against
	};

	using FDevRowPtr = TSharedPtr<FDevRow>;
}

class SRoseDevSpawn : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseDevSpawn) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARoseCharacter>, Character)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		CharWeak = InArgs._Character;
		TabOn  = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::TabOn, 8.f, RoseTheme::Accent, 1.f);
		TabOff = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::TabOff, 8.f);

		// Row hover feedback comes from the button STYLE rather than a manual
		// IsHovered() poll — SButton already tracks hover for its own brushes, so
		// there is nothing to tick and no self-referencing widget capture.
		// Transparent when idle so the list reads as plain rows until pointed at.
		RowStyle = FButtonStyle()
			.SetNormal(FSlateRoundedBoxBrush(FLinearColor::Transparent, 6.f))
			.SetHovered(FSlateRoundedBoxBrush(RoseTheme::Row, 6.f, RoseTheme::Accent, 1.f))
			.SetPressed(FSlateRoundedBoxBrush(RoseTheme::TabOn, 6.f, RoseTheme::Accent, 1.f))
			.SetNormalPadding(FMargin(0))
			.SetPressedPadding(FMargin(0));

		// STableRow draws its own zebra striping and hover/selection brushes,
		// which would fight the button style above.  Blank them all out so the
		// SButton stays the only thing painting a row.
		TableRowStyle = FTableRowStyle()
			.SetEvenRowBackgroundBrush(FSlateNoResource())
			.SetEvenRowBackgroundHoveredBrush(FSlateNoResource())
			.SetOddRowBackgroundBrush(FSlateNoResource())
			.SetOddRowBackgroundHoveredBrush(FSlateNoResource())
			.SetSelectorFocusedBrush(FSlateNoResource())
			.SetActiveBrush(FSlateNoResource())
			.SetActiveHoveredBrush(FSlateNoResource())
			.SetInactiveBrush(FSlateNoResource())
			.SetInactiveHoveredBrush(FSlateNoResource())
			.SetTextColor(RoseTheme::Text)
			.SetSelectedTextColor(RoseTheme::Text);

		BuildAllRows();

		TSharedRef<SWrapBox> Cats = SNew(SWrapBox).UseAllottedSize(true);
		for (int32 i = 0; i < UE_ARRAY_COUNT(kDevCats); ++i)
		{
			Cats->AddSlot().Padding(0, 0, 4, 4)
			[
				SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder")
				.OnClicked_Lambda([this, i]() { CatIdx = i; RebuildList(); return FReply::Handled(); })
				[
					SNew(SBorder)
					.BorderImage_Lambda([this, i]() {
						return (CatIdx == i ? TabOn : TabOff).Get(); })
					.Padding(FMargin(10, 4))
					[ SNew(STextBlock)
					  .Font(FCoreStyle::GetDefaultFontStyle("Bold", 9))
					  .ColorAndOpacity_Lambda([this, i]() {
						return CatIdx == i ? RoseTheme::Text : RoseTheme::TextDim; })
					  .Text(FText::FromString(kDevCats[i].Label)) ]
				]
			];
		}

		ChildSlot
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[ Cats ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 2, 0, 0)
			[
				SNew(SEditableTextBox)
				.HintText(FText::FromString(TEXT("filter by name or id")))
				.OnTextChanged_Lambda([this](const FText& T) {
					Filter = T.ToString();
					RebuildList();
				})
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 4, 0, 0)
			[ SNew(STextBlock)
			  .Font(FCoreStyle::GetDefaultFontStyle("Regular", 8))
			  .ColorAndOpacity(RoseTheme::TextDim)
			  .Text_Lambda([this]() {
				return FText::FromString(FString::Printf(
					TEXT("%d items — click: give 1, Shift+click: give 10"),
					Visible.Num())); }) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)
			[
				SNew(SBox).HeightOverride(400.f)
				[
					SAssignNew(List, SListView<FDevRowPtr>)
					.ListItemsSource(&Visible)
					// No selection: the row's SButton owns the click, and a
					// selection highlight on top of it would just be noise.
					.SelectionMode(ESelectionMode::None)
					// No ItemHeight — deprecated in 5.5 and tile-only; the list
					// takes each row's desired height, which is what we want
					// since every row is the same icon-sized height anyway.
					.OnGenerateRow(this, &SRoseDevSpawn::GenerateRow)
				]
			]
		];
		RebuildList();
	}

private:
	static constexpr float kIconSize = 26.f;

	// Icon brush for any (slot,id), cached by icon index.  Virtualisation means
	// this is only ever called for rows on screen, but the cache still matters:
	// scrolling back over a row regenerates it, and thousands of items share
	// icons.  KeepTextures pins the textures for as long as the window lives
	// (MakeIconBrush's contract).
	const FSlateBrush* IconFor(const FString& Slot, int32 Id)
	{
		ARoseCharacter* C = CharWeak.Get();
		if (!C) return nullptr;
		const int32 IconIdx = C->GetItemIconIdx(Slot, Id);
		if (IconIdx <= 0) return nullptr;
		if (const TSharedPtr<FSlateBrush>* Cached = IconCache.Find(IconIdx))
			return Cached->IsValid() ? Cached->Get() : nullptr;
		TSharedPtr<FSlateBrush> B = RoseUI::MakeIconBrush(IconIdx, kIconSize, KeepTextures);
		IconCache.Add(IconIdx, B);
		return B.IsValid() ? B.Get() : nullptr;
	}

	/** Every spawnable item, names resolved once. */
	void BuildAllRows()
	{
		AllRows.Reset();
		ARoseCharacter* C = CharWeak.Get();
		if (!C) return;

		TArray<ARoseCharacter::FRoseDevItem> Items;
		C->GetAllItemIdsForDev(Items);
		AllRows.Reserve(Items.Num());
		for (const ARoseCharacter::FRoseDevItem& It : Items)
		{
			FDevRowPtr R = MakeShared<FDevRow>();
			R->Slot = It.Slot;
			R->Id   = It.Id;
			R->Name = C->GetItemName(It.Slot, It.Id);
			R->Haystack = FString::Printf(TEXT("%d %s"), It.Id, *R->Name);
			AllRows.Add(R);
		}
	}

	/** Re-filter. Cheap now — pure string work over a prebuilt array, no asset
	 *  loads and no widgets, so a keystroke costs nothing regardless of size. */
	void RebuildList()
	{
		const FString CatSlot = kDevCats[CatIdx].Slot;
		TArray<FString> Terms;
		Filter.ParseIntoArrayWS(Terms);

		Visible.Reset();
		for (const FDevRowPtr& R : AllRows)
		{
			if (!CatSlot.IsEmpty() && R->Slot != CatSlot)
				continue;
			bool bOk = true;
			for (const FString& T : Terms)
				if (!R->Haystack.Contains(T)) { bOk = false; break; }
			if (bOk)
				Visible.Add(R);
		}
		if (List.IsValid())
			List->RequestListRefresh();
	}

	TSharedRef<ITableRow> GenerateRow(FDevRowPtr Item,
	                                  const TSharedRef<STableViewBase>& OwnerTable)
	{
		if (!Item.IsValid())
			return SNew(STableRow<FDevRowPtr>, OwnerTable);

		const FString Slot = Item->Slot;
		const int32   Id   = Item->Id;
		const FString Name = Item->Name.IsEmpty() ? FString(TEXT("(unnamed)")) : Item->Name;

		// Description is resolved here rather than up front, so it costs one
		// DataTable hit per VISIBLE row instead of one per item in the table.
		FString Desc;
		if (ARoseCharacter* C = CharWeak.Get())
			Desc = C->GetItemDescription(Slot, Id);
		if (Desc.IsEmpty())
			Desc = TEXT("(no description)");

		return SNew(STableRow<FDevRowPtr>, OwnerTable)
			.Style(&TableRowStyle)
			.Padding(FMargin(0, 1))
			[
				SNew(SButton).ButtonStyle(&RowStyle)
				.ContentPadding(FMargin(0))
				.OnClicked_Lambda([this, Slot, Id]() {
					if (ARoseCharacter* Ch = CharWeak.Get())
						Ch->RoseGive(Slot, Id,
							FSlateApplication::Get().GetModifierKeys().IsShiftDown() ? 10 : 1);
					return FReply::Handled();
				})
				[
					SNew(SBox).Padding(FMargin(6, 3))
					[
						SNew(SHorizontalBox)
						// Icon first so the list is scannable by art, not by text.
						// Explicitly hit-testable: a SelfHitTestInvisible box (the
						// SBox default) is never the hovered widget, so Slate would
						// never find its tooltip and would fall through to the row.
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SBox)
							.WidthOverride(kIconSize).HeightOverride(kIconSize)
							.Visibility(EVisibility::Visible)
							.ToolTipText(FText::FromString(
								FString::Printf(TEXT("%s\n%s"), *Name, *Desc)))
							[ SNew(SImage).Image(IconFor(Slot, Id)) ]
						]
						+ SHorizontalBox::Slot().AutoWidth().Padding(6, 0, 0, 0).VAlign(VAlign_Center)
						[ SNew(STextBlock)
						  .Font(FCoreStyle::GetDefaultFontStyle("Regular", 9))
						  .ColorAndOpacity(RoseTheme::TextDim)
						  .Text(FText::FromString(FString::Printf(TEXT("%4d"), Id))) ]
						+ SHorizontalBox::Slot().FillWidth(1.f).Padding(8, 0, 0, 0).VAlign(VAlign_Center)
						[ SNew(STextBlock)
						  .Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						  .ColorAndOpacity(RoseTheme::Text)
						  .Text(FText::FromString(Name)) ]
					]
				]
			];
	}

	TWeakObjectPtr<ARoseCharacter> CharWeak;
	TArray<FDevRowPtr> AllRows;                             // every item, built once
	TArray<FDevRowPtr> Visible;                             // current filter result
	TSharedPtr<SListView<FDevRowPtr>> List;
	TSharedPtr<FSlateBrush> TabOn, TabOff;
	FButtonStyle RowStyle;                                  // idle / hover / pressed row
	FTableRowStyle TableRowStyle;                            // blanked STableRow chrome
	TMap<int32, TSharedPtr<FSlateBrush>> IconCache;
	TArray<TStrongObjectPtr<UTexture2D>> KeepTextures;
	FString Filter;
	int32 CatIdx = 0;
};

TSharedRef<SWidget> RoseDevSpawn_MakeContent(URoseUIManager& UI)
{
	return SNew(SRoseDevSpawn).Character(UI.GetRoseCharacter());
}
