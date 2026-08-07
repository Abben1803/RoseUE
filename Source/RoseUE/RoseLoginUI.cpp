// SRoseLoginScreen — the sign-in / create-account panel shown over the Title
// platform scene before Character Select (built by ARoseCharSelectHUD).
//
// It talks to the backend service through URoseBackend (backend/README.md).  On
// a successful login it hands control to Character Select, which then reads the
// roster from the server instead of a local save game.
//
// OFFLINE: with no BackendUrl configured there is nothing to log into, so the
// screen collapses to a single "Play Offline" button and the old local-save
// flow runs unchanged.  Single-player development never needs a server.
#include "RoseBackend.h"
#include "RoseUIHelpers.h"
#include "RoseCharSelectHUD.h"
#include "RoseUITheme.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FSlateFontInfo LoginFont(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}
}

class SRoseLoginScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseLoginScreen) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARoseCharSelectHUD>, HUD)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		HUD = InArgs._HUD;

		PanelBrush = RoseUI::GlassPanel(RoseUI::EPanelKind::Window);
		if (!PanelBrush.IsValid())
			PanelBrush = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::Panel, 14.f,
				FLinearColor(RoseTheme::Accent.R, RoseTheme::Accent.G, RoseTheme::Accent.B, 0.5f), 1.5f);
		BtnBrush   = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::Button, 8.f, RoseTheme::Accent, 1.f);
		GoBrush    = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::Green, 8.f);
		// A scrim over the 3D scene so the form reads at any camera angle.
		ScrimBrush = MakeShared<FSlateRoundedBoxBrush>(FLinearColor(0.f, 0.f, 0.f, 0.55f), 0.f);

		ChildSlot
		[
			SNew(SOverlay)
			+ SOverlay::Slot()[ SNew(SImage).Image(ScrimBrush.Get()) ]
			+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
			[
				SNew(SBox).WidthOverride(380.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ SNew(SImage).Image(PanelBrush.Get()) ]
					+ SOverlay::Slot().Padding(24.f)[ SAssignNew(Host, SBox) ]
				]
			]
		];

		Rebuild();
	}

private:
	TWeakObjectPtr<ARoseCharSelectHUD> HUD;
	TSharedPtr<SBox> Host;
	TSharedPtr<SEditableTextBox> UserBox, PassBox, ConfirmBox;
	TSharedPtr<FSlateBrush> PanelBrush, BtnBrush, GoBrush, ScrimBrush;

	bool bRegisterMode = false;
	bool bBusy = false;                 // a request is in flight — buttons locked
	FString Status;                     // shown under the form
	bool bStatusIsError = true;

	URoseBackend* Backend() const
	{
		UWorld* W = HUD.IsValid() ? HUD->GetWorld() : nullptr;
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<URoseBackend>() : nullptr;
	}

	void SetStatus(const FString& Text, bool bError)
	{
		Status = Text;
		bStatusIsError = bError;
	}

	void Rebuild()
	{
		if (Host.IsValid())
			Host->SetContent(BuildForm());
	}

	TSharedRef<SWidget> BuildForm()
	{
		URoseBackend* B = Backend();

		// ── offline: nothing to sign into ──
		if (!B || !B->IsConfigured())
		{
			return SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 10)
				[ SNew(STextBlock).Font(LoginFont(20, true)).ColorAndOpacity(RoseTheme::Gold)
				  .Text(FText::FromString(TEXT("ROSE Online"))) ]
				+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
				[ SNew(STextBlock).Font(LoginFont(10)).ColorAndOpacity(RoseTheme::TextDim)
				  .AutoWrapText(true)
				  .Text(FText::FromString(TEXT(
					  "No server configured — playing offline. Characters are stored on this "
					  "machine only."))) ]
				+ SVerticalBox::Slot().AutoHeight()
				[ Button(TEXT("Play Offline"), GoBrush, [this]() { Finish(); }) ];
		}

		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 4)
			[ SNew(STextBlock).Font(LoginFont(20, true)).ColorAndOpacity(RoseTheme::Gold)
			  .Text(FText::FromString(TEXT("ROSE Online"))) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 16)
			[ SNew(STextBlock).Font(LoginFont(10)).ColorAndOpacity(RoseTheme::TextDim)
			  .Text(FText::FromString(bRegisterMode ? TEXT("Create an account")
			                                        : TEXT("Sign in to continue"))) ]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
			[
				SAssignNew(UserBox, SEditableTextBox)
				.HintText(FText::FromString(TEXT("Username")))
				.Font(LoginFont(11))
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type T)
					{ if (T == ETextCommit::OnEnter && PassBox.IsValid())
						FSlateApplication::Get().SetKeyboardFocus(PassBox); })
			]
			+ SVerticalBox::Slot().AutoHeight()
			[
				SAssignNew(PassBox, SEditableTextBox)
				.HintText(FText::FromString(TEXT("Password")))
				.Font(LoginFont(11))
				// Masked, and SEditableTextBox in password mode also keeps the
				// text out of the undo buffer and clipboard.
				.IsPassword(true)
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type T)
					{ if (T == ETextCommit::OnEnter) Submit(); })
			];

		if (bRegisterMode)
		{
			Col->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
			[
				SAssignNew(ConfirmBox, SEditableTextBox)
				.HintText(FText::FromString(TEXT("Confirm password")))
				.Font(LoginFont(11))
				.IsPassword(true)
				.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type T)
					{ if (T == ETextCommit::OnEnter) Submit(); })
			];
		}

		Col->AddSlot().AutoHeight().Padding(0, 14, 0, 0)
		[
			Button(bRegisterMode ? TEXT("Create Account") : TEXT("Sign In"), GoBrush,
				[this]() { Submit(); })
		];

		Col->AddSlot().AutoHeight().Padding(0, 8, 0, 0)
		[
			Button(bRegisterMode ? TEXT("Back to Sign In") : TEXT("Create Account"), BtnBrush,
				[this]()
				{
					bRegisterMode = !bRegisterMode;
					SetStatus(FString(), false);
					Rebuild();
				})
		];

		Col->AddSlot().AutoHeight().Padding(0, 12, 0, 0)
		[
			SNew(SBox).MinDesiredHeight(30.f)
			[
				SNew(STextBlock)
				.Font(LoginFont(10)).AutoWrapText(true)
				.ColorAndOpacity_Lambda([this]()
					{ return bStatusIsError ? RoseTheme::Danger : RoseTheme::Green; })
				.Text_Lambda([this]() { return FText::FromString(Status); })
			]
		];

		return Col;
	}

	void Submit()
	{
		if (bBusy)
			return;
		URoseBackend* B = Backend();
		if (!B)
			return;

		const FString User = UserBox.IsValid() ? UserBox->GetText().ToString().TrimStartAndEnd() : FString();
		const FString Pass = PassBox.IsValid() ? PassBox->GetText().ToString() : FString();

		if (User.IsEmpty() || Pass.IsEmpty())
		{
			SetStatus(TEXT("Enter a username and password."), true);
			return;
		}
		if (bRegisterMode)
		{
			const FString Confirm = ConfirmBox.IsValid() ? ConfirmBox->GetText().ToString() : FString();
			if (Pass != Confirm)
			{
				SetStatus(TEXT("Passwords do not match."), true);
				return;
			}
		}

		bBusy = true;
		SetStatus(bRegisterMode ? TEXT("Creating account...") : TEXT("Signing in..."), false);

		TWeakPtr<SRoseLoginScreen> WeakSelf = SharedThis(this);

		if (bRegisterMode)
		{
			B->Register(User, Pass, FRoseBackendResult::CreateLambda(
				[WeakSelf, B, User, Pass](bool bOk, const FString& Err)
				{
					TSharedPtr<SRoseLoginScreen> Self = WeakSelf.Pin();
					if (!Self.IsValid())
						return;    // screen went away mid-request
					if (!bOk)
					{
						Self->bBusy = false;
						Self->SetStatus(Err, true);
						return;
					}
					// Registration succeeded — sign straight in so the player
					// does not have to type it all again.
					Self->DoLogin(User, Pass);
				}));
			return;
		}

		DoLogin(User, Pass);
	}

	void DoLogin(const FString& User, const FString& Pass)
	{
		URoseBackend* B = Backend();
		if (!B)
			return;

		bBusy = true;
		SetStatus(TEXT("Signing in..."), false);

		TWeakPtr<SRoseLoginScreen> WeakSelf = SharedThis(this);
		B->Login(User, Pass, FRoseBackendResult::CreateLambda(
			[WeakSelf](bool bOk, const FString& Err)
			{
				TSharedPtr<SRoseLoginScreen> Self = WeakSelf.Pin();
				if (!Self.IsValid())
					return;
				Self->bBusy = false;
				if (!bOk)
				{
					// The service answers 429 with a Retry-After when a source
					// is guessing too fast; surface its wording rather than
					// inventing one, so the reason is honest.
					Self->SetStatus(Err, true);
					return;
				}
				Self->SetStatus(TEXT("Signed in."), false);
				Self->Finish();
			}));
	}

	// Hand over to Character Select.
	void Finish()
	{
		if (HUD.IsValid())
			HUD->ShowCharacterSelect();
	}

	TSharedRef<SWidget> Button(const FString& Label, TSharedPtr<FSlateBrush> Brush,
		TFunction<void()> OnClick)
	{
		// Under the glass skin the plate IS the button style, so its hover and
		// pressed art actually plays; the flat skin keeps drawing a static
		// rounded box behind the label.
		const FButtonStyle* Glass = RoseUI::GlassButton(RoseUI::EButtonKind::Action);
		return SNew(SButton)
			.ButtonStyle(Glass ? Glass : &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(FMargin(0))
			.IsEnabled_Lambda([this]() { return !bBusy; })
			.OnClicked_Lambda([OnClick]() { if (OnClick) OnClick(); return FReply::Handled(); })
			[
				SNew(SBox).HeightOverride(38.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[ SNew(SImage).Image(Brush.Get())
					  .Visibility(Glass ? EVisibility::Collapsed : EVisibility::HitTestInvisible) ]
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[ SNew(STextBlock).Font(LoginFont(12, true)).ColorAndOpacity(RoseTheme::Text)
					  .Text(FText::FromString(Label)) ]
				]
			];
	}
};

TSharedRef<SWidget> RoseLogin_Make(ARoseCharSelectHUD* HUD)
{
	return SNew(SRoseLoginScreen).HUD(HUD);
}
