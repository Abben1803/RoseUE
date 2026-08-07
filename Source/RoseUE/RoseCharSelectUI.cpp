// SRoseCharSelect — the ROSE-style Character Select / Create overlay (built by
// ARoseCharSelectHUD).  A right-hand window over the 3D Title-platform scene:
//
//   List mode (default): the saved roster (level-sorted, same order as the arc
//     avatars).  Click a row -> selects that avatar and the camera focuses it.
//     Buttons: Create / Delete / Start.  Start (or double-click an avatar) enters
//     the world as the selected character.
//   Create mode: a single editable avatar on the front pedestal; gender / hair /
//     face / name controls; Confirm adds the character (max 5), Cancel returns.
//
// The 3D scene + camera are owned by ARoseCharSelectDirector; this widget drives
// it (SelectIndex / SetCreateMode / RefreshRoster).
//
// TWO SOURCES OF TRUTH, picked by whether a backend is configured:
//
//   ONLINE  — the roster comes from the server (URoseBackend::FetchRoster);
//     create/delete are API calls; Start asks for a one-shot world ticket and
//     ClientTravels to the zone server with it.  The local save game is still
//     written, but only as a DISPLAY CACHE so ARoseCharSelectDirector can build
//     the 3D avatar arc without being taught about async fetches.
//
//   OFFLINE — no BackendUrl: the old behaviour exactly.  The save game is the
//     roster and Start OpenLevels the start town locally.
#include "RoseBackend.h"
#include "RoseUIHelpers.h"
#include "RoseCharSelectHUD.h"
#include "RoseCharSelectDirector.h"
#include "RoseCharacterCreator.h"
#include "RoseCharSlotSave.h"
#include "RoseGameInstance.h"
#include "RoseUITheme.h"

#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
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
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}
	constexpr int32 kMaxChars = 5;
}

class SRoseCharSelect : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseCharSelect) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARoseCharSelectHUD>, HUD)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		HUD = InArgs._HUD;

		PanelBrush = RoseUI::GlassPanel(RoseUI::EPanelKind::Window);
		if (!PanelBrush.IsValid())
			PanelBrush = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::Panel, 14.f,
				FLinearColor(RoseTheme::Accent.R, RoseTheme::Accent.G, RoseTheme::Accent.B, 0.5f), 1.5f);
		RowBrush   = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::Row, 6.f);
		RowSelBrush = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::TabOn, 6.f, RoseTheme::Accent, 1.f);
		BtnBrush   = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::Button, 8.f, RoseTheme::Accent, 1.f);
		TabOnBrush = RoseUI::GlassTab(true);
		TabOffBrush= RoseUI::GlassTab(false);
		if (!TabOnBrush.IsValid())  TabOnBrush = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::TabOn, 8.f, RoseTheme::Accent, 1.f);
		if (!TabOffBrush.IsValid()) TabOffBrush= MakeShared<FSlateRoundedBoxBrush>(RoseTheme::TabOff, 8.f);
		EnterBrush = MakeShared<FSlateRoundedBoxBrush>(RoseTheme::Green, 8.f);
		DangerBrush= MakeShared<FSlateRoundedBoxBrush>(RoseTheme::Danger, 8.f);

		FindDirector();
		// Show the cached roster immediately so the screen is never blank, then
		// let the server's answer replace it.
		LoadRoster();
		if (Roster.Num() > 0) { Selected = 0; if (Director.IsValid()) Director->SelectIndex(0); }

		ChildSlot
		[
			SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot()
			  .Anchors(FAnchors(1.f, 0.f))
			  .Alignment(FVector2D(1.f, 0.f))
			  .Offset(FMargin(-24.f, 24.f, 0.f, 0.f))
			  .AutoSize(true)
			[
				SNew(SBox).WidthOverride(360.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()[ SNew(SImage).Image(PanelBrush.Get()) ]
					+ SOverlay::Slot().Padding(18.f)
					[ SAssignNew(ContentHost, SBox) ]
				]
			]
		];

		Rebuild();
		FetchRosterFromServer();
	}

private:
	TWeakObjectPtr<ARoseCharSelectHUD> HUD;
	TWeakObjectPtr<ARoseCharSelectDirector> Director;
	TArray<FRoseCharSlot> Roster;
	int32 Selected = INDEX_NONE;
	bool bCreateMode = false;
	// Non-empty while a backend request is in flight, or holding its error.
	// Shown under the roster and used to lock the action buttons.
	FString Busy;

	TSharedPtr<SBox> ContentHost;
	TSharedPtr<SVerticalBox> RosterBox;
	TSharedPtr<SEditableTextBox> NameBox;
	TSharedPtr<FSlateBrush> PanelBrush, RowBrush, RowSelBrush, BtnBrush, TabOnBrush, TabOffBrush, EnterBrush, DangerBrush;

	UWorld* World() const { return HUD.IsValid() ? HUD->GetWorld() : nullptr; }

	void FindDirector()
	{
		if (UWorld* W = World())
			for (TActorIterator<ARoseCharSelectDirector> It(W); It; ++It) { Director = *It; break; }
	}
	ARoseCharacterCreator* CreateAvatar() const
	{
		return Director.IsValid() ? Director->GetCreateAvatar() : nullptr;
	}

	URoseBackend* Backend() const
	{
		UWorld* W = World();
		UGameInstance* GI = W ? W->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<URoseBackend>() : nullptr;
	}
	bool IsOnline() const
	{
		URoseBackend* B = Backend();
		return B && B->IsConfigured() && B->IsLoggedIn();
	}

	// ── roster persistence (level-sorted + capped, matching the arc order) ──
	void LoadRoster()
	{
		Roster.Reset();
		if (UGameplayStatics::DoesSaveGameExist(URoseCharSlotSave::SlotName(), 0))
			if (URoseCharSlotSave* S = Cast<URoseCharSlotSave>(
					UGameplayStatics::LoadGameFromSlot(URoseCharSlotSave::SlotName(), 0)))
				Roster = S->Slots;
		SortAndCap();
	}

	void SortAndCap()
	{
		Roster.Sort([](const FRoseCharSlot& A, const FRoseCharSlot& B) { return A.Level > B.Level; });
		if (Roster.Num() > kMaxChars) Roster.SetNum(kMaxChars);
	}

	// Online: pull the roster from the server and replace whatever the cache
	// held.  The server is authoritative — a stale local copy must never decide
	// what a player owns.
	void FetchRosterFromServer()
	{
		URoseBackend* B = Backend();
		if (!B || !IsOnline())
			return;

		Busy = TEXT("Loading characters...");
		TWeakPtr<SRoseCharSelect> WeakSelf = SharedThis(this);
		B->FetchRoster(FRoseBackendRoster::CreateLambda(
			[WeakSelf](bool bOk, const FString& Err, const TArray<FRoseBackendCharacter>& Chars)
			{
				TSharedPtr<SRoseCharSelect> Self = WeakSelf.Pin();
				if (!Self.IsValid())
					return;
				Self->Busy.Reset();
				if (!bOk)
				{
					Self->Busy = FString::Printf(TEXT("Could not load characters: %s"), *Err);
					return;
				}

				Self->Roster.Reset();
				for (const FRoseBackendCharacter& C : Chars)
				{
					FRoseCharSlot Slot;
					Slot.BackendId = C.Id;
					Slot.Name = C.Name;
					Slot.Gender = C.Gender;
					Slot.Hair = C.Hair;
					Slot.Face = C.Face;
					Slot.Level = C.Level;
					Self->Roster.Add(Slot);
				}
				Self->SortAndCap();
				// Write the cache so the Director can build the 3D arc from it.
				Self->SaveRoster();
				if (Self->Director.IsValid())
					Self->Director->RefreshRoster();
				Self->Selected = Self->Roster.Num() > 0 ? 0 : INDEX_NONE;
				if (Self->Director.IsValid())
					Self->Director->SelectIndex(Self->Selected);
				Self->RebuildRoster();
			}));
	}

	void SaveRoster()
	{
		URoseCharSlotSave* S = Cast<URoseCharSlotSave>(
			UGameplayStatics::CreateSaveGameObject(URoseCharSlotSave::StaticClass()));
		if (!S) return;
		S->Slots = Roster;
		UGameplayStatics::SaveGameToSlot(S, URoseCharSlotSave::SlotName(), 0);
	}

	// ── mode switching ──
	void SetMode(bool bCreate)
	{
		bCreateMode = bCreate;
		if (Director.IsValid()) Director->SetCreateMode(bCreate);
		Rebuild();
	}

	void Rebuild()
	{
		if (!ContentHost.IsValid()) return;
		ContentHost->SetContent(bCreateMode ? BuildCreate() : BuildList());
	}

	// ── LIST MODE ──
	TSharedRef<SWidget> BuildList()
	{
		TSharedRef<SVerticalBox> Col = SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[ SNew(STextBlock).Font(Font(18, true)).ColorAndOpacity(RoseTheme::Gold)
			  .Text(FText::FromString(TEXT("Select Character"))) ]

			+ SVerticalBox::Slot().AutoHeight()
			[
				SNew(SBox).MaxDesiredHeight(210.f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()[ SAssignNew(RosterBox, SVerticalBox) ]
				]
			]

			+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				  [ ActionButton(TEXT("Create"), BtnBrush, RoseTheme::Text,
				      [this]() { OnCreatePressed(); },
				      [this]() { return Roster.Num() < kMaxChars && Busy.IsEmpty(); }) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6, 0, 0, 0)
				  [ ActionButton(TEXT("Delete"), DangerBrush, RoseTheme::Text,
				      [this]() { OnDelete(); },
				      [this]() { return Roster.IsValidIndex(Selected) && Busy.IsEmpty(); }) ]
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)
			[ ActionButton(TEXT("Start"), EnterBrush, RoseTheme::Text,
			    [this]() { OnStart(); },
			    [this]() { return Roster.IsValidIndex(Selected) && Busy.IsEmpty(); }) ]

			// Progress / error from the backend (empty offline, and empty while
			// nothing is in flight).
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
			[
				SNew(STextBlock).Font(Font(10)).AutoWrapText(true)
				.ColorAndOpacity(RoseTheme::TextDim)
				.Text_Lambda([this]() { return FText::FromString(Busy); })
			];

		RebuildRoster();
		return Col;
	}

	void RebuildRoster()
	{
		if (!RosterBox.IsValid()) return;
		RosterBox->ClearChildren();
		if (Roster.Num() == 0)
		{
			RosterBox->AddSlot().AutoHeight().Padding(2, 6)
			[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(RoseTheme::TextDim)
			  .Text(FText::FromString(TEXT("No characters yet — press Create."))) ];
			return;
		}
		for (int32 i = 0; i < Roster.Num(); ++i)
		{
			const FRoseCharSlot Slot = Roster[i];
			const int32 Idx = i;
			RosterBox->AddSlot().AutoHeight().Padding(0, 0, 0, 5)
			[
				SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0))
				.OnClicked_Lambda([this, Idx]() { OnRowClicked(Idx); return FReply::Handled(); })
				[
					SNew(SBox).HeightOverride(38.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[ SNew(SImage).Image_Lambda([this, Idx]() -> const FSlateBrush* {
							return (Idx == Selected ? RowSelBrush : RowBrush).Get(); }) ]
						+ SOverlay::Slot().Padding(10, 0).VAlign(VAlign_Center)
						[ SNew(STextBlock).Font(Font(11, true)).ColorAndOpacity(RoseTheme::Text)
						  .Text(FText::FromString(FString::Printf(TEXT("%s   Lv %d   %s"),
							  *Slot.Name, Slot.Level, *Slot.Gender))) ]
					]
				]
			];
		}
	}

	void OnRowClicked(int32 Idx)
	{
		Selected = Idx;
		if (Director.IsValid()) Director->SelectIndex(Idx);
	}

	void OnStart()
	{
		if (!Roster.IsValidIndex(Selected)) return;
		UWorld* W = World();
		if (!W) return;
		const FRoseCharSlot S = Roster[Selected];

		// ── offline: straight into the start town, no server involved ──
		if (!IsOnline() || S.BackendId == 0)
		{
			if (URoseGameInstance* GI = Cast<URoseGameInstance>(UGameplayStatics::GetGameInstance(W)))
				GI->EnterWorldAsNewCharacter(W, S.Gender, S.Hair, S.Face, S.Name);
			return;
		}

		// ── online: ask for a one-shot world ticket, then connect with it.
		// The ticket — not a character id — is what the zone server redeems, so
		// this client cannot ask to enter as somebody else's character.
		URoseBackend* B = Backend();
		if (!B) return;

		Busy = TEXT("Entering world...");
		RebuildRoster();
		TWeakPtr<SRoseCharSelect> WeakSelf = SharedThis(this);
		B->EnterWorld(S.BackendId, FRoseBackendTicket::CreateLambda(
			[WeakSelf](bool bOk, const FString& Err, const FString& Ticket, const FString& Address)
			{
				TSharedPtr<SRoseCharSelect> Self = WeakSelf.Pin();
				if (!Self.IsValid())
					return;
				if (!bOk)
				{
					// 409 here = the character is still held by a zone server
					// (a previous session that has not released it yet).
					Self->Busy = FString::Printf(TEXT("Cannot enter: %s"), *Err);
					Self->RebuildRoster();
					return;
				}

				UWorld* W = Self->World();
				APlayerController* PC = W ? W->GetFirstPlayerController() : nullptr;
				if (!PC)
					return;
				// The loading screen is armed the same way a warp arms it.
				URoseGameInstance::ShowLoadingScreen(TEXT("L_JPT01"));
				PC->ClientTravel(FString::Printf(TEXT("%s?ticket=%s"), *Address, *Ticket),
					ETravelType::TRAVEL_Absolute);
			}));
	}

	void OnDelete()
	{
		if (!Roster.IsValidIndex(Selected)) return;
		const FRoseCharSlot S = Roster[Selected];

		if (IsOnline() && S.BackendId != 0)
		{
			URoseBackend* B = Backend();
			if (!B) return;
			Busy = TEXT("Deleting...");
			RebuildRoster();
			TWeakPtr<SRoseCharSelect> WeakSelf = SharedThis(this);
			B->DeleteCharacter(S.BackendId, FRoseBackendResult::CreateLambda(
				[WeakSelf](bool bOk, const FString& Err)
				{
					TSharedPtr<SRoseCharSelect> Self = WeakSelf.Pin();
					if (!Self.IsValid())
						return;
					Self->Busy.Reset();
					if (!bOk)
					{
						Self->Busy = FString::Printf(TEXT("Could not delete: %s"), *Err);
						Self->RebuildRoster();
						return;
					}
					// Re-fetch rather than patching locally: the server's list
					// is the truth, and this keeps the two from drifting.
					Self->FetchRosterFromServer();
				}));
			return;
		}

		Roster.RemoveAt(Selected);
		SaveRoster();
		if (Roster.Num() == 0) Selected = INDEX_NONE;
		else Selected = FMath::Clamp(Selected, 0, Roster.Num() - 1);
		if (Director.IsValid())
		{
			Director->RefreshRoster();
			Director->SelectIndex(Selected);
		}
		RebuildRoster();
	}

	void OnCreatePressed()
	{
		if (Roster.Num() >= kMaxChars) return;
		if (NameBox.IsValid()) NameBox->SetText(FText::GetEmpty());
		SetMode(true);
	}

	// ── CREATE MODE ──
	TSharedRef<SWidget> BuildCreate()
	{
		return SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 12)
			[ SNew(STextBlock).Font(Font(18, true)).ColorAndOpacity(RoseTheme::Gold)
			  .Text(FText::FromString(TEXT("Create Character"))) ]

			+ SVerticalBox::Slot().AutoHeight()[ GenderRow() ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 8, 0, 0)[ CycleRow(TEXT("Hair"), true) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 6, 0, 0)[ CycleRow(TEXT("Face"), false) ]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
			[
				SAssignNew(NameBox, SEditableTextBox)
				.HintText(FText::FromString(TEXT("Character name")))
				.Font(Font(11))
			]
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 14, 0, 0)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot().FillWidth(1.f)
				  [ ActionButton(TEXT("Cancel"), BtnBrush, RoseTheme::Text, [this]() { SetMode(false); }) ]
				+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6, 0, 0, 0)
				  [ ActionButton(TEXT("Confirm"), EnterBrush, RoseTheme::Text,
				      [this]() { OnConfirmCreate(); }, [this]() { return Busy.IsEmpty(); }) ]
			]
			// The server's refusal (name taken, slots full) lands here.
			+ SVerticalBox::Slot().AutoHeight().Padding(0, 10, 0, 0)
			[
				SNew(STextBlock).Font(Font(10)).AutoWrapText(true)
				.ColorAndOpacity(RoseTheme::Danger)
				.Text_Lambda([this]() { return FText::FromString(Busy); })
			];
	}

	TSharedRef<SWidget> GenderRow()
	{
		auto Tab = [this](const FString& G) -> TSharedRef<SWidget>
		{
			return SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0))
				.OnClicked_Lambda([this, G]() { if (ARoseCharacterCreator* C = CreateAvatar()) C->SetGender(G); return FReply::Handled(); })
				[
					SNew(SBox).HeightOverride(30.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[ SNew(SImage).Image_Lambda([this, G]() -> const FSlateBrush* {
							ARoseCharacterCreator* C = CreateAvatar();
							const bool On = C && C->GetGender().Equals(G, ESearchCase::IgnoreCase);
							return (On ? TabOnBrush : TabOffBrush).Get(); }) ]
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
						[ SNew(STextBlock).Font(Font(10, true)).ColorAndOpacity(RoseTheme::Text)
						  .Text(FText::FromString(G)) ]
					]
				];
		};
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.f)[ Tab(TEXT("Female")) ]
			+ SHorizontalBox::Slot().FillWidth(1.f).Padding(6, 0, 0, 0)[ Tab(TEXT("Male")) ];
	}

	TSharedRef<SWidget> CycleRow(const FString& Label, bool bHair)
	{
		auto Arrow = [this, bHair](const TCHAR* Glyph, int32 Dir) -> TSharedRef<SWidget>
		{
			return SNew(SButton).ButtonStyle(FCoreStyle::Get(), "NoBorder").ContentPadding(FMargin(0))
				.OnClicked_Lambda([this, bHair, Dir]() {
					if (ARoseCharacterCreator* C = CreateAvatar())
					{
						if (bHair) { if (Dir > 0) C->NextHair(); else C->PrevHair(); }
						else       { if (Dir > 0) C->NextFace(); else C->PrevFace(); }
					}
					return FReply::Handled(); })
				[
					SNew(SBox).WidthOverride(34.f).HeightOverride(30.f)
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[ SNew(SImage).Image(BtnBrush.Get()) ]
						+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
						[ SNew(STextBlock).Font(Font(12, true)).ColorAndOpacity(RoseTheme::Text)
						  .Text(FText::FromString(Glyph)) ]
					]
				];
		};
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[ Arrow(TEXT("<"), -1) ]
			+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center).Padding(8, 0)
			[
				SNew(STextBlock).Justification(ETextJustify::Center)
				.Font(Font(11)).ColorAndOpacity(RoseTheme::Text)
				.Text_Lambda([this, Label, bHair]() {
					ARoseCharacterCreator* C = CreateAvatar();
					const int32 Id = C ? (bHair ? C->GetHairId() : C->GetFaceId()) : 0;
					return FText::FromString(FString::Printf(TEXT("%s  #%d"), *Label, Id)); })
			]
			+ SHorizontalBox::Slot().AutoWidth()[ Arrow(TEXT(">"), +1) ];
	}

	FString CurrentName() const
	{
		FString N = NameBox.IsValid() ? NameBox->GetText().ToString().TrimStartAndEnd() : FString();
		return N.IsEmpty() ? TEXT("Adventurer") : N;
	}

	void OnConfirmCreate()
	{
		ARoseCharacterCreator* C = CreateAvatar();
		if (!C || Roster.Num() >= kMaxChars) { SetMode(false); return; }

		if (IsOnline())
		{
			URoseBackend* B = Backend();
			if (!B) { SetMode(false); return; }
			const FString Name = CurrentName();
			Busy = TEXT("Creating character...");
			TWeakPtr<SRoseCharSelect> WeakSelf = SharedThis(this);
			B->CreateCharacter(Name, C->GetGender(), C->GetHairId(), C->GetFaceId(),
				FRoseBackendResult::CreateLambda(
					[WeakSelf](bool bOk, const FString& Err)
					{
						TSharedPtr<SRoseCharSelect> Self = WeakSelf.Pin();
						if (!Self.IsValid())
							return;
						Self->Busy.Reset();
						if (!bOk)
						{
							// Name already taken, slots full, bad name — the
							// server decides, and stays in create mode so the
							// player can fix it without retyping everything.
							Self->Busy = Err;
							Self->Rebuild();
							return;
						}
						Self->SetMode(false);
						Self->FetchRosterFromServer();
					}));
			return;
		}

		FRoseCharSlot Slot;
		Slot.Name = CurrentName();
		Slot.Gender = C->GetGender();
		Slot.Hair = C->GetHairId();
		Slot.Face = C->GetFaceId();
		Slot.Level = 1;
		Roster.Add(Slot);
		Roster.Sort([](const FRoseCharSlot& A, const FRoseCharSlot& B) { return A.Level > B.Level; });
		SaveRoster();
		if (Director.IsValid()) Director->RefreshRoster();
		Selected = Roster.IndexOfByPredicate([&Slot](const FRoseCharSlot& S) {
			return S.Name == Slot.Name && S.Hair == Slot.Hair && S.Face == Slot.Face; });
		if (Selected == INDEX_NONE) Selected = Roster.Num() - 1;
		if (Director.IsValid()) Director->SelectIndex(Selected);
		SetMode(false);
	}

	// ── shared button ──
	TSharedRef<SWidget> ActionButton(const FString& Label, TSharedPtr<FSlateBrush> Brush,
		const FLinearColor& TextColor, TFunction<void()> OnClick, TFunction<bool()> IsEnabled = nullptr)
	{
		// Glass: the plate becomes the button STYLE so hover/pressed art plays.
		// Flat: unchanged — a static rounded box behind the label.
		const FButtonStyle* Glass = RoseUI::GlassButton(RoseUI::EButtonKind::Action);
		return SNew(SButton)
			.ButtonStyle(Glass ? Glass : &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
			.ContentPadding(FMargin(0))
			.IsEnabled_Lambda([IsEnabled]() { return IsEnabled ? IsEnabled() : true; })
			.OnClicked_Lambda([OnClick]() { if (OnClick) OnClick(); return FReply::Handled(); })
			[
				SNew(SBox).HeightOverride(38.f)
				[
					SNew(SOverlay)
					+ SOverlay::Slot()
					[ SNew(SImage).Image(Brush.Get())
					  .Visibility(Glass ? EVisibility::Collapsed : EVisibility::HitTestInvisible) ]
					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[ SNew(STextBlock).Font(Font(12, true)).ColorAndOpacity(TextColor)
					  .Text(FText::FromString(Label)) ]
				]
			];
	}
};

TSharedRef<SWidget> RoseCharSelect_Make(ARoseCharSelectHUD* HUD)
{
	return SNew(SRoseCharSelect).HUD(HUD);
}
