// SRoseInventory — the modern inventory content (opened by I / the HUD hamburger
// "Bag", wrapped in SRoseModernWindow by ARoseCharacter::ToggleInventory).  Laid
// out like the current ROSE item window: top Equipment/Costume tabs, a full-body
// character paperdoll (live PaperdollRT) with equip slots down each side, a bag
// grid with category tabs below, and a zuly bar at the bottom.
//
// Equip slots reuse the character's equip path (GetEquippedId / CycleSlotItem /
// EquipItem / GetItemName / GetWeaponRow / GetArmorRow); click = cycle the slot's
// items, Shift+click = unequip.  The bag grid is presentational until a real
// inventory model exists.
#include "RoseCharacter.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "RoseUIWindow.h"
#include "RoseUIHelpers.h"
#include "RoseItemTypes.h"
#include "RoseUITheme.h"
#include "RoseDrops.h"   // RoseBagCategory / RoseIsEquipSlot

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/Texture2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Input/DragAndDrop.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Input/Reply.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SGridPanel.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SToolTip.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor kSlot   = RoseTheme::Slot;
	const FLinearColor kPreview= RoseTheme::Preview;
	const FLinearColor kTabOn  = RoseTheme::TabOn;
	const FLinearColor kTabOff = RoseTheme::TabOff;
	const FLinearColor kAccent = RoseTheme::Accent;
	const FLinearColor kGold   = RoseTheme::Gold;
	const FLinearColor kText   = RoseTheme::Text;
	const FLinearColor kTextDim= RoseTheme::TextDim;

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	// Classic item-tooltip requirement colours (green = met, red = unmet).
	const FLinearColor kReqOk (0.45f, 0.85f, 0.45f, 1.f);
	const FLinearColor kReqBad(0.95f, 0.35f, 0.35f, 1.f);
	const FLinearColor kBonus (0.55f, 0.70f, 0.95f, 1.f);

	// Slot -> the classic "Classification" line.
	FString ClassificationName(const FString& Slot)
	{
		const FString S = Slot.ToLower();
		if (S == TEXT("body"))   return TEXT("Body Armor");
		if (S == TEXT("arms"))   return TEXT("Gauntlet");
		if (S == TEXT("foot"))   return TEXT("Boots");
		if (S == TEXT("cap"))    return TEXT("Helmet");
		if (S == TEXT("back"))   return TEXT("Back Armor");
		if (S == TEXT("weapon")) return TEXT("Weapon");
		// "face" is the APPEARANCE face (face models); "faceitem" is the mask/
		// goggles EQUIP slot.  The old "Face Item" label on the appearance slot
		// made clicking it look like a broken mask equip (it cycles faces).
		if (S == TEXT("face"))     return TEXT("Face");
		if (S == TEXT("faceitem")) return TEXT("Face Item");
		if (S == TEXT("jewel"))  return TEXT("Jewelry");
		if (S == TEXT("subwpn")) return TEXT("Sub Weapon");
		return TEXT("Item");
	}

	// ITEM_QUALITY band -> rarity name.
	FString RarityName(int32 Quality)
	{
		if (Quality >= 100) return TEXT("Epic");
		if (Quality >= 60)  return TEXT("Rare");
		if (Quality >= 30)  return TEXT("Uncommon");
		return TEXT("Common");
	}

	// AT_CLASS value -> job name (datatype.h CLASS_* ids).
	FString JobClassName(int32 Job)
	{
		switch (Job)
		{
		case 0:   return TEXT("Visitor");
		case 111: return TEXT("Soldier");
		case 121: return TEXT("Knight");
		case 122: return TEXT("Champion");
		case 211: return TEXT("Muse");
		case 221: return TEXT("Mage");
		case 222: return TEXT("Cleric");
		case 311: return TEXT("Hawker");
		case 321: return TEXT("Raider");
		case 322: return TEXT("Scout");
		case 411: return TEXT("Dealer");
		case 421: return TEXT("Bourgeois");
		case 422: return TEXT("Artisan");
		default:  return FString::Printf(TEXT("Job %d"), Job);
		}
	}

	FString AbilityShort(int32 A)
	{
		switch (A)
		{
		case 10: return TEXT("STR");
		case 11: return TEXT("DEX");
		case 12: return TEXT("INT");
		case 13: return TEXT("CON");
		case 14: return TEXT("CHA");
		case 15: return TEXT("SEN");
		case 31: return TEXT("Level");
		default: return FString::Printf(TEXT("Ability#%d"), A);
		}
	}

	// Equip slots down each side of the paperdoll (ROSE-ish arrangement).
	struct FSlotDef { const TCHAR* Slot; const TCHAR* Label; };
	const FSlotDef kLeftSlots[]  = {
		{ TEXT("cap"),  TEXT("Helmet") }, { TEXT("face"), TEXT("Face") },
		{ TEXT("body"), TEXT("Armor")  }, { TEXT("arms"), TEXT("Gloves") },
	};
	const FSlotDef kRightSlots[] = {
		{ TEXT("weapon"), TEXT("Weapon") }, { TEXT("back"), TEXT("Back") },
		{ TEXT("foot"),   TEXT("Boots")  }, { TEXT("hair"), TEXT("Hair") },
	};
	// Accessory row under the paperdoll.  (No mesh part / item tables yet — these
	// are stat-only slots, shown ready for when accessory items exist.)
	const FSlotDef kAccessorySlots[] = {
		{ TEXT("ring"),     TEXT("Ring")     },
		{ TEXT("earring"),  TEXT("Earring")  },
		{ TEXT("necklace"), TEXT("Necklace") },
	};
}

// Drag payload: an item being moved between the bag and an equip slot.
class FRoseItemDragOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FRoseItemDragOp, FDragDropOperation)

	FString Slot;                 // item's slot type ("body","weapon",…)
	int32 Id = 0;
	bool bFromEquip = false;      // true = dragged off an equip slot (→ unequip)
	const FSlateBrush* Icon = nullptr;

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		// A null Icon draws NOTHING, which is indistinguishable from "drag and
		// drop is broken" — you pick an item up and nothing follows the cursor.
		// Fall back to a visible marker so the drag is always legible, even for
		// an item whose icon failed to resolve.
		return SNew(SBox).WidthOverride(40.f).HeightOverride(40.f)
			[
				Icon ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Icon))
				     : StaticCastSharedRef<SWidget>(
						SNew(SBorder)
						.BorderBackgroundColor(FLinearColor(1.f, 0.85f, 0.3f, 0.85f))
						.Padding(FMargin(2.f)))
			];
	}

	// Windowless = render the icon decorator IN the viewport (following the
	// cursor) instead of a separate cursor-decorator OS window that doesn't show
	// over a game viewport — so the item icon moves with the mouse in-game.
	virtual void Construct() override
	{
		bCreateNewWindow = false;
		FDragDropOperation::Construct();
	}
};

// A slot that is both a drag SOURCE and a drop TARGET, and still supports a
// plain click (fires when released without a drag).  Used for equip slots and
// bag cells.
class SRoseDragSlot : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal(TSharedPtr<FRoseItemDragOp>, FBeginDrag);
	DECLARE_DELEGATE_RetVal_OneParam(bool, FCanAccept, const TSharedPtr<FRoseItemDragOp>&);
	DECLARE_DELEGATE_OneParam(FOnDropItem, const TSharedPtr<FRoseItemDragOp>&);

	SLATE_BEGIN_ARGS(SRoseDragSlot) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FBeginDrag, OnBeginDrag)
		SLATE_EVENT(FCanAccept, CanAccept)
		SLATE_EVENT(FOnDropItem, OnDropItem)
		SLATE_EVENT(FSimpleDelegate, OnClick)
		SLATE_ATTRIBUTE(FText, ToolTipText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnBeginDrag = InArgs._OnBeginDrag;
		CanAccept = InArgs._CanAccept;
		OnDropItem = InArgs._OnDropItem;
		OnClick = InArgs._OnClick;
		SetToolTipText(InArgs._ToolTipText);
		ChildSlot[ InArgs._Content.Widget ];
	}

	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Ev) override
	{
		if (Ev.GetEffectingButton() == EKeys::LeftMouseButton)
			return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
		return FReply::Unhandled();
	}
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Ev) override
	{
		if (Ev.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			OnClick.ExecuteIfBound();   // released without dragging = a click
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	virtual FReply OnDragDetected(const FGeometry&, const FPointerEvent&) override
	{
		if (OnBeginDrag.IsBound())
			if (TSharedPtr<FRoseItemDragOp> Op = OnBeginDrag.Execute())
				return FReply::Handled().BeginDragDrop(Op.ToSharedRef());
		return FReply::Unhandled();
	}
	virtual FReply OnDragOver(const FGeometry&, const FDragDropEvent& Ev) override
	{
		TSharedPtr<FRoseItemDragOp> Op = Ev.GetOperationAs<FRoseItemDragOp>();
		return (Op.IsValid() && (!CanAccept.IsBound() || CanAccept.Execute(Op)))
			? FReply::Handled() : FReply::Unhandled();
	}
	virtual FReply OnDrop(const FGeometry&, const FDragDropEvent& Ev) override
	{
		TSharedPtr<FRoseItemDragOp> Op = Ev.GetOperationAs<FRoseItemDragOp>();
		if (Op.IsValid() && (!CanAccept.IsBound() || CanAccept.Execute(Op)))
		{
			OnDropItem.ExecuteIfBound(Op);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	FBeginDrag OnBeginDrag;
	FCanAccept CanAccept;
	FOnDropItem OnDropItem;
	FSimpleDelegate OnClick;
};

class SRoseInventory : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseInventory) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARoseCharacter>, Char)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		CharWeak = InArgs._Char;

		// Slots use the skin's small inset frame (GEN_WND06) — a real recessed
		// well rather than a flat tinted square.
		SlotBrush   = RoseUI::GlassPanel(RoseUI::EPanelKind::Inset);
		if (!SlotBrush.IsValid())
			SlotBrush = MakeShared<FSlateRoundedBoxBrush>(kSlot, 6.f, FLinearColor(kAccent.R, kAccent.G, kAccent.B, 0.5f), 1.f);
		PreviewBrush= MakeShared<FSlateRoundedBoxBrush>(kPreview, 8.f, FLinearColor(kAccent.R, kAccent.G, kAccent.B, 0.5f), 1.f);
		TabOnBrush  = RoseUI::GlassTab(true);
		TabOffBrush = RoseUI::GlassTab(false);
		if (!TabOnBrush.IsValid())  TabOnBrush = MakeShared<FSlateRoundedBoxBrush>(kTabOn,  8.f, kAccent, 1.f);
		if (!TabOffBrush.IsValid()) TabOffBrush= MakeShared<FSlateRoundedBoxBrush>(kTabOff, 8.f);
		BarBrush    = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.02f,0.02f,0.04f,0.9f), 6.f);
		GoldFill    = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(kGold.R,kGold.G,kGold.B,0.25f), 6.f);
		TipBgBrush  = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.04f,0.05f,0.07f,0.97f), 6.f, kAccent, 1.f);

		// The window IS the client's dlgitem layout (306x468): frame, caption,
		// both tab rows, every equipment slot ghost and the 6x5 bag wells are
		// all authored art drawn by SRoseUIWindow at ROSE's own coordinates.
		// We only overlay the live parts — item icons, click/drag targets and
		// the footer numbers — anchored to the rects the layout reports, so the
		// grid can never drift from the client's.
		// The live layer goes INSIDE the window, through its Content slot — not
		// beside it in an SOverlay.  A sibling is a separate widget that merely
		// happens to overlap: it does not inherit the window's drag transform,
		// and it reads on screen as a second, stray copy of the inventory
		// floating over the world.  Content puts the cells in the layout's own
		// canvas, so they sit on their backings and travel with the window.
		//
		// SelfHitTestInvisible so the canvas itself does not swallow clicks
		// meant for the layout's tabs and buttons; its CHILDREN stay clickable.
		Overlay = SNew(SConstraintCanvas)
			.Visibility(EVisibility::SelfHitTestInvisible);

		ChildSlot
		[
			SNew(SBox).WidthOverride(kDlgW).HeightOverride(kDlgH)
			[
				SAssignNew(Layout, SRoseUIWindow)
				.Dialog(TEXT("dlgitem"))
				// dlgitem lists Costume FIRST, so without this the window opens
				// on Costume with every accessory slot greyed out.
				.InitialTab(TEXT("AVATA_TBTN"))
				.Content(Overlay)
				.OnTextValue(FRoseUITextValue::CreateSP(this, &SRoseInventory::LayoutText))
				.OnClose(FSimpleDelegate::CreateLambda([this]() {
					if (ARoseCharacter* C = Char()) C->ToggleInventory(); }))
			]
		];

		BuildOverlay();
	}

	// ── The live layer over the authored layout ──────────────────────────────
	//
	// Equipment slots carry no NAME in the XML — each is identified by its
	// distinct ghost sprite (GEN_INVSLOT01 = Ring, 02 = Necklace, ...), so the
	// sprite is the key.  Slots 17-22 are drawn by BOTH the Avatar and Costume
	// tabs at the same coordinates, hence taking the first rect.
	void BuildOverlay()
	{
		if (!Layout.IsValid() || !Overlay.IsValid())
			return;

		struct FSlotBind { const TCHAR* Gid; const TCHAR* Slot; const TCHAR* Label; };
		static const FSlotBind kBinds[] = {
			{ TEXT("GEN_INVSLOT01"), TEXT("ring"),     TEXT("Ring")     },
			{ TEXT("GEN_INVSLOT02"), TEXT("necklace"), TEXT("Necklace") },
			{ TEXT("GEN_INVSLOT03"), TEXT("earring"),  TEXT("Earring")  },
			// GEN_INVSLOT17 is the MASK slot, not the appearance face.
			//
			// "face" is the character's own face model, chosen at creation — it is
			// not equipment and must never be driven from the equip window, which
			// is what made clicking this slot change the character's face instead
			// of equipping a mask.  "faceitem" is the mask/goggles item slot.
			// GhostNameForSlot already returns the "faceitem" ghost art for this
			// position, so the empty-slot picture was the mask all along.
			{ TEXT("GEN_INVSLOT17"), TEXT("faceitem"), TEXT("Mask")     },
			{ TEXT("GEN_INVSLOT18"), TEXT("cap"),      TEXT("Head")     },
			{ TEXT("GEN_INVSLOT19"), TEXT("back"),     TEXT("Back")     },
			{ TEXT("GEN_INVSLOT08"), TEXT("weapon"),   TEXT("Weapon")   },
			{ TEXT("GEN_INVSLOT20"), TEXT("body"),     TEXT("Body")     },
			{ TEXT("GEN_INVSLOT07"), TEXT("subwpn"),   TEXT("Shield")   },
			{ TEXT("GEN_INVSLOT21"), TEXT("arms"),     TEXT("Arms")     },
			{ TEXT("GEN_INVSLOT22"), TEXT("foot"),     TEXT("Foot")     },
		};

		for (const FSlotBind& B : kBinds)
		{
			const TArray<FSlateRect>* Rects = Layout->GetSpriteRects(B.Gid);
			if (!Rects || Rects->Num() == 0)
			{
				UE_LOG(LogTemp, Warning, TEXT("[RoseUI] dlgitem has no %s slot"), B.Gid);
				continue;
			}
			FSlotDef Def{ B.Slot, B.Label };
			PlaceAt((*Rects)[0], EquipSlot(Def), TEXT("AVATA_TBTN"));
		}

		// Ammo (Arrow/Bullet/Cannon) live on the character's AmmoSlots, not the
		// equipment map, so they use their own cell.
		static const TCHAR* kAmmoGids[] = {
			TEXT("GEN_INVSLOT04"), TEXT("GEN_INVSLOT05"), TEXT("GEN_INVSLOT06") };
		for (int32 i = 0; i < 3; ++i)
			if (const TArray<FSlateRect>* R = Layout->GetSpriteRects(kAmmoGids[i]))
				if (R->Num() > 0)
					PlaceAt((*R)[0], AmmoSlotCell(i), TEXT("AVATA_TBTN"));

		// Bag: the layout names an icon anchor per cell (ITEM_SLOT<row><col>),
		// so the 6x5 grid is read, not assumed.
		for (int32 Row = 0; Row < kBagRows; ++Row)
			for (int32 Col = 0; Col < kBagCols; ++Col)
			{
				FSlateRect Rect;
				if (!Layout->GetAnchorRect(FString::Printf(TEXT("ITEM_SLOT%d%d"), Row, Col), Rect))
				{
					// Only row 0 and ITEM_SLOT10 are named in the XML; the rest of
					// the grid is regular, so derive them from the authored pitch.
					const float X = kBagX0 + Col * kBagPitch;
					const float Y = kBagY0 + Row * kBagPitch;
					Rect = FSlateRect(X, Y, X + kBagCell, Y + kBagCell);
				}
				PlaceAt(Rect, BagCell(Row * kBagCols + Col), nullptr);
			}
	}

	/** Put a live widget exactly over an authored rect, optionally only while a
	 *  given tab is active (tag = the TABBUTTON's NAME). */
	void PlaceAt(const FSlateRect& Rect, TSharedRef<SWidget> Widget, const TCHAR* TabTag)
	{
		if (TabTag)
		{
			const FString Tag(TabTag);
			Widget->SetVisibility(TAttribute<EVisibility>::CreateLambda([this, Tag]() {
				return (Layout.IsValid() && Layout->IsTabActive(Tag))
					? EVisibility::Visible : EVisibility::Collapsed; }));
		}
		Overlay->AddSlot()
			.Offset(FMargin(Rect.Left, Rect.Top, Rect.GetSize().X, Rect.GetSize().Y))
			.Alignment(FVector2D(0, 0))
			[ Widget ];
	}

	/** Which bag category the layout's active tab means.
	 *
	 *  The three bag tabs carry no NAME and no label, so they are identified by
	 *  their button IDs (53 Equipment, 63 Consume, 73 Material; 103 is the PAT
	 *  tab's single Equipment tab).  Tags embed the id as "#63#" so a substring
	 *  test cannot also match id 163. */
	int32 ActiveBagTab() const
	{
		if (!Layout.IsValid())
			return 0;
		if (Layout->IsTabActive(TEXT("#63#"))) return 1;   // Consume
		if (Layout->IsTabActive(TEXT("#73#"))) return 2;   // Material
		return 0;                                          // Equipment
	}

	/** Text the layout asks us for, keyed by the control's XML NAME.
	 *
	 *  The window title and the tab labels are NOT in the XML — the glass client
	 *  sets them in code, which is why its three top tabs are three identical
	 *  plates.  Supplying them here is what makes the window read as ROSE's. */
	FText LayoutText(const FRoseUIKey& Key, int32 Row) const
	{
		if (Key.Is(TEXT("CAPTION")))
			return FText::FromString(TEXT("Inventory"));
		if (Key.Is(TEXT("AVATA_TBTN")))   return FText::FromString(TEXT("Avatar"));
		if (Key.Is(TEXT("COSTUME_TBTN"))) return FText::FromString(TEXT("Costume"));
		if (Key.Is(TEXT("PAT_TBTN")))     return FText::FromString(TEXT("PAT"));

		// The bag tabs carry no NAME at all, only IDs, so they key off those:
		// 53/63/73 are the Avatar tab's row and 103 is the PAT tab's single one.
		switch (Key.Id)
		{
		case 53: case 103: return FText::FromString(TEXT("Equipment"));
		case 63:           return FText::FromString(TEXT("Consume"));
		case 73:           return FText::FromString(TEXT("Material"));
		default: break;
		}

		ARoseCharacter* C = Char();
		if (!C)
			return FText::GetEmpty();
		if (Key.Is(TEXT("ZULY")))
			return FText::AsNumber(C->GetZuly());
		return FText::GetEmpty();
	}

private:
	ARoseCharacter* Char() const { return CharWeak.Get(); }

	// ── Top tabs: Equipment / Costume ─────────────────────────────────────────
	TSharedRef<SWidget> Tab(const FString& Label, int32 Index, int32& State, float Height = 28.f)
	{
		int32* StatePtr = &State;
		return SNew(SButton)
			.ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0))
			.OnClicked_Lambda([StatePtr, Index]() { *StatePtr = Index; return FReply::Handled(); })
			[
				SNew(SBox).HeightOverride(Height)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ SNew(SImage).Image_Lambda([StatePtr, Index, this]() -> const FSlateBrush* {
						return (*StatePtr == Index ? TabOnBrush : TabOffBrush).Get(); }) ]
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[ SNew(STextBlock).Font(Font(9, true))
					  .ColorAndOpacity_Lambda([StatePtr, Index]() { return *StatePtr == Index ? kText : kTextDim; })
					  .Text(FText::FromString(Label)) ]
				]
			];
	}

	TSharedRef<SWidget> BuildTopTabs()
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[ Tab(TEXT("Equipment"), 0, TopTab) ]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(5, 0, 0, 0)[ Tab(TEXT("Costume"), 1, TopTab) ]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(5, 0, 0, 0)[ Tab(TEXT("PAT"), 2, TopTab) ];
	}

	// ── Paperdoll: equip slots | preview | equip slots ────────────────────────
	// Icon for any (slot,id) item — used by both equip slots and bag cells.
	FSlateBrush* IconForItem(const FString& Slot, int32 Id)
	{
		ARoseCharacter* C = Char();
		if (!C || Id < 0) return nullptr;
		int32 IconIdx = 0;
		if (Slot == TEXT("weapon"))
		{
			if (const FRoseWeaponRow* R = C->GetWeaponRow(Id)) IconIdx = R->IconIdx;
		}
		else if (const FRoseArmorRow* R = C->GetArmorRow(Slot, Id))
			IconIdx = R->IconIdx;
		else if (const FRoseSimpleItemRow* SR = C->GetSimpleItemRow(Slot, Id))
			IconIdx = SR->IconIdx;   // consumable / gem / material
		if (IconIdx <= 0) return nullptr;
		if (TSharedPtr<FSlateBrush>* Cached = IconCache.Find(IconIdx))
			return Cached->IsValid() ? Cached->Get() : nullptr;
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *FString::Printf(
			TEXT("/Game/UI/Icons/icon_%05d.icon_%05d"), IconIdx, IconIdx));
		TSharedPtr<FSlateBrush> B;
		if (Tex)
		{
			B = MakeShared<FSlateBrush>();
			B->SetResourceObject(Tex);
			B->ImageSize = FVector2D(38.f, 38.f);
			KeepTextures.Add(TStrongObjectPtr<UTexture2D>(Tex));
		}
		IconCache.Add(IconIdx, B);
		return B.IsValid() ? B.Get() : nullptr;
	}

	FSlateBrush* IconFor(const FString& Slot)
	{
		ARoseCharacter* C = Char();
		return C ? IconForItem(Slot, C->GetEquippedId(Slot)) : nullptr;
	}

	// ── Empty-slot ghost silhouettes (/Game/UI/SlotGhosts) ───────────────────
	// ROSE paints these into the inventory window background; build_slot_ghosts.py
	// crops them into per-slot alpha-keyed PNGs so our individually-built Slate
	// slots can show one when nothing is equipped.
	// The classic art has no hair ghost, so "hair" deliberately has no entry.
	static const TCHAR* GhostNameForSlot(const FString& Slot)
	{
		if (Slot == TEXT("face") || Slot == TEXT("faceitem")) return TEXT("faceitem");
		if (Slot == TEXT("cap"))      return TEXT("cap");
		if (Slot == TEXT("back"))     return TEXT("back");
		if (Slot == TEXT("body"))     return TEXT("body");
		if (Slot == TEXT("arms"))     return TEXT("arms");
		if (Slot == TEXT("foot"))     return TEXT("foot");
		if (Slot == TEXT("weapon"))   return TEXT("weapon");
		if (Slot == TEXT("subwpn"))   return TEXT("subwpn");
		if (Slot == TEXT("ring"))     return TEXT("ring");
		if (Slot == TEXT("necklace")) return TEXT("necklace");
		if (Slot == TEXT("earring"))  return TEXT("earring");
		return nullptr;
	}

	FSlateBrush* GhostBrush(const TCHAR* Name)
	{
		if (!Name) return nullptr;
		const FString N(Name);
		if (TSharedPtr<FSlateBrush>* Cached = GhostCache.Find(N))
			return Cached->IsValid() ? Cached->Get() : nullptr;
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *FString::Printf(
			TEXT("/Game/UI/SlotGhosts/slotghost_%s.slotghost_%s"), *N, *N));
		TSharedPtr<FSlateBrush> B;
		if (Tex)
		{
			B = MakeShared<FSlateBrush>();
			B->SetResourceObject(Tex);
			B->ImageSize = FVector2D(34.f, 34.f);
			// Subdued: the ghost is a hint, never mistakable for a real item.
			B->TintColor = FSlateColor(FLinearColor(1.f, 1.f, 1.f, 0.55f));
			KeepTextures.Add(TStrongObjectPtr<UTexture2D>(Tex));
		}
		GhostCache.Add(N, B);
		return B.IsValid() ? B.Get() : nullptr;
	}

	// Equipped icon, or NOTHING when the slot is empty.
	//
	// No ghost silhouette: dlgitem already draws a labelled slot for every
	// position (Ring / Face / Head / ...), so an overlaid ghost just sat on top
	// of the client's own art and made the Avatar tab look unlike Costume.
	const FSlateBrush* EquipSlotImage(const FString& Slot)
	{
		return IconFor(Slot);
	}

	TSharedRef<SWidget> EquipSlot(const FSlotDef& Def)
	{
		const FString S(Def.Slot);
		const FString Label(Def.Label);
		TSharedRef<SRoseDragSlot> Cell = SNew(SRoseDragSlot)
			// Click = cycle this slot; Shift+click = unequip (unchanged).
			.OnClick_Lambda([this, S]() {
				ARoseCharacter* C = Char();
				if (!C) return;
				if (FSlateApplication::Get().GetModifierKeys().IsShiftDown()) C->EquipItem(S, -1);
				else C->CycleSlotItem(S, +1);
			})
			// Drag the equipped item off → unequip (drop it on the bag).
			.OnBeginDrag_Lambda([this, S]() -> TSharedPtr<FRoseItemDragOp> {
				ARoseCharacter* C = Char();
				const int32 Id = C ? C->GetEquippedId(S) : -1;
				if (Id < 0) return nullptr;
				TSharedRef<FRoseItemDragOp> Op = MakeShared<FRoseItemDragOp>();
				Op->Slot = S; Op->Id = Id; Op->bFromEquip = true; Op->Icon = IconForItem(S, Id);
				return Op;
			})
			// Accept a matching-type item dragged from the bag → equip it.
			.CanAccept_Lambda([S](const TSharedPtr<FRoseItemDragOp>& Op) {
				return Op.IsValid() && !Op->bFromEquip && Op->Slot == S;
			})
			.OnDropItem_Lambda([this, S](const TSharedPtr<FRoseItemDragOp>& Op) {
				if (ARoseCharacter* C = Char()) C->EquipFromBag(S, Op->Id);
			})
			[
				SNew(SBox).WidthOverride(44.f).HeightOverride(44.f)
				[
					SNew(SOverlay)
					// No backing either — the layout's GEN_INVSLOT sprite IS the
					// slot; ours only doubled it.
					+ SOverlay::Slot().Padding(3.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[ SNew(SImage).Image_Lambda([this, S]() -> const FSlateBrush* {
						return EquipSlotImage(S); }) ]
				]
			];
		// Same rich tooltip the bag uses, built from the equipped (slot,id).
		Cell->SetToolTip(MakeItemTooltip([this, S, Label]() {
			ARoseCharacter* C = Char();
			const int32 Id = C ? C->GetEquippedId(S) : -1;
			FTipInfo T = (C && Id >= 0)
			? TipInfoFor(S, Id, 1, C->GetEquippedBonus(S), C->GetEquippedAppraised(S),
				C->GetEquippedRefine(S))
			: FTipInfo();
			if (!T.bValid) { T.Name = FString::Printf(TEXT("%s — empty"), *Label); }
			T.Hint = TEXT("click: next   shift+click: remove   drag off to unequip");
			return T;
		}));
		return Cell;
	}

	TSharedRef<SWidget> SlotColumn(const FSlotDef* Slots, int32 Count)
	{
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		for (int32 i = 0; i < Count; ++i)
			Col->AddSlot().AutoHeight().Padding(0, i == 0 ? 0 : 6, 0, 0)[ EquipSlot(Slots[i]) ];
		return Col;
	}

	TSharedRef<SWidget> BuildPaperdoll()
	{
		// ARPG-style equip grid (no live-preview viewport — user call, 2026-07-20):
		//         [Face]  [Helmet] [Hair]
		// [Weapon]         [Body]        [SubWpn]
		//          [Ring][Earring][Necklace]
		// [Gloves]         [Back]        [Boots]
		static const FSlotDef SFace   = { TEXT("face"),     TEXT("Face")     };
		static const FSlotDef SMask   = { TEXT("faceitem"), TEXT("Mask")     };
		static const FSlotDef SHelm   = { TEXT("cap"),      TEXT("Helmet")   };
		static const FSlotDef SHair   = { TEXT("hair"),     TEXT("Hair")     };
		static const FSlotDef SWpn    = { TEXT("weapon"),   TEXT("Weapon")   };
		static const FSlotDef SBody   = { TEXT("body"),     TEXT("Body")     };
		static const FSlotDef SSub    = { TEXT("subwpn"),   TEXT("SubWpn")   };
		static const FSlotDef SRing   = { TEXT("ring"),     TEXT("Ring")     };
		static const FSlotDef SEar    = { TEXT("earring"),  TEXT("Earring")  };
		static const FSlotDef SNeck   = { TEXT("necklace"), TEXT("Necklace") };
		static const FSlotDef SGlove  = { TEXT("arms"),     TEXT("Gloves")   };
		static const FSlotDef SBack   = { TEXT("back"),     TEXT("Back")     };
		static const FSlotDef SBoots  = { TEXT("foot"),     TEXT("Boots")    };

		auto Row = [this](std::initializer_list<const FSlotDef*> Defs, float Gap) {
			TSharedRef<SHorizontalBox> R = SNew(SHorizontalBox);
			bool bFirst = true;
			for (const FSlotDef* D : Defs)
			{
				R->AddSlot().AutoWidth().Padding(bFirst ? 0.f : Gap, 0, 0, 0)
					[ EquipSlot(*D) ];
				bFirst = false;
			}
			return R;
		};

		return SNew(SBox).Visibility_Lambda([this]() { return TopTab == 0 ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
			[ Row({ &SFace, &SMask, &SHelm, &SHair }, 8.f) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0).HAlign(HAlign_Center)
			[ Row({ &SWpn, &SBody, &SSub }, 26.f) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0).HAlign(HAlign_Center)
			[ Row({ &SRing, &SEar, &SNeck }, 8.f) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0).HAlign(HAlign_Center)
			[ Row({ &SGlove, &SBack, &SBoots }, 26.f) ]
			// Ammo slots (Arrow / Bullet / Throw) under the grid.
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0).HAlign(HAlign_Center)
			[ AccessoryRow() ]
		];
	}

	TSharedRef<SWidget> AccessoryRow()
	{
		// Ammo (t_eSHOT) slots: Arrow / Bullet / Throw — click an ammo material
		// in the bag to load; click a loaded slot to return it to the bag.
		// (Ring/earring/necklace live in the ARPG grid now.)
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
		for (int32 s = 0; s < 3; ++s)
			Row->AddSlot().AutoWidth().Padding(s == 0 ? 0 : 8, 0, 0, 0)[ AmmoSlotCell(s) ];
		return Row;
	}

	TSharedRef<SWidget> AmmoSlotCell(int32 ShotIdx)
	{
		static const TCHAR* kShotNames[3] = { TEXT("Arrow"), TEXT("Bullet"), TEXT("Throw") };
		// t_eSHOT order matches the classic container's right-hand ghost column.
		static const TCHAR* kShotGhosts[3] = { TEXT("arrow"), TEXT("bullet"), TEXT("cannon") };
		TSharedRef<SButton> Cell = SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder")
			.OnClicked_Lambda([this, ShotIdx]() {
				if (ARoseCharacter* C = Char()) C->UnequipAmmo(ShotIdx);
				return FReply::Handled();
			})
			[
				SNew(SBox).WidthOverride(44.f).HeightOverride(44.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ SNew(SImage).Image(SlotBrush.Get()) ]
					+ SOverlay::Slot().Padding(3.f).HAlign(HAlign_Center).VAlign(VAlign_Center)
					[ SNew(SImage).Image_Lambda([this, ShotIdx]() -> const FSlateBrush* {
						ARoseCharacter* C = Char();
						if (!C || !C->AmmoSlots.IsValidIndex(ShotIdx) ||
							C->AmmoSlots[ShotIdx].Id < 0 || C->AmmoSlots[ShotIdx].Count <= 0)
							return GhostBrush(kShotGhosts[ShotIdx]);
						return const_cast<SRoseInventory*>(this)->IconForItem(
							TEXT("material"), C->AmmoSlots[ShotIdx].Id);
					}) ]
					+ SOverlay::Slot().VAlign(VAlign_Bottom).HAlign(HAlign_Right).Padding(2.f)
					[ SNew(STextBlock).Font(Font(8)).ColorAndOpacity(kText)
					  .Text_Lambda([this, ShotIdx]() {
						ARoseCharacter* C = Char();
						const int32 N = (C && C->AmmoSlots.IsValidIndex(ShotIdx)) ? C->AmmoSlots[ShotIdx].Count : 0;
						return N > 0 ? FText::FromString(FString::FromInt(N)) : FText::GetEmpty();
					  }) ]
				]
			];
		// Loaded ammo gets the full item tooltip; empty shows the shot type.
		const FString ShotName(kShotNames[ShotIdx]);
		Cell->SetToolTip(MakeItemTooltip([this, ShotIdx, ShotName]() {
			ARoseCharacter* C = Char();
			FTipInfo T;
			if (C && C->AmmoSlots.IsValidIndex(ShotIdx) &&
				C->AmmoSlots[ShotIdx].Id >= 0 && C->AmmoSlots[ShotIdx].Count > 0)
				T = TipInfoFor(TEXT("material"), C->AmmoSlots[ShotIdx].Id,
					C->AmmoSlots[ShotIdx].Count, 0, true);
			if (!T.bValid) T.Name = FString::Printf(TEXT("%s — empty"), *ShotName);
			T.Hint = TEXT("click a matching material in the bag to load; click here to unload");
			return T;
		}));
		return Cell;
	}

	// ── PAT pane (TopTab 2): cart / castle-gear part slots + fuel ─────────────
	TSharedRef<SWidget> BuildPatPane()
	{
		static const TCHAR* kPartNames[5] = {
			TEXT("Body"), TEXT("Engine"), TEXT("Wheels"), TEXT("Ability"), TEXT("Weapon") };
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox);
		for (int32 p = 0; p < 5; ++p)
		{
			const FString PartName(kPartNames[p]);
			TSharedRef<SButton> PartCell = SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder")
					.OnClicked_Lambda([this, p]() {
						if (ARoseCharacter* C = Char()) C->EquipRidePart(p, -1);   // unequip
						return FReply::Handled();
					})
					[
						SNew(SBox).WidthOverride(50.f).HeightOverride(50.f)
						[
							SNew(SOverlay)
							+ SOverlay::Slot()[ SNew(SImage).Image(SlotBrush.Get()) ]
							+ SOverlay::Slot().Padding(4.f)
							[ SNew(SImage).Image_Lambda([this, p]() -> const FSlateBrush* {
								ARoseCharacter* C = Char();
								const int32 Id = C ? C->GetRidePart(p) : -1;
								return Id >= 0
									? const_cast<SRoseInventory*>(this)->IconForItem(TEXT("pat"), Id)
									: nullptr;
							}) ]
						]
					];
			// PAT part slots get the same rich tooltip as everything else.
			PartCell->SetToolTip(MakeItemTooltip([this, p, PartName]() {
				ARoseCharacter* C = Char();
				const int32 Id = C ? C->GetRidePart(p) : -1;
				FTipInfo T = (C && Id >= 0) ? TipInfoFor(TEXT("pat"), Id, 1, 0, true) : FTipInfo();
				if (!T.bValid) T.Name = FString::Printf(TEXT("%s — empty"), *PartName);
				T.Hint = TEXT("click a part in the bag to mount it; click here to remove");
				return T;
			}));

			Col->AddSlot().AutoHeight().Padding(0, p == 0 ? 0 : 6, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth()[ PartCell ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(8, 0, 0, 0).VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(kTextDim)
					  .Text(FText::FromString(kPartNames[p])) ]
					+ SVerticalBox::Slot().AutoHeight()
					[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(kText)
					  .Text_Lambda([this, p]() {
						ARoseCharacter* C = Char();
						const int32 Id = C ? C->GetRidePart(p) : -1;
						return FText::FromString(Id >= 0 ? C->GetItemName(TEXT("pat"), Id) : TEXT("-"));
					  }) ]
				]
			];
		}
		// Fuel gauge (AT_FUEL): filled by class-317 fuel consumables in the bag.
		Col->AddSlot().AutoHeight().Padding(0, 10, 0, 0)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()
			[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(kTextDim)
			  .Text_Lambda([this]() {
				ARoseCharacter* C = Char();
				return FText::FromString(FString::Printf(TEXT("Fuel  %.0f / %.0f"),
					C ? C->GetFuel() : 0.f, C ? C->GetMaxFuel() : 0.f));
			  }) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 3, 0, 0)
			[ SNew(SBox).HeightOverride(8.f)
			  [ SNew(SProgressBar)
				.Percent_Lambda([this]() {
					ARoseCharacter* C = Char();
					return (C && C->GetMaxFuel() > 0.f) ? C->GetFuel() / C->GetMaxFuel() : 0.f;
				}) ] ]
		];
		Col->AddSlot().AutoHeight().Padding(0, 6, 0, 0)
		[ SNew(STextBlock).Font(Font(9)).ColorAndOpacity(kTextDim).AutoWrapText(true)
		  .Text(FText::FromString(TEXT(
			"Click a cart/castle-gear part in the bag to mount it; click a slot to remove. "
			"Use a fuel item from the bag to refill."))) ];

		return SNew(SBox)
			.Visibility_Lambda([this]() { return TopTab == 2 ? EVisibility::Visible : EVisibility::Collapsed; })
			[ Col ];
	}

	// ── Bag category tabs + grid (presentational until an inventory model) ────
	// (BuildBagTabs removed — the layout draws and owns the bag tabs now.)


	// Bag item shown in visible-cell #Index (after the category-tab filter), or
	// null.  Category: 0 All / 1 Consumable / 2 Material (equipment shows in All).
	const FRoseItemStack* BagItemAt(int32 Index) const
	{
		ARoseCharacter* C = Char();
		if (!C) return nullptr;
		const TArray<FRoseItemStack>& B = C->GetBag();
		int32 Visible = 0;
		for (const FRoseItemStack& S : B)
		{
			if (RoseBagCategory(S.Slot) != ActiveBagTab()) continue;   // Equipment / Consume / Material
			if (Visible == Index) return &S;
			++Visible;
		}
		return nullptr;
	}

	// Real Bag array index for a visible cell (EquipAmmoFromBag mutates by index).
	int32 BagRealIndex(int32 Index) const
	{
		ARoseCharacter* C = Char();
		if (!C) return INDEX_NONE;
		const TArray<FRoseItemStack>& B = C->GetBag();
		int32 Visible = 0;
		for (int32 i = 0; i < B.Num(); ++i)
		{
			if (RoseBagCategory(B[i].Slot) != ActiveBagTab()) continue;
			if (Visible == Index) return i;
			++Visible;
		}
		return INDEX_NONE;
	}

	static bool IsEquipSlot(const FString& Slot)
	{
		// Single source of truth (RoseDrops).  This widget copy used to omit
		// faceitem/subwpn, which made masks and shields silently un-clickable
		// in the bag.  hair stays UI-only (appearance, not a droppable slot).
		return RoseIsEquipSlot(Slot) || Slot == TEXT("hair");
	}

	// ── Classic item tooltip (matches the ROSE item-info box) ─────────────────
	struct FTipReq { bool bPresent = false; FString Label; bool bMet = true; };
	struct FTipInfo
	{
		bool bValid = false, bEquip = false, bWeapon = false;
		FString Name, Classification, Rarity, Description;
		int32 Defense = 0, MagicResist = 0, Durability = 0, Attack = 0;
		int32 Dodge = 0, WeightT = 0, Bonus = 0, Count = 1, Refine = 0;
		bool bAppraised = true;
		// Resolved bonus-option stats ("STR +1, Max HP +5") — shown BLUE once
		// appraised; unappraised items show a RED [Unappraised] line instead.
		FString BonusText;
		FTipReq Reqs[2];
		// Non-equipment (consumable / material / gem / pat): the classic
		// "Type: <STR_ITEMTYPE name>   Quality: n" line and the effect line.
		FString TypeName, EffectLine;
		int32 Quality = 0;
		// Free-form footer (slot hints on equip/PAT/ammo slots); empty on bag cells.
		FString Hint;
	};

	// Gather everything the tooltip shows for one (slot,id) instance.  Bag cells
	// pass the stack's Count/Bonus/bAppraised; equip, PAT and ammo slots pass
	// their own.  This is the single source of tooltip content.
	FTipInfo TipInfoFor(const FString& Slot, int32 Id, int32 Count,
		int32 Bonus, bool bAppraised, int32 Refine = 0) const
	{
		FTipInfo T;
		ARoseCharacter* C = Char();
		if (!C || Id < 0) return T;
		T.bValid = true;
		T.Name = C->GetItemName(Slot, Id);
		T.Description = C->GetItemDescription(Slot, Id);
		T.Classification = ClassificationName(Slot);
		T.Count = Count;
		T.Bonus = Bonus;
		T.bAppraised = bAppraised;
		T.Refine = Refine;
		// Named option stats from LIST_JEMITEM (Bonus is a ROW ID, not a magnitude).
		if (Bonus > 0 && bAppraised)
			T.BonusText = C->BonusStatText(Bonus);
		T.bEquip = RoseIsEquipSlot(Slot);
		T.bWeapon = Slot.Equals(TEXT("weapon"), ESearchCase::IgnoreCase);

		int32 ReqStat[2] = { 0, 0 }, ReqAmt[2] = { 0, 0 }, Quality = 0;
		if (T.bWeapon)
		{
			if (const FRoseWeaponRow* R = C->GetWeaponRow(Id))
			{
				T.Attack = R->AttackPower; T.Durability = R->Durability;
				T.WeightT = R->Weight; Quality = R->Quality;
				ReqStat[0] = R->ReqStat1; ReqAmt[0] = R->ReqAmount1;
				ReqStat[1] = R->ReqStat2; ReqAmt[1] = R->ReqAmount2;
			}
		}
		else if (const FRoseArmorRow* R = C->GetArmorRow(Slot, Id))
		{
			T.Defense = R->Defense; T.MagicResist = R->MagicResist;
			T.Durability = R->Durability; T.WeightT = R->Weight; Quality = R->Quality;
			if (R->BonusStat1 == 22) T.Dodge = R->BonusAmount1;   // AT_AVOID
			if (R->BonusStat2 == 22) T.Dodge = R->BonusAmount2;
			ReqStat[0] = R->ReqStat1; ReqAmt[0] = R->ReqAmount1;
			ReqStat[1] = R->ReqStat2; ReqAmt[1] = R->ReqAmount2;
		}
		else if (const FRoseSimpleItemRow* SR = C->GetSimpleItemRow(Slot, Id))
		{
			// Non-equipment: everything past name+description used to be hidden.
			Quality = SR->Quality;
			T.TypeName = SR->TypeName;
			T.WeightT = SR->Weight;
			T.EffectLine = DescribeUseEffect(*SR);
		}
		T.Quality = Quality;
		T.Rarity = RarityName(Quality);

		for (int32 k = 0; k < 2; ++k)
		{
			if (ReqStat[k] == 0) continue;
			FTipReq& Q = T.Reqs[k];
			Q.bPresent = true;
			Q.bMet = C->GetAbilityValue(ReqStat[k]) >= ReqAmt[k];
			if (ReqStat[k] == 31)
				Q.Label = FString::Printf(TEXT("[Equipment Conditions: Level %d]"), ReqAmt[k]);
			else if (ReqStat[k] == 4)
				Q.Label = FString::Printf(TEXT("[Job Name: %s Job]"), *JobClassName(ReqAmt[k]));
			else
				Q.Label = FString::Printf(TEXT("[Requires %s %d]"), *AbilityShort(ReqStat[k]), ReqAmt[k]);
		}
		return T;
	}

	// Bag-stack overload — behaviour unchanged.
	FTipInfo TipInfo(const FRoseItemStack* It) const
	{
		if (!It) return FTipInfo();
		return TipInfoFor(It->Slot, It->Id, It->Count, It->Bonus, It->bAppraised, It->Refine);
	}

	// The classic "[HP 1000]" effect line for a use item (LIST_USEITEM effect
	// block).  Empty for items with no modelled effect.
	static FString DescribeUseEffect(const FRoseSimpleItemRow& R)
	{
		auto AbilityName = [](int32 A) -> const TCHAR* {
			switch (A)
			{
			case 16: return TEXT("HP");
			case 17: return TEXT("MP");
			case 30: return TEXT("XP");
			case 40: return TEXT("Zuly");
			case 76: return TEXT("Stamina");
			case 77: return TEXT("Fuel");
			default: return nullptr;
			}
		};
		if (R.Subtype == 317)       // USE_ITEM_FUEL — refills the cart engine
			return TEXT("[Refuels the engine]");
		const TCHAR* N = AbilityName(R.AddAbility);
		if (!N || R.AddValue == 0) return FString();
		// StatusId != 0 = delivered over time at StatusPerSec per second.
		if (R.StatusId != 0 && R.StatusPerSec > 0)
			return FString::Printf(TEXT("[%s %d over %.0fs]"), N, R.AddValue,
				(float)R.AddValue / (float)R.StatusPerSec);
		return FString::Printf(TEXT("[%s %d]"), N, R.AddValue);
	}

	// A rich, colour-coded tooltip driven by a live FTipInfo provider — bag
	// cells, equip slots, PAT part slots and ammo slots all share it.
	TSharedRef<SToolTip> MakeItemTooltip(TFunction<FTipInfo()> Provider)
	{
		// ── 1:1 with the client's item tooltip ───────────────────────────────
		//
		//   Dimple Robe                                   gold
		//   Defense:242  Magic Defense:278  Dodge Rate:76 blue
		//   Durability:120 / 120                          blue
		//   Type:Magic Armor                              blue
		//   [Max MP 120]                                  green   (bonus option)
		//   [Job Class: Mage or Cleric]                   green   (requirement)
		//   [Requires:Level 160]                          green/red
		//   Weight:20                                     blue
		//   A cute, classy vest made by a famous designer. grey
		//
		// "Item Grade" is deliberately absent — the client we build against has
		// no such field, so printing one would be inventing data.
		TSharedRef<TFunction<FTipInfo()>> P = MakeShared<TFunction<FTipInfo()>>(MoveTemp(Provider));
		auto I = [P]() { return (*P)(); };
		auto Vis = [](bool b) { return b ? EVisibility::Visible : EVisibility::Collapsed; };

		const FLinearColor kStat(0.62f, 0.80f, 0.95f, 1.f);   // the client's pale blue
		const FLinearColor kGreen(0.45f, 0.90f, 0.45f, 1.f);
		const FLinearColor kRed  (0.95f, 0.35f, 0.35f, 1.f);

		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

		auto Line = [&](TFunction<FString()> Get, const FLinearColor& Col, bool bBold = false)
		{
			Box->AddSlot().AutoHeight()
			[
				SNew(STextBlock).Font(Font(bBold ? 11 : 9, bBold)).AutoWrapText(true)
				.ColorAndOpacity(Col)
				.Text_Lambda([Get]() { return FText::FromString(Get()); })
				.Visibility_Lambda([Get]() {
					return Get().IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			];
		};

		// Name (+refine / xN).  (+N) is the REFINE grade, the classic convention.
		Line([I]() {
			FTipInfo T = I();
			FString N = T.Name;
			if (T.Refine > 0) N += FString::Printf(TEXT(" (+%d)"), T.Refine);
			if (T.Count > 1)  N += FString::Printf(TEXT(" x%d"), T.Count);
			return N;
		}, kGold, /*bBold*/ true);

		// Combat stats, all on ONE line like the client.
		Line([I]() {
			FTipInfo T = I();
			if (!T.bEquip) return FString();
			TArray<FString> Parts;
			if (T.bWeapon && T.Attack > 0) Parts.Add(FString::Printf(TEXT("Attack:%d"), T.Attack));
			if (T.Defense > 0)     Parts.Add(FString::Printf(TEXT("Defense:%d"), T.Defense));
			if (T.MagicResist > 0) Parts.Add(FString::Printf(TEXT("Magic Defense:%d"), T.MagicResist));
			if (T.Dodge > 0)       Parts.Add(FString::Printf(TEXT("Dodge Rate:%d"), T.Dodge));
			return FString::Join(Parts, TEXT("  "));
		}, kStat);

		Line([I]() {
			FTipInfo T = I();
			return T.Durability > 0
				? FString::Printf(TEXT("Durability:%d / %d"), T.Durability, T.Durability)
				: FString();
		}, kStat);

		Line([I]() {
			FTipInfo T = I();
			return T.TypeName.IsEmpty() ? FString()
				: FString::Printf(TEXT("Type:%s"), *T.TypeName);
		}, kStat);

		// Bonus option, or the unappraised marker.
		Line([I]() {
			FTipInfo T = I();
			if (T.Bonus > 0 && !T.bAppraised) return FString(TEXT("[Unappraised]"));
			return T.BonusText.IsEmpty() ? FString()
				: FString::Printf(TEXT("[%s]"), *T.BonusText);
		}, kGreen);

		// Requirements — green when met, red when not, one line each.
		for (int32 k = 0; k < 2; ++k)
		{
			Box->AddSlot().AutoHeight()
			[
				SNew(STextBlock).Font(Font(9)).AutoWrapText(true)
				.ColorAndOpacity_Lambda([I, k]() {
					return I().Reqs[k].bMet ? FLinearColor(0.45f, 0.90f, 0.45f, 1.f)
					                        : FLinearColor(0.95f, 0.35f, 0.35f, 1.f); })
				.Text_Lambda([I, k]() { return FText::FromString(I().Reqs[k].Label); })
				.Visibility_Lambda([I, k, Vis]() { return Vis(I().Reqs[k].bPresent); })
			];
		}

		Line([I]() {
			FTipInfo T = I();
			return T.WeightT > 0 ? FString::Printf(TEXT("Weight:%d"), T.WeightT) : FString();
		}, kStat);

		// Effect line for consumables ("[HP 300 over 4s]").
		Line([I]() { return I().EffectLine; }, kGreen);

		// Flavour text last, in grey.
		Line([I]() { return I().Description; }, kTextDim);

		// Slot hint (equip/PAT/ammo cells only).
		Line([I]() { return I().Hint; }, RoseTheme::Dim);

		return SNew(SToolTip)
		[
			SNew(SBorder).BorderImage(TipBgBrush.Get()).Padding(FMargin(8.f, 6.f))
			[ SNew(SBox).MaxDesiredWidth(320.f)[ Box ] ]
		];
	}

	TSharedRef<SWidget> BagCell(int32 Index)
	{
		TSharedRef<SRoseDragSlot> Cell = SNew(SRoseDragSlot)
			// Click = equip (swap) if it's an equipment item.
			.OnClick_Lambda([this, Index]() {
				const FRoseItemStack* It = BagItemAt(Index);
				ARoseCharacter* C = Char();
				if (!It || !C) return;
				if (IsEquipSlot(It->Slot))
				{
					const FString Slot = It->Slot; const int32 Id = It->Id;   // copy (equip mutates the bag)
					C->EquipFromBag(Slot, Id);
					return;
				}
				// PAT part -> its ride slot; ammo material -> its shot slot;
				// fuel consumable (class 317) -> refill the cart.
				if (It->Slot == TEXT("pat"))
				{
					if (const FRoseSimpleItemRow* R = C->GetSimpleItemRow(TEXT("pat"), It->Id))
						C->EquipRidePart(ARoseCharacter::RidePartForClass(R->Subtype), It->Id);
					return;
				}
				if (It->Slot == TEXT("material") &&
					ARoseCharacter::ShotTypeForClass(
						C->GetSimpleItemRow(TEXT("material"), It->Id)
							? C->GetSimpleItemRow(TEXT("material"), It->Id)->Subtype : 0) >= 0)
				{
					C->EquipAmmoFromBag(BagRealIndex(Index));
					return;
				}
				if (It->Slot == TEXT("consumable"))
				{
					// Full use path (requirements, cooldown group, instant vs
					// over-time restore, stack decrement).  Fuel items route
					// through it too.
					const int32 Id = It->Id;   // copy (using mutates the bag)
					FString Reason;
					if (!C->TryUseConsumableItem(Id, Reason))
						UE_LOG(LogTemp, Log, TEXT("[Rose] consumable %d not used: %s"),
							Id, *Reason);
				}
			})
			// Drag a bag item out → onto its equip slot to equip it.
			.OnBeginDrag_Lambda([this, Index]() -> TSharedPtr<FRoseItemDragOp> {
				const FRoseItemStack* It = BagItemAt(Index);
				if (!It) return nullptr;
				TSharedRef<FRoseItemDragOp> Op = MakeShared<FRoseItemDragOp>();
				Op->Slot = It->Slot; Op->Id = It->Id; Op->bFromEquip = false;
				Op->Icon = IconForItem(It->Slot, It->Id);
				return Op;
			})
			// Accept an item dragged off an equip slot → unequip into the bag.
			.CanAccept_Lambda([](const TSharedPtr<FRoseItemDragOp>& Op) {
				return Op.IsValid() && Op->bFromEquip;
			})
			.OnDropItem_Lambda([this](const TSharedPtr<FRoseItemDragOp>& Op) {
				if (ARoseCharacter* C = Char()) C->UnequipToBag(Op->Slot);
			})
			[
				SNew(SBox).WidthOverride(50.f).HeightOverride(50.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ SNew(SImage).Image(SlotBrush.Get()) ]
					+ SOverlay::Slot().Padding(4.f)
					[ SNew(SImage).Image_Lambda([this, Index]() -> const FSlateBrush* {
						const FRoseItemStack* It = BagItemAt(Index);
						return It ? IconForItem(It->Slot, It->Id) : nullptr; }) ]
					// Stack count, bottom-right.
					+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(3.f)
					[ SNew(STextBlock).Font(Font(8, true)).ColorAndOpacity(kText)
					  .Text_Lambda([this, Index]() {
						const FRoseItemStack* It = BagItemAt(Index);
						return (It && It->Count > 1) ? FText::AsNumber(It->Count) : FText::GetEmpty(); }) ]
				]
			];
		// Rich classic tooltip (rebuilds live from the cell's current item).
		Cell->SetToolTip(MakeItemTooltip([this, Index]() { return TipInfo(BagItemAt(Index)); }));
		return Cell;
	}

	TSharedRef<SWidget> BuildBagGrid()
	{
		const int32 Cols = 5, Rows = 5;
		TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(3.f));
		for (int32 r = 0; r < Rows; ++r)
			for (int32 c = 0; c < Cols; ++c)
				Grid->AddSlot(c, r)[ BagCell(r * Cols + c) ];
		return Grid;
	}

	// ── Zuly (currency) bar ───────────────────────────────────────────────────
	TSharedRef<SWidget> BuildZulyBar()
	{
		return SNew(SBox).HeightOverride(22.f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(BarBrush.Get()) ]
			+ SOverlay::Slot().Padding(8, 0).VAlign(VAlign_Center)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
				[ SNew(STextBlock).Font(Font(9, true)).ColorAndOpacity(kGold).Text(FText::FromString(TEXT("Zuly"))) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).HAlign(HAlign_Right).VAlign(VAlign_Center)
				[ SNew(STextBlock).Font(Font(10, true)).ColorAndOpacity(kText)
				  .Text_Lambda([this]() { ARoseCharacter* C = Char(); return FText::AsNumber(C ? C->GetZuly() : 0); }) ]
			]
		];
	}

	// dlgitem's authored geometry (see the layout, not a screenshot).
	static constexpr float kDlgW = 306.f, kDlgH = 468.f;
	static constexpr int32 kBagCols = 6, kBagRows = 5;
	static constexpr float kBagX0 = 20.f, kBagY0 = 201.f;   // first icon anchor
	static constexpr float kBagPitch = 45.f, kBagCell = 38.f;

	TSharedPtr<SRoseUIWindow>   Layout;    // the client's dlgitem
	TSharedPtr<SConstraintCanvas> Overlay; // our live widgets over it

	TWeakObjectPtr<ARoseCharacter> CharWeak;
	int32 TopTab = 0;    // 0 Equipment / 1 Costume
	// BagTab is gone: the LAYOUT owns tab state now (see ActiveBagTab).
	TSharedPtr<FSlateBrush> PreviewLive;
	TMap<int32, TSharedPtr<FSlateBrush>> IconCache;
	TMap<FString, TSharedPtr<FSlateBrush>> GhostCache;
	TArray<TStrongObjectPtr<UTexture2D>> KeepTextures;
	TSharedPtr<FSlateBrush> SlotBrush, PreviewBrush, TabOnBrush, TabOffBrush, BarBrush, GoldFill, TipBgBrush;
};

TSharedRef<SWidget> RoseInventory_MakeContent(ARoseCharacter& Char)
{
	return SNew(SRoseInventory).Char(&Char);
}
