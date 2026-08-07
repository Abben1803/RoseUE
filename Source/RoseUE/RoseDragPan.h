// SRoseDragPan — makes ANY overlay widget draggable by grabbing empty space.
//
// Wraps content and accumulates a RenderTransform translation from left-mouse
// drags. Slate routes events deepest-first, so interactive children (buttons,
// text boxes, drag slots) still consume their clicks — only clicks nothing
// else handled start a window drag. Used for the always-on overlays (chat,
// minimap, NPC dialog, store); SRoseModernWindow already drags via title bar.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class SRoseDragPan : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseDragPan) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		ChildSlot[ InArgs._Content.Widget ];
	}

	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Ev) override
	{
		if (Ev.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			DragStart = Ev.GetScreenSpacePosition() - Offset;
			bDragging = true;
			return FReply::Handled().CaptureMouse(SharedThis(this));
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseMove(const FGeometry&, const FPointerEvent& Ev) override
	{
		if (bDragging && HasMouseCapture())
		{
			Offset = Ev.GetScreenSpacePosition() - DragStart;
			SetRenderTransform(FSlateRenderTransform(Offset));
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Ev) override
	{
		if (bDragging && Ev.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			bDragging = false;
			return FReply::Handled().ReleaseMouseCapture();
		}
		return FReply::Unhandled();
	}

private:
	FVector2D Offset = FVector2D::ZeroVector;
	FVector2D DragStart = FVector2D::ZeroVector;
	bool bDragging = false;
};
