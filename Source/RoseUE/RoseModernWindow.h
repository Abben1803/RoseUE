// SRoseModernWindow — reusable modern draggable window chrome for the overhauled
// ROSE UI (character sheet, skill panel, skill tree, …).  A rounded dark-glass
// panel with a title bar (drag handle) + close button and a single content slot.
// Matches the modern HUD palette (RoseHUD.cpp).
//
// Positioning: the window keeps a public `Position` (screen-space top-left in
// Slate units); the owner adds it to the viewport under an SConstraintCanvas
// whose slot Offset is bound to `Position`, so dragging the title bar (which
// mutates Position) moves the window — the same DPI-correct pattern as
// SRoseUIWindow.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FSlateBrush;

class SRoseModernWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseModernWindow)
		: _Width(360.f), _Height(0.f) {}
		SLATE_ARGUMENT(FText, Title)
		SLATE_ARGUMENT(float, Width)
		SLATE_ARGUMENT(float, Height)          // 0 = size to content
		SLATE_EVENT(FSimpleDelegate, OnClose)
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

	// Screen-space top-left in Slate units; the owner's canvas slot binds its
	// Offset to this, and title-bar dragging mutates it.
	FVector2D Position = FVector2D(200.f, 200.f);

	virtual FReply OnMouseButtonDown(const FGeometry& Geo, const FPointerEvent& Ev) override;
	virtual FReply OnMouseButtonUp(const FGeometry& Geo, const FPointerEvent& Ev) override;
	virtual FReply OnMouseMove(const FGeometry& Geo, const FPointerEvent& Ev) override;

private:
	FSimpleDelegate OnCloseEvent;
	float TitleBarHeight = 32.f;

	bool bDragging = false;
	FVector2D DragStart = FVector2D::ZeroVector;   // cursor (screen) at drag start
	FVector2D DragOrigin = FVector2D::ZeroVector;  // Position at drag start

	// PanelBrush = outer frame, BodyBrush = content panel (glass only),
	// TitleBrush = flat title plate (flat theme only; null under glass).
	TSharedPtr<FSlateBrush> PanelBrush, TitleBrush, BodyBrush;

};
