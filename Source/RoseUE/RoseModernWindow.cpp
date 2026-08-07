#include "RoseModernWindow.h"
#include "RoseUIHelpers.h"
#include "RoseUITheme.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor kPanel   = RoseTheme::Panel;
	const FLinearColor kTitle   = RoseTheme::Title;
	const FLinearColor kAccent  = RoseTheme::Accent;
	const FLinearColor kText    = RoseTheme::Text;
	const FLinearColor kTextDim = RoseTheme::TextDim;
}

void SRoseModernWindow::Construct(const FArguments& InArgs)
{
	OnCloseEvent = InArgs._OnClose;

	const FButtonStyle* CloseStyle = RoseUI::GlassButton(RoseUI::EButtonKind::Close);

	// ── Chrome ───────────────────────────────────────────────────────────────
	//
	// Glass skin (rose.GlassUI 1, the default): re-skin the chrome ONLY, using
	// the same nine-slices ROSE itself composites — GEN_WND01 is the outer
	// window (its top band reads as the title strip) and GEN_WND04 is the body
	// panel that starts just below it.  Every window's CONTENTS are untouched,
	// which is the whole point: one frame swap re-themes the entire UI.
	//
	// `rose.GlassUI 0` falls back to the flat RoseUITheme panels, and so does a
	// missing sprite — the UI must never come up as an empty box.
	PanelBrush = RoseUI::GlassPanel(RoseUI::EPanelKind::Window);
	if (PanelBrush.IsValid())
	{
		BodyBrush = RoseUI::GlassPanel(RoseUI::EPanelKind::Body);
		// The outer frame already draws the title band; a second plate over it
		// would just muddy the glass.
		TitleBrush.Reset();
	}

	if (!PanelBrush.IsValid())
	{
		PanelBrush = MakeShared<FSlateRoundedBoxBrush>(kPanel, 14.f,
			FLinearColor(kAccent.R, kAccent.G, kAccent.B, 0.55f), 1.5f);
		// Only the top corners want rounding on the title, but a uniform radius reads
		// fine layered over the panel; keep it simple.
		TitleBrush = MakeShared<FSlateRoundedBoxBrush>(kTitle, 12.f);
	}

	const float W = InArgs._Width;
	const float H = InArgs._Height;

	TSharedRef<SBox> Frame = SNew(SBox).WidthOverride(W);
	if (H > 0.f)
		Frame->SetHeightOverride(H);

	Frame->SetContent(
		SNew(SOverlay)
		+ SOverlay::Slot()[ SNew(SImage).Image(PanelBrush.Get()) ]
		+ SOverlay::Slot().Padding(6.f)
		[
			SNew(SVerticalBox)
			// ── Title bar (drag handle) ──────────────────────────────────────
			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).HeightOverride(TitleBarHeight)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ SNew(SImage).Image(TitleBrush.Get())
					                   .Visibility(TitleBrush.IsValid()
					                       ? EVisibility::HitTestInvisible : EVisibility::Collapsed) ]
					+ SOverlay::Slot().Padding(10, 0).VAlign(VAlign_Center)
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
						[ SNew(STextBlock)
						  .Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
						  .ColorAndOpacity(kText).Text(InArgs._Title) ]
						+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
						[
							SNew(SButton)
							.ButtonStyle(CloseStyle ? CloseStyle
							             : &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
							.ContentPadding(FMargin(0))
							.ToolTipText(FText::FromString(TEXT("Close")))
							.OnClicked_Lambda([this]() { OnCloseEvent.ExecuteIfBound(); return FReply::Handled(); })
							[
								// The glass close plate already draws its own X; the text
								// glyph is only for the flat skin.
								SNew(SBox).WidthOverride(CloseStyle ? 29.f : 22.f)
								          .HeightOverride(CloseStyle ? 25.f : 22.f)
								[ SNew(STextBlock)
								  .Visibility(CloseStyle ? EVisibility::Collapsed : EVisibility::Visible)
								  .Font(FCoreStyle::GetDefaultFontStyle("Bold", 13))
								  .ColorAndOpacity(kTextDim).Justification(ETextJustify::Center)
								  .Text(FText::FromString(FString::Chr(0x2715))) ]
							]
						]
					]
				]
			]
			// ── Content ──────────────────────────────────────────────────────
			+ SVerticalBox::Slot().FillHeight(1.f).Padding(0, 6, 0, 0)
			[
				SNew(SOverlay)
				+ SOverlay::Slot()[ SNew(SImage).Image(BodyBrush.Get())
				                    .Visibility(BodyBrush.IsValid()
				                        ? EVisibility::HitTestInvisible : EVisibility::Collapsed) ]
				+ SOverlay::Slot().Padding(BodyBrush.IsValid() ? FMargin(6.f) : FMargin(0.f))
				[ InArgs._Content.Widget ]
			]
		]);

	ChildSlot[ Frame ];
}

FReply SRoseModernWindow::OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev)
{
	if (Ev.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		const FVector2D Local = Geo.AbsoluteToLocal(Ev.GetScreenSpacePosition());
		// Drag only from the title bar band (leaves buttons/content clickable).
		if (Local.Y >= 0.f && Local.Y <= TitleBarHeight + 6.f)
		{
			bDragging = true;
			DragStart = Ev.GetScreenSpacePosition();
			DragOrigin = Position;
			return FReply::Handled().CaptureMouse(AsShared());
		}
	}
	return FReply::Unhandled();
}

FReply SRoseModernWindow::OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Ev)
{
	if (bDragging && Ev.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		bDragging = false;
		return FReply::Handled().ReleaseMouseCapture();
	}
	return FReply::Unhandled();
}

FReply SRoseModernWindow::OnMouseMove(const FGeometry& Geo, const FPointerEvent& Ev)
{
	if (bDragging)
	{
		Position = DragOrigin + (Ev.GetScreenSpacePosition() - DragStart) / Geo.Scale;
		return FReply::Handled();
	}
	return FReply::Unhandled();
}
