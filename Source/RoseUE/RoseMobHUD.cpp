#include "RoseMobHUD.h"

#include "RoseCharacter.h"
#include "RoseMonster.h"

#include "Components/SkeletalMeshComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "EngineUtils.h"
#include "Camera/PlayerCameraManager.h"
#include "GameFramework/PlayerController.h"

namespace
{
	// ROSE-style palette: red gauge, gold selection ring.
	const FLinearColor kBarFill(0.80f, 0.10f, 0.08f, 0.95f);
	const FLinearColor kBarBack(0.05f, 0.05f, 0.05f, 0.60f);
	const FLinearColor kBorder(0.f, 0.f, 0.f, 0.70f);
	const FLinearColor kBorderTarget(1.f, 0.80f, 0.15f, 0.90f);
	const FLinearColor kName(1.f, 1.f, 1.f, 0.95f);
	const FLinearColor kNameAggro(1.f, 0.45f, 0.40f, 0.95f);
	const FLinearColor kNameTarget(1.f, 0.85f, 0.30f, 1.f);
	const FLinearColor kShadow(0.f, 0.f, 0.f, 0.80f);
}

void ARoseMobHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas || !PlayerOwner || !PlayerOwner->PlayerCameraManager)
		return;

	ARoseCharacter* Player = Cast<ARoseCharacter>(GetOwningPawn());
	ARoseMonster* Target = Player ? Player->GetTargetMonster() : nullptr;
	const FVector CamLoc = PlayerOwner->PlayerCameraManager->GetCameraLocation();

	for (TActorIterator<ARoseMonster> It(GetWorld()); It; ++It)
	{
		ARoseMonster* M = *It;
		// No plate for corpses or mobs that never initialized (empty name).
		if (!M || M->IsDead() || M->GetDisplayName().IsEmpty())
			continue;
		if (FVector::Dist(CamLoc, M->GetActorLocation()) > PlateDrawDistance)
			continue;
		DrawMobPlate(M, M == Target);
	}
}

void ARoseMobHUD::DrawMobPlate(ARoseMonster* Mob, bool bIsTarget)
{
	// Anchor just above the mesh's bounds so big and small mobs both clear it.
	const FBoxSphereBounds B = Mob->GetMesh()->Bounds;
	const FVector Anchor = B.Origin + FVector(0.f, 0.f, B.BoxExtent.Z + 18.f);
	const FVector S = Project(Anchor);
	if (S.Z <= 0.f)   // behind the camera
		return;

	const float BarW = bIsTarget ? 84.f : 68.f;
	const float BarH = bIsTarget ? 9.f : 7.f;
	const float BarX = S.X - BarW * 0.5f;
	const float BarY = S.Y;
	const float Frac = Mob->GetMaxHP() > 0.f
		? FMath::Clamp(Mob->GetHP() / Mob->GetMaxHP(), 0.f, 1.f) : 0.f;

	// Border → track → fill (fill drains left-to-right like the classic gauge).
	DrawRect(bIsTarget ? kBorderTarget : kBorder, BarX - 1.f, BarY - 1.f, BarW + 2.f, BarH + 2.f);
	DrawRect(kBarBack, BarX, BarY, BarW, BarH);
	if (Frac > 0.f)
		DrawRect(kBarFill, BarX, BarY, BarW * Frac, BarH);

	// "<name> Lv<n>" centred above the bar, with a 1px drop shadow.
	UFont* NameFont = GEngine ? GEngine->GetMediumFont() : nullptr;
	if (NameFont)
	{
		const FString Label = FString::Printf(TEXT("%s Lv%d"), *Mob->GetDisplayName(), Mob->Level);
		float TW = 0.f, TH = 0.f;
		GetTextSize(Label, TW, TH, NameFont);
		const float TX = S.X - TW * 0.5f;
		const float TY = BarY - TH - 3.f;
		const FLinearColor Col = bIsTarget ? kNameTarget
			: Mob->IsAggroed() ? kNameAggro : kName;
		DrawText(Label, kShadow, TX + 1.f, TY + 1.f, NameFont);
		DrawText(Label, Col, TX, TY, NameFont);
	}

	// The target also shows the exact HP under its bar (ROSE target gauge).
	UFont* HPFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (bIsTarget && HPFont)
	{
		const FString HPText = FString::Printf(TEXT("%d / %d"),
			FMath::CeilToInt(Mob->GetHP()), FMath::CeilToInt(Mob->GetMaxHP()));
		float TW = 0.f, TH = 0.f;
		GetTextSize(HPText, TW, TH, HPFont);
		const float TX = S.X - TW * 0.5f;
		const float TY = BarY + BarH + 2.f;
		DrawText(HPText, kShadow, TX + 1.f, TY + 1.f, HPFont);
		DrawText(HPText, kName, TX, TY, HPFont);
	}
}
