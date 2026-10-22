// Copyright BigCar Team. All Rights Reserved.
//
// Garage: filterable vehicle list (left), the rotating showroom car with buy/select actions (center) and tabs for
// performance (ratings, numbers, upgrades), equipment (7 slots, melee-only items) and looks (paints, wheels, loadout
// cosmetics) on the right. Operates on the active profile (local, or online when "use online save" is on).

#include "CoreGlobals.h"
#include "Core/BCDataSubsystem.h"
#include "Core/BCProfileSubsystem.h"
#include "Core/BigCarTypes.h"
#include "CrashCore/Abilities.h"
#include "CrashCore/Profile.h"
#include "CrashCore/Stats.h"
#include "Gameplay/BCPlayerController.h"
#include "Online/BCOnlineSubsystem.h"
#include "UI/BCUI.h"
#include "UI/BCUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SUniformGridPanel.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

#include <algorithm>

namespace BCGarageState
{
int32 RightTab = 0;
int32 OwnedFilter = 0; // 0 = all, 1 = owned
int32 ClassFilter = -1; // -1 = all classes, else crash::VehicleClass
int32 EquipSlotIndex = 0;
int32 CosmeticTab = 0;	// 0 paints, 1 wheels, 2 loadout
} // namespace BCGarageState

class SBCGarageScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBCGarageScreen) : _Owner(nullptr) {}
		SLATE_ARGUMENT(ABCPlayerController*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SBCGarageScreen() override;

private:
	const crash::GameDatabase* GetDb() const;
	const crash::Profile* GetProfile() const;
	const crash::VehicleDef* GetVehicleDef() const;
	const crash::VehicleBuild* GetOwnedBuild() const;
	crash::VehicleBuild GetBuild() const;
	bool IsSelectedCar() const;
	void UpdateStatsCache() const;
	static bool IsGoldOnly(const crash::Price& Price) { return Price.currency == crash::Currency::Gold && !Price.IsFree(); }

	TArray<const crash::VehicleDef*> FilteredVehicles() const;
	TArray<int32> PresentClasses() const;
	void CycleClass(int32 Dir);
	void SelectVehicle(const FString& Id);
	void RefreshPreview();
	void HandleProfileChanged();
	void GoToStore();

	void RebuildVehicleList();
	void RebuildEquipItems();
	void RebuildCosmetics();

	TSharedRef<SWidget> MakeVehicleList();
	TSharedRef<SWidget> MakeVehicleRow(const crash::VehicleDef& Def);
	TSharedRef<SWidget> MakeCarInfo();
	TSharedRef<SWidget> MakePerformanceTab();
	TSharedRef<SWidget> MakeUpgradeRow(crash::UpgradeCategory Category);
	TSharedRef<SWidget> MakeEquipmentTab();
	TSharedRef<SWidget> MakeEquipItemCard(const crash::EquipmentDef& Item);
	TSharedRef<SWidget> MakeCosmeticsTab();
	TSharedRef<SWidget> MakeCosmeticCard(const crash::CosmeticDef& Cosmetic);
	TSharedRef<SWidget> MakeNumber(const char* NameKey, TFunction<FText()> Value);

	TWeakObjectPtr<ABCPlayerController> PC;
	TWeakObjectPtr<UBCProfileSubsystem> ProfileSub;
	FDelegateHandle ProfileHandle;
	FString VehicleId;
	FString PreviewPaint;
	FString PreviewWheels;
	TSharedPtr<SVerticalBox> VehicleList;
	TSharedPtr<SVerticalBox> EquipItems;
	TSharedPtr<SVerticalBox> CosmeticBox;

	mutable uint64 CacheFrame = MAX_uint64;
	mutable crash::EffectiveStats CachedStats;
	mutable crash::DisplayRatings CachedRatings;
	mutable int32 CachedCr = 0;
};

void SBCGarageScreen::Construct(const FArguments& InArgs)
{
	PC = InArgs._Owner;
	ABCPlayerController* P = PC.Get();
	if (UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(P))
	{
		ProfileSub = Profile;
		ProfileHandle = Profile->OnChanged.AddSP(this, &SBCGarageScreen::HandleProfileChanged);
		VehicleId = BC::ToFString(Profile->Active().selected);
	}
	if (!GetVehicleDef())
	{
		const TArray<const crash::VehicleDef*> All = FilteredVehicles();
		VehicleId = All.Num() > 0 ? BC::ToFString(All[0]->id) : FString();
	}

	TArray<FText> RightTabs = {BCStyle::Txt(P, "ui.garage.tab.performance"), BCStyle::Txt(P, "ui.garage.tab.equipment"),
							   BCStyle::Txt(P, "ui.garage.tab.looks")};

	TSharedRef<SWidget> Right =
		SNew(SBox)
		.WidthOverride(540.0f)
		[
			BCStyle::Panel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					BCStyle::TabRow(RightTabs, []() { return BCGarageState::RightTab; }, [](int32 Index) { BCGarageState::RightTab = Index; }, 19)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SWidgetSwitcher)
					.WidgetIndex_Lambda([]() { return BCGarageState::RightTab; })
					+ SWidgetSwitcher::Slot()[MakePerformanceTab()]
					+ SWidgetSwitcher::Slot()[MakeEquipmentTab()]
					+ SWidgetSwitcher::Slot()[MakeCosmeticsTab()]
				],
				FMargin(16.0f, 12.0f))
		];

	TSharedRef<SWidget> Body =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(400.0f)
			[
				MakeVehicleList()
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Bottom)
		.HAlign(HAlign_Center)
		.Padding(16.0f, 0.0f)
		[
			MakeCarInfo()
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			Right
		];

	// Online save switch (garage operations go to the backend profile while it is on).
	TSharedRef<SWidget> HeaderExtra =
		SNew(SHorizontalBox)
		.ToolTipText(BCStyle::Txt(P, "ui.garage.online_toggle_hint"))
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SSpacer)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SNew(STextBlock).Text(BCStyle::Txt(P, "ui.online.use_online_save")).Font(BCStyle::Font(15)).ColorAndOpacity(BCStyle::TextSecondary())
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			BCStyle::Toggle(
				P,
				[this]() {
					const UBCProfileSubsystem* Profile = ProfileSub.Get();
					return Profile && Profile->IsOnline();
				},
				[this](bool bOn) {
					if (UBCProfileSubsystem* Profile = ProfileSub.Get())
					{
						Profile->SetPreferOnline(bOn);
					}
				},
				BCStyle::Bind<bool>([this]() {
					const UBCOnlineSubsystem* Online = UBCOnlineSubsystem::Get(PC.Get());
					return Online && Online->IsLoggedIn() && Online->HasProfile();
				}))
		];

	ChildSlot
	[
		BCStyle::ScreenFrame(P, BCStyle::Txt(P, "ui.garage.title"), Body, 0.25f, HeaderExtra)
	];

	RebuildVehicleList();
	RebuildEquipItems();
	RebuildCosmetics();
	RefreshPreview();
}

SBCGarageScreen::~SBCGarageScreen()
{
	if (UBCProfileSubsystem* Profile = ProfileSub.Get())
	{
		Profile->OnChanged.Remove(ProfileHandle);
	}
}

// ---------------------------------------------------------------------------------------------
// Data

const crash::GameDatabase* SBCGarageScreen::GetDb() const
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
	return Data ? &Data->Db() : nullptr;
}

const crash::Profile* SBCGarageScreen::GetProfile() const
{
	const UBCProfileSubsystem* Profile = ProfileSub.Get();
	return Profile ? &Profile->Active() : nullptr;
}

const crash::VehicleDef* SBCGarageScreen::GetVehicleDef() const
{
	const crash::GameDatabase* Db = GetDb();
	return (Db && !VehicleId.IsEmpty()) ? Db->FindVehicle(BC::ToStd(VehicleId)) : nullptr;
}

const crash::VehicleBuild* SBCGarageScreen::GetOwnedBuild() const
{
	const crash::Profile* Pr = GetProfile();
	return Pr ? Pr->FindVehicle(BC::ToStd(VehicleId)) : nullptr;
}

crash::VehicleBuild SBCGarageScreen::GetBuild() const
{
	if (const crash::VehicleBuild* Owned = GetOwnedBuild())
	{
		return *Owned;
	}
	const crash::GameDatabase* Db = GetDb();
	if (Db && GetVehicleDef())
	{
		return crash::MakeDefaultBuild(*Db, BC::ToStd(VehicleId));
	}
	return crash::VehicleBuild();
}

bool SBCGarageScreen::IsSelectedCar() const
{
	const crash::Profile* Pr = GetProfile();
	return Pr && BC::ToFString(Pr->selected) == VehicleId;
}

void SBCGarageScreen::UpdateStatsCache() const
{
	if (CacheFrame == GFrameCounter)
	{
		return;
	}
	CacheFrame = GFrameCounter;
	const crash::GameDatabase* Db = GetDb();
	if (!Db || !GetVehicleDef())
	{
		CachedStats = crash::EffectiveStats();
		CachedRatings = crash::DisplayRatings();
		CachedCr = 0;
		return;
	}
	const crash::VehicleBuild Build = GetBuild();
	CachedStats = crash::ComputeEffectiveStats(*Db, Build);
	CachedRatings = crash::ComputeDisplayRatings(CachedStats);
	CachedCr = crash::ComputeCombatRating(*Db, Build);
}

TArray<const crash::VehicleDef*> SBCGarageScreen::FilteredVehicles() const
{
	TArray<const crash::VehicleDef*> Out;
	const crash::GameDatabase* Db = GetDb();
	const crash::Profile* Pr = GetProfile();
	if (!Db)
	{
		return Out;
	}
	for (const crash::VehicleDef& V : Db->Vehicles())
	{
		if (BCGarageState::OwnedFilter == 1 && !(Pr && Pr->OwnsVehicle(V.id)))
		{
			continue;
		}
		if (BCGarageState::ClassFilter >= 0 && static_cast<int32>(V.vehicleClass) != BCGarageState::ClassFilter)
		{
			continue;
		}
		Out.Add(&V);
	}
	Out.Sort([](const crash::VehicleDef& A, const crash::VehicleDef& B) {
		if (A.tier != B.tier)
		{
			return A.tier < B.tier;
		}
		if (A.price.amount != B.price.amount)
		{
			return A.price.amount < B.price.amount;
		}
		return A.id < B.id;
	});
	return Out;
}

TArray<int32> SBCGarageScreen::PresentClasses() const
{
	TArray<int32> Out;
	if (const crash::GameDatabase* Db = GetDb())
	{
		for (const crash::VehicleDef& V : Db->Vehicles())
		{
			Out.AddUnique(static_cast<int32>(V.vehicleClass));
		}
	}
	Out.Sort();
	return Out;
}

void SBCGarageScreen::CycleClass(int32 Dir)
{
	TArray<int32> Classes = PresentClasses();
	Classes.Insert(-1, 0);
	int32 Index = Classes.IndexOfByKey(BCGarageState::ClassFilter);
	if (Index == INDEX_NONE)
	{
		Index = 0;
	}
	Index = (Index + Dir + Classes.Num()) % Classes.Num();
	BCGarageState::ClassFilter = Classes[Index];
	RebuildVehicleList();
}

void SBCGarageScreen::SelectVehicle(const FString& Id)
{
	if (Id == VehicleId)
	{
		return;
	}
	VehicleId = Id;
	PreviewPaint.Reset();
	PreviewWheels.Reset();
	CacheFrame = MAX_uint64;
	RebuildEquipItems();
	RebuildCosmetics();
	RefreshPreview();
}

void SBCGarageScreen::RefreshPreview()
{
	ABCPlayerController* P = PC.Get();
	if (!P || !GetVehicleDef())
	{
		return;
	}
	crash::VehicleBuild Build = GetBuild();
	if (!PreviewPaint.IsEmpty())
	{
		Build.paint = BC::ToStd(PreviewPaint);
	}
	if (!PreviewWheels.IsEmpty())
	{
		Build.wheels = BC::ToStd(PreviewWheels);
	}
	P->PreviewBuild(FBCVehicleBuild::FromCrash(Build));
}

void SBCGarageScreen::HandleProfileChanged()
{
	CacheFrame = MAX_uint64;
	RebuildVehicleList();
	RebuildEquipItems();
	RebuildCosmetics();
	RefreshPreview();
}

void SBCGarageScreen::GoToStore()
{
	if (ABCPlayerController* P = PC.Get())
	{
		P->OpenScreen(EBCScreen::Store);
	}
}

// ---------------------------------------------------------------------------------------------
// Vehicle list

TSharedRef<SWidget> SBCGarageScreen::MakeVehicleList()
{
	ABCPlayerController* P = PC.Get();
	TArray<FText> Filters = {BCStyle::Txt(P, "ui.garage.filter.all"), BCStyle::Txt(P, "ui.garage.filter.owned")};
	return BCStyle::Panel(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			BCStyle::TabRow(
				Filters, []() { return BCGarageState::OwnedFilter; },
				[this](int32 Index) {
					BCGarageState::OwnedFilter = Index;
					RebuildVehicleList();
				},
				16)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.0f, 8.0f, 0.0f, 8.0f)
		[
			BCStyle::CycleSelector(BCStyle::Bind<FText>([this]() -> FText {
									   if (BCGarageState::ClassFilter < 0)
									   {
										   return BCStyle::Txt(PC.Get(), "ui.garage.all_classes");
									   }
									   return BCStyle::Txt(PC.Get(), BCStyle::ClassKey(static_cast<crash::VehicleClass>(BCGarageState::ClassFilter)));
								   }),
								   [this]() { CycleClass(-1); }, [this]() { CycleClass(1); }, true, 340.0f)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(VehicleList, SVerticalBox)
			]
		],
		FMargin(12.0f, 10.0f));
}

void SBCGarageScreen::RebuildVehicleList()
{
	if (!VehicleList.IsValid())
	{
		return;
	}
	VehicleList->ClearChildren();
	const TArray<const crash::VehicleDef*> Vehicles = FilteredVehicles();
	if (Vehicles.Num() == 0)
	{
		VehicleList->AddSlot().AutoHeight().Padding(8.0f)[BCStyle::Label(BCStyle::Txt(PC.Get(), "ui.garage.no_vehicles"), 15, BCStyle::TextMuted())];
		return;
	}
	for (const crash::VehicleDef* V : Vehicles)
	{
		VehicleList->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[MakeVehicleRow(*V)];
	}
}

TSharedRef<SWidget> SBCGarageScreen::MakeVehicleRow(const crash::VehicleDef& Def)
{
	ABCPlayerController* P = PC.Get();
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(P);
	const crash::GameDatabase* Db = GetDb();
	const crash::Profile* Pr = GetProfile();
	const bool bOwned = Pr && Pr->OwnsVehicle(Def.id);
	const bool bSelected = Pr && Pr->selected == Def.id;
	const bool bLevelLocked = !bOwned && Pr && Pr->level < Def.unlockLevel;
	const crash::BrandDef* Brand = Db ? Db->FindBrand(Def.brand) : nullptr;
	const FString Id = BC::ToFString(Def.id);

	TSharedRef<SWidget> Status = SNullWidget::NullWidget;
	if (bSelected)
	{
		Status = BCStyle::Badge(BCStyle::Txt(P, "ui.garage.in_use"), BCStyle::Accent(), 12);
	}
	else if (bOwned)
	{
		Status = BCStyle::Label(BCStyle::Txt(P, "ui.garage.owned"), 14, BCStyle::Success(), true);
	}
	else if (bLevelLocked)
	{
		Status = BCStyle::Label(BCStyle::Fmt(P, "ui.garage.unlock_level", {FString::FromInt(Def.unlockLevel)}), 13, BCStyle::Warning(), true);
	}
	else
	{
		Status = BCStyle::PriceLabel(P, Def.price, 15);
	}

	TSharedRef<SWidget> Content =
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
				.Text(Data ? Data->VehicleName(Def) : FText::FromString(Id))
				.Font(BCStyle::Font(16, true))
				.ColorAndOpacity(bLevelLocked ? BCStyle::TextMuted() : BCStyle::TextPrimary())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("T%d  \u00B7  "), Def.tier) + BCStyle::Txt(P, BCStyle::ClassKey(Def.vehicleClass)).ToString()))
				.Font(BCStyle::Font(13))
				.ColorAndOpacity(BCStyle::TextMuted())
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(8.0f, 0.0f, 0.0f, 0.0f)
		[
			Status
		];

	return BCStyle::SelectableCard(
		Content, [this, Id]() { return VehicleId == Id; }, [this, Id]() { SelectVehicle(Id); },
		Brand ? BCStyle::FromCrash(Brand->color) : BCStyle::TextMuted(), FMargin(10.0f, 8.0f));
}

// ---------------------------------------------------------------------------------------------
// Center: name + actions

TSharedRef<SWidget> SBCGarageScreen::MakeCarInfo()
{
	ABCPlayerController* P = PC.Get();
	auto Owned = [this]() { return GetOwnedBuild() != nullptr; };
	auto LevelLocked = [this]() {
		const crash::VehicleDef* Def = GetVehicleDef();
		const crash::Profile* Pr = GetProfile();
		return Def && Pr && !Pr->OwnsVehicle(Def->id) && Pr->level < Def->unlockLevel;
	};
	auto GoldOnly = [this]() {
		const crash::VehicleDef* Def = GetVehicleDef();
		return Def && IsGoldOnly(Def->price);
	};

	return SNew(SBorder)
		.BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x08, 0x0A, 0x0D, 0.72f), 10.0f))
		.Padding(FMargin(28.0f, 16.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock)
				.Font(BCStyle::FontOutlined(34, true, 2))
				.ColorAndOpacity(BCStyle::TextPrimary())
				.Text_Lambda([this]() -> FText {
					const crash::VehicleDef* Def = GetVehicleDef();
					const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
					return (Def && Data) ? Data->VehicleName(*Def) : FText::GetEmpty();
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(BCStyle::Font(16))
				.ColorAndOpacity(BCStyle::TextSecondary())
				.Text_Lambda([this]() -> FText {
					const crash::VehicleDef* Def = GetVehicleDef();
					const crash::GameDatabase* Db = GetDb();
					const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
					if (!Def || !Db || !Data)
					{
						return FText::GetEmpty();
					}
					const crash::BrandDef* Brand = Db->FindBrand(Def->brand);
					FString Line = Brand ? Data->BrandName(*Brand).ToString() + TEXT("  \u00B7  ") : FString();
					Line += BCStyle::Txt(PC.Get(), BCStyle::ClassKey(Def->vehicleClass)).ToString() + TEXT("  \u00B7  ") +
							BCStyle::Txt(PC.Get(), BCStyle::DrivetrainKey(Def->drivetrain)).ToString() + FString::Printf(TEXT("  \u00B7  T%d"), Def->tier);
					return FText::FromString(Line);
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 4.0f, 0.0f, 10.0f)
			[
				SNew(STextBlock)
				.Font(BCStyle::Font(20, true))
				.ColorAndOpacity(BCStyle::Warning())
				.Text_Lambda([this]() -> FText {
					UpdateStatsCache();
					return BCStyle::Fmt(PC.Get(), "ui.menu.combat_rating", {FString::FromInt(CachedCr)});
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			[
				SNew(SHorizontalBox)
				// Buy.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f)
				[
					SNew(SBox)
					.Visibility_Lambda([Owned, GoldOnly]() -> EVisibility { return (!Owned() && !GoldOnly()) ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						BCStyle::ConfirmButton(
							P, BCStyle::EButtonKind::Primary,
							BCStyle::Bind<FText>([this, LevelLocked]() -> FText {
								const crash::VehicleDef* Def = GetVehicleDef();
								if (!Def)
								{
									return FText::GetEmpty();
								}
								if (LevelLocked())
								{
									return BCStyle::Fmt(PC.Get(), "ui.garage.unlock_level", {FString::FromInt(Def->unlockLevel)});
								}
								return BCStyle::Fmt(PC.Get(), "ui.garage.buy_for", {BCStyle::PriceText(PC.Get(), Def->price).ToString()});
							}),
							[this]() {
								UBCProfileSubsystem* Profile = ProfileSub.Get();
								if (Profile && !VehicleId.IsEmpty())
								{
									Profile->BuyVehicle(VehicleId, BCStyle::Feedback(PC.Get(), "ui.garage.bought"));
								}
							},
							BCStyle::Bind<bool>([LevelLocked]() { return !LevelLocked(); }), 240.0f, 20)
					]
				]
				// Gold-only cars come from the store.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f)
				[
					SNew(SBox)
					.Visibility_Lambda([Owned, GoldOnly]() -> EVisibility { return (!Owned() && GoldOnly()) ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.garage.get_in_store"), [this]() { GoToStore(); }, true, 240.0f, 18)
					]
				]
				// Select.
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(4.0f, 0.0f)
				[
					SNew(SBox)
					.Visibility_Lambda([this, Owned]() -> EVisibility { return (Owned() && !IsSelectedCar()) ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						BCStyle::PrimaryButton(BCStyle::Txt(P, "ui.garage.select"),
											   [this]() {
												   if (UBCProfileSubsystem* Profile = ProfileSub.Get())
												   {
													   Profile->SelectVehicle(VehicleId, BCStyle::Feedback(PC.Get(), "ui.garage.selected"));
												   }
											   },
											   true, 240.0f, 20)
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f)
				[
					SNew(SBox)
					.Visibility_Lambda([this, Owned]() -> EVisibility { return (Owned() && IsSelectedCar()) ? EVisibility::Visible : EVisibility::Collapsed; })
					[
						BCStyle::Badge(BCStyle::Txt(P, "ui.garage.current_car"), BCStyle::Accent(), 16)
					]
				]
			]
		];
}

// ---------------------------------------------------------------------------------------------
// Performance

TSharedRef<SWidget> SBCGarageScreen::MakeNumber(const char* NameKey, TFunction<FText()> Value)
{
	return SNew(SBorder)
		.BorderImage(BCStyle::CardBrush())
		.Padding(FMargin(10.0f, 6.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock).Text(BCStyle::Txt(PC.Get(), NameKey)).Font(BCStyle::Font(12)).ColorAndOpacity(BCStyle::TextMuted())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(BCStyle::Font(18, true))
				.ColorAndOpacity(BCStyle::TextPrimary())
				.Text_Lambda([Value]() -> FText { return Value ? Value() : FText::GetEmpty(); })
			]
		];
}

TSharedRef<SWidget> SBCGarageScreen::MakePerformanceTab()
{
	ABCPlayerController* P = PC.Get();
	TSharedRef<SVerticalBox> Bars = SNew(SVerticalBox);
	struct FStatLine
	{
		const char* Key;
		float crash::DisplayRatings::*Field;
	};
	static const FStatLine Lines[] = {
		{"ui.stat.speed", &crash::DisplayRatings::speed},		  {"ui.stat.acceleration", &crash::DisplayRatings::acceleration},
		{"ui.stat.handling", &crash::DisplayRatings::handling}, {"ui.stat.weight", &crash::DisplayRatings::weight},
		{"ui.stat.durability", &crash::DisplayRatings::durability}, {"ui.stat.ramming", &crash::DisplayRatings::ramming},
	};
	for (const FStatLine& Line : Lines)
	{
		float crash::DisplayRatings::*Field = Line.Field;
		Bars->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 3.0f)
			[
				BCStyle::StatBar(BCStyle::Txt(P, Line.Key), [this, Field]() {
					UpdateStatsCache();
					return CachedRatings.*Field;
				})
			];
	}

	TSharedRef<SUniformGridPanel> Numbers = SNew(SUniformGridPanel).SlotPadding(FMargin(3.0f));
	Numbers->AddSlot(0, 0)[MakeNumber("ui.stat.power", [this]() -> FText {
		UpdateStatsCache();
		return BCStyle::Fmt(PC.Get(), "ui.unit.kw", {FString::FromInt(FMath::RoundToInt(CachedStats.powerW / 1000.0f))});
	})];
	Numbers->AddSlot(1, 0)[MakeNumber("ui.stat.torque", [this]() -> FText {
		UpdateStatsCache();
		return BCStyle::Fmt(PC.Get(), "ui.unit.nm", {FString::FromInt(FMath::RoundToInt(CachedStats.torqueNm))});
	})];
	Numbers->AddSlot(2, 0)[MakeNumber("ui.stat.mass", [this]() -> FText {
		UpdateStatsCache();
		return BCStyle::Fmt(PC.Get(), "ui.unit.kg", {BCStyle::Num(FMath::RoundToInt(CachedStats.massKg))});
	})];
	Numbers->AddSlot(0, 1)[MakeNumber("ui.stat.top_speed", [this]() -> FText {
		UpdateStatsCache();
		return BCStyle::Fmt(PC.Get(), "ui.unit.kmh", {FString::FromInt(FMath::RoundToInt(CachedStats.topSpeed * 3.6f))});
	})];
	Numbers->AddSlot(1, 1)[MakeNumber("ui.stat.drivetrain", [this]() -> FText {
		const crash::VehicleDef* Def = GetVehicleDef();
		return Def ? BCStyle::Txt(PC.Get(), BCStyle::DrivetrainKey(Def->drivetrain)) : FText::GetEmpty();
	})];
	Numbers->AddSlot(2, 1)[MakeNumber("ui.stat.cr", [this]() -> FText {
		UpdateStatsCache();
		return FText::AsNumber(CachedCr);
	})];

	TSharedRef<SVerticalBox> Upgrades = SNew(SVerticalBox);
	for (int32 c = 0; c < crash::kUpgradeCategoryCount; ++c)
	{
		Upgrades->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 5.0f)[MakeUpgradeRow(static_cast<crash::UpgradeCategory>(c))];
	}

	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Bars]
			+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)[Numbers]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 16.0f, 0.0f, 8.0f)
			[
				BCStyle::SectionHeader(BCStyle::Txt(P, "ui.garage.upgrades"))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([this]() -> EVisibility { return GetOwnedBuild() ? EVisibility::Collapsed : EVisibility::Visible; })
				[
					BCStyle::Label(BCStyle::Txt(P, "ui.garage.buy_to_upgrade"), 14, BCStyle::Warning(), false, true)
				]
			]
			+ SVerticalBox::Slot().AutoHeight()[Upgrades]
		];
}

TSharedRef<SWidget> SBCGarageScreen::MakeUpgradeRow(crash::UpgradeCategory Category)
{
	ABCPlayerController* P = PC.Get();
	const crash::GameDatabase* Db = GetDb();
	if (!Db)
	{
		return SNullWidget::NullWidget;
	}
	const crash::UpgradeCategoryDef& CatDef = Db->Upgrade(Category);
	auto Level = [this, Category]() -> int32 {
		const crash::VehicleBuild* Owned = GetOwnedBuild();
		return Owned ? Owned->UpgradeLevel(Category) : 0;
	};
	auto NextCost = [this, Category, Level]() -> int64 {
		const crash::GameDatabase* Database = GetDb();
		const crash::VehicleDef* Def = GetVehicleDef();
		const int32 L = Level();
		if (!Database || !Def || L >= crash::kMaxUpgradeLevel)
		{
			return -1;
		}
		return static_cast<int64>(Database->UpgradeCost(*Def, Category, L + 1));
	};

	TSharedRef<SHorizontalBox> Pips = SNew(SHorizontalBox);
	for (int32 i = 0; i < crash::kMaxUpgradeLevel; ++i)
	{
		Pips->AddSlot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 3.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(16.0f)
				.HeightOverride(9.0f)
				[
					SNew(SBorder)
					.BorderImage(BCStyle::RoundedBrush(FLinearColor::White, 2.0f))
					.Padding(0.0f)
					.BorderBackgroundColor_Lambda([Level, i]() -> FSlateColor {
						return i < Level() ? FSlateColor(BCStyle::Accent()) : FSlateColor(BCStyle::Hex(0x33, 0x38, 0x40));
					})
				]
			];
	}

	return SNew(SBorder)
		.BorderImage(BCStyle::CardBrush())
		.Padding(FMargin(12.0f, 8.0f))
		.ToolTipText(BCStyle::TxtStd(P, CatDef.description))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock).Text(BCStyle::TxtStd(P, CatDef.name)).Font(BCStyle::Font(16, true)).ColorAndOpacity(BCStyle::TextPrimary())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					Pips
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(8.0f, 0.0f)
			[
				SNew(SBox)
				.WidthOverride(120.0f)
				[
					SNew(STextBlock)
					.Justification(ETextJustify::Right)
					.Font(BCStyle::Font(15, true))
					.ColorAndOpacity_Lambda([NextCost]() -> FSlateColor {
						return NextCost() < 0 ? FSlateColor(BCStyle::Success()) : FSlateColor(BCStyle::CreditsColor());
					})
					.Text_Lambda([this, NextCost]() -> FText {
						const int64 Cost = NextCost();
						return Cost < 0 ? BCStyle::Txt(PC.Get(), "ui.garage.max_level") : BCStyle::CurrencyText(PC.Get(), crash::Currency::Credits, Cost);
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.garage.upgrade"),
										 [this, Category]() {
											 if (UBCProfileSubsystem* Profile = ProfileSub.Get())
											 {
												 Profile->Upgrade(VehicleId, Category, BCStyle::Feedback(PC.Get(), "ui.garage.upgraded"));
											 }
										 },
										 BCStyle::Bind<bool>([this, NextCost]() {
											 const crash::Profile* Pr = GetProfile();
											 const int64 Cost = NextCost();
											 return GetOwnedBuild() && Pr && Cost >= 0 && static_cast<int64>(Pr->credits) >= Cost;
										 }),
										 96.0f, 15)
			]
		];
}

// ---------------------------------------------------------------------------------------------
// Equipment

TSharedRef<SWidget> SBCGarageScreen::MakeEquipmentTab()
{
	ABCPlayerController* P = PC.Get();
	TSharedRef<SUniformGridPanel> Slots = SNew(SUniformGridPanel).SlotPadding(FMargin(3.0f));
	for (int32 s = 0; s < crash::kEquipSlotCount; ++s)
	{
		const crash::EquipSlot SlotType = static_cast<crash::EquipSlot>(s);
		TSharedRef<SWidget> Content =
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock).Text(BCStyle::Txt(P, BCStyle::SlotKey(SlotType))).Font(BCStyle::Font(12, true)).ColorAndOpacity(BCStyle::TextMuted())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(BCStyle::Font(14, true))
				.ColorAndOpacity_Lambda([this, SlotType]() -> FSlateColor {
					const crash::GameDatabase* Db = GetDb();
					const crash::VehicleBuild Build = GetBuild();
					const crash::EquipmentDef* Item = Db ? Db->FindEquipment(Build.Equipped(SlotType)) : nullptr;
					return Item ? FSlateColor(BCStyle::RarityColor(Item->rarity)) : FSlateColor(BCStyle::TextMuted());
				})
				.Text_Lambda([this, SlotType]() -> FText {
					const crash::GameDatabase* Db = GetDb();
					const crash::VehicleBuild Build = GetBuild();
					const crash::EquipmentDef* Item = Db ? Db->FindEquipment(Build.Equipped(SlotType)) : nullptr;
					return Item ? BCStyle::TxtStd(PC.Get(), Item->name) : BCStyle::Txt(PC.Get(), "ui.garage.empty_slot");
				})
			];
		Slots->AddSlot(s % 2, s / 2)
			[
				BCStyle::SelectableCard(
					Content, [s]() { return BCGarageState::EquipSlotIndex == s; },
					[this, s]() {
						BCGarageState::EquipSlotIndex = s;
						RebuildEquipItems();
					},
					BCStyle::Accent(), FMargin(10.0f, 6.0f))
			];
	}

	return SNew(SScrollBox)
		+ SScrollBox::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot().AutoHeight()[Slots]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 14.0f, 0.0f, 8.0f)
			[
				SNew(STextBlock)
				.Font(BCStyle::Font(19, true))
				.ColorAndOpacity(BCStyle::TextPrimary())
				.Text_Lambda([this]() -> FText {
					return BCStyle::Fmt(PC.Get(), "ui.garage.slot_items",
										{BCStyle::Txt(PC.Get(), BCStyle::SlotKey(static_cast<crash::EquipSlot>(BCGarageState::EquipSlotIndex))).ToString()});
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SAssignNew(EquipItems, SVerticalBox)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 0.0f)
			[
				BCStyle::Label(BCStyle::Txt(P, "ui.garage.melee_only"), 12, BCStyle::TextMuted(), false, true)
			]
		];
}

void SBCGarageScreen::RebuildEquipItems()
{
	if (!EquipItems.IsValid())
	{
		return;
	}
	EquipItems->ClearChildren();
	const crash::GameDatabase* Db = GetDb();
	const crash::VehicleDef* Def = GetVehicleDef();
	if (!Db || !Def)
	{
		return;
	}
	BCGarageState::EquipSlotIndex = FMath::Clamp(BCGarageState::EquipSlotIndex, 0, crash::kEquipSlotCount - 1);
	const crash::EquipSlot SlotType = static_cast<crash::EquipSlot>(BCGarageState::EquipSlotIndex);
	TArray<const crash::EquipmentDef*> Items;
	for (const crash::EquipmentDef& E : Db->Equipment())
	{
		if (E.slot == SlotType && E.AllowsClass(Def->vehicleClass))
		{
			Items.Add(&E);
		}
	}
	Items.Sort([](const crash::EquipmentDef& A, const crash::EquipmentDef& B) {
		if (A.unlockLevel != B.unlockLevel)
		{
			return A.unlockLevel < B.unlockLevel;
		}
		return A.price.amount < B.price.amount;
	});
	if (Items.Num() == 0)
	{
		EquipItems->AddSlot().AutoHeight()[BCStyle::Label(BCStyle::Txt(PC.Get(), "ui.garage.no_items"), 14, BCStyle::TextMuted())];
		return;
	}
	for (const crash::EquipmentDef* E : Items)
	{
		EquipItems->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[MakeEquipItemCard(*E)];
	}
}

TSharedRef<SWidget> SBCGarageScreen::MakeEquipItemCard(const crash::EquipmentDef& Item)
{
	ABCPlayerController* P = PC.Get();
	const crash::GameDatabase* Db = GetDb();
	const crash::Profile* Pr = GetProfile();
	const crash::VehicleBuild* Owned = GetOwnedBuild();
	if (!Db || !Pr)
	{
		return SNullWidget::NullWidget;
	}
	const bool bOwnsItem = Pr->OwnsEquipment(*Db, Item.id);
	const bool bEquipped = Owned && Owned->Equipped(Item.slot) == Item.id;
	const bool bLevelLocked = !bOwnsItem && Pr->level < Item.unlockLevel;
	const bool bGold = !bOwnsItem && IsGoldOnly(Item.price);
	const FString ItemId = BC::ToFString(Item.id);
	const crash::EquipSlot SlotType = Item.slot;
	TWeakObjectPtr<ABCPlayerController> WeakPC = PC;
	TWeakObjectPtr<UBCProfileSubsystem> WeakProfile = ProfileSub;
	const FString CarId = VehicleId;
	const bool bCarOwned = Owned != nullptr;

	TSharedRef<SVerticalBox> Body = SNew(SVerticalBox);
	Body->AddSlot().AutoHeight()[BCStyle::Label(BCStyle::TxtStd(P, Item.description), 13, BCStyle::TextSecondary(), false, true)];
	if (Item.ability != crash::AbilityType::None)
	{
		const crash::AbilityTuning Tuning = crash::GetAbilityTuning(Item.ability);
		Body->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(SBorder)
				.BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x10, 0x22, 0x30, 0.9f), 6.0f, BCStyle::CreditsColor(), 1.0f))
				.Padding(FMargin(10.0f, 6.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BCStyle::Label(BCStyle::Fmt(P, "ui.garage.ability_line",
													{BCStyle::Txt(P, BCStyle::AbilityKey(Item.ability)).ToString(), FString::FromInt(FMath::RoundToInt(Tuning.cooldown))}),
									   14, BCStyle::CreditsColor(), true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BCStyle::Label(BCStyle::Txt(P, BCStyle::AbilityDescKey(Item.ability)), 12, BCStyle::TextSecondary(), false, true)
					]
				]
			];
	}
	Body->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				BCStyle::Label(BCStyle::Fmt(P, "ui.garage.cr_bonus", {FString::FromInt(Item.cr)}), 13, BCStyle::Warning(), true)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				BCStyle::Label(bLevelLocked ? BCStyle::Fmt(P, "ui.garage.unlock_level", {FString::FromInt(Item.unlockLevel)}) : FText::GetEmpty(), 13,
							   BCStyle::Warning(), true)
			]
		];

	TSharedRef<SHorizontalBox> Footer = SNew(SHorizontalBox);
	Footer->AddSlot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			bOwnsItem ? BCStyle::Label(BCStyle::Txt(P, bEquipped ? "ui.garage.equipped_state" : "ui.garage.owned"), 14,
									   bEquipped ? BCStyle::Accent() : BCStyle::Success(), true)
					  : BCStyle::PriceLabel(P, Item.price, 15)
		];
	if (!bOwnsItem && bGold)
	{
		Footer->AddSlot().AutoWidth()[BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.garage.get_in_store"), [this]() { GoToStore(); }, true, 0.0f, 14)];
	}
	else if (!bOwnsItem)
	{
		Footer->AddSlot()
			.AutoWidth()
			[
				BCStyle::ConfirmButton(
					P, BCStyle::EButtonKind::Primary, BCStyle::Txt(P, bCarOwned ? "ui.garage.buy_equip" : "ui.garage.buy"),
					[WeakPC, WeakProfile, ItemId, CarId, SlotType, bCarOwned]() {
						UBCProfileSubsystem* Profile = WeakProfile.Get();
						if (!Profile)
						{
							return;
						}
						Profile->BuyEquipment(ItemId, [WeakPC, WeakProfile, ItemId, CarId, SlotType, bCarOwned](bool bOk, const FString& Error) {
							ABCPlayerController* Owner = WeakPC.Get();
							if (!bOk)
							{
								if (Owner)
								{
									Owner->ShowToast(BCStyle::ErrorText(Owner, Error));
								}
								return;
							}
							UBCProfileSubsystem* Prof = WeakProfile.Get();
							if (bCarOwned && Prof)
							{
								Prof->Equip(CarId, SlotType, ItemId, BCStyle::Feedback(Owner, "ui.garage.bought_equipped"));
							}
							else if (Owner)
							{
								Owner->ShowToast(BCStyle::Txt(Owner, "ui.garage.bought"));
							}
						});
					},
					!bLevelLocked, 0.0f, 14)
			];
	}
	else if (bEquipped)
	{
		Footer->AddSlot()
			.AutoWidth()
			[
				BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.garage.unequip"),
										 [WeakPC, WeakProfile, CarId, SlotType]() {
											 if (UBCProfileSubsystem* Profile = WeakProfile.Get())
											 {
												 Profile->Equip(CarId, SlotType, FString(), BCStyle::Feedback(WeakPC.Get()));
											 }
										 },
										 true, 0.0f, 14)
			];
	}
	else
	{
		Footer->AddSlot()
			.AutoWidth()
			[
				BCStyle::PrimaryButton(BCStyle::Txt(P, "ui.garage.equip"),
									   [WeakPC, WeakProfile, CarId, SlotType, ItemId]() {
										   if (UBCProfileSubsystem* Profile = WeakProfile.Get())
										   {
											   Profile->Equip(CarId, SlotType, ItemId, BCStyle::Feedback(WeakPC.Get(), "ui.garage.equipped"));
										   }
									   },
									   bCarOwned, 0.0f, 14)
			];
	}

	const FText Subtitle = FText::FromString(BCStyle::Txt(P, BCStyle::RarityKey(Item.rarity)).ToString());
	return BCStyle::ItemCard(BCStyle::TxtStd(P, Item.name), Subtitle, BCStyle::RarityColor(Item.rarity), Body, Footer,
							 [bEquipped]() { return bEquipped; });
}

// ---------------------------------------------------------------------------------------------
// Looks

TSharedRef<SWidget> SBCGarageScreen::MakeCosmeticsTab()
{
	ABCPlayerController* P = PC.Get();
	TArray<FText> Tabs = {BCStyle::Txt(P, "ui.garage.looks.paint"), BCStyle::Txt(P, "ui.garage.looks.wheels"),
						  BCStyle::Txt(P, "ui.garage.looks.loadout")};
	return SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			BCStyle::TabRow(
				Tabs, []() { return BCGarageState::CosmeticTab; },
				[this](int32 Index) {
					BCGarageState::CosmeticTab = Index;
					RebuildCosmetics();
				},
				16)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(CosmeticBox, SVerticalBox)
			]
		];
}

void SBCGarageScreen::RebuildCosmetics()
{
	if (!CosmeticBox.IsValid())
	{
		return;
	}
	CosmeticBox->ClearChildren();
	ABCPlayerController* P = PC.Get();
	const crash::GameDatabase* Db = GetDb();
	if (!Db)
	{
		return;
	}
	auto AddGrid = [this](const TArray<const crash::CosmeticDef*>& Items) {
		TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(3.0f));
		for (int32 i = 0; i < Items.Num(); ++i)
		{
			Grid->AddSlot(i % 2, i / 2)[MakeCosmeticCard(*Items[i])];
		}
		CosmeticBox->AddSlot().AutoHeight()[Grid];
	};
	auto Collect = [Db](crash::ItemType Type) {
		TArray<const crash::CosmeticDef*> Out;
		for (const crash::CosmeticDef& C : Db->Cosmetics())
		{
			if (C.type == Type)
			{
				Out.Add(&C);
			}
		}
		return Out;
	};

	switch (BCGarageState::CosmeticTab)
	{
	case 1:
		CosmeticBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[BCStyle::Label(BCStyle::Txt(P, "ui.garage.looks.wheels_hint"), 13, BCStyle::TextMuted(), false, true)];
		AddGrid(Collect(crash::ItemType::Wheels));
		break;
	case 2:
	{
		CosmeticBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[BCStyle::Label(BCStyle::Txt(P, "ui.garage.looks.loadout_hint"), 13, BCStyle::TextMuted(), false, true)];
		struct FSection
		{
			crash::ItemType Type;
			const char* Key;
		};
		static const FSection Sections[] = {
			{crash::ItemType::Title, "ui.item.title"},
			{crash::ItemType::Banner, "ui.item.banner"},
			{crash::ItemType::Horn, "ui.item.horn"},
			{crash::ItemType::Sparks, "ui.item.sparks"},
		};
		for (const FSection& Section : Sections)
		{
			CosmeticBox->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 6.0f)[BCStyle::SectionHeader(BCStyle::Txt(P, Section.Key))];
			AddGrid(Collect(Section.Type));
		}
		break;
	}
	default:
		CosmeticBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[BCStyle::Label(BCStyle::Txt(P, "ui.garage.looks.paint_hint"), 13, BCStyle::TextMuted(), false, true)];
		AddGrid(Collect(crash::ItemType::Paint));
		break;
	}
}

TSharedRef<SWidget> SBCGarageScreen::MakeCosmeticCard(const crash::CosmeticDef& Cosmetic)
{
	ABCPlayerController* P = PC.Get();
	const crash::GameDatabase* Db = GetDb();
	const crash::Profile* Pr = GetProfile();
	const UBCProfileSubsystem* ProfileConst = ProfileSub.Get();
	if (!Db || !Pr || !ProfileConst)
	{
		return SNullWidget::NullWidget;
	}
	const crash::VehicleBuild* Owned = GetOwnedBuild();
	const bool bOwns = Pr->OwnsCosmetic(*Db, Cosmetic.id);
	const bool bGold = !bOwns && IsGoldOnly(Cosmetic.price);
	const FString CosId = BC::ToFString(Cosmetic.id);
	const crash::ItemType Type = Cosmetic.type;
	const bool bCarPart = Type == crash::ItemType::Paint || Type == crash::ItemType::Wheels;
	// Loadout cosmetics live in the local profile (the backend has no loadout endpoint).
	const crash::CosmeticLoadout& Loadout = ProfileConst->Local().loadout;
	bool bInUse = false;
	switch (Type)
	{
	case crash::ItemType::Paint:
		bInUse = Owned && Owned->paint == Cosmetic.id;
		break;
	case crash::ItemType::Wheels:
		bInUse = Owned && Owned->wheels == Cosmetic.id;
		break;
	case crash::ItemType::Title:
		bInUse = Loadout.title == Cosmetic.id;
		break;
	case crash::ItemType::Banner:
		bInUse = Loadout.banner == Cosmetic.id;
		break;
	case crash::ItemType::Horn:
		bInUse = Loadout.horn == Cosmetic.id;
		break;
	case crash::ItemType::Sparks:
		bInUse = Loadout.sparks == Cosmetic.id;
		break;
	default:
		break;
	}

	TWeakObjectPtr<ABCPlayerController> WeakPC = PC;
	TWeakObjectPtr<UBCProfileSubsystem> WeakProfile = ProfileSub;
	const FString CarId = VehicleId;
	const bool bCarOwned = Owned != nullptr;

	// Applies an owned cosmetic: paint/wheels on the car, the rest to the loadout.
	TFunction<void(UBCProfileSubsystem*)> Use = [WeakPC, CosId, CarId, Type](UBCProfileSubsystem* Profile) {
		if (!Profile)
		{
			return;
		}
		if (Type == crash::ItemType::Paint)
		{
			Profile->Customize(CarId, CosId, FString(), BCStyle::Feedback(WeakPC.Get(), "ui.garage.applied"));
			return;
		}
		if (Type == crash::ItemType::Wheels)
		{
			Profile->Customize(CarId, FString(), CosId, BCStyle::Feedback(WeakPC.Get(), "ui.garage.applied"));
			return;
		}
		const crash::CosmeticLoadout& L = Profile->Local().loadout;
		FString Title = BC::ToFString(L.title);
		FString Banner = BC::ToFString(L.banner);
		FString Horn = BC::ToFString(L.horn);
		FString Sparks = BC::ToFString(L.sparks);
		switch (Type)
		{
		case crash::ItemType::Title:
			Title = CosId;
			break;
		case crash::ItemType::Banner:
			Banner = CosId;
			break;
		case crash::ItemType::Horn:
			Horn = CosId;
			break;
		case crash::ItemType::Sparks:
			Sparks = CosId;
			break;
		default:
			break;
		}
		Profile->SetLoadout(Title, Banner, Horn, Sparks);
		if (ABCPlayerController* Owner = WeakPC.Get())
		{
			Owner->ShowToast(BCStyle::Txt(Owner, "ui.garage.applied"));
		}
	};

	// Swatch + info.
	FLinearColor SwatchColor = BCStyle::FromCrash(Cosmetic.color);
	FLinearColor SwatchSecond = FLinearColor::Transparent;
	if (Type == crash::ItemType::Banner && Cosmetic.colors.size() >= 2)
	{
		SwatchColor = BCStyle::FromCrash(Cosmetic.colors[0]);
		SwatchSecond = BCStyle::FromCrash(Cosmetic.colors[1]);
	}
	FText Detail = BCStyle::Txt(P, BCStyle::RarityKey(Cosmetic.rarity));
	if (Type == crash::ItemType::Paint)
	{
		Detail = FText::FromString(Detail.ToString() + TEXT("  \u00B7  ") + BCStyle::Txt(P, BCStyle::FinishKey(Cosmetic.finish)).ToString());
	}
	else if (Type == crash::ItemType::Wheels)
	{
		Detail = FText::FromString(Detail.ToString() + TEXT("  \u00B7  ") + BCStyle::Fmt(P, "ui.garage.spokes", {FString::FromInt(Cosmetic.spokes)}).ToString());
	}
	const bool bShowSwatch = Type != crash::ItemType::Title && Type != crash::ItemType::Horn;

	TSharedRef<SWidget> Header =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, bShowSwatch ? 10.0f : 0.0f, 0.0f)
		[
			bShowSwatch ? BCStyle::Swatch(SwatchColor, 30.0f, SwatchSecond) : SNullWidget::NullWidget
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock).Text(BCStyle::TxtStd(P, Cosmetic.name)).Font(BCStyle::Font(15, true)).ColorAndOpacity(BCStyle::TextPrimary())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock).Text(Detail).Font(BCStyle::Font(12)).ColorAndOpacity(BCStyle::RarityColor(Cosmetic.rarity))
			]
		];

	TSharedRef<SHorizontalBox> Footer = SNew(SHorizontalBox);
	if (bInUse)
	{
		Footer->AddSlot().AutoWidth().VAlign(VAlign_Center)[BCStyle::Badge(BCStyle::Txt(P, "ui.garage.in_use"), BCStyle::Accent(), 12)];
	}
	else if (bOwns)
	{
		Footer->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)[BCStyle::Label(BCStyle::Txt(P, "ui.garage.owned"), 13, BCStyle::Success(), true)];
		Footer->AddSlot()
			.AutoWidth()
			[
				BCStyle::PrimaryButton(BCStyle::Txt(P, "ui.garage.use"), [WeakProfile, Use]() { Use(WeakProfile.Get()); }, !bCarPart || bCarOwned, 0.0f, 14)
			];
	}
	else if (bGold)
	{
		Footer->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)[BCStyle::Badge(BCStyle::Txt(P, "ui.garage.store_exclusive"), BCStyle::GoldColor(), 11)];
		Footer->AddSlot().AutoWidth()[BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.garage.to_store"), [this]() { GoToStore(); }, true, 0.0f, 13)];
	}
	else
	{
		Footer->AddSlot().FillWidth(1.0f).VAlign(VAlign_Center)[BCStyle::PriceLabel(P, Cosmetic.price, 14)];
		Footer->AddSlot()
			.AutoWidth()
			[
				BCStyle::ConfirmButton(
					P, BCStyle::EButtonKind::Primary, BCStyle::Txt(P, "ui.garage.buy"),
					[WeakPC, WeakProfile, CosId, Use, bCarPart, bCarOwned]() {
						UBCProfileSubsystem* Profile = WeakProfile.Get();
						if (!Profile)
						{
							return;
						}
						Profile->BuyCosmetic(CosId, [WeakPC, WeakProfile, Use, bCarPart, bCarOwned](bool bOk, const FString& Error) {
							ABCPlayerController* Owner = WeakPC.Get();
							if (!bOk)
							{
								if (Owner)
								{
									Owner->ShowToast(BCStyle::ErrorText(Owner, Error));
								}
								return;
							}
							// Bought: apply it right away when possible.
							if (!bCarPart || bCarOwned)
							{
								Use(WeakProfile.Get());
							}
							else if (Owner)
							{
								Owner->ShowToast(BCStyle::Txt(Owner, "ui.garage.bought"));
							}
						});
					},
					true, 0.0f, 14)
			];
	}

	TFunction<bool()> IsPreviewed = nullptr;
	TFunction<void()> OnClick = nullptr;
	if (bCarPart)
	{
		// Clicking a paint / wheel previews it on the showroom car.
		IsPreviewed = [this, CosId, Type]() { return (Type == crash::ItemType::Paint ? PreviewPaint : PreviewWheels) == CosId; };
		OnClick = [this, CosId, Type]() {
			if (Type == crash::ItemType::Paint)
			{
				PreviewPaint = CosId;
			}
			else
			{
				PreviewWheels = CosId;
			}
			RefreshPreview();
		};
	}
	return BCStyle::ItemCard(FText::GetEmpty(), FText::GetEmpty(), BCStyle::RarityColor(Cosmetic.rarity), Header, Footer, IsPreviewed, OnClick);
}

TSharedRef<SWidget> BCUI::MakeGarage(ABCPlayerController* PC)
{
	return SNew(SBCGarageScreen).Owner(PC);
}
