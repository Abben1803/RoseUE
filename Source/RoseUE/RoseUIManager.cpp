#include "RoseUIManager.h"
#include "RoseCharacter.h"
#include "RoseDialog.h"
#include "RoseNpc.h"
#include "RoseUIWindow.h"
#include "RoseModernWindow.h"

#include "Components/InputComponent.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/PlayerController.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "RoseDragPan.h"

URoseUIManager::URoseUIManager()
{
	PrimaryComponentTick.bCanEverTick = false;
}

ARoseCharacter* URoseUIManager::GetRoseCharacter() const
{
	return Cast<ARoseCharacter>(GetOwner());
}

void URoseUIManager::BeginPlay()
{
	Super::BeginPlay();
	EnsureInit();
}

// Build the HUD + windows once, for the local player's pawn only.
//
// This used to be BeginPlay's body.  It moved behind a re-callable guard for
// networking: on a CLIENT the pawn's Controller has usually not replicated yet
// when BeginPlay runs, so the local-player test below would fail and the player
// would end up with no HUD at all.  ARoseCharacter::EnsureLocalSetup calls this
// again from PossessedBy / OnRep_Controller; bInitialized keeps it to once.
void URoseUIManager::EnsureInit()
{
	if (bInitialized)
		return;

	// Only the LOCAL PLAYER's pawn owns the HUD.  Otherwise every ARoseCharacter
	// in the level (other players' proxies, test spawns, unpossessed copies)
	// builds its own windows and seeds the shared chat log — the "tripled chat"
	// + duplicate HUD bug.
	ARoseCharacter* Owner = GetRoseCharacter();
	if (!Owner || !Owner->IsLocallyControlled() || !Cast<APlayerController>(Owner->GetController()))
		return;
	if (Owner->GetNetMode() == NM_DedicatedServer)
		return;
	bInitialized = true;

	RoseUIHud_Register(*this);
	RoseUIMinimap_Register(*this);
	RoseUISkills_Register(*this);
	RoseUIChat_Register(*this);
	RoseUIMisc_Register(*this);

	// Faithful auto-open: the client shows exactly the dialogs whose XML root has
	// DEFAULT_VISIBLE=1 (dlginfo/dlgchat/dlgminimap/dlgquickbar) — see
	// IT_MGR::InitInterfacePos.  The feature files' bOpenAtStart guesses (e.g. the
	// menu POPUP) are ignored in favour of this.
	for (const TPair<FName, FRoseUIWindowDef>& P : Defs)
		if (IsDefaultVisible(P.Value.Dialog))
		{
			bHasHUD = true;
			Open(P.Key);
		}

	// The modern always-on HUD sits under the windows (ZOrder 5); the classic
	// hud_* windows are retired (RoseUIHud_Register is now a no-op).
	if (GEngine && GEngine->GameViewport)
	{
		// The classic client's hardware cursors (src/client/res/*.cur, hotspots
		// embedded) — copied to Content/Slate/Cursors.  ARoseCharacter's hover
		// feedback switches the PC's CurrentMouseCursor between these shapes:
		// Crosshairs = attack, Hand = talk (NPC), GrabHand = pickup.
		GEngine->GameViewport->SetHardwareCursor(EMouseCursor::Default,
			FName(TEXT("Slate/Cursors/cursor_default")), FVector2D::ZeroVector);
		GEngine->GameViewport->SetHardwareCursor(EMouseCursor::Crosshairs,
			FName(TEXT("Slate/Cursors/cursor_attack")), FVector2D::ZeroVector);
		GEngine->GameViewport->SetHardwareCursor(EMouseCursor::Hand,
			FName(TEXT("Slate/Cursors/cursor_npc")), FVector2D::ZeroVector);
		GEngine->GameViewport->SetHardwareCursor(EMouseCursor::GrabHand,
			FName(TEXT("Slate/Cursors/cursor_pickup")), FVector2D::ZeroVector);
		bHasHUD = true;
		HudRoot = RoseHUD_Make(*this);
		GEngine->GameViewport->AddViewportWidgetContent(HudRoot.ToSharedRef(), 5);

		// Modern chat panel, always on, bottom-left (ZOrder 6, above the HUD).
		ChatRoot = SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot()
			.Anchors(FAnchors(0, 1)).Alignment(FVector2D(0, 1))
			.AutoSize(true).Offset(FMargin(8, 8, 0, 0))
			[ SNew(SRoseDragPan)[ RoseChat_Make(*this) ] ];
		GEngine->GameViewport->AddViewportWidgetContent(ChatRoot.ToSharedRef(), 6);

		// Modern minimap, top-right, shown at start (M toggles).
		MinimapRoot = SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot()
			.Anchors(FAnchors(1, 0)).Alignment(FVector2D(1, 0))
			.AutoSize(true).Offset(FMargin(8, 8, 0, 0))
			[ SNew(SRoseDragPan)[ RoseMinimap_Make(*this) ] ];
		GEngine->GameViewport->AddViewportWidgetContent(MinimapRoot.ToSharedRef(), 6);
	}
	UpdateInputMode();
}

void URoseUIManager::EndPlay(const EEndPlayReason::Type Reason)
{
	// Window content lambdas capture the owner — tear down before it goes.
	TArray<FName> Ids;
	OpenWindows.GetKeys(Ids);
	for (const FName& Id : Ids)
		Close(Id);
	if (GEngine && GEngine->GameViewport)
	{
		if (HudRoot.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(HudRoot.ToSharedRef());
		if (SkillPanelRoot.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(SkillPanelRoot.ToSharedRef());
		if (SkillTreeRoot.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(SkillTreeRoot.ToSharedRef());
		if (OptionsRoot.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(OptionsRoot.ToSharedRef());
		if (QuestJournalRoot.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(QuestJournalRoot.ToSharedRef());
		if (ChatRoot.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(ChatRoot.ToSharedRef());
		if (MinimapRoot.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(MinimapRoot.ToSharedRef());
		if (DialogRoot.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(DialogRoot.ToSharedRef());
		if (StoreRoot.IsValid())
			GEngine->GameViewport->RemoveViewportWidgetContent(StoreRoot.ToSharedRef());
	}
	HudRoot.Reset();
	SkillPanelRoot.Reset(); SkillPanelWindow.Reset();
	SkillTreeRoot.Reset(); SkillTreeWindow.Reset();
	OptionsRoot.Reset(); OptionsWindow.Reset();
	QuestJournalRoot.Reset(); QuestJournalWindow.Reset();
	ChatRoot.Reset();
	MinimapRoot.Reset();
	DialogRoot.Reset();
	ActiveDialog.Reset();
	StoreRoot.Reset();
	Super::EndPlay(Reason);
}

void URoseUIManager::RegisterWindow(FName Id, FRoseUIWindowDef Def)
{
	if (Def.bOpenAtStart)
		bHasHUD = true;
	if (BoundInput && Def.ToggleKey.IsValid())
		BindToggleKey(Id, Def.ToggleKey);
	Defs.Add(Id, MoveTemp(Def));
}

void URoseUIManager::SetupInput(UInputComponent* Input)
{
	BoundInput = Input;
	for (const TPair<FName, FRoseUIWindowDef>& P : Defs)
		if (P.Value.ToggleKey.IsValid())
			BindToggleKey(P.Key, P.Value.ToggleKey);
	// The modern skill panel (K) and skill tree (X) are bound directly on the
	// character (ARoseCharacter::SetupPlayerInputComponent) — see ToggleSkillPanel
	// / ToggleSkillTree — since they are viewport overlays, not registered defs.
}

// Open/close a modern draggable overlay window: wrap Content in SRoseModernWindow
// and add it under an SConstraintCanvas whose Offset tracks the window's Position
// (title-bar drag).  Remembers the dragged position for the session.
void URoseUIManager::ToggleModernWindow(FName Id, const FText& Title, float Width,
	TSharedRef<SWidget> Content, TSharedPtr<SWidget>& Root, TSharedPtr<SRoseModernWindow>& Window)
{
	if (!GEngine || !GEngine->GameViewport)
		return;
	if (Root.IsValid())
	{
		if (Window.IsValid())
			DragMemory.Add(Id, Window->Position);
		GEngine->GameViewport->RemoveViewportWidgetContent(Root.ToSharedRef());
		Root.Reset();
		Window.Reset();
		UpdateInputMode();
		return;
	}

	TWeakObjectPtr<URoseUIManager> Weak(this);
	TSharedRef<SRoseModernWindow> Win = SNew(SRoseModernWindow)
		.Title(Title).Width(Width)
		.OnClose(FSimpleDelegate::CreateLambda([Weak, Id]() {
			if (Weak.IsValid()) Weak->CloseModernWindow(Id);
		}))
		[ Content ];
	if (const FVector2D* Pos = DragMemory.Find(Id))
		Win->Position = *Pos;
	else
		Win->Position = FVector2D(360.f, 150.f);
	Window = Win;

	Root = SNew(SConstraintCanvas)
		+ SConstraintCanvas::Slot()
		.Offset_Lambda([Win]() { return FMargin(Win->Position.X, Win->Position.Y, 0.f, 0.f); })
		.Alignment(FVector2D(0, 0)).AutoSize(true)
		[ Win ];

	GEngine->GameViewport->AddViewportWidgetContent(Root.ToSharedRef(), 10);
	UpdateInputMode();
}

// A LAYOUT-DRIVEN window hosts itself: the converted dialog already draws its
// frame, caption and close button, and drags by its own title bar.  Wrapping it
// in SRoseModernWindow would draw a second window around the first.
void URoseUIManager::ToggleLayoutWindow(FName Id, TSharedRef<SWidget> Content,
                                        TSharedPtr<SWidget>& Root)
{
	if (!GEngine || !GEngine->GameViewport)
		return;
	if (Root.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Root.ToSharedRef());
		Root.Reset();
		UpdateInputMode();
		return;
	}

	const FVector2D* Pos = DragMemory.Find(Id);
	const FVector2D At = Pos ? *Pos : FVector2D(360.f, 150.f);
	Root = SNew(SConstraintCanvas)
		+ SConstraintCanvas::Slot()
		.Offset(FMargin(At.X, At.Y, 0.f, 0.f))
		.Alignment(FVector2D(0, 0)).AutoSize(true)
		[ Content ];

	GEngine->GameViewport->AddViewportWidgetContent(Root.ToSharedRef(), 10);
	UpdateInputMode();
}

void URoseUIManager::CloseModernWindow(FName Id)
{
	// Layout-hosted windows have no SRoseModernWindow to look up.
	if (Id == TEXT("questjournal") && QuestJournalRoot.IsValid())
	{
		if (GEngine && GEngine->GameViewport)
			GEngine->GameViewport->RemoveViewportWidgetContent(QuestJournalRoot.ToSharedRef());
		QuestJournalRoot.Reset();
		UpdateInputMode();
		return;
	}
	if (Id == TEXT("skillpanel") && SkillPanelRoot.IsValid())
	{
		if (SkillPanelWindow.IsValid()) DragMemory.Add(Id, SkillPanelWindow->Position);
		GEngine->GameViewport->RemoveViewportWidgetContent(SkillPanelRoot.ToSharedRef());
		SkillPanelRoot.Reset(); SkillPanelWindow.Reset();
	}
	else if (Id == TEXT("skilltree") && SkillTreeRoot.IsValid())
	{
		if (SkillTreeWindow.IsValid()) DragMemory.Add(Id, SkillTreeWindow->Position);
		GEngine->GameViewport->RemoveViewportWidgetContent(SkillTreeRoot.ToSharedRef());
		SkillTreeRoot.Reset(); SkillTreeWindow.Reset();
	}
	else if (Id == TEXT("options") && OptionsRoot.IsValid())
	{
		if (OptionsWindow.IsValid()) DragMemory.Add(Id, OptionsWindow->Position);
		GEngine->GameViewport->RemoveViewportWidgetContent(OptionsRoot.ToSharedRef());
		OptionsRoot.Reset(); OptionsWindow.Reset();
	}
	else if (Id == TEXT("questjournal") && QuestJournalRoot.IsValid())
	{
		if (QuestJournalWindow.IsValid()) DragMemory.Add(Id, QuestJournalWindow->Position);
		GEngine->GameViewport->RemoveViewportWidgetContent(QuestJournalRoot.ToSharedRef());
		QuestJournalRoot.Reset(); QuestJournalWindow.Reset();
	}
	UpdateInputMode();
}

void URoseUIManager::ToggleSkillPanel()
{
	ToggleModernWindow(TEXT("skillpanel"), FText::FromString(TEXT("Skills")), 376.f,
		RoseSkillPanel_Make(*this), SkillPanelRoot, SkillPanelWindow);
}

void URoseUIManager::ToggleSkillTree()
{
	ToggleModernWindow(TEXT("skilltree"), FText::FromString(RoseSkillTree_Title(*this)), 640.f,
		RoseSkillTree_MakeContent(*this), SkillTreeRoot, SkillTreeWindow);
}

void URoseUIManager::FocusChat()
{
	RoseChat_FocusInput();
}

void URoseUIManager::ToggleOptions()
{
	ToggleModernWindow(TEXT("options"), FText::FromString(TEXT("Options")), 380.f,
		RoseOptions_MakeContent(*this), OptionsRoot, OptionsWindow);
}

void URoseUIManager::ToggleQuestJournal()
{
	// dlgquest draws its own frame, caption and close button.
	ToggleLayoutWindow(TEXT("questjournal"), RoseQuestJournal_MakeContent(*this),
		QuestJournalRoot);
}

void URoseUIManager::ToggleGripTuner()
{
	extern TSharedRef<SWidget> RoseGripTuner_MakeContent(ARoseCharacter& Char);
	ARoseCharacter* C = GetRoseCharacter();
	if (!C)
		return;
	ToggleModernWindow(TEXT("griptuner"), FText::FromString(TEXT("Weapon Grip")), 320.f,
		RoseGripTuner_MakeContent(*C), GripTunerRoot, GripTunerWindow);
}

void URoseUIManager::ToggleDevSpawn()
{
	ToggleModernWindow(TEXT("devspawn"), FText::FromString(TEXT("DEV — Spawn Item")), 360.f,
		RoseDevSpawn_MakeContent(*this), DevSpawnRoot, DevSpawnWindow);
}

void URoseUIManager::OpenNpcDialog(ARoseNpc* Npc)
{
	DialogNpc = Npc;
	if (!GEngine || !GEngine->GameViewport || !Npc || !Npc->HasDialog())
		return;
	if (DialogRoot.IsValid())   // one conversation at a time (client IsDlgOpened gate)
		return;

	TSharedPtr<FRoseDialogSession> Session =
		FRoseDialogSession::Start(Npc->ConStem, GetRoseCharacter(), Npc);
	if (!Session.IsValid() || Session->bClosed)
		return;   // gate check failed or the script had nothing to show
	Session->NpcName = Npc->GetDisplayName();

	ActiveDialog = Session;
	DialogRoot = SNew(SRoseDragPan)[ RoseDialog_Make(*this, Session) ];
	GEngine->GameViewport->AddViewportWidgetContent(DialogRoot.ToSharedRef(), 12);
	UpdateInputMode();
}

void URoseUIManager::CloseNpcDialog()
{
	DialogNpc = nullptr;
	if (DialogRoot.IsValid() && GEngine && GEngine->GameViewport)
		GEngine->GameViewport->RemoveViewportWidgetContent(DialogRoot.ToSharedRef());
	DialogRoot.Reset();
	ActiveDialog.Reset();
	UpdateInputMode();
}

void URoseUIManager::OpenStore(ARoseNpc* Npc)
{
	if (!GEngine || !GEngine->GameViewport || !Npc || !Npc->HasStore())
		return;
	CloseStore();   // replace any previous store
	StoreRoot = SNew(SRoseDragPan)[ RoseStore_Make(*this, Npc) ];
	GEngine->GameViewport->AddViewportWidgetContent(StoreRoot.ToSharedRef(), 13);
	UpdateInputMode();
}

void URoseUIManager::CloseStore()
{
	if (StoreRoot.IsValid() && GEngine && GEngine->GameViewport)
		GEngine->GameViewport->RemoveViewportWidgetContent(StoreRoot.ToSharedRef());
	StoreRoot.Reset();
	UpdateInputMode();
}

void URoseUIManager::ToggleMinimap()
{
	if (!GEngine || !GEngine->GameViewport)
		return;
	if (MinimapRoot.IsValid())
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(MinimapRoot.ToSharedRef());
		MinimapRoot.Reset();
	}
	else
	{
		MinimapRoot = SNew(SConstraintCanvas)
			+ SConstraintCanvas::Slot()
			.Anchors(FAnchors(1, 0)).Alignment(FVector2D(1, 0))
			.AutoSize(true).Offset(FMargin(8, 8, 0, 0))
			[ SNew(SRoseDragPan)[ RoseMinimap_Make(*this) ] ];
		GEngine->GameViewport->AddViewportWidgetContent(MinimapRoot.ToSharedRef(), 6);
	}
}

void URoseUIManager::BindToggleKey(FName Id, const FKey& Key)
{
	if (!BoundInput || BoundKeys.Contains(Id))
		return;
	BoundKeys.Add(Id);
	FInputKeyBinding KB(FInputChord(Key), IE_Pressed);
	KB.bConsumeInput = true;
	KB.KeyDelegate.GetDelegateForManualSet().BindWeakLambda(this, [this, Id]() {
		Toggle(Id);
	});
	BoundInput->KeyBindings.Emplace(MoveTemp(KB));
}

void URoseUIManager::Toggle(FName Id)
{
	if (IsOpen(Id))
		Close(Id);
	else
		Open(Id);
}

TSharedPtr<SRoseUIWindow> URoseUIManager::GetWindow(FName Id) const
{
	const FOpenWindow* W = OpenWindows.Find(Id);
	return W ? W->Window : nullptr;
}

void URoseUIManager::Open(FName Id)
{
	if (IsOpen(Id) || !GEngine || !GEngine->GameViewport)
		return;
	const FRoseUIWindowDef* Def = Defs.Find(Id);
	if (!Def)
		return;

	TSharedPtr<SWidget> Content = Def->BuildContent ? Def->BuildContent(*this) : nullptr;

	TWeakObjectPtr<URoseUIManager> Weak(this);
	const bool bHasButton = (bool)Def->OnButton;
	FOpenWindow W;
	W.Window = SNew(SRoseUIWindow)
		.Dialog(Def->Dialog)
		.Content(Content)
		.OnClose(FSimpleDelegate::CreateLambda([Weak, Id]() {
			if (Weak.IsValid())
				Weak->Close(Id);
		}))
		.OnButton(FOnRoseUIButton::CreateLambda([Weak, Id, bHasButton](const FString& Label) {
			if (!bHasButton || !Weak.IsValid())
				return;
			if (const FRoseUIWindowDef* D = Weak->Defs.Find(Id))
				if (D->OnButton)
					D->OnButton(*Weak.Get(), Label);
		}));

	// Faithful placement: the client positions each dialog from its XML root's
	// DEFAULT_X/DEFAULT_Y enum (LEFT/CENTER/RIGHT × TOP/CENTER/BOTTOM) + ADJUST_X/Y
	// relative to the live screen (IT_MGR::InitInterfacePos).  The manager reads
	// those straight from the converted layout — no per-window guessing.
	if (TSharedPtr<FJsonObject> Root = SRoseUIWindow::LoadLayout(Def->Dialog))
	{
		double V;
		W.Window->AnchorX = Root->TryGetNumberField(TEXT("default_x"), V) ? (int32)V : 0;
		W.Window->AnchorY = Root->TryGetNumberField(TEXT("default_y"), V) ? (int32)V : 0;
		W.Window->Adjust.X = Root->TryGetNumberField(TEXT("adjust_x"), V) ? (float)V : 0.f;
		W.Window->Adjust.Y = Root->TryGetNumberField(TEXT("adjust_y"), V) ? (float)V : 0.f;
	}
	if (const FVector2D* Drag = DragMemory.Find(Id))
		W.Window->DragDelta = *Drag;   // restore a prior session drag
	// Place via SConstraintCanvas ANCHORS (DPI-correct) rather than pixel math:
	// the enum → a corner/centre fraction used for BOTH the anchor point and the
	// window's alignment, so a fraction of 1 pins the window's right/bottom edge
	// to the screen's right/bottom (exactly IT_MGR::InitInterfacePos).  Offset =
	// ADJUST + the user's drag.  (GetViewportSize returns PIXELS while the canvas
	// works in Slate units — mixing them threw right/bottom windows off-screen.)
	const FVector2D Frac(W.Window->AnchorX * 0.5f, W.Window->AnchorY * 0.5f);
	TSharedPtr<SRoseUIWindow> Win = W.Window;   // slot lambda keeps it alive
	W.Root = SNew(SConstraintCanvas)
		+ SConstraintCanvas::Slot()
		.Anchors(FAnchors(Frac.X, Frac.Y))
		.Alignment(Frac)
		.AutoSize(true)
		.Offset_Lambda([Win]() {
			const FVector2D O = Win->Adjust + Win->DragDelta;
			return FMargin(O.X, O.Y, 0.f, 0.f);   // AutoSize → size from the widget
		})
		[ Win.ToSharedRef() ];

	GEngine->GameViewport->AddViewportWidgetContent(W.Root.ToSharedRef(), Def->ZOrder);
	OpenWindows.Add(Id, MoveTemp(W));
	UpdateInputMode();
}

void URoseUIManager::Close(FName Id)
{
	FOpenWindow W;
	if (!OpenWindows.RemoveAndCopyValue(Id, W))
		return;
	if (GEngine && GEngine->GameViewport && W.Root.IsValid())
		GEngine->GameViewport->RemoveViewportWidgetContent(W.Root.ToSharedRef());
	// Persist the dragged position for the session (faithful anchor re-applies
	// on reopen; only the user's drag delta is remembered).
	if (W.Window.IsValid())
		DragMemory.Add(Id, W.Window->DragDelta);
	UpdateInputMode();
}

bool URoseUIManager::IsDefaultVisible(const FString& Dialog)
{
	if (TSharedPtr<FJsonObject> Root = SRoseUIWindow::LoadLayout(Dialog))
	{
		double V;
		return Root->TryGetNumberField(TEXT("default_visible"), V) && V != 0.0;
	}
	return false;
}

void URoseUIManager::Reopen(FName Id)
{
	if (!IsOpen(Id))
		return;
	Close(Id);   // Close stashes the dragged position back into the def
	Open(Id);
}

void URoseUIManager::ToggleGeneric(const FString& Dialog)
{
	const FName Id(*FString::Printf(TEXT("generic_%s"), *Dialog));
	if (!Defs.Contains(Id))
	{
		FRoseUIWindowDef Def;
		Def.Dialog = Dialog;
		Def.Offset = FVector2D(240, 140);
		Defs.Add(Id, MoveTemp(Def));
	}
	Toggle(Id);
}

void URoseUIManager::UpdateInputMode()
{
	if (ARoseCharacter* C = GetRoseCharacter())
		C->UpdateUIInputMode();
}
