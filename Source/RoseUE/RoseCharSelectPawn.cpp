#include "RoseCharSelectPawn.h"
#include "RoseCharSelectDirector.h"
#include "RoseCharSlotSave.h"
#include "RoseGameInstance.h"

#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h" // BindKey
#include "InputCoreTypes.h"            // EKeys
#include "EngineUtils.h"               // TActorIterator
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"  // FindLookAtRotation

ARoseCharSelectPawn::ARoseCharSelectPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	RootComponent = Camera;
}

void ARoseCharSelectPawn::BeginPlay()
{
	Super::BeginPlay();

	UWorld* W = GetWorld();
	if (!W) return;

	for (TActorIterator<ARoseCharSelectDirector> It(W); It; ++It) { Director = *It; break; }

	// Seed the framing so we don't interp from the origin on the first frame.
	FVector Loc, Look;
	if (Director.IsValid() && Director->GetDesiredFraming(Loc, Look))
	{
		LookTarget = Look;
		SetActorLocationAndRotation(Loc, UKismetMathLibrary::FindLookAtRotation(Loc, Look));
	}
}

void ARoseCharSelectPawn::Tick(float Dt)
{
	Super::Tick(Dt);
	Elapsed += Dt;

	FVector DesiredLoc, DesiredLook;
	if (Director.IsValid() && Director->GetDesiredFraming(DesiredLoc, DesiredLook))
	{
		const FVector NewLoc = FMath::VInterpTo(GetActorLocation(), DesiredLoc, Dt, MoveInterpSpeed);
		LookTarget = FMath::VInterpTo(LookTarget, DesiredLook, Dt, LookInterpSpeed);
		const FRotator NewRot = UKismetMathLibrary::FindLookAtRotation(NewLoc, LookTarget);
		SetActorLocationAndRotation(NewLoc, NewRot);
	}
	else
	{
		Orbit(Dt);
	}
}

void ARoseCharSelectPawn::SetupPlayerInputComponent(UInputComponent* Input)
{
	Super::SetupPlayerInputComponent(Input);
	if (Input)
		Input->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &ARoseCharSelectPawn::OnClick);
}

void ARoseCharSelectPawn::OnClick()
{
	if (!Director.IsValid() || Director->IsCreateMode()) return;

	APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr;
	if (!PC) return;

	float MX, MY;
	if (!PC->GetMousePosition(MX, MY)) return;

	int32 Index;
	if (!Director->PickAvatarAtScreen(MX, MY, Index)) return;

	const double Now = FPlatformTime::Seconds();
	const bool bDouble = (Index == LastClickIndex) && (Now - LastClickTime < 0.35);
	LastClickTime = Now;
	LastClickIndex = Index;

	Director->SelectIndex(Index);
	if (bDouble) EnterSelected();
}

void ARoseCharSelectPawn::EnterSelected()
{
	UWorld* W = GetWorld();
	if (!W || !Director.IsValid()) return;

	FRoseCharSlot Slot;
	if (!Director->GetSelectedSlot(Slot)) return;

	if (URoseGameInstance* GI = Cast<URoseGameInstance>(UGameplayStatics::GetGameInstance(W)))
		GI->EnterWorldAsNewCharacter(W, Slot.Gender, Slot.Hair, Slot.Face, Slot.Name);
}

void ARoseCharSelectPawn::Orbit(float /*Dt*/)
{
	const float Ang = FMath::DegreesToRadians(Elapsed * OrbitDegPerSec);
	const FVector Loc = FocusPoint + FVector(
		FMath::Cos(Ang) * OrbitRadius,
		FMath::Sin(Ang) * OrbitRadius,
		OrbitHeight);
	SetActorLocationAndRotation(Loc, UKismetMathLibrary::FindLookAtRotation(Loc, FocusPoint));
}
