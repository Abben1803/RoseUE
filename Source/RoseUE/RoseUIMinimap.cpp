// ROSE minimap — dlgminimap window showing the current zone's MINIMAP texture,
// panned so the player pixel sits at the viewport centre, with a heading arrow
// and a zone-name + grid-coord readout.  Registered by RoseUIMinimap_Register
// (called from URoseUIManager::BeginPlay).  M toggles it; open at start.
//
// ─────────────────────────────────────────────────────────────────────────────
//  Authority: src/client/interface/dlgs/cminimapdlg.cpp.  The world→pixel math
//  below is transcribed from it (line cites inline); the manifest
//  (Content/UI/Minimaps/minimaps.json) is produced by tools/build_minimaps.py,
//  which documents the same derivation.
//
//  Constants (all confirmed in local source):
//    MINIMAP_RESOLUTION_PER_MAP = 64   px per 160m zone block   (cminimapdlg.cpp:45)
//    Block world size           = 16*4*250 = 16000 cm = 160 m
//    fGetWorldDistancePerPixel  = 16000/64 = 250 cm/px          (cminimapdlg.cpp:690-693)
//
//  CalculateDisplayPos() — texture top-left region world coords (cminimapdlg.cpp:158-162):
//    MinWorldX = 16000 * start_x
//    MaxWorldY = 16000 * (64 - start_y + 1)
//
//  Draw() sprite-centre — ROSE world (wx,wy) cm → texture pixel (u,v).  The
//  "+64" is the one-block border MINIMAP.DDS carries on each side, matching the
//  "-2" block trim at cminimapdlg.cpp:171-172 (cminimapdlg.cpp:210-215):
//    u = (wx - MinWorldX) / 250 + 64
//    v = (MaxWorldY - wy) / 250 + 64
//
//  This port's ROSE→UE transform (tools/export_mob_spawns.py:8):
//    UE_X =  rose_x_cm + 520000   ;  UE_Y = -(rose_y_cm + 520000)
//  Substituting rose_x = UE_X-520000, rose_y = -UE_Y-520000:
//    u = ((UE_X - 520000) - 16000*start_x) / 250 + 64
//    v = (16000*(65 - start_y) + UE_Y + 520000) / 250 + 64
//
//  Arrow rotation: ROSE rotates the cursor by -D3DXToRadian(direction)
//  (cminimapdlg.cpp:250,271) — i.e. clockwise-negative about the pixel.  The
//  pawn's yaw (UE degrees) drives it; grid readout = ROSE cm / 100 like the
//  classic "%d,%d" print (cminimapdlg.cpp:300-302).
// ─────────────────────────────────────────────────────────────────────────────
#include "RoseUIManager.h"
#include "RoseUIWindow.h"
#include "RoseUIHelpers.h"
#include "RoseUITheme.h"
#include "RoseCharacter.h"
#include "RoseNpc.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "EngineUtils.h"           // TActorIterator

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"

#include "Brushes/SlateRoundedBoxBrush.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Rendering/SlateRenderTransform.h"
#include "Math/TransformCalculus2D.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Text/STextBlock.h"

// ─────────────────────────────────────────────────────────────────────────────
//  Manifest — one entry per zone that has an imported minimap texture.
//  Loaded once, file-static, from Content/UI/Minimaps/minimaps.json.
// ─────────────────────────────────────────────────────────────────────────────
namespace
{
	struct FMinimapEntry
	{
		FString Texture;   // "/Game/UI/Minimaps/<ZONE>"
		int32   StartX = 31;
		int32   StartY = 31;
		int32   PxW = 0;
		int32   PxH = 0;
		FString ZoneName;
	};

	const TMap<FString, FMinimapEntry>& GetMinimapManifest()
	{
		static TMap<FString, FMinimapEntry> Manifest;
		static bool bLoaded = false;
		if (bLoaded)
			return Manifest;
		bLoaded = true;

		const FString Path = FPaths::ProjectContentDir() / TEXT("UI/Minimaps/minimaps.json");
		FString Raw;
		if (!FFileHelper::LoadFileToString(Raw, *Path))
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoseUI] minimap manifest missing: %s"), *Path);
			return Manifest;
		}

		TSharedPtr<FJsonObject> Root;
		TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Raw);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoseUI] minimap manifest parse failed: %s"), *Path);
			return Manifest;
		}

		for (const auto& Pair : Root->Values)
		{
			const TSharedPtr<FJsonObject> O = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
			if (!O.IsValid())
				continue;
			FMinimapEntry E;
			E.Texture  = RoseUI::Str(O, TEXT("texture"));
			E.StartX   = (int32)RoseUI::Num(O, TEXT("start_x"), 31.f);
			E.StartY   = (int32)RoseUI::Num(O, TEXT("start_y"), 31.f);
			E.PxW      = (int32)RoseUI::Num(O, TEXT("px_w"));
			E.PxH      = (int32)RoseUI::Num(O, TEXT("px_h"));
			E.ZoneName = RoseUI::Str(O, TEXT("zone_name"));
			Manifest.Add(FString(*Pair.Key).ToUpper(), MoveTemp(E));
		}
		UE_LOG(LogTemp, Log, TEXT("[RoseUI] minimap manifest: %d zones"), Manifest.Num());
		return Manifest;
	}

	// Current zone key: strip the PIE prefix ("UEDPIE_%d_") and the "L_" level
	// prefix from GetMapName(), leaving "<ZONE>".
	FString CurrentZoneKey(UWorld* World)
	{
		if (!World)
			return FString();
		FString Map = World->GetMapName();
		// "UEDPIE_0_L_JPT01" → "L_JPT01": drop a leading "UEDPIE_<digits>_".
		const FString PiePrefix = TEXT("UEDPIE_");
		if (Map.StartsWith(PiePrefix))
		{
			FString Rest = Map.RightChop(PiePrefix.Len());
			int32 Under;
			if (Rest.FindChar(TEXT('_'), Under))
				Map = Rest.RightChop(Under + 1);
		}
		if (Map.StartsWith(TEXT("L_")))
			Map = Map.RightChop(2);
		return Map.ToUpper();
	}
}

// ─────────────────────────────────────────────────────────────────────────────
//  The minimap content overlay.  Covers the whole window; positions the map
//  viewport at the authored PANE-110 rect.  Inside a ClipToBounds SBox sits the
//  full-size map SImage, offset each frame (RenderTransform, attribute lambda)
//  so the player pixel lands at the viewport centre.  On top: the heading arrow
//  (rotated to pawn yaw) and a zone-name + grid-coord readout.
// ─────────────────────────────────────────────────────────────────────────────
class SRoseMinimap : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseMinimap) {}
		SLATE_ARGUMENT(TWeakObjectPtr<URoseUIManager>, UI)
	SLATE_END_ARGS()

	// Same constants as cminimapdlg.cpp / build_minimaps.py.
	static constexpr float kPxPerBlock   = 64.f;      // MINIMAP_RESOLUTION_PER_MAP
	static constexpr float kBlockCm      = 16000.f;   // 16*4*250
	static constexpr float kCmPerPx      = 250.f;     // kBlockCm / kPxPerBlock
	static constexpr float kCenterWorld  = 520000.f;  // export_mob_spawns.py CENTER_WORLD

	void Construct(const FArguments& InArgs)
	{
		UIWeak = InArgs._UI;
		Rebuild();
	}

	// Re-resolve zone + texture whenever the world's map changes: the overlay
	// is created in manager BeginPlay, which can precede/outlive zone travel
	// (char select -> town, warps) — a construct-time-only resolve left the
	// minimap permanently empty in those flows.
	virtual void Tick(const FGeometry& Geo, const double InCurrentTime, const float InDeltaTime) override
	{
		SCompoundWidget::Tick(Geo, InCurrentTime, InDeltaTime);

		if (InCurrentTime >= NextZoneCheck)
		{
			NextZoneCheck = InCurrentTime + 0.5;
			const FString Zone = CurrentZoneKey(UIWeak.IsValid() ? UIWeak->GetWorld() : nullptr);
			if (!Zone.IsEmpty() && Zone != CachedZone)
			{
				Rebuild();
				return;   // fresh tree; let the marks repopulate next tick
			}
		}

		// NPC marks are built from live actors, which do not exist yet when the
		// widget is first constructed (the UI comes up before the level finishes
		// spawning them) — hence a poll rather than a one-shot.  They never move,
		// so a change in their NUMBER is the only thing worth reacting to.
		if (InCurrentTime >= NextNpcCheck)
		{
			NextNpcCheck = InCurrentTime + 1.0;
			if (CountNpcs() != MarkedNpcCount
				|| !FMath::IsNearlyEqual(MarkedZoom, MapZoom))
				RebuildNpcMarkers();
		}
	}

	int32 CountNpcs() const
	{
		UWorld* W = UIWeak.IsValid() ? UIWeak->GetWorld() : nullptr;
		if (!W)
			return 0;
		int32 N = 0;
		for (TActorIterator<ARoseNpc> It(W); It; ++It)
			++N;
		return N;
	}

	void Rebuild()
	{
		// Rebuild() replaces the whole tree, MarkerCanvas included, so the marks
		// must be considered unbuilt again or the Tick poll sees a matching count
		// and never repopulates the new canvas.
		MarkedNpcCount = -1;

		// The map viewport is dlgminimap's PANE named "MINIMAP".  The old code
		// searched for a 164x164 pane, which is the CLASSIC layout's size — the
		// glass one is 142x142 at (33,33), so the search always failed and fell
		// back to classic coordinates.  That is why the minimap was broken:
		// the map drew at the wrong place and size, outside the frame.
		FVector2D VpPos(33.f, 33.f);
		FVector2D VpSize(142.f, 142.f);
		if (TSharedPtr<FJsonObject> Doc = RoseUI::LoadLayout(TEXT("dlgminimap")))
		{
			TArray<TPair<TSharedPtr<FJsonObject>, FVector2D>> Panes;
			RoseUI::FindNodes(Doc, TEXT("PANE"), Panes);
			for (const auto& P : Panes)
			{
				if (RoseUI::Str(P.Key, TEXT("name")) != TEXT("MINIMAP"))
					continue;
				VpPos = P.Value;
				VpSize = FVector2D(RoseUI::Num(P.Key, TEXT("width"), 142.f),
				                   RoseUI::Num(P.Key, TEXT("height"), 142.f));
				break;
			}
		}
		ViewportSize = VpSize;

		const FString Zone = CurrentZoneKey(UIWeak.IsValid() ? UIWeak->GetWorld() : nullptr);
		CachedZone = Zone;
		const FMinimapEntry* Entry = GetMinimapManifest().Find(Zone);
		if (!Entry)
			UE_LOG(LogTemp, Log, TEXT("[RoseUI] minimap: no manifest entry for zone '%s'"), *Zone);
		MapBrush.Reset();

		if (Entry && !Entry->Texture.IsEmpty())
		{
			StartX = Entry->StartX;
			StartY = Entry->StartY;
			TexW = (float)FMath::Max(1, Entry->PxW);
			TexH = (float)FMath::Max(1, Entry->PxH);
			FString AssetName; Entry->Texture.Split(TEXT("/"), nullptr, &AssetName,
				ESearchCase::CaseSensitive, ESearchDir::FromEnd);
			const FString FullPath = FString::Printf(TEXT("%s.%s"), *Entry->Texture, *AssetName);
			MapBrush = RoseUI::MakeTextureBrush(FullPath, TexW, TexH, KeepTextures);
		}
		ArrowBrush = RoseUI::MakeTextureBrush(
			TEXT("/Game/UI/Minimaps/MINIMAP_ARROW.MINIMAP_ARROW"), 16.f, 16.f, KeepTextures);

		// NPC mark: a gold dot.  Drawn rather than sprited — the glass skin ships
		// no NPC marker (only ID_MINIMAP_PARTYMEMBER), and a rounded box at half
		// its own width is a circle.
		if (!NpcDotBrush.IsValid())
			NpcDotBrush = MakeShared<FSlateRoundedBoxBrush>(
				FLinearColor(1.f, 0.82f, 0.28f, 1.f), kMarkPx * 0.5f,
				FLinearColor(0.15f, 0.12f, 0.05f, 1.f), 1.f);
		// GEN_MINIMAP10_SURROUND, not GEN_MINIMAP10.
		//
		// GEN_MINIMAP10 is MINIMAP_ALPHAMASK: opaque BLACK inside the disc
		// (RGBA 0,0,0,255) and clear outside — an alpha mask for the client's 3D
		// pipeline to CLIP with, not something to composite.  Drawing it over the
		// map painted a solid black disc over the map, which is exactly what the
		// minimap has been showing.
		//
		// Slate can only clip to a rectangle, and the disc is viewport-space (the
		// map pans underneath it), so roundness cannot be baked into the zone
		// textures either.  _SURROUND is that same mask with its alpha inverted
		// and filled with the panel colour: opaque everywhere the disc is not, so
		// it hides the map's square corners the way the frame is meant to.
		MaskBrush = RoseUI::MakeSpriteBrush(TEXT("GEN_MINIMAP10_SURROUND"),
			VpSize.X, VpSize.Y, KeepTextures);

		ZoneLabel = (Entry && !Entry->ZoneName.IsEmpty()) ? Entry->ZoneName
			: (Zone.IsEmpty() ? TEXT("Unknown Zone") : Zone);

		// Live layer, INSIDE the window so it moves when the bar is dragged.
		TSharedRef<SConstraintCanvas> Canvas = SNew(SConstraintCanvas);
		Canvas->SetVisibility(EVisibility::SelfHitTestInvisible);
		Canvas->AddSlot()
			.Offset(FMargin(VpPos.X, VpPos.Y, VpSize.X, VpSize.Y))
			.Alignment(FVector2D(0, 0))
			[
				SNew(SBox)
				.WidthOverride(VpSize.X).HeightOverride(VpSize.Y)
				.Clipping(EWidgetClipping::ClipToBounds)
				[
					SNew(SOverlay)
					// No backdrop layer: anything the zone map does not cover
					// stays transparent.  A flat grey fill used to sit under the
					// map here and it is not wanted — it also hid a black map
					// texture behind something that looked deliberate.
					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
					[
						SNew(SBox)
						.WidthOverride(TexW).HeightOverride(TexH)
						.Visibility(MapBrush.IsValid()
							? EVisibility::HitTestInvisible : EVisibility::Collapsed)
						.RenderTransformPivot(FVector2D::ZeroVector)
						.RenderTransform(TAttribute<TOptional<FSlateRenderTransform>>::CreateSP(
							this, &SRoseMinimap::MapTransform))
						[ SNew(SImage).Image(MapBrush.Get()) ]
					]

					// NPC marks, in the SAME pixel space as the map and carrying
					// the same transform, so they pan with it.  Above the map but
					// BELOW the mask, so a mark outside the disc is hidden by the
					// mask rather than floating over the frame.
					+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top)
					[
						SNew(SBox)
						.WidthOverride(TexW).HeightOverride(TexH)
						// SelfHitTestInvisible, not HitTestInvisible: the latter
						// excludes CHILDREN from hit testing too and the marks
						// would never receive hover, killing their tooltips.
						.Visibility(EVisibility::SelfHitTestInvisible)
						.RenderTransformPivot(FVector2D::ZeroVector)
						.RenderTransform(TAttribute<TOptional<FSlateRenderTransform>>::CreateSP(
							this, &SRoseMinimap::MarkerTransform))
						[
							SAssignNew(MarkerCanvas, SConstraintCanvas)
							.Visibility(EVisibility::SelfHitTestInvisible)
						]
					]

					// MINIMAP_ALPHAMASK (GEN_MINIMAP10) is opaque OUTSIDE the disc
					// and clear inside, so drawing it OVER the map is what makes
					// the minimap round.  The layout already places it, but our
					// Content overlay paints above the layout — so the map was
					// covering the mask and the result was a grey SQUARE.
					+ SOverlay::Slot()
					[
						SNew(SImage).Image(MaskBrush.Get())
						.Visibility(MaskBrush.IsValid()
							? EVisibility::HitTestInvisible : EVisibility::Collapsed)
					]

					+ SOverlay::Slot().HAlign(HAlign_Center).VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(16.f).HeightOverride(16.f)
						.Visibility(ArrowBrush.IsValid()
							? EVisibility::HitTestInvisible : EVisibility::Collapsed)
						[
							SNew(SImage)
							.Image(ArrowBrush.IsValid() ? ArrowBrush.Get() : nullptr)
							.RenderTransformPivot(FVector2D(0.5f, 0.5f))
							.RenderTransform(TAttribute<TOptional<FSlateRenderTransform>>::CreateSP(
								this, &SRoseMinimap::ArrowTransform))
						]
					]
				]
			];

		ChildSlot
		[
			SNew(SBox).WidthOverride(206.f).HeightOverride(210.f)
			[
				SAssignNew(Layout, SRoseUIWindow)
				.Dialog(TEXT("dlgminimap"))
				.Content(Canvas)
				.OnTextValue(FRoseUITextValue::CreateSP(this, &SRoseMinimap::MapText))
				.OnActivate(FOnRoseUIActivate::CreateSP(this, &SRoseMinimap::MapButton))
			]
		];

		ApplyMinimizedState();
	}

	/** MINIMIZED and NORMAL are alternate panes; only one may be shown. */
	void ApplyMinimizedState()
	{
		if (!Layout.IsValid())
			return;
		Layout->SetPaneVisibility(TEXT("NORMAL"),
			bMinimized ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
		Layout->SetPaneVisibility(TEXT("MINIMIZED"),
			bMinimized ? EVisibility::SelfHitTestInvisible : EVisibility::Collapsed);
		Layout->SetPaneVisibility(TEXT("MINIMAP"),
			bMinimized ? EVisibility::Collapsed : EVisibility::SelfHitTestInvisible);
	}

	FText MapText(const FRoseUIKey& Key, int32 Row) const
	{
		// ZONENAME only: dlgminimap ALSO has a caption, and answering both
		// printed the zone name twice, stacked.
		if (Key.Is(TEXT("ZONENAME")))
			return FText::FromString(ZoneLabel);
		if (Key.Is(TEXT("COORDINATE")))
			return CoordText();
		if (Key.Is(TEXT("TIME")))
		{
			const FDateTime Now = FDateTime::Now();
			return FText::FromString(Now.ToString(TEXT("%h:%M %A")));
		}
		return FText::GetEmpty();
	}

	void MapButton(const FRoseUIKey& Key, int32 Row)
	{
		if (Key.Is(TEXT("MINIMIZE")))      { bMinimized = true;  ApplyMinimizedState(); }
		else if (Key.Is(TEXT("RESTORE")))  { bMinimized = false; ApplyMinimizedState(); }
		// The map itself follows MapZoom through MapTransform every frame, but the
		// marks are laid out at fixed pixel offsets, so they need re-placing.
		// Done here rather than waiting for the 1 Hz poll to notice.
		else if (Key.Is(TEXT("ZOOMIN")))
		{
			MapZoom = FMath::Clamp(MapZoom * 1.25f, 0.5f, 4.f);
			RebuildNpcMarkers();
		}
		else if (Key.Is(TEXT("ZOOMOUT")))
		{
			MapZoom = FMath::Clamp(MapZoom / 1.25f, 0.5f, 4.f);
			RebuildNpcMarkers();
		}
	}

private:
	APawn* GetPawn() const
	{
		if (!UIWeak.IsValid())
			return nullptr;
		ARoseCharacter* C = UIWeak->GetRoseCharacter();
		return Cast<APawn>(C);
	}

	// Player pixel in full-texture space (see the header derivation).
	/** World cm -> minimap pixel, the mapping documented at the top of this file.
	 *  Split out of PlayerPixel so NPC marks land in exactly the same space as
	 *  the player arrow — two copies of this arithmetic would drift apart. */
	FVector2D WorldToPixel(const FVector& Loc) const
	{
		const float U = ((Loc.X - kCenterWorld) - kBlockCm * StartX) / kCmPerPx + kPxPerBlock;
		const float V = (kBlockCm * (65.f - StartY) + Loc.Y + kCenterWorld) / kCmPerPx + kPxPerBlock;
		return FVector2D(U, V);
	}

	FVector2D PlayerPixel() const
	{
		const APawn* P = GetPawn();
		if (!P)
			return FVector2D(TexW * 0.5f, TexH * 0.5f);
		return WorldToPixel(P->GetActorLocation());
	}

	/** Place a dot per town NPC, at map pixel coordinates.
	 *
	 *  Sourced from the live ARoseNpc actors rather than re-reading the IFO: the
	 *  map importer already placed them from LUMP_TERRAIN_MOB, and the actor
	 *  carries the resolved LIST_NPC name that the tooltip needs.  Re-parsing the
	 *  IFO here would be a second source of truth that could disagree.
	 *
	 *  NPCs do not move, so this runs only when their number changes. */
	void RebuildNpcMarkers()
	{
		if (!MarkerCanvas.IsValid())
			return;
		UWorld* W = UIWeak.IsValid() ? UIWeak->GetWorld() : nullptr;
		if (!W)
			return;

		MarkerCanvas->ClearChildren();
		int32 Count = 0;
		for (TActorIterator<ARoseNpc> It(W); It; ++It)
		{
			ARoseNpc* Npc = *It;
			if (!Npc)
				continue;
			++Count;
			// Zoom is applied HERE rather than by the layer transform, so the dot
			// tracks the map while keeping its own size.
			const FVector2D Px = WorldToPixel(Npc->GetActorLocation()) * MapZoom;
			FString Name = Npc->GetDisplayName();
			if (Name.IsEmpty())
				Name = FString::Printf(TEXT("NPC %d"), Npc->NpcId);

			MarkerCanvas->AddSlot()
				.Offset(FMargin(Px.X - kMarkPx * 0.5f, Px.Y - kMarkPx * 0.5f,
				                kMarkPx, kMarkPx))
				.Alignment(FVector2D(0.f, 0.f))
				[
					SNew(SBox)
					.WidthOverride(kMarkPx).HeightOverride(kMarkPx)
					// Explicitly Visible, not the SBox default: a
					// SelfHitTestInvisible box never becomes the hovered widget,
					// so Slate would never find this tooltip.
					.Visibility(EVisibility::Visible)
					.ToolTipText(FText::FromString(Name))
					[ SNew(SImage).Image(NpcDotBrush.Get()) ]
				];
		}
		MarkedNpcCount = Count;
		MarkedZoom = MapZoom;
	}

	// Translate the full map so the player pixel sits at the viewport centre.
	// Scale about the TOP-LEFT, then translate so the player lands dead centre:
	//   pixel -> pixel*Zoom + (Viewport/2 - PlayerPixel*Zoom)
	// which puts PlayerPixel at Viewport/2 for any zoom.  Both layers set
	// RenderTransformPivot to (0,0); Slate's default pivot is the widget CENTRE,
	// which would scale about the middle of the 576x512 texture and throw the
	// centring off by more the further the player is from the map's middle.
	TOptional<FSlateRenderTransform> MapTransform() const
	{
		const FVector2D Offset = ViewportSize * 0.5f - PlayerPixel() * MapZoom;
		return FSlateRenderTransform(MapZoom, Offset);
	}

	/** Same placement as the map, but WITHOUT the scale: the marks are laid out
	 *  at already-zoomed pixel coordinates, so scaling here too would inflate the
	 *  dots themselves (7px becomes 28px at 4x). */
	TOptional<FSlateRenderTransform> MarkerTransform() const
	{
		return FSlateRenderTransform(ViewportSize * 0.5f - PlayerPixel() * MapZoom);
	}

	// Rotate the arrow to the pawn's yaw.  ROSE uses -radians (cminimapdlg.cpp:271);
	// UE yaw increases clockwise in screen space with +Y down, matching the map's
	// v-axis, so a straight negative-yaw rotation aligns the cursor with heading.
	TOptional<FSlateRenderTransform> ArrowTransform() const
	{
		const APawn* P = GetPawn();
		const float YawDeg = P ? P->GetActorRotation().Yaw : 0.f;
		const float Rad = -FMath::DegreesToRadians(YawDeg);
		return FSlateRenderTransform(FQuat2D(Rad));
	}

	FText CoordText() const
	{
		const APawn* P = GetPawn();
		if (!P)
			return FText::GetEmpty();
		// ROSE grid readout = ROSE cm / 100 (cminimapdlg.cpp:300-302).
		const FVector Loc = P->GetActorLocation();
		const int32 GX = (int32)((Loc.X - kCenterWorld) / 100.f);
		const int32 GY = (int32)((-Loc.Y - kCenterWorld) / 100.f);
		return FText::FromString(FString::Printf(TEXT("%d, %d"), GX, GY));
	}

	TWeakObjectPtr<URoseUIManager> UIWeak;
	FString CachedZone;        // zone the widget is currently built for
	double NextZoneCheck = 0;  // throttle for the Tick zone poll
	TSharedPtr<SRoseUIWindow> Layout;
	TSharedPtr<FSlateBrush> MaskBrush;         // GEN_MINIMAP10 — makes it round
	FString ZoneLabel;
	bool  bMinimized = false;
	float MapZoom = 1.f;

	TSharedPtr<FSlateBrush> MapBrush;
	TSharedPtr<FSlateBrush> ArrowBrush;
	TSharedPtr<FSlateBrush> FrameBrush;

	// NPC marks: a canvas in map-pixel space plus the dot they all share.
	static constexpr float kMarkPx = 7.f;
	TSharedPtr<SConstraintCanvas> MarkerCanvas;
	TSharedPtr<FSlateBrush> NpcDotBrush;
	int32  MarkedNpcCount = -1;   // -1 = never built
	float  MarkedZoom = -1.f;     // zoom the marks were laid out at
	double NextNpcCheck = 0;      // throttle for the Tick NPC poll
	TArray<TStrongObjectPtr<UTexture2D>> KeepTextures;   // pin textures with the widget

	FVector2D ViewportSize = FVector2D(164.f, 164.f);
	float TexW = 1.f, TexH = 1.f;
	int32 StartX = 31, StartY = 31;
};

// ─────────────────────────────────────────────────────────────────────────────
//  Factory — the modern minimap is an always-on overlay (top-right) created by
//  URoseUIManager::BeginPlay and toggled by M (URoseUIManager::ToggleMinimap);
//  no classic dlgminimap window is registered anymore.
// ─────────────────────────────────────────────────────────────────────────────
TSharedRef<SWidget> RoseMinimap_Make(URoseUIManager& UI)
{
	return SNew(SRoseMinimap).UI(&UI);
}

void RoseUIMinimap_Register(URoseUIManager& UI)
{
	// Nothing to register — the overlay is created directly by the manager.
}
