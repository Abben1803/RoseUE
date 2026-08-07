#include "RoseLoadingScreen.h"
#include "RoseUITheme.h"

#include "Engine/Texture2D.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Colors/SColorBlock.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Images/SThrobber.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScaleBox.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	const TCHAR* kTips[] = {
		TEXT("Tip: Press Spacebar to pick up nearby loot."),
		TEXT("Tip: A bow scales its damage off DEX, a wand off INT."),
		TEXT("Tip: Right-drag to orbit the camera, mouse wheel to zoom."),
		TEXT("Tip: Talk to town NPCs to pick up quests."),
		TEXT("Tip: Press Q for your quest journal, I for your bag."),
		TEXT("Tip: Higher-level monsters give more experience."),
	};

	FSlateFontInfo Font(int32 Size, bool bBold = false)
	{
		return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
	}

	// Classic per-continent loading art (Content/UI/Loading, imported from
	// CONTROL/LOADING/{ELDEON,JUNON,LUNAR}.DDS).  Chosen by zone prefix:
	// E* = Eldeon, L* = Lunar, everything else (J*, AGIT, TITLE, SUM) = Junon.
	// Brushes + textures are cached/rooted for the session (Slate has no UObject
	// owner to keep the texture alive).  Returns null if the art isn't imported
	// yet — the caller falls back to the plain dark backdrop.
	const FSlateBrush* PlanetBrush(const FString& ZoneName)
	{
		FString Planet = TEXT("JUNON");
		if (ZoneName.Len() > 0)
		{
			const TCHAR C = FChar::ToUpper(ZoneName[0]);
			if (C == TEXT('E')) Planet = TEXT("ELDEON");
			else if (C == TEXT('L')) Planet = TEXT("LUNAR");
		}

		static TMap<FString, TSharedPtr<FSlateBrush>> Cache;
		if (TSharedPtr<FSlateBrush>* Found = Cache.Find(Planet))
			return Found->IsValid() ? Found->Get() : nullptr;

		TSharedPtr<FSlateBrush> Brush;
		const FString Path = FString::Printf(
			TEXT("/Game/UI/Loading/LOADING_%s.LOADING_%s"), *Planet, *Planet);
		if (UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *Path))
		{
			Tex->AddToRoot();
			Brush = MakeShared<FSlateBrush>();
			Brush->SetResourceObject(Tex);
			Brush->ImageSize = FVector2D(Tex->GetSizeX(), Tex->GetSizeY());
			Brush->DrawAs = ESlateBrushDrawType::Image;
		}
		Cache.Add(Planet, Brush);
		return Brush.IsValid() ? Brush.Get() : nullptr;
	}
}

TSharedRef<SWidget> RoseLoadingScreen_Make(const FString& ZoneName)
{
	const FString Tip = kTips[FMath::RandRange(0, (int32)UE_ARRAY_COUNT(kTips) - 1)];
	const FSlateBrush* Bg = PlanetBrush(ZoneName);

	TSharedRef<SOverlay> Root = SNew(SOverlay);

	// Base dark backdrop (also the fallback when art isn't imported).
	Root->AddSlot()
	[ SNew(SColorBlock).Color(FLinearColor(0.02f, 0.025f, 0.035f, 1.f)) ];

	// Continent art, scaled to cover the screen.
	if (Bg)
		Root->AddSlot()
		[ SNew(SScaleBox).Stretch(EStretch::ScaleToFill)[ SNew(SImage).Image(Bg) ] ];

	// Legibility scrim over the art.
	Root->AddSlot()
	[ SNew(SColorBlock).Color(FLinearColor(0.f, 0.f, 0.f, 0.45f)) ];

	// Centred title + spinner.
	Root->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Center)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[ SNew(STextBlock).Font(Font(13)).ColorAndOpacity(RoseTheme::TextDim)
		  .Text(FText::FromString(TEXT("ENTERING"))) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 6, 0, 0)
		[ SNew(STextBlock).Font(Font(34, true)).ColorAndOpacity(RoseTheme::Gold)
		  .Text(FText::FromString(ZoneName)) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 24, 0, 0)
		[ SNew(SThrobber).NumPieces(7) ]
	];

	// Loading label + tip, bottom-centre.
	Root->AddSlot().HAlign(HAlign_Center).VAlign(VAlign_Bottom).Padding(0, 0, 0, 48)
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center)
		[ SNew(STextBlock).Font(Font(11)).ColorAndOpacity(RoseTheme::Text)
		  .Text(FText::FromString(TEXT("Loading..."))) ]
		+ SVerticalBox::Slot().AutoHeight().HAlign(HAlign_Center).Padding(0, 10, 0, 0)
		[ SNew(STextBlock).Font(Font(10)).ColorAndOpacity(RoseTheme::TextDim)
		  .Text(FText::FromString(Tip)) ]
	];

	return Root;
}
