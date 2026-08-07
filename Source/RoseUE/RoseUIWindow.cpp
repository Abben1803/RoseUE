#include "RoseUIWindow.h"

#include "Dom/JsonObject.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "RoseUIHelpers.h"

#include "HAL/IConsoleManager.h"

#include "Styling/CoreStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableText.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

// JSON numbers arrive as doubles; the layouts store ints.
static float Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, float Def = 0.f)
{
	double V;
	return (O.IsValid() && O->TryGetNumberField(Field, V)) ? (float)V : Def;
}

static FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
{
	FString V;
	return (O.IsValid() && O->TryGetStringField(Field, V)) ? V : FString();
}

static TAutoConsoleVariable<int32> CVarUIDump(
	TEXT("rose.UIDump"), 0,
	TEXT("1 = log every layout control's computed rect as windows are built."),
	ECVF_Default);

bool SRoseUIWindow::IsTabActive(const FString& TagContains) const
{
	for (int32 P = 0; P < ActiveTabs.Num(); ++P)
	{
		const int32 A = ActiveTabs[P];
		if (TabTags.IsValidIndex(P) && TabTags[P].IsValidIndex(A)
			&& TabTags[P][A].Contains(TagContains))
			return true;
	}
	return false;
}

TSharedPtr<FJsonObject> SRoseUIWindow::LoadLayout(const FString& DialogName)
{
	const FString Path = FPaths::ProjectContentDir() / TEXT("UI/Layouts") / DialogName + TEXT(".json");
	FString Raw;
	if (!FFileHelper::LoadFileToString(Raw, *Path))
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseUI] layout missing: %s (run tools/gen_ui_json.py)"), *Path);
		return nullptr;
	}
	TSharedPtr<FJsonObject> Doc;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Raw), Doc) || !Doc.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseUI] bad layout json: %s"), *Path);
		return nullptr;
	}
	return Doc;
}

const FSlateBrush* SRoseUIWindow::SpriteBrush(const FString& Gid, float W, float H)
{
	if (Gid.IsEmpty())
		return nullptr;
	// Sprite assets are named by sanitized GID (tools/build_ui_sprites.py safe()).
	FString Safe = Gid;
	for (TCHAR& C : Safe)
		if (!FChar::IsAlnum(C) && C != TEXT('_'))
			C = TEXT('_');
	const FString Path = FString::Printf(TEXT("/Game/UI/Sprites/%s.%s"), *Safe, *Safe);
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path);
	if (!Tex)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseUI] sprite not imported: %s"), *Gid);
		return nullptr;
	}
	Textures.Emplace(Tex);
	TSharedPtr<FSlateBrush> B = MakeShared<FSlateBrush>();
	B->SetResourceObject(Tex);
	B->ImageSize = FVector2D(W > 0 ? W : Tex->GetSizeX(), H > 0 ? H : Tex->GetSizeY());
	Brushes.Add(B);
	return B.Get();
}

TSharedPtr<SButton> SRoseUIWindow::MakeSpriteButton(const TSharedPtr<FJsonObject>& Node,
                                                    float W, float H, TFunction<void()> OnClick)
{
	// Classic client: NORMALGID/OVERGID/DOWNGID (io_imageres sprites by name).
	FString GidUp = Str(Node, TEXT("normalgid"));
	if (GidUp.IsEmpty()) GidUp = Str(Node, TEXT("gid_up"));
	if (GidUp.IsEmpty()) GidUp = Str(Node, TEXT("gid"));
	const FSlateBrush* Up = SpriteBrush(GidUp, W, H);
	if (!Up)
		return nullptr;
	FString GidOver = Str(Node, TEXT("overgid"));
	if (GidOver.IsEmpty()) GidOver = Str(Node, TEXT("gid_over"));
	FString GidDown = Str(Node, TEXT("downgid"));
	if (GidDown.IsEmpty()) GidDown = Str(Node, TEXT("gid_down"));
	const FSlateBrush* Over = SpriteBrush(GidOver, W, H);
	const FSlateBrush* Down = SpriteBrush(GidDown, W, H);

	TSharedPtr<FButtonStyle> Style = MakeShared<FButtonStyle>();
	Style->SetNormal(*Up);
	Style->SetHovered(Over ? *Over : *Up);
	Style->SetPressed(Down ? *Down : *Up);
	Style->SetNormalPadding(FMargin(0));
	Style->SetPressedPadding(FMargin(0));
	ButtonStyles.Add(Style);   // must outlive the button

	return SNew(SButton)
		.ButtonStyle(Style.Get())
		.ContentPadding(FMargin(0))
		.OnClicked_Lambda([Fn = MoveTemp(OnClick)]() {
			Fn();
			return FReply::Handled();
		});
}


// -- Layout helpers ---------------------------------------------------------

FRoseUIKey SRoseUIWindow::KeyOf(const TSharedPtr<FJsonObject>& Node) const
{
	FRoseUIKey K;
	K.Name = Str(Node, TEXT("name"));
	K.Id   = (int32)Num(Node, TEXT("id"), -1.f);
	return K;
}

// FONT is a 6-digit id of the form HH00SS: SS is the point size and HH the
// weight/family (only 10, 11 and 21 occur).  This encoding post-dates the
// classic client, so it is not in src/ -- it is read off the data: 110009 is
// what every TABBUTTON and BUTTON uses (bold labels in the client) while
// 100010 is what CAPTIONs and body text use (regular).  Hence bold = second
// digit set.  Family 2 (21xxxx) has no separate face on disk, so both families
// render with the default face at the authored size.
FSlateFontInfo SRoseUIWindow::LayoutFont(const TSharedPtr<FJsonObject>& Node) const
{
	const int32 Id = (int32)Num(Node, TEXT("font"), 100010.f);
	const int32 Size = (Id % 100) > 0 ? (Id % 100) : 10;
	const bool bBold = ((Id / 10000) % 10) == 1;
	return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
}

bool SRoseUIWindow::GetAnchorRect(const FString& Name, FSlateRect& Out) const
{
	if (const FSlateRect* R = AnchorRects.Find(Name))
	{
		Out = *R;
		return true;
	}
	return false;
}

void SRoseUIWindow::SetPaneVisibility(const FString& PaneName, TAttribute<EVisibility> Vis)
{
	if (TSharedPtr<SConstraintCanvas>* Pane = NamedPanes.Find(PaneName))
	{
		if (Pane->IsValid())
			(*Pane)->SetVisibility(MoveTemp(Vis));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseUI] no pane named '%s'"), *PaneName);
	}
}

const TArray<FSlateRect>* SRoseUIWindow::GetSpriteRects(const FString& Gid) const
{
	return SpriteRects.Find(Gid);
}

namespace
{
	// HALIGN is the client's own enum; the layouts only ever use 0/2/4/8.
	ETextJustify::Type JustifyOf(float H)
	{
		return H >= 2.f ? ETextJustify::Right
		     : H >= 1.f ? ETextJustify::Center
		                : ETextJustify::Left;
	}
}


// A button's visible TEXT is not in the layout -- the glass client sets it in
// code (its Avatar/Costume/PAT tabs are three identical plates).  So a button
// renders its element text if the XML had any, otherwise whatever the text
// binding returns for its key, otherwise nothing.
TSharedRef<SWidget> SRoseUIWindow::ButtonLabel(const TSharedPtr<FJsonObject>& Node)
{
	const FString Inline = Str(Node, TEXT("label"));
	const FRoseUIKey Key = KeyOf(Node);
	FRoseUITextValue Tv = TextValue;
	return SNew(STextBlock)
		.Font(LayoutFont(Node))
		.Justification(ETextJustify::Center)
		.ColorAndOpacity(FLinearColor::White)
		.Text_Lambda([Tv, Key, Inline]() {
			if (Tv.IsBound())
			{
				const FText T = Tv.Execute(Key, -1);
				if (!T.IsEmpty())
					return T;
			}
			return Inline.IsEmpty() ? FText::GetEmpty() : FText::FromString(Inline);
		});
}

// A control's position is X + OFFSETX (and Y + OFFSETY), never one or the
// other.  This is `IT_MGR`'s own model — SetPosition then SetOffset — and using
// OFFSET-or-X silently broke the glass layouts: they write "offsetx": 0
// EXPLICITLY, so the fallback never fired and all 1447 X/Y-positioned controls
// (every window frame slice, every close button) collapsed onto the window
// origin.  The classic layouts happened to use OFFSET only, which is why the
// old rule survived this long.

// The ROOT's WIDTH/HEIGHT is not always the real window size — dlgavata claims
// 269x329 while its own frame runs to y=416, which clipped the entire stat
// block.  The FRAME slices are authoritative: they are what the client draws as
// the window's edges.  Measured over the GEN_WND* family only, because ordinary
// content legitimately overhangs (dlgitem has a 306-wide text field at x=27).
static void MeasureFrame(const TSharedPtr<FJsonObject>& Node, float BaseX, float BaseY,
                         FVector2D& InOutSize)
{
	if (!Node.IsValid())
		return;
	const float X = BaseX + Num(Node, TEXT("x")) + Num(Node, TEXT("offsetx"));
	const float Y = BaseY + Num(Node, TEXT("y")) + Num(Node, TEXT("offsety"));
	if (Str(Node, TEXT("gid")).StartsWith(TEXT("GEN_WND")))
	{
		InOutSize.X = FMath::Max(InOutSize.X, X + Num(Node, TEXT("width")));
		InOutSize.Y = FMath::Max(InOutSize.Y, Y + Num(Node, TEXT("height")));
	}
	const TArray<TSharedPtr<FJsonValue>>* Kids;
	if (Node->TryGetArrayField(TEXT("children"), Kids))
		for (const TSharedPtr<FJsonValue>& K : *Kids)
			MeasureFrame(K->AsObject(), X, Y, InOutSize);
}

void SRoseUIWindow::AddNode(const TSharedRef<SConstraintCanvas>& Canvas,
                            const TSharedPtr<FJsonObject>& Node, float BaseX, float BaseY)
{
	const FString Type = Str(Node, TEXT("type"));
	const float OffX = BaseX + Num(Node, TEXT("x")) + Num(Node, TEXT("offsetx"));
	const float OffY = BaseY + Num(Node, TEXT("y")) + Num(Node, TEXT("offsety"));
	const float W = Num(Node, TEXT("width"));
	const float H = Num(Node, TEXT("height"));

	// `rose.UIDump 1` logs where every control ACTUALLY lands.  A layout can
	// compose perfectly on paper and still be placed wrong in Slate; guessing
	// from a screenshot is how you end up fixing the wrong thing.
	static const auto* DumpCVar =
		IConsoleManager::Get().FindConsoleVariable(TEXT("rose.UIDump"));
	if (DumpCVar && DumpCVar->GetInt() != 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[UIDump] %-12s id=%-4d name='%s' at (%.0f,%.0f) %.0fx%.0f gid='%s'"),
			*Type, (int32)Num(Node, TEXT("id"), -1.f), *Str(Node, TEXT("name")),
			OffX, OffY, W, H,
			*(Str(Node, TEXT("gid")).IsEmpty() ? Str(Node, TEXT("normalgid"))
			                                   : Str(Node, TEXT("gid"))));
	}

	if (Type == TEXT("CAPTION"))
	{
		// The frame art draws the caption BAR; the title TEXT is set by the game
		// (the XML has only where to put it), so it comes from the text binding
		// under the key "CAPTION".  TEXTOFFSETX/Y is where the client puts it.
		CaptionRect = FSlateRect(OffX, OffY, OffX + W, OffY + H);
		// Defer the title text: the caption is the FIRST child in most layouts,
		// so drawing it here puts it UNDER the frame images that follow.
		CaptionNode = Node;
		return;
	}

	if (Type == TEXT("IMAGE"))
	{
		// Record where this image landed BEFORE drawing it: dlgitem names its
		// bag icon anchors (ITEM_SLOT00…) and identifies each equipment slot by
		// its distinct sprite, and the owner needs those rects to overlay item
		// icons at the client's own coordinates rather than rebuilding a grid.
		const FString ImgName = Str(Node, TEXT("name"));
		const FString ImgGid  = Str(Node, TEXT("gid"));
		const FSlateRect Rect(RectBase.X + OffX, RectBase.Y + OffY,
		                      RectBase.X + OffX + W, RectBase.Y + OffY + H);
		if (!ImgName.IsEmpty())
			AnchorRects.Add(ImgName, Rect);
		if (!ImgGid.IsEmpty())
			SpriteRects.FindOrAdd(ImgGid).Add(Rect);

		// A named IMAGE with NO sprite but a FONT is a TEXT FIELD, not art: the
		// client draws a string into that rect (dlginfo's HP_PERCENT,
		// INFO_JOB_VALUE, ...).  735 images carry a name and no gid; the 484
		// with a font are text, the rest are icon anchors the owner fills in.
		if (ImgGid.IsEmpty() && !ImgName.IsEmpty() && Num(Node, TEXT("font")) > 0.f)
		{
			const FRoseUIKey Key = KeyOf(Node);
			FRoseUITextValue Tv = TextValue;
			const float TX = Num(Node, TEXT("textoffsetx"));
			const float TY = Num(Node, TEXT("textoffsety"));
			const FLinearColor Col(Num(Node, TEXT("r"), 255.f) / 255.f,
			                       Num(Node, TEXT("g"), 255.f) / 255.f,
			                       Num(Node, TEXT("b"), 255.f) / 255.f, 1.f);
			// Keep the authored RECT and inset by TEXTOFFSET, rather than sliding
			// the rect — sliding breaks right-aligned fields, whose alignment is
			// measured from the rect's right edge.
			Canvas->AddSlot()
				.Offset(FMargin(OffX, OffY, W, H))
				.Alignment(FVector2D(0, 0))
				[
					SNew(SBox).VAlign(VAlign_Center)
					.Padding(FMargin(TX > 0.f ? TX : 0.f, TY, TX < 0.f ? -TX : 0.f, 0.f))
					[
						SNew(STextBlock)
						.Font(LayoutFont(Node))
						.Justification(JustifyOf(Num(Node, TEXT("halign"))))
						.ColorAndOpacity(Col)
						.Text_Lambda([Tv, Key]() {
							return Tv.IsBound() ? Tv.Execute(Key, -1) : FText::GetEmpty(); })
					]
				];
			return;
		}

		if (const FSlateBrush* B = SpriteBrush(ImgGid, W, H))
		{
			// ALPHAVALUE (CTImage::SetAlphaValue): 0-255 per-image opacity — the
			// client draws e.g. the chat backdrop (CHAT_LIST_BG) at 64/255 ≈ 25%.
			// Ignoring it renders translucent panels as solid black.
			const float A = Num(Node, TEXT("alphavalue"), 255.f);
			Canvas->AddSlot()
				.Offset(FMargin(OffX, OffY, W, H))
				.Alignment(FVector2D(0, 0))
				[
					SNew(SImage).Image(B)
					.ColorAndOpacity(FLinearColor(1.f, 1.f, 1.f,
						A > 0.f ? FMath::Clamp(A / 255.f, 0.f, 1.f) : 1.f))
				];
		}
		return;
	}

	if (Type == TEXT("BUTTON"))
	{
		// The button's meaning is the XML element text ("CLOSE", "STR", ...);
		// classic buttons carry no NAME, and the close button's sprite also
		// contains CLOSE (e.g. UI07_BTN_CLOSE_NORMAL).
		const FString Label = Str(Node, TEXT("label"));
		// Buttons are addressed by KEY as well as by label: the glass layouts
		// name their buttons (dlgminimap's MINIMIZE / ZOOMIN / WORLDMAP) and
		// carry no element text at all, so a label-only callback reaches none
		// of them.
		const FRoseUIKey BtnKey = KeyOf(Node);
		FOnRoseUIActivate BtnAct = ActivateEvent;
		FString GidUp = Str(Node, TEXT("normalgid"));
		if (GidUp.IsEmpty()) GidUp = Str(Node, TEXT("gid_up"));
		// The GLASS skin's close button carries no name and no label — it is the
		// GEN_BTN01 "X" plate sitting in the window's top-right.  The classic
		// rule (CLOSE somewhere in the name/sprite) matches only 5 of the 64
		// layouts that have buttons, so it cannot stand alone here.
		//
		// The position test is not decoration: GEN_BTN01 is also used for
		// per-row "remove" buttons (ITEMMALLCART stacks four of them down the
		// middle), and treating those as a window close would shut the window.
		const bool bGlassClose = GidUp == TEXT("GEN_BTN01")
			&& OffY <= 40.f && (Size.X - OffX) <= 90.f;
		const bool bClose = bGlassClose
			|| Label.Contains(TEXT("CLOSE"))
			|| Str(Node, TEXT("name")).Contains(TEXT("CLOSE"))
			|| GidUp.Contains(TEXT("CLOSE"));
		TSharedPtr<SButton> Btn = MakeSpriteButton(Node, W, H, [this, bClose, Label, BtnKey, BtnAct]() {
			if (bClose)
				OnClose.ExecuteIfBound();
			else
			{
				BtnAct.ExecuteIfBound(BtnKey, -1);
				if (!Label.IsEmpty())
					OnButton.ExecuteIfBound(Label);
			}
		});
		if (Btn)
		{
			Btn->SetContent(SNew(SBox).VAlign(VAlign_Center)[ ButtonLabel(Node) ]);
			Canvas->AddSlot()
				.Offset(FMargin(OffX, OffY, W, H))
				.Alignment(FVector2D(0, 0))
				[ Btn.ToSharedRef() ];
		}
		return;
	}

	if (Type == TEXT("TABBEDPANE"))
	{
		// Real tab switching: every tab's TABBUTTON stays visible + clickable;
		// each tab's content lives in its own canvas whose visibility follows
		// the pane's active index.  Default tab = the EQUIPMENT/ABILITY one
		// (the client's initial state), else the first.
		const int32 PaneIdx = ActiveTabs.Add(0);
		TabTags.AddDefaulted();
		const TArray<TSharedPtr<FJsonValue>>* Tabs;
		if (!Node->TryGetArrayField(TEXT("children"), Tabs))
			return;

		// LOCAL, not a member: a shared list would let one pane flush another
		// pane's buttons into the wrong canvas.
		// Widget + rect, so a tab can be a plain button OR a button with its
		// selected-state overlay stacked on top.
		struct FPendingWidget { TSharedRef<SWidget> Widget; FSlateRect Rect; };
		TArray<FPendingWidget> PendingWidgets;

		// A pane with ONE tab has nothing to switch, so its tab button is dead
		// weight — and worse, it can sit exactly on top of a real one.  dlgitem
		// does exactly that: its single-tab pane 100 puts a button at (10,170),
		// the same spot as pane 50's "Equipment" (id 53).  Pane 100 is built
		// later, so its button was on top and ate every click on Equipment,
		// while Consume and Material (unobstructed) worked fine.
		int32 RealTabCount = 0;
		for (const TSharedPtr<FJsonValue>& T : *Tabs)
			if (T->AsObject().IsValid() && Str(T->AsObject(), TEXT("type")) == TEXT("TAB"))
				++RealTabCount;
		const bool bWantButtons = RealTabCount > 1;

		int32 TabIdx = 0;
		for (const TSharedPtr<FJsonValue>& T : *Tabs)
		{
			const TSharedPtr<FJsonObject> Tab = T->AsObject();
			if (Str(Tab, TEXT("type")) != TEXT("TAB"))
			{
				AddNode(Canvas, Tab, OffX, OffY);
				continue;
			}
			const TArray<TSharedPtr<FJsonValue>>* TK;
			if (!Tab->TryGetArrayField(TEXT("children"), TK))
				continue;

			TSharedRef<SConstraintCanvas> TabCanvas = SNew(SConstraintCanvas);
			// SelfHitTestInvisible, NOT Visible: this canvas spans everything
			// from the pane's origin to the window edge, and a plain Visible
			// canvas is itself hit-testable — it would swallow every click that
			// lands on its empty area, including the tab buttons underneath.
			TabCanvas->SetVisibility(TAttribute<EVisibility>::CreateLambda(
				[this, PaneIdx, TabIdx]() {
					return (ActiveTabs.IsValidIndex(PaneIdx) && ActiveTabs[PaneIdx] == TabIdx)
						? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed;
				}));
			Canvas->AddSlot()
				.Offset(FMargin(OffX, OffY, Size.X - OffX, Size.Y - OffY))
				.Alignment(FVector2D(0, 0))
				[ TabCanvas ];

			FString TabTag;   // identifies the tab for IsTabActive + the default
			for (const TSharedPtr<FJsonValue>& K : *TK)
			{
				const TSharedPtr<FJsonObject> Kid = K->AsObject();
				if (Str(Kid, TEXT("type")) == TEXT("TABBUTTON"))
				{
					const float BX = OffX + Num(Kid, TEXT("x")) + Num(Kid, TEXT("offsetx"));
					const float BY = OffY + Num(Kid, TEXT("y")) + Num(Kid, TEXT("offsety"));
					const float BW = Num(Kid, TEXT("width"));
					const float BH = Num(Kid, TEXT("height"));
					TSharedPtr<SButton> Btn = bWantButtons
						? MakeSpriteButton(Kid, BW, BH,
							[this, PaneIdx, TabIdx]() {
								if (ActiveTabs.IsValidIndex(PaneIdx))
									ActiveTabs[PaneIdx] = TabIdx;
							})
						: nullptr;
					if (Btn)
					{
						Btn->SetContent(SNew(SBox).VAlign(VAlign_Center)[ ButtonLabel(Kid) ]);
						// The ACTIVE tab draws its DOWN sprite on top — that is
						// the lit/red-glow state in the glass skin, and without
						// it every tab looks identical no matter which is open.
						// Same treatment RADIOBUTTON groups already get.
						if (const FSlateBrush* DownB = SpriteBrush(Str(Kid, TEXT("downgid")), BW, BH))
						{
							TSharedRef<SOverlay> TabStack = SNew(SOverlay);
							TabStack->AddSlot()[ Btn.ToSharedRef() ];
							TabStack->AddSlot()
							[
								SNew(SImage).Image(DownB)
								.Visibility(TAttribute<EVisibility>::CreateLambda(
									[this, PaneIdx, TabIdx]() {
										return (ActiveTabs.IsValidIndex(PaneIdx)
										        && ActiveTabs[PaneIdx] == TabIdx)
											? EVisibility::HitTestInvisible
											: EVisibility::Collapsed; }))
							];
							PendingWidgets.Add({ TabStack, FSlateRect(BX, BY, BX + BW, BY + BH) });
						}
						else
						{
							PendingWidgets.Add({ Btn.ToSharedRef(),
								FSlateRect(BX, BY, BX + BW, BY + BH) });
						}
					}
					// The tag must uniquely identify the tab.  Sprite alone is not
					// enough (dlgitem's Avatar/Costume/PAT share GEN_BTN64) and
					// neither is the name (its three BAG tabs have none at all),
					// so the button's ID goes in too, delimited so a substring
					// search for "#63#" cannot also match id 163.
					TabTag += (Str(Kid, TEXT("label")) + Str(Kid, TEXT("normalgid"))
					           + Str(Kid, TEXT("name"))).ToUpper();
					TabTag += FString::Printf(TEXT("#%d#"), (int32)Num(Kid, TEXT("id"), -1.f));
				}
				else
				{
					// The tab canvas sits at (OffX,OffY) and lays its children
					// out from 0 — so shift the RECT origin by the same amount,
					// or recorded rects come back tab-relative.
					const FVector2D Saved = RectBase;
					RectBase += FVector2D(OffX, OffY);
					AddNode(TabCanvas, Kid, 0.f, 0.f);
					RectBase = Saved;
				}
			}
			TabTags[PaneIdx].Add(TabTag);
			// XML tab order is NOT display order, so index 0 is not a safe
			// default — dlgitem lists Costume before Avatar.  The caller's
			// InitialTab wins; the classic equipment/ability guess is a fallback.
			if (!InitialTabTag.IsEmpty())
			{
				if (TabTag.Contains(InitialTabTag))
					ActiveTabs[PaneIdx] = TabIdx;
			}
			else if (TabTag.Contains(TEXT("EQUIPMENT")) || TabTag.Contains(TEXT("ABILITY")))
			{
				ActiveTabs[PaneIdx] = TabIdx;
			}
			++TabIdx;
		}

		// Tab buttons go on TOP of every tab's content.
		for (const FPendingWidget& P : PendingWidgets)
			Canvas->AddSlot()
				.Offset(FMargin(P.Rect.Left, P.Rect.Top,
				                P.Rect.GetSize().X, P.Rect.GetSize().Y))
				.Alignment(FVector2D(0, 0))
				[ P.Widget ];
		return;
	}

	if (Type == TEXT("STATIC"))
	{
		// No visual -- the client uses these purely as named anchor rects for
		// content it draws itself (dlgquest's SMALL_POS, etc.).
		const FString AnchorName = Str(Node, TEXT("name"));
		if (!AnchorName.IsEmpty())
			AnchorRects.Add(AnchorName,
				FSlateRect(RectBase.X + OffX, RectBase.Y + OffY,
				           RectBase.X + OffX + W, RectBase.Y + OffY + H));
		return;
	}

	if (Type == TEXT("GUAGE"))
	{
		// BGID = empty track, GID = fill.  The fill is CLIPPED by width rather
		// than scaled, so its lengthwise highlight stays anchored to the left
		// end and simply shortens -- scaling would slide the highlight along.
		const FSlateBrush* Track = SpriteBrush(Str(Node, TEXT("bgid")), W, H);
		const FSlateBrush* Fill  = SpriteBrush(Str(Node, TEXT("gid")),  W, H);
		const FRoseUIKey Key = KeyOf(Node);
		FRoseUIGaugeValue Gv = GaugeValue;
		FRoseUITextValue  Tv = TextValue;

		TSharedRef<SOverlay> Stack = SNew(SOverlay);
		if (Track)
			Stack->AddSlot()[ SNew(SImage).Image(Track) ];
		if (Fill)
			Stack->AddSlot().HAlign(HAlign_Left)
			[
				SNew(SBox).HeightOverride(H).Clipping(EWidgetClipping::ClipToBounds)
				.WidthOverride(TAttribute<FOptionalSize>::CreateLambda([Gv, Key, W]() {
					const float P = Gv.IsBound() ? Gv.Execute(Key) : 0.f;
					return FOptionalSize(FMath::Clamp(P, 0.f, 1.f) * W); }))
				[ SNew(SImage).Image(Fill) ]
			];

		// TEXTOFFSETX/Y nudge the value text off the gauge's own box.
		const float TX = Num(Node, TEXT("textoffsetx"));
		const float TY = Num(Node, TEXT("textoffsety"));
		// The value text fills the gauge and is aligned by HALIGN — it must NOT
		// be centred and then nudged, or a right-aligned readout (HALIGN=2,
		// TEXTOFFSETX=-51 on dlginfo's HP/MP) lands in the middle of the bar.
		// A negative TEXTOFFSETX is an inset from the RIGHT edge.
		const FMargin TextPad(TX > 0.f ? TX : 0.f, TY, TX < 0.f ? -TX : 0.f, 0.f);
		Stack->AddSlot().Padding(TextPad).VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(LayoutFont(Node))
				.Justification(JustifyOf(Num(Node, TEXT("halign"))))
				.ColorAndOpacity(FLinearColor::White)
				.Text_Lambda([Tv, Key]() {
					return Tv.IsBound() ? Tv.Execute(Key, -1) : FText::GetEmpty(); })
			];

		Canvas->AddSlot().Offset(FMargin(OffX, OffY, W, H)).Alignment(FVector2D(0, 0))
			[ Stack ];
		return;
	}

	if (Type == TEXT("PUSHBUTTON") || Type == TEXT("RADIOBUTTON"))
	{
		// Both are ordinary sprite-triplet buttons.  A RADIOBUTTON additionally
		// belongs to a RADIOBOXID group: the selected member draws its PRESSED
		// sprite, which is how the client shows the Skill window's left column
		// and the Clan window's tabs as "lit".
		const bool bRadio = Type == TEXT("RADIOBUTTON");
		const int32 Group = (int32)Num(Node, TEXT("radioboxid"), -1.f);
		int32 Index = 0;
		if (bRadio)
		{
			Index = RadioCounts.FindOrAdd(Group);
			RadioCounts[Group] = Index + 1;
			if (!RadioSelection.Contains(Group))
				RadioSelection.Add(Group, 0);      // first member starts selected
		}
		const FRoseUIKey Key = KeyOf(Node);
		const FString BtnLabel = Str(Node, TEXT("label"));
		FOnRoseUIActivate Act = ActivateEvent;

		TSharedPtr<SButton> Btn = MakeSpriteButton(Node, W, H,
			[this, bRadio, Group, Index, Key, BtnLabel, Act]() {
				if (bRadio)
					RadioSelection.FindOrAdd(Group) = Index;
				Act.ExecuteIfBound(Key, bRadio ? Index : -1);
				if (!BtnLabel.IsEmpty())
					OnButton.ExecuteIfBound(BtnLabel);
			});
		if (!Btn)
			return;

		Btn->SetContent(SNew(SBox).VAlign(VAlign_Center)[ ButtonLabel(Node) ]);
		TSharedRef<SOverlay> Stack = SNew(SOverlay);
		Stack->AddSlot()[ Btn.ToSharedRef() ];
		if (bRadio)
		{
			// Selected overlay = the DOWN sprite drawn over the button.
			if (const FSlateBrush* Down = SpriteBrush(Str(Node, TEXT("downgid")), W, H))
				Stack->AddSlot()
				[
					SNew(SImage).Image(Down)
					.Visibility(TAttribute<EVisibility>::CreateLambda([this, Group, Index]() {
						const int32* Sel = RadioSelection.Find(Group);
						return (Sel && *Sel == Index) ? EVisibility::HitTestInvisible
						                              : EVisibility::Collapsed; }))
				];
		}
		Canvas->AddSlot().Offset(FMargin(OffX, OffY, W, H)).Alignment(FVector2D(0, 0))
			[ Stack ];
		return;
	}

	if (Type == TEXT("CHECKBOX"))
	{
		const FSlateBrush* OnB  = SpriteBrush(Str(Node, TEXT("checkgid")),   W, H);
		const FSlateBrush* OffB = SpriteBrush(Str(Node, TEXT("uncheckgid")), W, H);
		const FRoseUIKey Key = KeyOf(Node);
		FRoseUIStateValue Sv = StateValue;
		FOnRoseUIActivate Act = ActivateEvent;

		Canvas->AddSlot().Offset(FMargin(OffX, OffY, W, H)).Alignment(FVector2D(0, 0))
		[
			SNew(SButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(FMargin(0))
			.OnClicked_Lambda([Act, Key]() { Act.ExecuteIfBound(Key, -1); return FReply::Handled(); })
			[
				SNew(SImage).Image_Lambda([Sv, Key, OnB, OffB]() -> const FSlateBrush* {
					const bool bOn = Sv.IsBound() && Sv.Execute(Key) != 0;
					return bOn ? OnB : OffB; })
			]
		];
		return;
	}

	if (Type == TEXT("TABLE"))
	{
		// A cell grid (COLUMNCOUNT x EXTENT of CELLWIDTH/CELLHEIGHT with
		// ROWMARGIN/COLMARGIN between).  Cells are inset wells; what goes IN
		// them is game data, so each fires ActivateEvent with its flat index.
		const int32 Cols = FMath::Max(1, (int32)Num(Node, TEXT("columncount"), 1.f));
		const int32 Rows = FMath::Max(1, (int32)Num(Node, TEXT("extent"), 1.f));
		const float CW = Num(Node, TEXT("cellwidth"), 20.f);
		const float CH = Num(Node, TEXT("cellheight"), 20.f);
		const float CM = Num(Node, TEXT("colmargin"));
		const float RM = Num(Node, TEXT("rowmargin"));
		const FRoseUIKey Key = KeyOf(Node);
		FOnRoseUIActivate Act = ActivateEvent;
		TSharedPtr<FSlateBrush> Well = RoseUI::GlassPanel(RoseUI::EPanelKind::Inset);
		if (Well.IsValid())
			Brushes.Add(Well);

		for (int32 R = 0; R < Rows; ++R)
			for (int32 C = 0; C < Cols; ++C)
			{
				const int32 Flat = R * Cols + C;
				Canvas->AddSlot()
					.Offset(FMargin(OffX + C * (CW + CM), OffY + R * (CH + RM), CW, CH))
					.Alignment(FVector2D(0, 0))
					[
						SNew(SButton)
						.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
						.ContentPadding(FMargin(0))
						.OnClicked_Lambda([Act, Key, Flat]() {
							Act.ExecuteIfBound(Key, Flat); return FReply::Handled(); })
						[ SNew(SImage).Image(Well.Get()) ]
					];
			}
		return;
	}

	if (Type == TEXT("LISTBOX") || Type == TEXT("ZLISTBOX") || Type == TEXT("JLISTBOX"))
	{
		// EXTENT is the visible row count; rows come from the text provider.
		// No scrollbar plumbing yet -- SCROLLBAR nodes reference these by
		// LISTBOXID and render as their sprite only.
		const int32 Extent = FMath::Max(1, (int32)Num(Node, TEXT("extent"), 1.f));
		const float Space = Num(Node, TEXT("linespace"));
		const float Line = Num(Node, TEXT("charheight"), 12.f) + Space;
		const FRoseUIKey Key = KeyOf(Node);
		FRoseUITextValue Tv = TextValue;
		FRoseUICountValue Cv = RowCount;
		FOnRoseUIActivate Act = ActivateEvent;
		// ZLISTBOX is the SELECTABLE variant and carries no SELECTABLE attribute
		// (dlgquest's quest list is one); plain LISTBOX honours the flag, which
		// is how the same dialog's description list stays inert.
		const bool bSelectable = Type != TEXT("LISTBOX")
			|| Num(Node, TEXT("selectable")) != 0.f;
		const FSlateFontInfo RowFont = LayoutFont(Node);

		TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		for (int32 R = 0; R < Extent; ++R)
		{
			TSharedRef<STextBlock> Text = SNew(STextBlock)
				.Font(RowFont)
				.ColorAndOpacity(FLinearColor::White)
				.Text_Lambda([Tv, Key, R]() {
					return Tv.IsBound() ? Tv.Execute(Key, R) : FText::GetEmpty(); })
				.Visibility(TAttribute<EVisibility>::CreateLambda([Cv, Key, R]() {
					const int32 N = Cv.IsBound() ? Cv.Execute(Key) : 0;
					return R < N ? EVisibility::Visible : EVisibility::Hidden; }));

			TSharedRef<SWidget> Row = bSelectable
				? StaticCastSharedRef<SWidget>(SNew(SButton)
					.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
					.ContentPadding(FMargin(0))
					.HAlign(HAlign_Left)
					.OnClicked_Lambda([Act, Key, R]() {
						Act.ExecuteIfBound(Key, R); return FReply::Handled(); })
					[ Text ])
				: StaticCastSharedRef<SWidget>(Text);

			Rows->AddSlot().AutoHeight().Padding(0, 0, 0, Space)
				[ SNew(SBox).HeightOverride(Line)[ Row ] ];
		}
		Canvas->AddSlot().Offset(FMargin(OffX, OffY, W, H)).Alignment(FVector2D(0, 0))
			[ SNew(SBox).Clipping(EWidgetClipping::ClipToBounds)[ Rows ] ];
		return;
	}

	if (Type == TEXT("EDITBOX"))
	{
		const FRoseUIKey Key = KeyOf(Node);
		FRoseUITextValue Tv = TextValue;
		const FLinearColor Col(Num(Node, TEXT("r"), 255.f) / 255.f,
		                       Num(Node, TEXT("g"), 255.f) / 255.f,
		                       Num(Node, TEXT("b"), 255.f) / 255.f, 1.f);
		Canvas->AddSlot().Offset(FMargin(OffX, OffY, W, H)).Alignment(FVector2D(0, 0))
		[
			SNew(SEditableText)
			.Font(LayoutFont(Node))
			.ColorAndOpacity(Col)
			.IsPassword(Num(Node, TEXT("password")) != 0.f)
			.Text_Lambda([Tv, Key]() {
				return Tv.IsBound() ? Tv.Execute(Key, -1) : FText::GetEmpty(); })
		];
		return;
	}

	if (Type == TEXT("COMBOBOX") || Type == TEXT("JCOMBOBOX"))
	{
		// The list body is drawn by the sibling TOP/MIDDLE/BOTTOMIMAGE nodes;
		// the box itself is the current value plus a click target.
		const FRoseUIKey Key = KeyOf(Node);
		FRoseUITextValue Tv = TextValue;
		FOnRoseUIActivate Act = ActivateEvent;
		Canvas->AddSlot().Offset(FMargin(OffX, OffY, W, H)).Alignment(FVector2D(0, 0))
		[
			SNew(SButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(FMargin(0))
			.OnClicked_Lambda([Act, Key]() { Act.ExecuteIfBound(Key, -1); return FReply::Handled(); })
			[
				SNew(STextBlock).Font(LayoutFont(Node)).ColorAndOpacity(FLinearColor::White)
				.Text_Lambda([Tv, Key]() {
					return Tv.IsBound() ? Tv.Execute(Key, -1) : FText::GetEmpty(); })
			]
		];
		return;
	}

	if (Type == TEXT("SKILL"))
	{
		// A skill-tree node: INDEX identifies the skill and IMAGE names its icon
		// DDS.  Our icon assets are keyed by skill icon NUMBER, not by file
		// name, so resolving the art is left to the owner via the activate
		// binding; the node renders at the client's authored position and size.
		const int32 SkillIndex = (int32)Num(Node, TEXT("index"), -1.f);
		FRoseUIKey Key;
		Key.Name = Str(Node, TEXT("image"));
		Key.Id = SkillIndex;
		FOnRoseUIActivate Act = ActivateEvent;
		TSharedPtr<FSlateBrush> Well = RoseUI::GlassPanel(RoseUI::EPanelKind::Inset);
		if (Well.IsValid())
			Brushes.Add(Well);
		const float SW = W > 0.f ? W : 40.f;
		const float SH = H > 0.f ? H : 40.f;
		Canvas->AddSlot().Offset(FMargin(OffX, OffY, SW, SH)).Alignment(FVector2D(0, 0))
		[
			SNew(SButton)
			.ButtonStyle(&FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(FMargin(0))
			.ToolTipText(FText::FromString(Key.Name))
			.OnClicked_Lambda([Act, Key]() { Act.ExecuteIfBound(Key, -1); return FReply::Handled(); })
			[ SNew(SImage).Image(Well.Get()) ]
		];
		return;
	}

	if (Type == TEXT("SCROLLBOX") || Type == TEXT("SCROLLBAR")
		|| Type == TEXT("TOPIMAGE") || Type == TEXT("MIDDLEIMAGE") || Type == TEXT("BOTTOMIMAGE"))
	{
		// Decorative for now: draw the sprite where the client draws it.  These
		// are the scroll thumb and the combo-list body slices.
		if (const FSlateBrush* B = SpriteBrush(Str(Node, TEXT("gid")), W, H))
			Canvas->AddSlot().Offset(FMargin(OffX, OffY, W, H)).Alignment(FVector2D(0, 0))
				[ SNew(SImage).Image(B) ];
		return;
	}

	// A NAMED pane gets its own canvas, so the owner can show or hide it as a
	// unit (dlgquickbar's HORIZONTAL vs HORIZONTAL_HOVER).  Unnamed panes are
	// plain grouping and just recurse.
	if (Type == TEXT("PANE") && !Str(Node, TEXT("name")).IsEmpty())
	{
		const FString PaneName = Str(Node, TEXT("name"));
		TSharedRef<SConstraintCanvas> PaneCanvas = SNew(SConstraintCanvas);
		// SelfHitTestInvisible for the same reason the tab canvases are: a
		// pane spans a large rect and would otherwise swallow clicks meant for
		// whatever is underneath.
		PaneCanvas->SetVisibility(EVisibility::SelfHitTestInvisible);
		NamedPanes.Add(PaneName, PaneCanvas);

		Canvas->AddSlot()
			.Offset(FMargin(OffX, OffY, FMath::Max(Size.X - OffX, W),
			                FMath::Max(Size.Y - OffY, H)))
			.Alignment(FVector2D(0, 0))
			[ PaneCanvas ];

		const TArray<TSharedPtr<FJsonValue>>* PaneKids;
		if (Node->TryGetArrayField(TEXT("children"), PaneKids))
		{
			const FVector2D Saved = RectBase;
			RectBase += FVector2D(OffX, OffY);   // same reason as TAB, above
			for (const TSharedPtr<FJsonValue>& K : *PaneKids)
				AddNode(PaneCanvas, K->AsObject(), 0.f, 0.f);
			RectBase = Saved;
		}
		return;
	}

	// PANE / anything else: containers — recurse; children inherit the offset.
	const TArray<TSharedPtr<FJsonValue>>* Kids;
	if (Node->TryGetArrayField(TEXT("children"), Kids))
		for (const TSharedPtr<FJsonValue>& K : *Kids)
			AddNode(Canvas, K->AsObject(), OffX, OffY);
}

void SRoseUIWindow::Construct(const FArguments& InArgs)
{
	OnClose = InArgs._OnClose;
	OnButton = InArgs._OnButton;
	InitialTabTag = InArgs._InitialTab.ToUpper();
	GaugeValue = InArgs._OnGaugeValue;
	TextValue  = InArgs._OnTextValue;
	RowCount   = InArgs._OnRowCount;
	StateValue = InArgs._OnState;
	ActivateEvent = InArgs._OnActivate;

	TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
	TSharedPtr<FJsonObject> Doc = LoadLayout(InArgs._Dialog);
	if (Doc.IsValid())
	{
		Size = FVector2D(Num(Doc, TEXT("width"), 300), Num(Doc, TEXT("height"), 200));
		// Grow to the frame's real extent before laying anything out — Size is
		// read during AddNode (tab canvases, the close-button test).
		MeasureFrame(Doc, 0.f, 0.f, Size);

		// Classic layouts: panes are complementary regions of the window
		// (e.g. dlgitem's equip pane at Y=33 + bag pane at Y=255) — render all.
		const TArray<TSharedPtr<FJsonValue>>* Kids;
		if (Doc->TryGetArrayField(TEXT("children"), Kids))
			for (const TSharedPtr<FJsonValue>& K : *Kids)
				AddNode(Canvas, K->AsObject(), 0.f, 0.f);
	}

	// Caption text LAST so it sits above the frame art (see AddNode).
	if (CaptionNode.IsValid())
	{
		FRoseUIKey Key;
		Key.Name = TEXT("CAPTION");
		Key.Id = (int32)Num(CaptionNode, TEXT("id"), 0.f);
		FRoseUITextValue Tv = TextValue;
		// The title sits at TEXTOFFSETX from the WINDOW edge — the caption's own
		// OFFSETX belongs to the drag bar, not the text (dlgitem's 27 pushed
		// "Inventory" into the tab row).  Vertically it is centred in the title
		// bar rather than pinned to its top, which is where ROSE draws it.
		const float CX = Num(CaptionNode, TEXT("textoffsetx"));
		const float CY = Num(CaptionNode, TEXT("y")) + Num(CaptionNode, TEXT("offsety"));
		// Centre within a TITLE BAR, not within the caption rect: dlginfo's
		// caption is the whole 210x100 window (the entire panel is the drag
		// region), so centring put the character's name halfway down, straight
		// across the MP bar.  Cap it to a title bar's height.
		const float CH = FMath::Min(Num(CaptionNode, TEXT("height"), 23.f), 24.f);
		Canvas->AddSlot()
			.Offset(FMargin(CX, CY, Num(CaptionNode, TEXT("width"), Size.X), CH))
			.Alignment(FVector2D(0, 0))
			[
				SNew(SBox).VAlign(VAlign_Center)
				.Padding(FMargin(0.f, Num(CaptionNode, TEXT("textoffsety")), 0.f, 0.f))
				[
					SNew(STextBlock)
					.Font(LayoutFont(CaptionNode))
					.ColorAndOpacity(FLinearColor::White)
					.Text_Lambda([Tv, Key]() {
						return Tv.IsBound() ? Tv.Execute(Key, -1) : FText::GetEmpty(); })
				]
			];
	}

	// Content covers the whole window — callers position children absolutely
	// (e.g. equip slots at the client's authored coordinates).
	if (InArgs._Content.IsValid())
		Canvas->AddSlot()
			.Offset(FMargin(0.f, 0.f, Size.X, Size.Y))
			.Alignment(FVector2D(0, 0))
			[ InArgs._Content.ToSharedRef() ];

	// Default drag region if the layout had no CAPTION: the top strip.
	if (CaptionRect.GetArea() <= 0.f)
		CaptionRect = FSlateRect(0, 0, Size.X, 24.f);

	ChildSlot
	[
		// Clip content to the window frame — the client's controls never draw
		// outside their dialog (e.g. skill-tree nodes past the panel bounds).
		SNew(SBox).WidthOverride(Size.X).HeightOverride(Size.Y)
		.Clipping(EWidgetClipping::ClipToBounds)
		[ Canvas ]
	];
}

// Faithful to IT_MGR::InitInterfacePos: LEFT/TOP=0, CENTER=(screen−win)/2,
// RIGHT/BOTTOM=screen−win, then + ADJUST (+ any user drag).
FVector2D SRoseUIWindow::ComputeTopLeft(const FVector2D& Viewport) const
{
	const float X = AnchorX == 2 ? Viewport.X - Size.X
		: AnchorX == 1 ? (Viewport.X - Size.X) * 0.5f : 0.f;
	const float Y = AnchorY == 2 ? Viewport.Y - Size.Y
		: AnchorY == 1 ? (Viewport.Y - Size.Y) * 0.5f : 0.f;
	return FVector2D(X, Y) + Adjust + DragDelta;
}

FReply SRoseUIWindow::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev)
{
	const FVector2D Local = Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition());
	if (Ev.GetEffectingButton() == EKeys::LeftMouseButton && CaptionRect.ContainsPoint(Local))
	{
		bDragging = true;
		DragStart = Ev.GetScreenSpacePosition();
		DragOrigin = DragDelta;   // drag adds to the faithful anchor placement
		return FReply::Handled().CaptureMouse(SharedThis(this));
	}
	return FReply::Unhandled();
}

FReply SRoseUIWindow::OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Ev)
{
	if (bDragging && Ev.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SRoseUIWindow::OnMouseMove(const FGeometry& Geo, const FPointerEvent& Ev)
{
	if (bDragging)
	{
		DragDelta = DragOrigin + (Ev.GetScreenSpacePosition() - DragStart) / Geo.Scale;
		// Move ourselves.  This used to only update DragDelta, which nothing
		// read — and because the caption click was Handled here, the host's
		// drag wrapper never saw it either, so windows could not be moved at
		// all.  A render transform keeps it self-contained: no host support
		// needed, and every layout window becomes draggable by its title bar.
		SetRenderTransform(FSlateRenderTransform(DragDelta));
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
