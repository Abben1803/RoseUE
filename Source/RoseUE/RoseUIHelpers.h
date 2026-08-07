// Shared helpers for the ROSE classic-UI feature files (RoseUIHud/Minimap/
// Skills/Chat/Misc) — layout-JSON queries + sprite/icon brush loading, so
// feature code never duplicates SRoseUIWindow internals.
#pragma once

#include "CoreMinimal.h"
#include "UObject/StrongObjectPtr.h"

class FJsonObject;
struct FSlateBrush;
struct FButtonStyle;
class UTexture2D;

namespace RoseUI
{
	// JSON field access (the layouts store numbers as doubles).
	float Num(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, float Def = 0.f);
	FString Str(const TSharedPtr<FJsonObject>& O, const TCHAR* Field);

	// Load Content/UI/Layouts/<name>.json (logs when missing/bad).
	TSharedPtr<FJsonObject> LoadLayout(const FString& DialogName);

	// Depth-first search for the first node of Type whose label+gid+normalgid
	// contains Match (empty Match = first of Type).  OutAbsPos receives the
	// node's window-space position (parent offsets accumulated, same math as
	// SRoseUIWindow::AddNode).
	TSharedPtr<FJsonObject> FindNode(const TSharedPtr<FJsonObject>& Root,
	                                 const FString& Type, const FString& Match,
	                                 FVector2D* OutAbsPos = nullptr);

	// Collect ALL nodes of Type (with window-space positions), depth-first.
	void FindNodes(const TSharedPtr<FJsonObject>& Root, const FString& Type,
	               TArray<TPair<TSharedPtr<FJsonObject>, FVector2D>>& Out);

	// Brush over /Game/UI/Sprites/<sanitized gid> (build_ui_sprites.py naming).
	// The texture is pinned in KeepTextures — keep that array alive as long as
	// the brush is on screen.  Null when the sprite asset is missing.
	TSharedPtr<FSlateBrush> MakeSpriteBrush(const FString& Gid, float W, float H,
	                                        TArray<TStrongObjectPtr<UTexture2D>>& KeepTextures);

	// Brush over /Game/UI/Icons/icon_%05d (5-digit pad — UDIM gotcha).  ITEM icons.
	// The three surfaces the glass skin defines, named for their role rather
	// than their sprite so call sites do not hardcode GEN_WNDxx.
	enum class EPanelKind : uint8
	{
		Window,   // outer window frame (top band reads as the title strip)
		Body,     // content panel inside a window
		Inset,    // small pane / slot backing
	};

	// The shared glass brush for a surface, or NULL when the glass skin is off
	// or its sprite is missing — callers keep their flat RoseUITheme brush as
	// the fallback, so the UI never comes up as an empty box.
	TSharedPtr<FSlateBrush> GlassPanel(EPanelKind Kind);

	// A tab plate: selected draws hot, unselected cool.  NULL when glass is off.
	TSharedPtr<FSlateBrush> GlassTab(bool bSelected);

	// The bars dlginfo.xml defines.  Each has its OWN track — HP's and MP's
	// differ, so there is no single shared track brush.
	enum class EGaugeKind : uint8 { HP, MP, EXP, Summon };

	// Fill (bTrack=false) or that bar's authored track (bTrack=true); NULL when
	// the glass skin is off / the sprite is missing.
	TSharedPtr<FSlateBrush> GlassGauge(EGaugeKind Kind, bool bTrack = false);

	// Glass button plates.  The art is authored as normal/hover/pressed
	// triplets, so these are real FButtonStyles — the hover glow is the whole
	// character of this skin.
	enum class EButtonKind : uint8
	{
		Action,   // wide plate, stretches to any width (GEN_BTN21..23)
		Small,    // compact plate (GEN_BTN79..81)
		Close,    // the window X (GEN_BTN01..03), fixed 29x25
	};

	// Shared style, or NULL when the glass skin is off / its sprites are
	// missing — callers keep whatever they used before.
	const FButtonStyle* GlassButton(EButtonKind Kind);

	// `rose.GlassUI` — glass nine-slice chrome vs the flat RoseUITheme panels.
	bool UseGlassSkin();

	// Nine-slice window chrome from the glass UI (GEN_WNDxx).  Pass the prefix
	// WITHOUT _BOX; the margin comes from sprites.json, so frames with 7/14/30px
	// corners slice correctly too.
	TSharedPtr<FSlateBrush> MakeWindowFrameBrush(const FString& Prefix,
		TArray<TStrongObjectPtr<UTexture2D>>& KeepTextures);

	TSharedPtr<FSlateBrush> MakeIconBrush(int32 IconIdx, float Size,
	                                      TArray<TStrongObjectPtr<UTexture2D>>& KeepTextures);

	// Brush over /Game/UI/SkillIcons/skillicon_%05d — SKILL icons come from a
	// SEPARATE sheet (SKILLICON.TSI / IMAGE_RES_SKILL_ICON), NOT the item sheet.
	TSharedPtr<FSlateBrush> MakeSkillIconBrush(int32 IconNo, float Size,
	                                           TArray<TStrongObjectPtr<UTexture2D>>& KeepTextures);

	// Brush over any texture asset path (e.g. /Game/UI/Minimaps/JPT01).
	TSharedPtr<FSlateBrush> MakeTextureBrush(const FString& AssetPath, float W, float H,
	                                         TArray<TStrongObjectPtr<UTexture2D>>& KeepTextures);
}
