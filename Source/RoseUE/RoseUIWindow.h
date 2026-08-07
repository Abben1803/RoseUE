// SRoseUIWindow — renders a converted classic-client ROSE dialog
// (Content/UI/Layouts/<dlg>.json from tools/gen_ui_json.py) with the sliced
// sprite textures at /Game/UI/Sprites.  JSON-driven — no XML at runtime.
// Faithful to the client's XML UI (src/client/interface, io_imageres.cpp:
// sprites resolve by GID name; MODULEID picks the TSI, which the name-keyed
// atlas already flattens).
//
// Controls: IMAGE, PANE (all rendered — complementary regions), TABBEDPANE
// (active tab only), CAPTION (drag region), BUTTON/TABBUTTON/PUSHBUTTON
// (NORMALGID/OVERGID/DOWNGID; a *CLOSE* sprite closes the window), RADIOBUTTON
// (grouped by RADIOBOXID, selected draws its DOWN sprite), CHECKBOX
// (CHECKGID/UNCHECKGID), GUAGE (BGID track + GID fill + centred text), TABLE
// (cell grid), LISTBOX/ZLISTBOX (scrolling text rows), EDITBOX, COMBOBOX,
// SKILL (tree node), STATIC (named anchor rect, no visual), plus an optional
// caller-supplied content overlay.
//
// LIVE DATA: the layouts describe shape, never values.  Game code supplies
// those through the Bind* delegates below, keyed by the control's XML NAME (or
// ID when unnamed) — e.g. dlginfo's gauges are NAME="HP"/"MP"/"EXP_MINI"/
// "SUMMON_GAUGE".  A control with no binding renders empty rather than
// disappearing, so an unbound window still looks right.
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/SCompoundWidget.h"

class FJsonObject;
class SConstraintCanvas;
class UTexture2D;

/** A labeled layout button was clicked (label = the XML element text, e.g.
 *  "STR" on dlgavata's plus buttons); close buttons fire OnClose instead. */
DECLARE_DELEGATE_OneParam(FOnRoseUIButton, const FString& /*Label*/);

/** Identifies a control to the game code supplying its value: the XML NAME if
 *  it has one, else the ID.  Both are passed — some controls are named, many
 *  are only numbered. */
struct FRoseUIKey
{
	FString Name;
	int32   Id = 0;
	bool Is(const TCHAR* InName) const { return Name.Equals(InName, ESearchCase::IgnoreCase); }
};

/** 0..1 fill fraction for a GUAGE. */
DECLARE_DELEGATE_RetVal_OneParam(float, FRoseUIGaugeValue, const FRoseUIKey&);
/** Text for a GUAGE overlay, EDITBOX, STATIC label or LISTBOX row (Row = -1 for
 *  the control itself, >= 0 for a list row). */
DECLARE_DELEGATE_RetVal_TwoParams(FText, FRoseUITextValue, const FRoseUIKey&, int32 /*Row*/);
/** Row count for a LISTBOX/ZLISTBOX/TABLE. */
DECLARE_DELEGATE_RetVal_OneParam(int32, FRoseUICountValue, const FRoseUIKey&);
/** Current on/off for a CHECKBOX, or the selected index of a RADIOBOX. */
DECLARE_DELEGATE_RetVal_OneParam(int32, FRoseUIStateValue, const FRoseUIKey&);
/** A control was activated: button/checkbox/radio/list row (Row = -1 if none). */
DECLARE_DELEGATE_TwoParams(FOnRoseUIActivate, const FRoseUIKey&, int32 /*Row*/);

class ROSEUE_API SRoseUIWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseUIWindow) {}
		/** Dialog name, e.g. "dlgitem" (Content/UI/Layouts/<name>.json). */
		SLATE_ARGUMENT(FString, Dialog)
		/** Widget overlaid over the WHOLE window (position children absolutely). */
		SLATE_ARGUMENT(TSharedPtr<SWidget>, Content)
		/** Called when the window's close button is clicked. */
		SLATE_EVENT(FSimpleDelegate, OnClose)
		/** Called when any other labeled layout button is clicked. */
		SLATE_EVENT(FOnRoseUIButton, OnButton)
		/** Which tab a multi-tab pane opens on, matched against the tab's tag
		 *  (its TABBUTTON name/label/sprite, uppercased).  Needed because tab
		 *  ORDER in the XML is not display order: dlgitem lists Costume first,
		 *  so defaulting to index 0 opens the wrong tab with every accessory
		 *  slot greyed out. */
		SLATE_ARGUMENT(FString, InitialTab)
		/** Live-data providers — see the FRoseUIKey note above. */
		SLATE_EVENT(FRoseUIGaugeValue, OnGaugeValue)
		SLATE_EVENT(FRoseUITextValue,  OnTextValue)
		SLATE_EVENT(FRoseUICountValue, OnRowCount)
		SLATE_EVENT(FRoseUIStateValue, OnState)
		SLATE_EVENT(FOnRoseUIActivate, OnActivate)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	/** Screen-space top-left; the owner binds its canvas slot to this (drag). */
	FVector2D Position = FVector2D(200.f, 200.f);
	FVector2D Size = FVector2D(300.f, 200.f);

	// ── Faithful placement (IT_MGR::InitInterfacePos + dlg root DEFAULT_*) ─────
	// The client positions each dialog by a corner/centre enum + a pixel adjust,
	// relative to the live screen size — not a fixed point.  AnchorX/Y use the
	// TDXP_/TDYP_ enums (0 = LEFT/TOP, 1 = CENTER, 2 = RIGHT/BOTTOM); Adjust is
	// ADJUST_X/Y; DragDelta accumulates user dragging on top.
	int32 AnchorX = 0, AnchorY = 0;
	FVector2D Adjust = FVector2D::ZeroVector;
	FVector2D DragDelta = FVector2D::ZeroVector;
	/** Top-left for the current viewport = corner(anchor) + Adjust + DragDelta. */
	FVector2D ComputeTopLeft(const FVector2D& Viewport) const;

	// Caption drag
	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Ev) override;
	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Ev) override;

	/** Load a converted layout JSON, or null (logs why). */
	static TSharedPtr<FJsonObject> LoadLayout(const FString& DialogName);

	/** Show or hide a NAMED pane at runtime.
	 *
	 *  Layouts carry ALTERNATE panes and let the client pick between them:
	 *  dlgquickbar has HORIZONTAL / HORIZONTAL_HOVER / VERTICAL / VERTICAL_HOVER,
	 *  where the _HOVER variants are the frame + rotate/page buttons that only
	 *  appear while the cursor is over the bar.  Rendering them all at once is
	 *  wrong, so the owner drives them. */
	void SetPaneVisibility(const FString& PaneName, TAttribute<EVisibility> Vis);

	/** An authored rect by NAME, in window-local space.  Covers STATICs (pure
	 *  anchors like dlgquest's "SMALL_POS") and any NAMED image — dlgitem names
	 *  its bag icon anchors ITEM_SLOT00..ITEM_SLOT54.  The client uses these to
	 *  place content it draws itself, so callers can put icons exactly where
	 *  ROSE does instead of re-deriving a grid. */
	bool GetAnchorRect(const FString& Name, FSlateRect& Out) const;

	/** Authored rects of every IMAGE drawn with this GID, in layout order.
	 *  The equipment slots are unnamed but each uses a distinct sprite
	 *  (GEN_INVSLOT01 = Ring, 02 = Necklace, …), so the GID is what identifies
	 *  them; the bag wells all share GEN_DECO07, hence an array. */
	const TArray<FSlateRect>* GetSpriteRects(const FString& Gid) const;

	/** True if any tabbed pane's ACTIVE tab is tagged with this string (tag =
	 *  the tab button's label + sprite name, e.g. "EQUIPMENT", "ABILITY").
	 *  Lets caller content follow tab switches (equip slots, stat values). */
	bool IsTabActive(const FString& TagContains) const;

private:
	const FSlateBrush* SpriteBrush(const FString& Gid, float W, float H);
	void AddNode(const TSharedRef<SConstraintCanvas>& Canvas,
	             const TSharedPtr<FJsonObject>& Node, float BaseX, float BaseY);
	/** A button's visible text: its XML element text, else the text binding. */
	TSharedRef<SWidget> ButtonLabel(const TSharedPtr<FJsonObject>& Node);

	/** Sprite-styled SButton from a layout node, or null if no normal sprite. */
	TSharedPtr<class SButton> MakeSpriteButton(const TSharedPtr<FJsonObject>& Node,
	                                           float W, float H, TFunction<void()> OnClick);

	// One active-tab index per TABBEDPANE in the layout (tab switching), plus
	// each tab's identifying tag (button label + sprite name, uppercased).
	TArray<int32> ActiveTabs;
	TArray<TArray<FString>> TabTags;
	TArray<TSharedPtr<struct FButtonStyle>> ButtonStyles;

	// Tab buttons are placed only after every tab's content canvas exists, so
	// they end up on top of all of them (see the TABBEDPANE branch).  The list
	// itself is local to that branch because panes nest.
	struct FPendingButton
	{
		TSharedPtr<class SButton> Button;
		FSlateRect Rect;
	};

	// RADIOBUTTONs are grouped by their RADIOBOXID; the selected one in each
	// group draws its pressed sprite.  Keyed by that id, value = selected
	// button index within the group.
	TMap<int32, int32> RadioSelection;
	TMap<int32, int32> RadioCounts;      // next index to hand out per group

	// Named rects (anchors the client draws content into) + every IMAGE rect
	// keyed by its sprite, so callers can find slots without guessing a grid.
	TMap<FString, FSlateRect> AnchorRects;
	TMap<FString, TArray<FSlateRect>> SpriteRects;

	// Rects are recorded in WINDOW space, but TAB and named-PANE children are
	// laid out in their own canvas starting at base 0 — so the layout offset has
	// to be carried separately or GetAnchorRect/GetSpriteRects hand back
	// container-relative coordinates.  That is what threw the inventory's bag
	// cells out into the world: the wells live inside a TAB.
	FVector2D RectBase = FVector2D::ZeroVector;

	// Named PANEs get their own canvas so they can be shown/hidden as a unit.
	TMap<FString, TSharedPtr<class SConstraintCanvas>> NamedPanes;

	// Live-data providers.
	FString InitialTabTag;

	// The CAPTION is usually the layout's FIRST child, so its text has to be
	// added AFTER everything else or the frame images paint straight over it.
	TSharedPtr<FJsonObject> CaptionNode;

	FRoseUIGaugeValue GaugeValue;
	FRoseUITextValue  TextValue;
	FRoseUICountValue RowCount;
	FRoseUIStateValue StateValue;
	FOnRoseUIActivate ActivateEvent;

	/** Font from the layout's FONT id (see gen_ui_json: 100009/100010/210010…). */
	FSlateFontInfo LayoutFont(const TSharedPtr<FJsonObject>& Node) const;
	FRoseUIKey     KeyOf(const TSharedPtr<FJsonObject>& Node) const;

	FSimpleDelegate OnClose;
	FOnRoseUIButton OnButton;
	FSlateRect CaptionRect = FSlateRect(0, 0, 0, 0);   // local drag region
	bool bDragging = false;
	FVector2D DragStart = FVector2D::ZeroVector;       // cursor at drag start
	FVector2D DragOrigin = FVector2D::ZeroVector;      // Position at drag start

	// Brushes + their textures, kept alive for the widget's lifetime.
	TArray<TSharedPtr<FSlateBrush>> Brushes;
	TArray<TStrongObjectPtr<UTexture2D>> Textures;
};
