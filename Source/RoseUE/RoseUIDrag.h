// Shared skill drag-and-drop for the ROSE UI: drag a skill from the skill panel
// (SRoseSkillPanel) onto a hotbar slot (SRoseHUD skill wheel) to assign it.
#pragma once

#include "CoreMinimal.h"
#include "Input/DragAndDrop.h"
#include "Input/Reply.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Framework/Application/SlateApplication.h"

struct FSlateBrush;

// Payload: a skill being dragged to the hotbar (from the skill panel/tree), or
// between hotbar slots (FromHotbarSlot >= 0 → the drop swaps the two slots).
class FRoseSkillDragOp : public FDragDropOperation
{
public:
	DRAG_DROP_OPERATOR_TYPE(FRoseSkillDragOp, FDragDropOperation)

	int32 SkillId = 0;
	int32 FromHotbarSlot = -1;   // -1 = dragged from a skill window
	const FSlateBrush* Icon = nullptr;

	virtual TSharedPtr<SWidget> GetDefaultDecorator() const override
	{
		// See FRoseItemDragOp: a null Icon renders an INVISIBLE decorator, which
		// reads as the drag not working at all.
		return SNew(SBox).WidthOverride(44.f).HeightOverride(44.f)
			[
				Icon ? StaticCastSharedRef<SWidget>(SNew(SImage).Image(Icon))
				     : StaticCastSharedRef<SWidget>(
						SNew(SBorder)
						.BorderBackgroundColor(FLinearColor(1.f, 0.85f, 0.3f, 0.85f))
						.Padding(FMargin(2.f)))
			];
	}

	// Windowless = render the icon decorator IN the viewport (following the
	// cursor) instead of a separate cursor-decorator OS window, which doesn't
	// display over a game viewport.  This is what makes the icon move with the
	// mouse in-game.
	virtual void Construct() override
	{
		bCreateNewWindow = false;
		FDragDropOperation::Construct();
	}
};

// A drag SOURCE that still supports click / shift+click (skill-panel row icon).
class SRoseSkillDragBox : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_RetVal(TSharedPtr<FRoseSkillDragOp>, FBeginSkillDrag);

	SLATE_BEGIN_ARGS(SRoseSkillDragBox) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FBeginSkillDrag, OnBeginDrag)
		SLATE_EVENT(FSimpleDelegate, OnClick)
		SLATE_EVENT(FSimpleDelegate, OnShiftClick)
		SLATE_EVENT(FSimpleDelegate, OnRightClick)
		SLATE_ATTRIBUTE(FText, ToolTipText)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnBeginDrag = InArgs._OnBeginDrag;
		OnClick = InArgs._OnClick;
		OnShiftClick = InArgs._OnShiftClick;
		OnRightClick = InArgs._OnRightClick;
		SetToolTipText(InArgs._ToolTipText);
		ChildSlot[ InArgs._Content.Widget ];
	}

	virtual FReply OnMouseButtonDown(const FGeometry&, const FPointerEvent& Ev) override
	{
		if (Ev.GetEffectingButton() == EKeys::LeftMouseButton)
			return FReply::Handled().DetectDrag(SharedThis(this), EKeys::LeftMouseButton);
		// Claim the RMB press only when a handler exists (hotbar slot clear) —
		// otherwise let it pass so the camera-orbit RMB keeps working.
		if (Ev.GetEffectingButton() == EKeys::RightMouseButton && OnRightClick.IsBound())
			return FReply::Handled();
		return FReply::Unhandled();
	}
	virtual FReply OnMouseButtonUp(const FGeometry&, const FPointerEvent& Ev) override
	{
		if (Ev.GetEffectingButton() == EKeys::LeftMouseButton)
		{
			if (FSlateApplication::Get().GetModifierKeys().IsShiftDown())
				OnShiftClick.ExecuteIfBound();
			else
				OnClick.ExecuteIfBound();
			return FReply::Handled();
		}
		if (Ev.GetEffectingButton() == EKeys::RightMouseButton && OnRightClick.IsBound())
		{
			OnRightClick.ExecuteIfBound();
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}
	virtual FReply OnDragDetected(const FGeometry&, const FPointerEvent&) override
	{
		if (OnBeginDrag.IsBound())
			if (TSharedPtr<FRoseSkillDragOp> Op = OnBeginDrag.Execute())
				return FReply::Handled().BeginDragDrop(Op.ToSharedRef());
		return FReply::Unhandled();
	}

private:
	FBeginSkillDrag OnBeginDrag;
	FSimpleDelegate OnClick, OnShiftClick, OnRightClick;
};

// A drop TARGET that wraps content (a hotbar slot) and reports dropped skills.
// FromHotbarSlot is -1 for drops from a skill window, else the source slot
// (a slot-to-slot rearrange — the receiver swaps the two).
class SRoseSkillDropZone : public SCompoundWidget
{
public:
	DECLARE_DELEGATE_TwoParams(FOnDropSkill, int32 /*SkillId*/, int32 /*FromHotbarSlot*/);

	SLATE_BEGIN_ARGS(SRoseSkillDropZone) {}
		SLATE_DEFAULT_SLOT(FArguments, Content)
		SLATE_EVENT(FOnDropSkill, OnDropSkill)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		OnDropSkill = InArgs._OnDropSkill;
		ChildSlot[ InArgs._Content.Widget ];
	}

	virtual FReply OnDragOver(const FGeometry&, const FDragDropEvent& Ev) override
	{
		return Ev.GetOperationAs<FRoseSkillDragOp>().IsValid() ? FReply::Handled() : FReply::Unhandled();
	}
	virtual FReply OnDrop(const FGeometry&, const FDragDropEvent& Ev) override
	{
		TSharedPtr<FRoseSkillDragOp> Op = Ev.GetOperationAs<FRoseSkillDragOp>();
		if (Op.IsValid())
		{
			OnDropSkill.ExecuteIfBound(Op->SkillId, Op->FromHotbarSlot);
			return FReply::Handled();
		}
		return FReply::Unhandled();
	}

private:
	FOnDropSkill OnDropSkill;
};
