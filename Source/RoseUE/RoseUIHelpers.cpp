#include "RoseUIHelpers.h"
#include "RoseUIWindow.h"

#include "Dom/JsonObject.h"
#include "HAL/IConsoleManager.h"
#include "Engine/Texture2D.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Styling/SlateBrush.h"
#include "Styling/SlateTypes.h"

namespace RoseUI
{

float Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, float Def)
{
	double V;
	return (O.IsValid() && O->TryGetNumberField(Field, V)) ? (float)V : Def;
}

FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Field)
{
	FString V;
	return (O.IsValid() && O->TryGetStringField(Field, V)) ? V : FString();
}

TSharedPtr<FJsonObject> LoadLayout(const FString& DialogName)
{
	return SRoseUIWindow::LoadLayout(DialogName);
}

static TSharedPtr<FJsonObject> FindNodeRec(const TSharedPtr<FJsonObject>& Node,
                                           const FString& Type, const FString& Match,
                                           FVector2D Base, FVector2D* OutAbsPos)
{
	if (!Node.IsValid())
		return nullptr;
	const FVector2D Pos = Base + FVector2D(
		Num(Node, TEXT("x")) + Num(Node, TEXT("offsetx")),
		Num(Node, TEXT("y")) + Num(Node, TEXT("offsety")));
	if (Str(Node, TEXT("type")) == Type)
	{
		const FString Tag = Str(Node, TEXT("label")) + Str(Node, TEXT("gid"))
			+ Str(Node, TEXT("normalgid"));
		if (Match.IsEmpty() || Tag.Contains(Match))
		{
			if (OutAbsPos)
				*OutAbsPos = Pos;
			return Node;
		}
	}
	const TArray<TSharedPtr<FJsonValue>>* Kids;
	if (Node->TryGetArrayField(TEXT("children"), Kids))
		for (const TSharedPtr<FJsonValue>& K : *Kids)
			if (TSharedPtr<FJsonObject> R = FindNodeRec(K->AsObject(), Type, Match, Pos, OutAbsPos))
				return R;
	return nullptr;
}

TSharedPtr<FJsonObject> FindNode(const TSharedPtr<FJsonObject>& Root,
                                 const FString& Type, const FString& Match,
                                 FVector2D* OutAbsPos)
{
	// The root's own x/y are 0 — children accumulate from there (AddNode math).
	return FindNodeRec(Root, Type, Match, FVector2D::ZeroVector, OutAbsPos);
}

static void FindNodesRec(const TSharedPtr<FJsonObject>& Node, const FString& Type,
                         FVector2D Base, TArray<TPair<TSharedPtr<FJsonObject>, FVector2D>>& Out)
{
	if (!Node.IsValid())
		return;
	const FVector2D Pos = Base + FVector2D(
		Num(Node, TEXT("x")) + Num(Node, TEXT("offsetx")),
		Num(Node, TEXT("y")) + Num(Node, TEXT("offsety")));
	if (Str(Node, TEXT("type")) == Type)
		Out.Emplace(Node, Pos);
	const TArray<TSharedPtr<FJsonValue>>* Kids;
	if (Node->TryGetArrayField(TEXT("children"), Kids))
		for (const TSharedPtr<FJsonValue>& K : *Kids)
			FindNodesRec(K->AsObject(), Type, Pos, Out);
}

void FindNodes(const TSharedPtr<FJsonObject>& Root, const FString& Type,
               TArray<TPair<TSharedPtr<FJsonObject>, FVector2D>>& Out)
{
	FindNodesRec(Root, Type, FVector2D::ZeroVector, Out);
}

TSharedPtr<FSlateBrush> MakeTextureBrush(const FString& AssetPath, float W, float H,
                                         TArray<TStrongObjectPtr<UTexture2D>>& KeepTextures)
{
	UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *AssetPath);
	if (!Tex)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseUI] texture missing: %s"), *AssetPath);
		return nullptr;
	}
	KeepTextures.Emplace(Tex);
	TSharedPtr<FSlateBrush> B = MakeShared<FSlateBrush>();
	B->SetResourceObject(Tex);
	B->ImageSize = FVector2D(W > 0 ? W : Tex->GetSizeX(), H > 0 ? H : Tex->GetSizeY());
	return B;
}

TSharedPtr<FSlateBrush> MakeSpriteBrush(const FString& Gid, float W, float H,
                                        TArray<TStrongObjectPtr<UTexture2D>>& KeepTextures)
{
	if (Gid.IsEmpty())
		return nullptr;
	// Sprite assets are named by sanitized GID (tools/build_ui_sprites.py safe()).
	FString Safe = Gid;
	for (TCHAR& C : Safe)
		if (!FChar::IsAlnum(C) && C != TEXT('_'))
			C = TEXT('_');
	return MakeTextureBrush(
		FString::Printf(TEXT("/Game/UI/Sprites/%s.%s"), *Safe, *Safe), W, H, KeepTextures);
}

// Glass skin on/off.  The modern windows are ours (flat rounded panels from
// RoseUITheme); the glass skin re-chromes them with the GEN_WND nine-slices
// without touching any window's contents, so it has to be reversible at
// runtime for comparison.  `rose.GlassUI 0` restores the flat theme.
static TAutoConsoleVariable<int32> CVarGlassUI(
	TEXT("rose.GlassUI"), 1,
	TEXT("1 = chrome the modern windows with the glass GEN_WND nine-slices, 0 = flat theme panels."),
	ECVF_Default);

bool UseGlassSkin()
{
	return CVarGlassUI.GetValueOnAnyThread() != 0;
}

// A NINE-SLICE window frame from the glass UI.
//
// The glass skin draws every window as GEN_WNDxx_TL/TC/TR/CL/CC/CR/BL/BC/BR —
// 23px corners with 1px stretchable edges.  build_ui_sprites.py emits the union
// of those nine rects as <PREFIX>_BOX plus the corner size, so Slate can do the
// slicing itself with DrawAs=Box: one draw call and pixel-exact corners at any
// window size, instead of compositing nine images per window.
//
// The margin is read from sprites.json rather than hardcoded, because the
// frames are NOT all 23px — GEN_WND06/07 are 7, GEN_WND05/08 are 14 and
// GEN_WND11 is 30.
TSharedPtr<FSlateBrush> MakeWindowFrameBrush(const FString& Prefix,
                                             TArray<TStrongObjectPtr<UTexture2D>>& KeepTextures)
{
	if (Prefix.IsEmpty())
		return nullptr;

	const FString Gid = Prefix + TEXT("_BOX");

	// sprites.json carries each sprite's size and, for the frames we emit, its
	// corner margin.  Loaded once and cached — this runs per window build.
	static TSharedPtr<FJsonObject> Manifest;
	static bool bTried = false;
	if (!bTried)
	{
		bTried = true;
		FString Raw;
		const FString Path = FPaths::ProjectContentDir() / TEXT("UI/sprites.json");
		if (FFileHelper::LoadFileToString(Raw, *Path))
		{
			const TSharedRef<TJsonReader<>> R = TJsonReaderFactory<>::Create(Raw);
			FJsonSerializer::Deserialize(R, Manifest);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoseUI] sprites.json not found at %s"), *Path);
		}
	}

	FVector2D Size = FVector2D::ZeroVector;
	FMargin Corner;
	bool bHaveMargin = false;
	if (Manifest.IsValid())
	{
		const TSharedPtr<FJsonObject>* Rec = nullptr;
		if (Manifest->TryGetObjectField(Gid, Rec) && Rec && Rec->IsValid())
		{
			const TArray<TSharedPtr<FJsonValue>>* Sz = nullptr;
			if ((*Rec)->TryGetArrayField(TEXT("size"), Sz) && Sz && Sz->Num() == 2)
				Size = FVector2D((*Sz)[0]->AsNumber(), (*Sz)[1]->AsNumber());

			const TArray<TSharedPtr<FJsonValue>>* Mg = nullptr;
			if ((*Rec)->TryGetArrayField(TEXT("margin"), Mg) && Mg && Mg->Num() == 2
				&& Size.X > 0.f && Size.Y > 0.f)
			{
				// Slate margins are FRACTIONS of the image, not pixels.
				const float MX = (*Mg)[0]->AsNumber() / Size.X;
				const float MY = (*Mg)[1]->AsNumber() / Size.Y;
				Corner = FMargin(MX, MY, MX, MY);
				bHaveMargin = true;
			}
		}
	}

	// No margin means this is not one of OUR generated frames — a few glass
	// sprites are natively named "..._BOX" (CLANWARBOSS_HP_BOX).  Guessing a
	// corner size there would smear the art, so refuse rather than mis-slice.
	if (!bHaveMargin)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoseUI] no nine-slice margin for %s"), *Gid);
		return nullptr;
	}

	TSharedPtr<FSlateBrush> Brush = MakeSpriteBrush(Gid, Size.X, Size.Y, KeepTextures);
	if (!Brush.IsValid())
		return nullptr;

	Brush->DrawAs = ESlateBrushDrawType::Box;
	Brush->Margin = Corner;
	return Brush;
}

TSharedPtr<FSlateBrush> MakeIconBrush(int32 IconIdx, float Size,
                                      TArray<TStrongObjectPtr<UTexture2D>>& KeepTextures)
{
	if (IconIdx <= 0)
		return nullptr;
	// 5-digit padding: unpadded "icon_1023" would be UDIM-parsed on import.
	return MakeTextureBrush(
		FString::Printf(TEXT("/Game/UI/Icons/icon_%05d.icon_%05d"), IconIdx, IconIdx),
		Size, Size, KeepTextures);
}

TSharedPtr<FSlateBrush> MakeSkillIconBrush(int32 IconNo, float Size,
                                           TArray<TStrongObjectPtr<UTexture2D>>& KeepTextures)
{
	if (IconNo <= 0)
		return nullptr;
	return MakeTextureBrush(
		FString::Printf(TEXT("/Game/UI/SkillIcons/skillicon_%05d.skillicon_%05d"), IconNo, IconNo),
		Size, Size, KeepTextures);
}


// ── Shared frame cache ──────────────────────────────────────────────────────
//
// Every window wants the SAME handful of frames, so build each one once and
// hand out the shared brush.  That also solves texture lifetime: the frame
// textures are rooted here for the process rather than by each widget, so a
// window that owns no KeepTextures array (chat, dialog, login) can still be
// chromed.
namespace
{
	struct FFrameCache
	{
		TMap<FString, TSharedPtr<FSlateBrush>> Brushes;
		TArray<TStrongObjectPtr<UTexture2D>>   Textures;
	};

	FFrameCache& Frames()
	{
		static FFrameCache Cache;
		return Cache;
	}
}

TSharedPtr<FSlateBrush> GlassPanel(EPanelKind Kind)
{
	if (!UseGlassSkin())
		return nullptr;

	// Which nine-slice plays which role is not a guess — it is what the glass
	// dialogs themselves composite (see Content/UI/Layouts/dlgitem.json):
	// GEN_WND01 is the outer window, GEN_WND04 the body panel starting under
	// the title band, GEN_WND06 the small inset panes (7px corners).
	const TCHAR* Prefix =
		Kind == EPanelKind::Window ? TEXT("GEN_WND01") :
		Kind == EPanelKind::Body   ? TEXT("GEN_WND04") :
		                             TEXT("GEN_WND06");

	FFrameCache& Cache = Frames();
	if (TSharedPtr<FSlateBrush>* Hit = Cache.Brushes.Find(Prefix))
		return *Hit;

	// A null result is cached too — otherwise a missing sprite re-warns on
	// every window open.
	TSharedPtr<FSlateBrush> Brush = MakeWindowFrameBrush(Prefix, Cache.Textures);
	Cache.Brushes.Add(Prefix, Brush);
	return Brush;
}


// ── Glass buttons ───────────────────────────────────────────────────────────
//
// The glass skin authors buttons as CONSECUTIVE TRIPLETS — GEN_BTNn is normal,
// n+1 hovered (bright outer glow), n+2 pressed (darkened, inner glow).  That is
// not a guess: every BUTTON in the converted layouts lists them in exactly that
// order as normalgid/overgid/downgid.
//
// Our windows draw their button plate with an inner SBorder and give the
// SButton itself "NoBorder", which throws the hover and pressed art away.  A
// real FButtonStyle puts it back, so buttons light up the way ROSE's do.
//
// The wide plates are 3-SLICED horizontally: the rounded ends are ~8px and the
// middle is a smooth lengthwise sweep, so stretching only the middle keeps the
// ends crisp at any width.  A zero vertical margin means the art scales to the
// button's height instead of being sliced — correct here, because the plate is
// a single vertical gradient with a 5px glow margin baked in.
static TSharedPtr<FSlateBrush> ButtonBrush(int32 First, int32 Offset, float CapPx,
                                           TArray<TStrongObjectPtr<UTexture2D>>& Keep)
{
	const FString Gid = FString::Printf(TEXT("GEN_BTN%02d"), First + Offset);
	TSharedPtr<FSlateBrush> B = MakeSpriteBrush(Gid, 0.f, 0.f, Keep);   // 0 = natural size
	if (B.IsValid() && CapPx > 0.f && B->ImageSize.X > 2.f * CapPx)
	{
		B->DrawAs = ESlateBrushDrawType::Box;
		B->Margin = FMargin(CapPx / B->ImageSize.X, 0.f, CapPx / B->ImageSize.X, 0.f);
	}
	return B;
}

const FButtonStyle* GlassButton(EButtonKind Kind)
{
	if (!UseGlassSkin())
		return nullptr;

	// First sprite of the triplet + the horizontal cap that must not stretch.
	int32 First = 21;      // GEN_BTN21/22/23 — the wide action plate (138x33)
	float Cap   = 8.f;
	switch (Kind)
	{
	case EButtonKind::Small:  First = 79; Cap = 6.f; break;   // 53x28
	case EButtonKind::Close:  First =  1; Cap = 0.f; break;   // 29x25, has the X
	case EButtonKind::Action: default:                break;
	}

	static TMap<int32, TSharedPtr<FButtonStyle>> Cache;
	if (TSharedPtr<FButtonStyle>* Hit = Cache.Find(First))
		return Hit->Get();

	FFrameCache& Textures = Frames();
	TSharedPtr<FSlateBrush> N = ButtonBrush(First, 0, Cap, Textures.Textures);
	TSharedPtr<FSlateBrush> O = ButtonBrush(First, 1, Cap, Textures.Textures);
	TSharedPtr<FSlateBrush> D = ButtonBrush(First, 2, Cap, Textures.Textures);
	if (!N.IsValid() || !O.IsValid() || !D.IsValid())
	{
		Cache.Add(First, nullptr);      // cache the miss so it warns once
		return nullptr;
	}

	TSharedPtr<FButtonStyle> Style = MakeShared<FButtonStyle>();
	Style->SetNormal(*N).SetHovered(*O).SetPressed(*D).SetDisabled(*N);
	// The plates carry their own padding; extra press-offset would make them
	// jitter against the window frame.
	Style->SetNormalPadding(FMargin(0)).SetPressedPadding(FMargin(0));
	Cache.Add(First, Style);
	return Style.Get();
}


// ── Glass gauges ────────────────────────────────────────────────────────────
//
// Fill AND track come straight from dlginfo.xml's GUAGE nodes (GID = fill,
// BGID = track) — not from eyeballing the sprites.  That matters: HP and MP do
// NOT share a track (HP uses GEN_GAUGE04, MP uses GEN_GAUGE08), which no amount
// of looking at the art would have told us.
//
//   HP      GEN_GAUGE09 / GEN_GAUGE04       187x24  at (12,27)
//   MP      GEN_GAUGE07 / GEN_GAUGE08       187x24  at (12,48)
//   EXP     GEN_GAUGE05 / GEN_GAUGE06       187x11  at (12,69)
//   Summon  UI00_GUAGE_VIOLET / _BACKGROUND 100x10  at (110,117)
//
// Plain stretched images, never sliced: a fill is a single lengthwise sweep,
// and the caller clips it by width so the sweep stays anchored to the left end.
TSharedPtr<FSlateBrush> GlassGauge(EGaugeKind Kind, bool bTrack)
{
	if (!UseGlassSkin())
		return nullptr;

	const TCHAR* Gid = nullptr;
	switch (Kind)
	{
	case EGaugeKind::HP:     Gid = bTrack ? TEXT("GEN_GAUGE04") : TEXT("GEN_GAUGE09"); break;
	case EGaugeKind::MP:     Gid = bTrack ? TEXT("GEN_GAUGE08") : TEXT("GEN_GAUGE07"); break;
	case EGaugeKind::EXP:    Gid = bTrack ? TEXT("GEN_GAUGE06") : TEXT("GEN_GAUGE05"); break;
	case EGaugeKind::Summon: Gid = bTrack ? TEXT("UI00_GUAGE_BACKGROUND")
	                                      : TEXT("UI00_GUAGE_VIOLET"); break;
	default: return nullptr;
	}

	FFrameCache& Cache = Frames();
	if (TSharedPtr<FSlateBrush>* Hit = Cache.Brushes.Find(Gid))
		return *Hit;

	TSharedPtr<FSlateBrush> Brush = MakeSpriteBrush(Gid, 0.f, 0.f, Cache.Textures);
	Cache.Brushes.Add(Gid, Brush);
	return Brush;
}


// A tab plate.  Selected uses the triplet's HOVER art (bright orange) and
// unselected the NORMAL art, which is the reading the glass skin's own chat
// tabs use — GEN_CHATTAB01/02/03 are normal/hover/selected with the selected
// state drawn hot.  Sliced like the buttons so tabs stretch to any width.
TSharedPtr<FSlateBrush> GlassTab(bool bSelected)
{
	if (!UseGlassSkin())
		return nullptr;

	const TCHAR* Key = bSelected ? TEXT("#TAB_ON") : TEXT("#TAB_OFF");
	FFrameCache& Cache = Frames();
	if (TSharedPtr<FSlateBrush>* Hit = Cache.Brushes.Find(Key))
		return *Hit;

	TSharedPtr<FSlateBrush> Brush = ButtonBrush(79, bSelected ? 1 : 0, 6.f, Cache.Textures);
	Cache.Brushes.Add(Key, Brush);
	return Brush;
}

} // namespace RoseUI
