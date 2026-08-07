// Weapon grip tuner — a slider + numeric box per axis, live on the equipped
// weapon.
//
// The grip is ONE shared transform per hand (GripLocR/GripRotR/GripScaleR and
// the L pair), so tuning it here fixes every right-hand weapon at once.  Values
// write straight onto the character and re-apply immediately, so the sword in
// the viewport moves as the slider moves.
//
// "Save" calls SaveConfig() so the numbers survive a restart — otherwise a
// tuning session is lost the moment PIE stops, which is how the previous
// keyboard-nudge workflow kept losing work.
#include "RoseCharacter.h"
#include "RoseModernWindow.h"
#include "RoseUIManager.h"
#include "RoseUITheme.h"

#include "Widgets/SBoxPanel.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Input/SSpinBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FSlateFontInfo TunerFont(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}
}

class SRoseGripTuner : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseGripTuner) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARoseCharacter>, Char)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		CharWeak = InArgs._Char;

		TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

		// Which hand the sliders act on.
		Box->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(SCheckBox)
			.IsChecked_Lambda([this]() {
				ARoseCharacter* C = Char();
				return (C && C->IsTuningLeftHand()) ? ECheckBoxState::Checked
				                                    : ECheckBoxState::Unchecked; })
			.OnCheckStateChanged_Lambda([this](ECheckBoxState S) {
				if (ARoseCharacter* C = Char())
					C->SetTuningLeftHand(S == ECheckBoxState::Checked); })
			[
				SNew(STextBlock).Font(TunerFont(9))
				.ColorAndOpacity(RoseTheme::Text)
				.Text(FText::FromString(TEXT("Tune LEFT hand (off = right)")))
			]
		];

		// Which weapon type these sliders are editing.  The grip is per TYPE,
		// so this says what the numbers will apply to — and warns when nothing
		// is equipped, where edits fall back to the shared per-hand values.
		Box->AddSlot().AutoHeight().Padding(0, 0, 0, 6)
		[
			SNew(STextBlock).Font(TunerFont(9, true))
			.ColorAndOpacity(RoseTheme::Gold)
			.Text_Lambda([this]() {
				ARoseCharacter* C = Char();
				const int32 T = C ? C->GetTunedWeaponType() : 0;
				return FText::FromString(T != 0
					? FString::Printf(TEXT("Weapon type %d"), T)
					: TEXT("No weapon — editing the shared grip"));
			})
		];

		Row(Box, TEXT("Loc X"), -60.f, 60.f,
			[](FGrip& G) -> float& { return G.LocX; });
		Row(Box, TEXT("Loc Y"), -60.f, 60.f,
			[](FGrip& G) -> float& { return G.LocY; });
		Row(Box, TEXT("Loc Z"), -60.f, 60.f,
			[](FGrip& G) -> float& { return G.LocZ; });

		Row(Box, TEXT("Pitch"), -180.f, 180.f,
			[](FGrip& G) -> float& { return G.Pitch; });
		Row(Box, TEXT("Yaw"),   -180.f, 180.f,
			[](FGrip& G) -> float& { return G.Yaw; });
		Row(Box, TEXT("Roll"),  -180.f, 180.f,
			[](FGrip& G) -> float& { return G.Roll; });

		Row(Box, TEXT("Scale"), 0.1f, 3.f,
			[](FGrip& G) -> float& { return G.Scale; });

		Box->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(0, 0, 4, 0)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Save")))
				.ToolTipText(FText::FromString(
					TEXT("Write these values to config so they survive a restart")))
				.OnClicked_Lambda([this]() {
					if (ARoseCharacter* C = Char()) C->SaveGripConfig();
					return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot().FillWidth(1.f)
			[
				SNew(SButton)
				.Text(FText::FromString(TEXT("Reset")))
				.OnClicked_Lambda([this]() {
					if (ARoseCharacter* C = Char()) C->ResetGrip();
					return FReply::Handled(); })
			]
		];

		ChildSlot[ Box ];
	}

private:
	// A view onto whichever hand is being tuned, so each row is one accessor
	// rather than a left/right branch repeated seven times.
	//
	// Stored as FLOATS, not FVector/FRotator: those are double-precision in UE5,
	// and the row helper hands out a float& to bind sliders to.
	struct FGrip
	{
		float LocX = 0.f, LocY = 0.f, LocZ = 0.f;
		float Pitch = 0.f, Yaw = 0.f, Roll = 0.f;
		float Scale = 1.f;
	};

	TWeakObjectPtr<ARoseCharacter> CharWeak;
	ARoseCharacter* Char() const { return CharWeak.Get(); }

	FGrip Read() const
	{
		FGrip G;
		if (ARoseCharacter* C = Char())
		{
			FVector L; FRotator R; float S;
			C->GetGrip(L, R, S);
			G.LocX = (float)L.X; G.LocY = (float)L.Y; G.LocZ = (float)L.Z;
			G.Pitch = (float)R.Pitch; G.Yaw = (float)R.Yaw; G.Roll = (float)R.Roll;
			G.Scale = S;
		}
		return G;
	}

	void Write(const FGrip& G)
	{
		if (ARoseCharacter* C = Char())
			C->SetGrip(FVector(G.LocX, G.LocY, G.LocZ),
			           FRotator(G.Pitch, G.Yaw, G.Roll), G.Scale);
	}

	/** One labelled row: slider for coarse feel, spin box for exact numbers.
	 *  Both edit the same value, so they stay in step. */
	template <typename TPick>
	void Row(TSharedRef<SVerticalBox>& Box, const TCHAR* Label,
	         float Min, float Max, TPick Pick)
	{
		Box->AddSlot().AutoHeight().Padding(0, 2)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(52.f)
				[
					SNew(STextBlock).Font(TunerFont(9))
					.ColorAndOpacity(RoseTheme::TextDim)
					.Text(FText::FromString(Label))
				]
			]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(4, 0)
			[
				SNew(SSlider)
				.MinValue(Min).MaxValue(Max)
				.Value_Lambda([this, Pick]() { FGrip G = Read(); return Pick(G); })
				.OnValueChanged_Lambda([this, Pick](float V) {
					FGrip G = Read(); Pick(G) = V; Write(G); })
			]
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(70.f)
				[
					SNew(SSpinBox<float>)
					.MinValue(Min).MaxValue(Max)
					.MinSliderValue(Min).MaxSliderValue(Max)
					.Delta(0.1f)
					.Value_Lambda([this, Pick]() { FGrip G = Read(); return Pick(G); })
					.OnValueChanged_Lambda([this, Pick](float V) {
						FGrip G = Read(); Pick(G) = V; Write(G); })
					.OnValueCommitted_Lambda([this, Pick](float V, ETextCommit::Type) {
						FGrip G = Read(); Pick(G) = V; Write(G); })
				]
			]
		];
	}
};

TSharedRef<SWidget> RoseGripTuner_MakeContent(ARoseCharacter& Char)
{
	return SNew(SRoseGripTuner).Char(&Char);
}
