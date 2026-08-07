// NPC store window (SRoseStoreWindow) — opened by a CON script's
// GF_openStore.  Left: the NPC's sell tabs (npcs SellTab0-3 → stores table)
// with buy-on-click; right: the player's bag with sell-on-click.  Prices are
// the faithful CEconomy formulas (src/common/shared/ceconomy.cpp
// Get_ItemBuyPRICE / Get_ItemSellPRICE) at the client's default rates:
// item_rate 50, town_rate 100, world_rate 100, buy/sell skill 0.
#include "RoseCharacter.h"
#include "RoseUIHelpers.h"
#include "RoseNpc.h"
#include "RoseItemTypes.h"
#include "RoseDrops.h"
#include "RoseUIChat.h"
#include "RoseUIManager.h"
#include "RoseUITheme.h"

#include "Engine/DataTable.h"
#include "Engine/Texture2D.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SConstraintCanvas.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SWrapBox.h"
#include "Widgets/Text/STextBlock.h"

// ─── prices (ceconomy.cpp, client-default economy) ───────────────────────────
namespace RoseStorePrices
{
	constexpr float kItemRate = 50.f;    // m_btItemRATE init
	constexpr float kTownRate = 100.f;   // m_btTOWN_RATE init
	constexpr float kWorldRate = 100.f;  // g_nWorldRate

	// CEconomy::IsEssentialGoods — subtype 421-428 (medicine) / 311-312 (food).
	inline bool IsEssential(int32 Subtype)
	{
		return (Subtype >= 421 && Subtype <= 428) || (Subtype >= 311 && Subtype <= 312);
	}

	struct FPriceData
	{
		int32 BasePrice = 0, PriceRate = 0, Quality = 0, Durability = 0, Subtype = 0;
		bool bEquipment = false;
		bool bValid = false;
	};

	inline FPriceData Lookup(ARoseCharacter& C, const FString& Slot, int32 Id)
	{
		FPriceData D;
		if (Slot == TEXT("weapon"))
		{
			if (const FRoseWeaponRow* R = C.GetWeaponRow(Id))
			{
				D = { R->BasePrice, R->PriceRate, R->Quality, R->Durability, 0, true, true };
			}
		}
		else if (const FRoseArmorRow* R = C.GetArmorRow(Slot, Id))
		{
			D = { R->BasePrice, R->PriceRate, R->Quality, R->Durability, 0, true, true };
		}
		else if (const FRoseSimpleItemRow* R = C.GetSimpleItemRow(Slot, Id))
		{
			D = { R->BasePrice, R->PriceRate, R->Quality, 0, R->Subtype, false, true };
		}
		return D;
	}

	// Get_ItemBuyPRICE.
	inline int32 BuyPrice(ARoseCharacter& C, const FString& Slot, int32 Id)
	{
		const FPriceData D = Lookup(C, Slot, Id);
		if (!D.bValid) return 0;

		if (D.bEquipment)
			return int32(D.BasePrice * (D.Quality + 50.f) / 100.f + 0.5f);

		const bool bEssential =
			(Slot == TEXT("consumable") || Slot == TEXT("material")) && IsEssential(D.Subtype);
		const float Rate = bEssential ? kItemRate : kTownRate;
		return int32(D.BasePrice * (1.f + (Rate - 50.f) * D.PriceRate / 1000.f) + 0.5f);
	}

	// Get_ItemSellPRICE (fresh item: grade 0, full life 1000, no appraisal gem).
	inline int32 SellPrice(ARoseCharacter& C, const FString& Slot, int32 Id)
	{
		const FPriceData D = Lookup(C, Slot, Id);
		if (!D.bValid) return 0;

		if (D.bEquipment)
			return int32((float)D.BasePrice * (40.f + 0.f) * (200.f + D.Durability)
				* (200.f - kWorldRate) * 1.f / 1000000.f * ((4000.f + 1000.f) / 14000.f));

		const bool bEssential =
			(Slot == TEXT("consumable") || Slot == TEXT("material")) && IsEssential(D.Subtype);
		const float Rate = bEssential ? kItemRate : kTownRate;
		return int32(D.BasePrice * (1000.f + (Rate - 50.f) * D.PriceRate) * 1.f
			* (200.f - kWorldRate) / 180000.f);
	}
}

// ─── the window ──────────────────────────────────────────────────────────────
class SRoseStoreWindow : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SRoseStoreWindow) {}
		SLATE_ARGUMENT(TWeakObjectPtr<ARoseCharacter>, Player)
		SLATE_ARGUMENT(TWeakObjectPtr<ARoseNpc>, Npc)
		SLATE_EVENT(FSimpleDelegate, OnClosed)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		Player = InArgs._Player;
		Npc = InArgs._Npc;
		OnClosed = InArgs._OnClosed;

		StoreTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/DataTables/stores.stores"));
		if (ARoseNpc* N = Npc.Get())
			for (int32 T = 0; T < 4; ++T)
				if (N->SellTabs[T] > 0)
					Tabs.Add(N->SellTabs[T]);

		static FSlateRoundedBoxBrush FlatPanel(RoseTheme::Panel, 12.f);
		static FSlateRoundedBoxBrush FlatTitle(RoseTheme::Title, 8.f);
		// Glass skin when available, flat theme otherwise (see RoseUI::GlassPanel).
		GlassBrush = RoseUI::GlassPanel(RoseUI::EPanelKind::Window);
		const FButtonStyle* CloseStyle = RoseUI::GlassButton(RoseUI::EButtonKind::Close);
		const FSlateBrush* PanelBrushPtr = GlassBrush.IsValid()
			? GlassBrush.Get() : (const FSlateBrush*)&FlatPanel;
		const FSlateBrush* TitleBrushPtr = (const FSlateBrush*)&FlatTitle;

		ChildSlot
		[
			SNew(SBox).WidthOverride(700.f)
			[
				SNew(SBorder).BorderImage(PanelBrushPtr).Padding(10.f)
				[
					SNew(SVerticalBox)
					// title
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
					[
						SNew(SBorder).BorderImage(TitleBrushPtr).Padding(FMargin(12, 6))
						[
							SNew(SHorizontalBox)
							+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Text(FText::FromString(FString::Printf(TEXT("%s — Store"),
									Npc.IsValid() ? *Npc->GetDisplayName() : TEXT(""))))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 14))
								.ColorAndOpacity(RoseTheme::Accent)
							]
							+ SHorizontalBox::Slot().AutoWidth()
							[
								SNew(SButton)
								.ButtonStyle(CloseStyle ? CloseStyle
								             : &FCoreStyle::Get().GetWidgetStyle<FButtonStyle>("NoBorder"))
								.OnClicked_Lambda([this]() { OnClosed.ExecuteIfBound(); return FReply::Handled(); })
								[ SNew(SBox).WidthOverride(CloseStyle ? 29.f : 16.f)
								            .HeightOverride(CloseStyle ? 25.f : 16.f)
								  // The glass plate draws its own X.
								  [ SNew(STextBlock).Text(FText::FromString(TEXT("✕")))
								    .Visibility(CloseStyle ? EVisibility::Collapsed : EVisibility::Visible)
								    .Justification(ETextJustify::Center)
								    .ColorAndOpacity(RoseTheme::TextDim) ] ]
							]
						]
					]
					// tab row
					+ SVerticalBox::Slot().AutoHeight().Padding(0, 0, 0, 8)
					[ SAssignNew(TabRow, SHorizontalBox) ]
					// buy grid | sell list
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().FillWidth(0.58f).Padding(0, 0, 6, 0)
						[
							SNew(SBox).HeightOverride(380.f)
							[
								SNew(SScrollBox)
								+ SScrollBox::Slot()[ SAssignNew(BuyBox, SVerticalBox) ]
							]
						]
						+ SHorizontalBox::Slot().FillWidth(0.42f)
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight().Padding(2, 0, 0, 4)
							[
								SNew(STextBlock).Text(FText::FromString(TEXT("Your bag — click to sell")))
								.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
								.ColorAndOpacity(RoseTheme::TextDim)
							]
							+ SVerticalBox::Slot().AutoHeight()
							[
								SNew(SBox).HeightOverride(358.f)
								[
									SNew(SScrollBox)
									+ SScrollBox::Slot()[ SAssignNew(SellBox, SVerticalBox) ]
								]
							]
						]
					]
					// zuly footer
					+ SVerticalBox::Slot().AutoHeight().Padding(4, 8, 4, 0)
					[
						SNew(STextBlock)
						.Text_Lambda([this]() {
							ARoseCharacter* C = Player.Get();
							return FText::FromString(FString::Printf(TEXT("Zuly: %d"),
								C ? C->GetZuly() : 0)); })
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 12))
						.ColorAndOpacity(RoseTheme::Gold)
					]
				]
			]
		];

		BuildTabs();
		RebuildBuy();
		RebuildSell();
	}

	virtual void Tick(const FGeometry& G, const double T, const float Dt) override
	{
		SCompoundWidget::Tick(G, T, Dt);
		// Bag/zuly changed (buy, sell, pickup elsewhere) → refresh the sell side.
		ARoseCharacter* C = Player.Get();
		if (C && C->GetBagRevision() != SeenBagRevision)
			RebuildSell();
	}

private:
	TWeakObjectPtr<ARoseCharacter> Player;
	TWeakObjectPtr<ARoseNpc> Npc;
	FSimpleDelegate OnClosed;
	UDataTable* StoreTable = nullptr;
	TArray<int32> Tabs;                  // stores-table row ids
	int32 ActiveTab = 0;                 // index into Tabs
	int32 SeenBagRevision = -1;
	TSharedPtr<SHorizontalBox> TabRow;
	TSharedPtr<SVerticalBox> BuyBox;
	TSharedPtr<SVerticalBox> SellBox;
	TMap<int32, TSharedPtr<FSlateBrush>> IconCache;
	TArray<TStrongObjectPtr<UTexture2D>> KeepTextures;
	TSharedPtr<FSlateBrush> GlassBrush;

	const FRoseStoreRow* TabRowData(int32 TabId) const
	{
		if (!StoreTable) return nullptr;
		return StoreTable->FindRow<FRoseStoreRow>(
			*FString::Printf(TEXT("sell_%d"), TabId), TEXT("RoseStore"), false);
	}

	FSlateBrush* IconForItem(const FString& Slot, int32 Id)
	{
		ARoseCharacter* C = Player.Get();
		if (!C || Id < 0) return nullptr;
		int32 IconIdx = 0;
		if (Slot == TEXT("weapon"))
		{
			if (const FRoseWeaponRow* R = C->GetWeaponRow(Id)) IconIdx = R->IconIdx;
		}
		else if (const FRoseArmorRow* R = C->GetArmorRow(Slot, Id))
			IconIdx = R->IconIdx;
		else if (const FRoseSimpleItemRow* SR = C->GetSimpleItemRow(Slot, Id))
			IconIdx = SR->IconIdx;
		if (IconIdx <= 0) return nullptr;
		if (TSharedPtr<FSlateBrush>* Cached = IconCache.Find(IconIdx))
			return Cached->IsValid() ? Cached->Get() : nullptr;
		UTexture2D* Tex = LoadObject<UTexture2D>(nullptr, *FString::Printf(
			TEXT("/Game/UI/Icons/icon_%05d.icon_%05d"), IconIdx, IconIdx));
		TSharedPtr<FSlateBrush> B;
		if (Tex)
		{
			B = MakeShared<FSlateBrush>();
			B->SetResourceObject(Tex);
			B->ImageSize = FVector2D(30.f, 30.f);
			KeepTextures.Add(TStrongObjectPtr<UTexture2D>(Tex));
		}
		IconCache.Add(IconIdx, B);
		return B.IsValid() ? B.Get() : nullptr;
	}

	void BuildTabs()
	{
		TabRow->ClearChildren();
		for (int32 i = 0; i < Tabs.Num(); ++i)
		{
			const FRoseStoreRow* Row = TabRowData(Tabs[i]);
			const FString Label = Row && !Row->DisplayName.IsEmpty()
				? Row->DisplayName : FString::Printf(TEXT("Tab %d"), i + 1);
			TabRow->AddSlot().FillWidth(1.f).Padding(i ? 4.f : 0.f, 0, 0, 0)
			[
				SNew(SButton)
				.ButtonColorAndOpacity_Lambda([this, i]() {
					return ActiveTab == i ? RoseTheme::TabOn : RoseTheme::TabOff; })
				.OnClicked_Lambda([this, i]() {
					ActiveTab = i;
					RebuildBuy();
					return FReply::Handled();
				})
				.HAlign(HAlign_Center).ContentPadding(FMargin(4, 5))
				[
					SNew(STextBlock).Text(FText::FromString(Label))
					.Font(FCoreStyle::GetDefaultFontStyle("Bold", 10))
					.ColorAndOpacity(RoseTheme::Text)
				]
			];
		}
	}

	// One row per store item: icon + name + price, click = buy 1.
	void RebuildBuy()
	{
		BuyBox->ClearChildren();
		ARoseCharacter* C = Player.Get();
		if (!C || !Tabs.IsValidIndex(ActiveTab)) return;
		const FRoseStoreRow* Row = TabRowData(Tabs[ActiveTab]);
		if (!Row) return;

		static FSlateRoundedBoxBrush RowBrush(RoseTheme::Row, 6.f);
		static FSlateRoundedBoxBrush RowHover(RoseTheme::PanelHi, 6.f);
		static FButtonStyle RowStyle = FButtonStyle()
			.SetNormal(RowBrush).SetHovered(RowHover).SetPressed(RowBrush);

		TArray<FString> SNs;
		Row->Items.ParseIntoArray(SNs, TEXT(";"));
		for (const FString& SNStr : SNs)
		{
			const int32 SN = FCString::Atoi(*SNStr);
			if (SN <= 0) continue;
			const FString Slot = RoseItemTypeToSlot(SN / 1000);
			const int32 Id = SN % 1000;
			const int32 Price = RoseStorePrices::BuyPrice(*C, Slot, Id);
			const FString Name = C->GetItemName(Slot, Id);

			BuyBox->AddSlot().AutoHeight().Padding(2, 2)
			[
				SNew(SButton).ButtonStyle(&RowStyle).ContentPadding(FMargin(6, 4))
				.OnClicked_Lambda([this, Slot, Id, Price, Name]() {
					Buy(Slot, Id, Price, Name);
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 8, 0)
					[
						SNew(SBox).WidthOverride(30.f).HeightOverride(30.f)
						[ SNew(SImage).Image(IconForItem(Slot, Id)) ]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(Name))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 11))
						.ColorAndOpacity(RoseTheme::Text)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("%d z"), Price)))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 11))
						.ColorAndOpacity(RoseTheme::Gold)
					]
				]
			];
		}
	}

	// One row per bag stack: icon + name xN + unit sell price, click = sell 1.
	void RebuildSell()
	{
		SellBox->ClearChildren();
		ARoseCharacter* C = Player.Get();
		if (!C) return;
		SeenBagRevision = C->GetBagRevision();

		static FSlateRoundedBoxBrush RowBrush(RoseTheme::Slot, 6.f);
		static FSlateRoundedBoxBrush RowHover(RoseTheme::PanelHi, 6.f);
		static FButtonStyle RowStyle = FButtonStyle()
			.SetNormal(RowBrush).SetHovered(RowHover).SetPressed(RowBrush);

		for (const FRoseItemStack& St : C->Bag)
		{
			const FString Slot = St.Slot;
			const int32 Id = St.Id;
			const int32 Price = RoseStorePrices::SellPrice(*C, Slot, Id);
			const FString Name = C->GetItemName(Slot, Id);
			const FString Label = St.Count > 1
				? FString::Printf(TEXT("%s ×%d"), *Name, St.Count) : Name;

			SellBox->AddSlot().AutoHeight().Padding(2, 2)
			[
				SNew(SButton).ButtonStyle(&RowStyle).ContentPadding(FMargin(6, 3))
				.OnClicked_Lambda([this, Slot, Id, Price, Name]() {
					Sell(Slot, Id, Price, Name);
					return FReply::Handled();
				})
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0, 0, 6, 0)
					[
						SNew(SBox).WidthOverride(26.f).HeightOverride(26.f)
						[ SNew(SImage).Image(IconForItem(Slot, Id)) ]
					]
					+ SHorizontalBox::Slot().FillWidth(1.f).VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(Label))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						.ColorAndOpacity(RoseTheme::Text)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("+%d z"), Price)))
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 10))
						.ColorAndOpacity(RoseTheme::Green)
					]
				]
			];
		}
	}

	void Buy(const FString& Slot, int32 Id, int32 Price, const FString& Name)
	{
		ARoseCharacter* C = Player.Get();
		if (!C) return;
		if (C->GetZuly() < Price)
		{
			FRoseChatLog::Add(FRoseChatLog::EKind::System, TEXT("Not enough Zuly."));
			return;
		}
		C->AddZuly(-Price);
		C->AddItemToBag(Slot, Id, 1);
		FRoseChatLog::Add(FRoseChatLog::EKind::System,
			FString::Printf(TEXT("Bought %s for %d Zuly."), *Name, Price));
	}

	void Sell(const FString& Slot, int32 Id, int32 Price, const FString& Name)
	{
		ARoseCharacter* C = Player.Get();
		if (!C) return;
		C->ConsumeBagItem(Slot, Id, 1);
		C->AddZuly(Price);
		FRoseChatLog::Add(FRoseChatLog::EKind::System,
			FString::Printf(TEXT("Sold %s for %d Zuly."), *Name, Price));
	}
};

// ─── manager glue ────────────────────────────────────────────────────────────
TSharedRef<SWidget> RoseStore_Make(URoseUIManager& UI, ARoseNpc* Npc)
{
	TWeakObjectPtr<URoseUIManager> Weak(&UI);
	return SNew(SConstraintCanvas)
		+ SConstraintCanvas::Slot()
		.Anchors(FAnchors(0.5f, 0.5f))
		.Offset(FMargin(0.f, -40.f, 0.f, 0.f))
		.Alignment(FVector2D(0.5f, 0.5f))
		.AutoSize(true)
		[
			SNew(SRoseStoreWindow)
			.Player(UI.GetRoseCharacter())
			.Npc(Npc)
			.OnClosed(FSimpleDelegate::CreateLambda([Weak]() {
				if (Weak.IsValid()) Weak->CloseStore();
			}))
		];
}
