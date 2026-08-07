#include "RoseCharSelectHUD.h"

#include "Engine/GameViewportClient.h"
#include "Engine/Engine.h"
#include "GameFramework/PlayerController.h"
#include "Framework/Application/SlateApplication.h"

void ARoseCharSelectHUD::BeginPlay()
{
	Super::BeginPlay();

	if (!GEngine || !GEngine->GameViewport)
		return;

	// Login first.  It calls ShowCharacterSelect() when the player is signed in
	// — or immediately, via "Play Offline", when no backend is configured.
	Root = RoseLogin_Make(this);
	GEngine->GameViewport->AddViewportWidgetContent(Root.ToSharedRef(), 20);

	if (APlayerController* PC = GetOwningPlayerController())
	{
		PC->bShowMouseCursor = true;
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		PC->SetInputMode(Mode);
	}
}

void ARoseCharSelectHUD::ShowCharacterSelect()
{
	if (!GEngine || !GEngine->GameViewport)
		return;

	ClearRoot();
	Root = RoseCharSelect_Make(this);
	GEngine->GameViewport->AddViewportWidgetContent(Root.ToSharedRef(), 20);
}

void ARoseCharSelectHUD::ClearRoot()
{
	if (Root.IsValid() && GEngine && GEngine->GameViewport)
		GEngine->GameViewport->RemoveViewportWidgetContent(Root.ToSharedRef());
	Root.Reset();
}

void ARoseCharSelectHUD::EndPlay(const EEndPlayReason::Type Reason)
{
	ClearRoot();
	Super::EndPlay(Reason);
}
