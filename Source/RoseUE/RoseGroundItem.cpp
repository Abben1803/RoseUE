#include "RoseGroundItem.h"

#include "Components/StaticMeshComponent.h"
#include "Components/TextRenderComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"
#include "Net/UnrealNetwork.h"

ARoseGroundItem::ARoseGroundItem()
{
	PrimaryActorTick.bCanEverTick = true;

	// Loot is spawned by the server (ARoseMonster::Die → SpawnMonsterLoot) and
	// must be visible to everyone.  Movement is NOT replicated: the spin and bob
	// are cosmetic and run identically on every machine from the spawn
	// transform, so they cost nothing on the wire.
	bReplicates = true;
	SetReplicateMovement(false);

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	SetRootComponent(Mesh);
	// A small engine cube as the loot marker (icon-in-world is a later polish).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeFinder.Succeeded())
		Mesh->SetStaticMesh(CubeFinder.Object);
	Mesh->SetRelativeScale3D(FVector(0.18f, 0.18f, 0.18f));
	Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Mesh->SetCastShadow(false);

	Label = CreateDefaultSubobject<UTextRenderComponent>(TEXT("Label"));
	Label->SetupAttachment(Mesh);
	Label->SetRelativeLocation(FVector(0.f, 0.f, 40.f));
	Label->SetHorizontalAlignment(EHTA_Center);
	Label->SetWorldSize(18.f);
	Label->SetText(FText::GetEmpty());
	// Face the camera-ish; a proper billboard rotate is done in Tick.
}

void ARoseGroundItem::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ARoseGroundItem, bIsMoney);
	DOREPLIFETIME(ARoseGroundItem, Zuly);
	DOREPLIFETIME(ARoseGroundItem, Slot);
	DOREPLIFETIME(ARoseGroundItem, ItemId);
	DOREPLIFETIME(ARoseGroundItem, Count);
	DOREPLIFETIME(ARoseGroundItem, Bonus);
	DOREPLIFETIME(ARoseGroundItem, bAppraised);
	DOREPLIFETIME(ARoseGroundItem, DisplayName);
	DOREPLIFETIME(ARoseGroundItem, FieldModel);
}

void ARoseGroundItem::BeginPlay()
{
	Super::BeginPlay();
	BaseZ = GetActorLocation().Z;

	// The world label is decoration; a dedicated server has no renderer or
	// fonts and crashes driving a UTextRenderComponent.  The payload (which the
	// server DOES own) is unaffected.
	if (IsNetMode(NM_DedicatedServer) && Label)
	{
		Label->DestroyComponent();
		Label = nullptr;
	}

	RefreshVisual();
}

void ARoseGroundItem::InitMoney(int32 InZuly)
{
	bIsMoney = true;
	Zuly = InZuly;
	DisplayName = FString::Printf(TEXT("%d Zuly"), InZuly);
	RefreshVisual();
}

void ARoseGroundItem::InitItem(const FString& InSlot, int32 InId, int32 InCount, int32 InBonus,
                               bool bInAppraised, const FString& InName, int32 InFieldModel)
{
	bIsMoney = false;
	Slot = InSlot;
	ItemId = InId;
	Count = FMath::Max(1, InCount);
	Bonus = InBonus;
	bAppraised = bInAppraised;
	DisplayName = InName;
	FieldModel = InFieldModel;
	RefreshVisual();
}

void ARoseGroundItem::ApplyDropMesh()
{
	if (!Mesh || FieldModel <= 0)
		return;   // money, or an item whose row carries no field model — keep the cube

	// LIST_FIELDITEM row -> the mesh RoseImportEquipment built for it.  Loaded on
	// demand rather than preloaded: a zone can drop any of ~550 of these and only
	// a handful are ever on the ground at once.
	const FString Path = FString::Printf(
		TEXT("/Game/Rose/Equipment/FieldItem/SM_field_%d.SM_field_%d"),
		FieldModel, FieldModel);
	UStaticMesh* Drop = LoadObject<UStaticMesh>(nullptr, *Path);
	if (!Drop)
	{
		// 9 of the 559 referenced ids have no mesh in this build; the cube stands
		// in so the drop is still visible and pickable.
		UE_LOG(LogTemp, Verbose, TEXT("[Rose] no drop mesh %s"), *Path);
		return;
	}

	Mesh->SetStaticMesh(Drop);
	// The cube was scaled to 0.18 to read as a small marker; the ROSE field
	// models are already authored at world scale, so undo that.
	Mesh->SetRelativeScale3D(FVector::OneVector);
}

void ARoseGroundItem::RefreshVisual()
{
	// Before the Label guard: a dedicated server destroys Label in BeginPlay, and
	// the mesh swap must still run everywhere the payload lands.
	ApplyDropMesh();

	if (!Label)
		return;

	// Sit the label just above whatever mesh we ended up with — the authored
	// field models vary in height and the cube's fixed 40 buries the text in the
	// taller ones.
	if (Mesh && Mesh->GetStaticMesh())
	{
		const FBoxSphereBounds B = Mesh->GetStaticMesh()->GetBounds();
		const float TopZ = (B.Origin.Z + B.BoxExtent.Z) * Mesh->GetRelativeScale3D().Z;
		Label->SetRelativeLocation(FVector(0.f, 0.f, TopZ + 14.f));
	}
	FString Text = DisplayName;
	if (!bIsMoney)
	{
		if (Count > 1) Text += FString::Printf(TEXT(" x%d"), Count);
		if (Bonus > 0) Text += FString::Printf(TEXT(" (+%d)"), Bonus);
	}
	Label->SetText(FText::FromString(Text));
	// Colour: gold for money, cyan-ish for bonus items, white otherwise.
	const FColor C = bIsMoney ? FColor(255, 210, 90)
		: (Bonus > 0 ? FColor(140, 210, 255) : FColor(235, 235, 235));
	Label->SetTextRenderColor(C);
	if (Mesh)
	{
		const FLinearColor LC(C);
		Mesh->SetVectorParameterValueOnMaterials(TEXT("Color"), FVector(LC.R, LC.G, LC.B));
	}
}

void ARoseGroundItem::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Age += DeltaSeconds;
	// Only the server despawns; the destroy replicates to every client.  (A
	// client destroying its own copy would leave the drop alive on the server
	// and un-lootable.)
	if (Age >= Lifetime && HasAuthority())
	{
		Destroy();
		return;
	}
	// Gentle spin + bob so drops are easy to spot.
	BobPhase += DeltaSeconds;
	AddActorLocalRotation(FRotator(0.f, 90.f * DeltaSeconds, 0.f));
	FVector L = GetActorLocation();
	L.Z = BaseZ + 6.f + 4.f * FMath::Sin(BobPhase * 3.f);
	SetActorLocation(L);

	// Keep the label facing the local camera.
	if (Label)
	{
		if (APlayerController* PC = GetWorld() ? GetWorld()->GetFirstPlayerController() : nullptr)
		{
			FVector CamLoc; FRotator CamRot;
			PC->GetPlayerViewPoint(CamLoc, CamRot);
			const FVector Dir = (GetActorLocation() - CamLoc);
			Label->SetWorldRotation(Dir.Rotation());
		}
	}
}
