// Modern NPC conversation window (SRoseDialogWindow): a bottom-centred panel
// showing the NPC's say text + the player's clickable options, driven by a
// FRoseDialogSession (RoseDialog.cpp — the CEvent walker over the CON data).
// Options rebuild whenever the session's Revision changes; the window closes
// itself when the session ends.
#include "RoseDialog.h"
#include "RoseUIHelpers.h"
#include "RoseNpc.h"
#include "RoseUIManager.h"
#include "RoseUITheme.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const FLinearColor kPanel = RoseTheme::Panel;
	const FLinearColor kTitle = RoseTheme::Title;
	const FLinearColor kText = RoseTheme::Text;
	const FLinearColor kDim = RoseTheme::TextDim;
	const FLinearColor kAccent = RoseTheme::Accent;
	const FLinearColor kRow = RoseTheme::Row;
}

class SRoseDialogWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseDialogWindow) {}
		SLATE_ARGUMENT(TSharedPtr<FRoseDialogSession>, Session)
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Session = InArgs._Session;
		OnClosed = InArgs._OnClosed;

		static FSlateRoundedBoxBrush FlatPanel(kPanel, 12.f);
		static FSlateRoundedBoxBrush TitleBrush(kTitle, 8.f);
		// Glass skin when available, flat theme otherwise.
		GlassBrush = RoseUI::GlassPanel(RoseUI::EPanelKind::Window);
		const FButtonStyle* CloseStyle = RoseUI::GlassButton(RoseUI::EButtonKind::Close);
		const FSlateBrush* PanelBrushPtr = GlassBrush.IsValid()
			? GlassBrush.Get() : (const FSlateBrush*)&FlatPanel;

		ChildSlot
		[
			SNew(SBox).WidthOverride(620.f)
			[
				SNew(SBorder).BorderImage(PanelBrushPtr).Padding(10.f)
				[
					SNew(SVerticalBox)
					// title: NPC name + close
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
					[
						SNew(SBorder).BorderImage(&TitleBrush).Padding(FMargin(12, 6))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text_Lambda([this]() {
									return FText::FromString(Session.IsValid()
										? Session->NpcName : FString()); })
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 15))
								.ColorAndOpacity(kAccent)
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SButton)
								.ButtonStyle(CloseStyle ? CloseStyle
								             : &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
								.OnClicked_Lambda([this]() { CloseSelf(); return FReply::Handled(); })
								[
									SNew(SBox).WidthOverride(CloseStyle ? 29.f : 16.f)
									          .HeightOverride(CloseStyle ? 25.f : 16.f)
									// The glass plate draws its own X.
									[ SNew(STextBlock).Text(FText::FromString(TEXT("✕")))
									  .Visibility(CloseStyle ? EVisibility::Collapsed : EVisibility::Visible)
									  .Justification(ETextJustify::Center)
									  .ColorAndOpacity(kDim) ]
								]
							]
						]
					]
					// NPC say text
					+ SVerticalBox::Slot().AutoHeight().Padding(6, 0, 6, 10)
					[
						SNew(STextBlock)
						.Text_Lambda([this]() {
							return FText::FromString(Session.IsValid()
								? Session->SayText : FString()); })
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 13))
						.ColorAndOpacity(kText)
						.AutoWrapText(true)
					]
					// options
					+ SVerticalBox::Slot().AutoHeight().MaxHeight(260.f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SAssignNew(OptionsBox, SVerticalBox)
						]
					]
				]
			]
		];

		RebuildOptions();
	}

	virtual void Tick(const FGeometry& Geo, const double Time, const float Dt) override
	{
		SCompoundWidget::Tick(Geo, Time, Dt);
		if (!Session.IsValid()) return;
		if (Session->bClosed) { CloseSelf(); return; }
		if (Session->Revision != SeenRevision)
			RebuildOptions();
	}

private:
	TSharedPtr<FRoseDialogSession> Session;
	TSharedPtr<SVerticalBox> OptionsBox;
	TSharedPtr<FSlateBrush> GlassBrush;   // shared glass frame, null under the flat skin
	FSimpleDelegate OnClosed;
	int32 SeenRevision = -1;
	bool bClosing = false;

	void CloseSelf()
	{
		if (bClosing) return;
		bClosing = true;
		if (Session.IsValid()) Session->bClosed = true;
		OnClosed.ExecuteIfBound();
	}

	void RebuildOptions()
	{
		SeenRevision = Session->Revision;
		OptionsBox->ClearChildren();

		static FSlateRoundedBoxBrush RowBrush(kRow, 6.f);
		static FSlateRoundedBoxBrush RowHover(FLinearColor(kRow.R * 1.6f, kRow.G * 1.6f, kRow.B * 1.6f, kRow.A), 6.f);
		static FButtonStyle RowStyle = FButtonStyle()
			.SetNormal(RowBrush).SetHovered(RowHover).SetPressed(RowBrush);

		for (int32 i = 0; i < Session->Options.Num(); ++i)
		{
			const FString Text = Session->Options[i].Text;
			OptionsBox->AddSlot().AutoHeight().Padding(4, 3)
			[
				SNew(SButton)
				.ButtonStyle(&RowStyle)
				.ContentPadding(FMargin(12, 7))
				.OnClicked_Lambda([this, i]() {
					if (Session.IsValid() && !bClosing)
						Session->ClickOption(i);
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("•  %s"), *Text)))
					.Font(FCoreStyle::GetDefaultFontStyle("Regular", 12))
					.ColorAndOpacity(kText)
					.AutoWrapText(true)
				]
			];
		}
	}
};

// ─── manager glue ────────────────────────────────────────────────────────────
TSharedRef<SWidget> RoseDialog_Make(URoseUIManager& UI, TSharedPtr<FRoseDialogSession> Session)
{
	TWeakObjectPtr<URoseUIManager> Weak(&UI);
	return SNew(SConstraintCanvas)
		+ SConstraintCanvas::Slot()
		.Anchors(FAnchors(0.5f, 1.f, 0.5f, 1.f))       // bottom-centre
		.Offset(FMargin(0.f, -170.f, 0.f, 0.f))
		.Alignment(FVector2D(0.5f, 1.f))
		.AutoSize(true)
		[
			SNew(SRoseDialogWindow)
			.Session(Session)
			.OnClosed(FSimpleDelegate::CreateLambda([Weak]() {
				if (Weak.IsValid()) Weak->CloseNpcDialog();
			}))
		];
}
