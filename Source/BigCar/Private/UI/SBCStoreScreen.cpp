// Copyright BigCar Team. All Rights Reserved.
//
// Store (online only): featured, daily deals with refresh countdown, vehicles, equipment, cosmetics, bundles, gold
// packs (sandbox payments in development) and the season pass with free/premium tracks.

#include "Core/BCDataSubsystem.h"
#include "Core/BCProfileSubsystem.h"
#include "Core/BigCarTypes.h"
#include "CrashCore/Profile.h"
#include "Gameplay/BCPlayerController.h"
#include "Misc/DateTime.h"
#include "Misc/Timespan.h"
#include "Online/BCOnlineSubsystem.h"
#include "UI/BCUI.h"
#include "UI/BCUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace BCStoreScreenState
{
int32 Tab = 0;

enum ETab : int32
{
	Featured = 0,
	Daily,
	Vehicles,
	Equipment,
	Cosmetics,
	Bundles,
	Gold,
	Pass,
	NumTabs
};
} // namespace BCStoreScreenState

class SBCStoreScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBCStoreScreen) : _Owner(nullptr) {}
		SLATE_ARGUMENT(ABCPlayerController*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SBCStoreScreen() override;

private:
	bool IsLoggedIn() const;
	void Fetch();
	void Rebuild();
	void SelectTab(int32 Index);
	void HandleState();
	void HandleChanged();

	void BuildOffers(const TArray<const FBCStoreOffer*>& Offers);
	void BuildGold();
	void BuildPass();
	TSharedRef<SWidget> MakeOfferCard(const FBCStoreOffer& Offer);
	TSharedRef<SWidget> MakeRewardList(const TArray<crash::ItemRef>& Items, bool bDim);
	TSharedRef<SWidget> MakeTierCell(int32 Tier, bool bPremium);
	FText DailyCountdown() const;

	TWeakObjectPtr<ABCPlayerController> PC;
	TWeakObjectPtr<UBCOnlineSubsystem> OnlineSub;
	TWeakObjectPtr<UBCProfileSubsystem> ProfileSub;
	FDelegateHandle StateHandle;
	FDelegateHandle StoreHandle;
	FDelegateHandle ProfileHandle;
	TSharedPtr<SVerticalBox> ContentBox;
	bool bFetchRequested = false;
};

void SBCStoreScreen::Construct(const FArguments& InArgs)
{
	PC = InArgs._Owner;
	ABCPlayerController* P = PC.Get();
	ProfileSub = UBCProfileSubsystem::Get(P);
	if (UBCOnlineSubsystem* Sub = UBCOnlineSubsystem::Get(P))
	{
		OnlineSub = Sub;
		StateHandle = Sub->OnStateChanged.AddSP(this, &SBCStoreScreen::HandleState);
		StoreHandle = Sub->OnStoreChanged.AddSP(this, &SBCStoreScreen::HandleChanged);
		ProfileHandle = Sub->OnProfileChanged.AddSP(this, &SBCStoreScreen::HandleChanged);
	}

	TArray<FText> Tabs = {
		BCStyle::Txt(P, "ui.store.tab.featured"), BCStyle::Txt(P, "ui.store.tab.daily"),	 BCStyle::Txt(P, "ui.store.tab.vehicles"),
		BCStyle::Txt(P, "ui.store.tab.equipment"), BCStyle::Txt(P, "ui.store.tab.cosmetics"), BCStyle::Txt(P, "ui.store.tab.bundles"),
		BCStyle::Txt(P, "ui.store.tab.gold"),		  BCStyle::Txt(P, "ui.store.tab.pass"),
	};

	TSharedRef<SWidget> LoginPanel =
		SNew(SBox)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(560.0f)
			[
				BCStyle::Panel(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock).Text(BCStyle::Txt(P, "ui.store.login_required")).Font(BCStyle::FontOutlined(26, true, 1)).ColorAndOpacity(BCStyle::TextPrimary())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 18.0f)
					[
						BCStyle::Label(BCStyle::Txt(P, "ui.store.login_desc"), 15, BCStyle::TextSecondary(), false, true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BCStyle::PrimaryButton(BCStyle::Txt(P, "ui.store.go_online"), [this]() {
							if (ABCPlayerController* Owner = PC.Get())
							{
								Owner->OpenScreen(EBCScreen::Online);
							}
						})
					],
					FMargin(28.0f, 24.0f))
			]
		];

	TSharedRef<SWidget> StorePanel =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BCStyle::TabRow(Tabs, []() { return BCStoreScreenState::Tab; }, [this](int32 Index) { SelectTab(Index); }, 18)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(BCStyle::Font(16, true))
				.ColorAndOpacity(BCStyle::Warning())
				.Visibility_Lambda([]() -> EVisibility {
					return BCStoreScreenState::Tab == BCStoreScreenState::Daily ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.Text_Lambda([this]() -> FText { return DailyCountdown(); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Font(BCStyle::Font(13))
				.ColorAndOpacity(BCStyle::TextMuted())
				.Visibility_Lambda([this]() -> EVisibility {
					const UBCProfileSubsystem* Profile = ProfileSub.Get();
					return (Profile && !Profile->IsOnline()) ? EVisibility::Visible : EVisibility::Collapsed;
				})
				.Text(BCStyle::Txt(P, "ui.store.online_save_note"))
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(ContentBox, SVerticalBox)
			]
		];

	// Online wallet (store purchases always use the online account).
	TSharedRef<SWidget> HeaderExtra =
		SNew(SHorizontalBox)
		.Visibility_Lambda([this]() -> EVisibility { return IsLoggedIn() ? EVisibility::Visible : EVisibility::Collapsed; })
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(STextBlock).Text(BCStyle::Txt(P, "ui.store.online_wallet")).Font(BCStyle::Font(14)).ColorAndOpacity(BCStyle::TextSecondary())
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 12.0f, 0.0f)
		[
			SNew(STextBlock)
			.Font(BCStyle::Font(17, true))
			.ColorAndOpacity(BCStyle::CreditsColor())
			.Text_Lambda([this]() -> FText {
				const UBCOnlineSubsystem* Sub = OnlineSub.Get();
				return BCStyle::CurrencyText(PC.Get(), crash::Currency::Credits, (Sub && Sub->HasProfile()) ? Sub->Profile().credits : 0);
			})
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(BCStyle::Font(17, true))
			.ColorAndOpacity(BCStyle::GoldColor())
			.Text_Lambda([this]() -> FText {
				const UBCOnlineSubsystem* Sub = OnlineSub.Get();
				return BCStyle::CurrencyText(PC.Get(), crash::Currency::Gold, (Sub && Sub->HasProfile()) ? Sub->Profile().gold : 0);
			})
		];

	TSharedRef<SWidget> Body =
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() -> EVisibility { return IsLoggedIn() ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				LoginPanel
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() -> EVisibility { return IsLoggedIn() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				StorePanel
			]
		];

	ChildSlot
	[
		BCStyle::ScreenFrame(P, BCStyle::Txt(P, "ui.store.title"), Body, 0.8f, HeaderExtra)
	];

	Fetch();
	Rebuild();
}

SBCStoreScreen::~SBCStoreScreen()
{
	if (UBCOnlineSubsystem* Sub = OnlineSub.Get())
	{
		Sub->OnStateChanged.Remove(StateHandle);
		Sub->OnStoreChanged.Remove(StoreHandle);
		Sub->OnProfileChanged.Remove(ProfileHandle);
	}
}

bool SBCStoreScreen::IsLoggedIn() const
{
	const UBCOnlineSubsystem* Sub = OnlineSub.Get();
	return Sub && Sub->IsLoggedIn();
}

void SBCStoreScreen::Fetch()
{
	UBCOnlineSubsystem* Sub = OnlineSub.Get();
	if (!Sub || !Sub->IsLoggedIn())
	{
		return;
	}
	bFetchRequested = true;
	Sub->FetchStore(BCStyle::Feedback(PC.Get()));
}

void SBCStoreScreen::SelectTab(int32 Index)
{
	BCStoreScreenState::Tab = FMath::Clamp(Index, 0, BCStoreScreenState::NumTabs - 1);
	Rebuild();
}

void SBCStoreScreen::HandleState()
{
	if (IsLoggedIn() && !bFetchRequested)
	{
		Fetch();
	}
	if (!IsLoggedIn())
	{
		bFetchRequested = false;
	}
	Rebuild();
}

void SBCStoreScreen::HandleChanged()
{
	Rebuild();
}

FText SBCStoreScreen::DailyCountdown() const
{
	const UBCOnlineSubsystem* Sub = OnlineSub.Get();
	if (!Sub || Sub->Store().DailyExpires.IsEmpty())
	{
		return FText::GetEmpty();
	}
	FDateTime Expires;
	if (!FDateTime::ParseIso8601(*Sub->Store().DailyExpires, Expires))
	{
		return FText::GetEmpty();
	}
	const double Seconds = (Expires - FDateTime::UtcNow()).GetTotalSeconds();
	return BCStyle::Fmt(PC.Get(), "ui.store.daily_refresh", {BCStyle::Clock(static_cast<float>(FMath::Max(0.0, Seconds)))});
}

void SBCStoreScreen::Rebuild()
{
	if (!ContentBox.IsValid())
	{
		return;
	}
	ContentBox->ClearChildren();
	ABCPlayerController* P = PC.Get();
	const UBCOnlineSubsystem* Sub = OnlineSub.Get();
	if (!Sub || !Sub->IsLoggedIn())
	{
		return;
	}
	const FBCStoreState& Store = Sub->Store();
	if (!Store.bValid)
	{
		ContentBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 60.0f, 0.0f, 12.0f)
			[
				BCStyle::Label(BCStyle::Txt(P, "ui.common.loading"), 20, BCStyle::TextSecondary(), true)
			];
		ContentBox->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.common.retry"), [this]() { Fetch(); })
			];
		return;
	}

	TArray<const FBCStoreOffer*> Offers;
	switch (BCStoreScreenState::Tab)
	{
	case BCStoreScreenState::Daily:
		for (const FBCStoreOffer& O : Store.Daily)
		{
			Offers.Add(&O);
		}
		BuildOffers(Offers);
		break;
	case BCStoreScreenState::Gold:
		BuildGold();
		break;
	case BCStoreScreenState::Pass:
		BuildPass();
		break;
	default:
	{
		const TCHAR* Section = TEXT("featured");
		switch (BCStoreScreenState::Tab)
		{
		case BCStoreScreenState::Vehicles:
			Section = TEXT("vehicles");
			break;
		case BCStoreScreenState::Equipment:
			Section = TEXT("equipment");
			break;
		case BCStoreScreenState::Cosmetics:
			Section = TEXT("cosmetics");
			break;
		case BCStoreScreenState::Bundles:
			Section = TEXT("bundles");
			break;
		default:
			break;
		}
		const bool bFeatured = BCStoreScreenState::Tab == BCStoreScreenState::Featured;
		for (const FBCStoreOffer& O : Store.Offers)
		{
			if (O.Section == Section || (bFeatured && O.bFeatured))
			{
				Offers.Add(&O);
			}
		}
		BuildOffers(Offers);
		break;
	}
	}
}

void SBCStoreScreen::BuildOffers(const TArray<const FBCStoreOffer*>& Offers)
{
	if (Offers.Num() == 0)
	{
		ContentBox->AddSlot().AutoHeight().Padding(8.0f, 40.0f)[BCStyle::Label(BCStyle::Txt(PC.Get(), "ui.store.empty"), 18, BCStyle::TextMuted())];
		return;
	}
	constexpr int32 Columns = 3;
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(8.0f));
	for (int32 i = 0; i < Offers.Num(); ++i)
	{
		Grid->AddSlot(i % Columns, i / Columns)[MakeOfferCard(*Offers[i])];
	}
	ContentBox->AddSlot().AutoHeight()[Grid];
}

TSharedRef<SWidget> SBCStoreScreen::MakeRewardList(const TArray<crash::ItemRef>& Items, bool bDim)
{
	ABCPlayerController* P = PC.Get();
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(P);
	TSharedRef<SVerticalBox> List = SNew(SVerticalBox);
	if (Items.Num() == 0)
	{
		List->AddSlot().AutoHeight()[BCStyle::Label(FText::FromString(TEXT("\u2014")), 14, BCStyle::TextMuted())];
		return List;
	}
	for (const crash::ItemRef& Item : Items)
	{
		FLinearColor NameColor = bDim ? BCStyle::TextMuted() : BCStyle::TextPrimary();
		if (!bDim && Item.type == crash::ItemType::Currency)
		{
			NameColor = Item.id == "gold" ? BCStyle::GoldColor() : BCStyle::CreditsColor();
		}
		List->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 1.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					BCStyle::Badge(BCStyle::Txt(P, BCStyle::ItemTypeKey(Item.type)), BCStyle::TextSecondary(), 10, false)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					BCStyle::Label(Data ? Data->ItemName(Item) : FText::FromString(BC::ToFString(Item.id)), 14, NameColor, true, true)
				]
			];
	}
	return List;
}

TSharedRef<SWidget> SBCStoreScreen::MakeOfferCard(const FBCStoreOffer& Offer)
{
	ABCPlayerController* P = PC.Get();
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(P);
	const UBCOnlineSubsystem* Sub = OnlineSub.Get();
	if (!Data || !Sub)
	{
		return SNullWidget::NullWidget;
	}
	const crash::GameDatabase& Db = Data->Db();
	const crash::Profile& Profile = Sub->Profile();

	bool bAllOwned = Offer.Items.Num() > 0;
	bool bAnyOwnable = false;
	for (const crash::ItemRef& Item : Offer.Items)
	{
		if (Item.type == crash::ItemType::Currency)
		{
			bAllOwned = false;
			continue;
		}
		bAnyOwnable = true;
		bAllOwned &= Sub->HasProfile() && Profile.Owns(Db, Item);
	}
	bAllOwned &= bAnyOwnable;
	const bool bSoldOut = Offer.Limit > 0 && Offer.Purchased >= Offer.Limit;
	const bool bDiscounted = !Offer.BasePrice.IsFree() && Offer.BasePrice.amount > Offer.Price.amount;
	const FLinearColor Stripe = Offer.bDaily ? BCStyle::Warning() : (Offer.bFeatured ? BCStyle::Accent() : BCStyle::Hex(0x3A, 0x40, 0x49));

	FText Title = FText::GetEmpty();
	FText Kind = FText::GetEmpty();
	if (Offer.Items.Num() > 0)
	{
		Title = Data->ItemName(Offer.Items[0]);
		Kind = BCStyle::Txt(P, BCStyle::ItemTypeKey(Offer.Items[0].type));
		if (Offer.Items.Num() > 1)
		{
			Kind = BCStyle::Fmt(P, "ui.store.bundle_of", {FString::FromInt(Offer.Items.Num())});
		}
	}
	// Paint offers get a color swatch.
	FLinearColor SwatchColor = FLinearColor::Transparent;
	if (Offer.Items.Num() > 0 && Offer.Items[0].type == crash::ItemType::Paint)
	{
		if (const crash::CosmeticDef* Cos = Db.FindCosmetic(Offer.Items[0].id))
		{
			SwatchColor = BCStyle::FromCrash(Cos->color);
		}
	}

	TSharedRef<SHorizontalBox> Badges = SNew(SHorizontalBox);
	if (!Offer.Tag.IsEmpty())
	{
		Badges->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)[BCStyle::Badge(BCStyle::TxtF(P, Offer.Tag), BCStyle::Accent(), 11)];
	}
	if (Offer.bDaily && Offer.Discount > 0.0f)
	{
		Badges->AddSlot().AutoWidth().Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[BCStyle::Badge(FText::FromString(FString::Printf(TEXT("-%d%%"), FMath::RoundToInt(Offer.Discount * 100.0f))), BCStyle::Danger(), 12)];
	}
	if (Offer.Limit > 0)
	{
		Badges->AddSlot().AutoWidth()[BCStyle::Badge(
			BCStyle::Fmt(P, "ui.store.limit", {FString::FromInt(FMath::Min(Offer.Purchased, Offer.Limit)), FString::FromInt(Offer.Limit)}),
			BCStyle::TextSecondary(), 11, false)];
	}

	TSharedRef<SWidget> Body =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			Badges
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, SwatchColor.A > 0.0f ? 10.0f : 0.0f, 0.0f)
			[
				SwatchColor.A > 0.0f ? BCStyle::Swatch(SwatchColor, 34.0f) : SNullWidget::NullWidget
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock).Text(Title).Font(BCStyle::Font(20, true)).ColorAndOpacity(BCStyle::TextPrimary()).AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock).Text(Kind).Font(BCStyle::Font(13)).ColorAndOpacity(BCStyle::TextMuted())
				]
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			Offer.Items.Num() > 1 ? MakeRewardList(Offer.Items, false) : SNullWidget::NullWidget
		];

	// Price + action.
	TSharedRef<SHorizontalBox> Footer = SNew(SHorizontalBox);
	Footer->AddSlot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				bDiscounted ? BCStyle::PriceLabel(P, Offer.BasePrice, 13, true) : SNullWidget::NullWidget
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				BCStyle::PriceLabel(P, Offer.Price, 19)
			]
		];
	if (bAllOwned)
	{
		Footer->AddSlot().AutoWidth().VAlign(VAlign_Center)[BCStyle::Badge(BCStyle::Txt(P, "ui.store.owned"), BCStyle::Success(), 13)];
	}
	else if (bSoldOut)
	{
		Footer->AddSlot().AutoWidth().VAlign(VAlign_Center)[BCStyle::Badge(BCStyle::Txt(P, "ui.store.purchased"), BCStyle::TextMuted(), 13)];
	}
	else
	{
		TWeakObjectPtr<ABCPlayerController> WeakPC = PC;
		TWeakObjectPtr<UBCOnlineSubsystem> WeakSub = OnlineSub;
		const FString OfferId = Offer.Id;
		const bool bDaily = Offer.bDaily;
		const int32 DailySlot = Offer.Slot;
		Footer->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				BCStyle::ConfirmButton(
					P, BCStyle::EButtonKind::Primary, BCStyle::Txt(P, "ui.store.buy"),
					[WeakPC, WeakSub, OfferId, bDaily, DailySlot]() {
						UBCOnlineSubsystem* Online = WeakSub.Get();
						if (!Online)
						{
							return;
						}
						if (bDaily)
						{
							Online->PurchaseDaily(DailySlot, BCStyle::Feedback(WeakPC.Get(), "ui.store.purchase_done"));
						}
						else
						{
							Online->PurchaseOffer(OfferId, BCStyle::Feedback(WeakPC.Get(), "ui.store.purchase_done"));
						}
					},
					true, 110.0f, 16)
			];
	}

	return BCStyle::ItemCard(FText::GetEmpty(), FText::GetEmpty(), Stripe, Body, Footer);
}

void SBCStoreScreen::BuildGold()
{
	ABCPlayerController* P = PC.Get();
	const UBCOnlineSubsystem* Sub = OnlineSub.Get();
	if (!Sub)
	{
		return;
	}
	const TArray<FBCGoldPack>& Packs = Sub->Store().GoldPacks;
	ContentBox->AddSlot()
		.AutoHeight()
		.Padding(8.0f, 0.0f, 8.0f, 10.0f)
		[
			SNew(SBorder)
			.BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x10, 0x22, 0x30, 0.9f), 6.0f, BCStyle::CreditsColor(), 1.0f))
			.Padding(FMargin(14.0f, 8.0f))
			[
				BCStyle::Label(BCStyle::Txt(P, "ui.store.sandbox_note"), 14, BCStyle::CreditsColor(), false, true)
			]
		];
	if (Packs.Num() == 0)
	{
		ContentBox->AddSlot().AutoHeight().Padding(8.0f, 40.0f)[BCStyle::Label(BCStyle::Txt(P, "ui.store.empty"), 18, BCStyle::TextMuted())];
		return;
	}
	constexpr int32 Columns = 3;
	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(8.0f));
	TWeakObjectPtr<ABCPlayerController> WeakPC = PC;
	TWeakObjectPtr<UBCOnlineSubsystem> WeakSub = OnlineSub;
	for (int32 i = 0; i < Packs.Num(); ++i)
	{
		const FBCGoldPack& Pack = Packs[i];
		const FString PackId = Pack.Id;
		TSharedRef<SWidget> Body =
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(BCStyle::CurrencyText(P, crash::Currency::Gold, static_cast<int64>(Pack.Gold)))
				.Font(BCStyle::FontOutlined(32, true, 1))
				.ColorAndOpacity(BCStyle::GoldColor())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Visibility(Pack.Bonus > 0 ? EVisibility::Visible : EVisibility::Collapsed)
				.Text(BCStyle::Fmt(P, "ui.store.gold_bonus", {BCStyle::Num(Pack.Bonus)}))
				.Font(BCStyle::Font(15, true))
				.ColorAndOpacity(BCStyle::Success())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(BCStyle::Fmt(P, "ui.store.price_cny", {FString::Printf(TEXT("%.2f"), Pack.PriceCny)}))
				.Font(BCStyle::Font(22, true))
				.ColorAndOpacity(BCStyle::TextPrimary())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Text(BCStyle::Fmt(P, "ui.store.price_usd", {FString::Printf(TEXT("%.2f"), Pack.PriceUsd)}))
				.Font(BCStyle::Font(14))
				.ColorAndOpacity(BCStyle::TextSecondary())
			];
		TSharedRef<SWidget> Footer = BCStyle::ConfirmButton(
			P, BCStyle::EButtonKind::Primary, BCStyle::Txt(P, "ui.store.buy"),
			[WeakPC, WeakSub, PackId]() {
				UBCOnlineSubsystem* Online = WeakSub.Get();
				if (!Online)
				{
					return;
				}
				Online->BuyGold(PackId, [WeakPC](bool bOk, const FString& Error) {
					ABCPlayerController* Owner = WeakPC.Get();
					if (!Owner)
					{
						return;
					}
					if (bOk && Error.IsEmpty())
					{
						Owner->ShowToast(BCStyle::Txt(Owner, "ui.store.gold_added"));
					}
					else
					{
						Owner->ShowToast(BCStyle::ErrorText(Owner, Error));
					}
				});
			},
			true, 0.0f, 16);
		Grid->AddSlot(i % Columns, i / Columns)[BCStyle::ItemCard(FText::GetEmpty(), FText::GetEmpty(), BCStyle::GoldColor(), Body, Footer)];
	}
	ContentBox->AddSlot().AutoHeight()[Grid];
}

TSharedRef<SWidget> SBCStoreScreen::MakeTierCell(int32 Tier, bool bPremium)
{
	ABCPlayerController* P = PC.Get();
	const UBCOnlineSubsystem* Sub = OnlineSub.Get();
	if (!Sub)
	{
		return SNullWidget::NullWidget;
	}
	const FBCStoreState& Store = Sub->Store();
	const FBCSeasonTier& TierDef = Store.Tiers[Tier - 1];
	const TArray<crash::ItemRef>& Items = bPremium ? TierDef.Premium : TierDef.Free;
	const bool bReached = Tier <= Store.PlayerTier;
	const bool bClaimed = (bPremium ? Store.ClaimedPremium : Store.ClaimedFree).Contains(Tier);
	const bool bTrackOwned = !bPremium || Store.bPremium;

	TSharedRef<SWidget> Action = SNullWidget::NullWidget;
	if (Items.Num() == 0)
	{
		Action = SNullWidget::NullWidget;
	}
	else if (bClaimed)
	{
		Action = BCStyle::Label(BCStyle::Txt(P, "ui.store.claimed"), 13, BCStyle::Success(), true);
	}
	else if (!bTrackOwned)
	{
		Action = BCStyle::Label(BCStyle::Txt(P, "ui.store.premium_locked"), 13, BCStyle::GoldColor(), true);
	}
	else if (!bReached)
	{
		Action = BCStyle::Label(BCStyle::Txt(P, "ui.common.locked"), 13, BCStyle::TextMuted(), true);
	}
	else
	{
		TWeakObjectPtr<ABCPlayerController> WeakPC = PC;
		TWeakObjectPtr<UBCOnlineSubsystem> WeakSub = OnlineSub;
		Action = BCStyle::PrimaryButton(BCStyle::Txt(P, "ui.store.claim"),
										[WeakPC, WeakSub, Tier, bPremium]() {
											if (UBCOnlineSubsystem* Online = WeakSub.Get())
											{
												Online->ClaimSeasonTier(Tier, bPremium, BCStyle::Feedback(WeakPC.Get(), "ui.store.claim_done"));
											}
										},
										true, 0.0f, 14);
	}

	return SNew(SBorder)
		.BorderImage(bPremium ? BCStyle::RoundedBrush(BCStyle::Hex(0x2A, 0x22, 0x0A, 0.85f), 6.0f) : BCStyle::CardBrush())
		.Padding(FMargin(10.0f, 6.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				MakeRewardList(Items, !bReached || bClaimed)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f, 0.0f, 0.0f)
			[
				Action
			]
		];
}

void SBCStoreScreen::BuildPass()
{
	ABCPlayerController* P = PC.Get();
	const UBCOnlineSubsystem* Sub = OnlineSub.Get();
	if (!Sub)
	{
		return;
	}
	const FBCStoreState& Store = Sub->Store();
	if (Store.Tiers.Num() == 0)
	{
		ContentBox->AddSlot().AutoHeight().Padding(8.0f, 40.0f)[BCStyle::Label(BCStyle::Txt(P, "ui.store.no_season"), 18, BCStyle::TextMuted())];
		return;
	}
	const int64 PerTier = FMath::Max<int64>(1, Store.XpPerTier);
	const bool bMaxed = Store.PlayerTier >= Store.Tiers.Num();
	const float TierProgress = bMaxed ? 1.0f : static_cast<float>(static_cast<double>(Store.PlayerXp % PerTier) / static_cast<double>(PerTier));
	TWeakObjectPtr<ABCPlayerController> WeakPC = PC;
	TWeakObjectPtr<UBCOnlineSubsystem> WeakSub = OnlineSub;

	TSharedRef<SWidget> PremiumAction = Store.bPremium
		? BCStyle::Badge(BCStyle::Txt(P, "ui.store.premium_active"), BCStyle::GoldColor(), 15)
		: BCStyle::ConfirmButton(
			  P, BCStyle::EButtonKind::Primary,
			  BCStyle::Fmt(P, "ui.store.buy_premium", {BCStyle::PriceText(P, Store.PremiumPrice).ToString()}),
			  [WeakPC, WeakSub]() {
				  if (UBCOnlineSubsystem* Online = WeakSub.Get())
				  {
					  Online->BuySeasonPremium(BCStyle::Feedback(WeakPC.Get(), "ui.store.premium_done"));
				  }
			  },
			  true, 260.0f, 17);

	ContentBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 12.0f)
		[
			BCStyle::Panel(
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(Store.SeasonName.IsEmpty() ? BCStyle::Txt(P, "ui.store.tab.pass") : BCStyle::TxtF(P, Store.SeasonName))
						.Font(BCStyle::FontOutlined(28, true, 1))
						.ColorAndOpacity(BCStyle::GoldColor())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 6.0f)
					[
						SNew(STextBlock)
						.Text(BCStyle::Fmt(P, "ui.store.pass_tier", {FString::FromInt(Store.PlayerTier), FString::FromInt(Store.Tiers.Num())}))
						.Font(BCStyle::Font(18, true))
						.ColorAndOpacity(BCStyle::TextPrimary())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SBox)
						.WidthOverride(420.0f)
						[
							BCStyle::Bar(TOptional<float>(TierProgress), FSlateColor(BCStyle::GoldColor()), 10.0f)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(bMaxed ? BCStyle::Txt(P, "ui.store.pass_maxed")
									 : BCStyle::Fmt(P, "ui.store.pass_xp", {BCStyle::Num(Store.PlayerXp % PerTier), BCStyle::Num(PerTier)}))
						.Font(BCStyle::Font(13))
						.ColorAndOpacity(BCStyle::TextMuted())
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					PremiumAction
				],
				FMargin(20.0f, 16.0f))
		];

	// Column headers.
	ContentBox->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox).WidthOverride(80.0f)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(4.0f, 0.0f)
			[
				BCStyle::Label(BCStyle::Txt(P, "ui.store.track_free"), 15, BCStyle::TextSecondary(), true)
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(4.0f, 0.0f)
			[
				BCStyle::Label(BCStyle::Txt(P, "ui.store.track_premium"), 15, BCStyle::GoldColor(), true)
			]
		];

	for (int32 Tier = 1; Tier <= Store.Tiers.Num(); ++Tier)
	{
		const bool bReached = Tier <= Store.PlayerTier;
		ContentBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Fill)
				[
					SNew(SBox)
					.WidthOverride(80.0f)
					[
						SNew(SBorder)
						.BorderImage(BCStyle::RoundedBrush(bReached ? BCStyle::Accent() : BCStyle::Hex(0x26, 0x2A, 0x31), 6.0f))
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Text(FText::AsNumber(Tier))
							.Font(BCStyle::Font(22, true))
							.ColorAndOpacity(bReached ? BCStyle::TextOnAccent() : BCStyle::TextMuted())
						]
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(4.0f, 0.0f)
				[
					MakeTierCell(Tier, false)
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(4.0f, 0.0f)
				[
					MakeTierCell(Tier, true)
				]
			];
	}
}

TSharedRef<SWidget> BCUI::MakeStore(ABCPlayerController* PC)
{
	return SNew(SBCStoreScreen).Owner(PC);
}
