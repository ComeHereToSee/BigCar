// Copyright BigCar Team. All Rights Reserved.
//
// Main menu: logo + mode buttons on the left, profile header (name, level/XP, wallet, online status) top-right and the
// selected car card bottom-right over the rotating showroom car.

#include "Core/BCDataSubsystem.h"
#include "Core/BCGameInstance.h"
#include "Core/BCProfileSubsystem.h"
#include "Core/BigCarTypes.h"
#include "CrashCore/Profile.h"
#include "CrashCore/Stats.h"
#include "Framework/Application/SlateApplication.h"
#include "Gameplay/BCPlayerController.h"
#include "Online/BCOnlineSubsystem.h"
#include "UI/BCUI.h"
#include "UI/BCUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

class SBCMainMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBCMainMenu) : _Owner(nullptr) {}
		SLATE_ARGUMENT(ABCPlayerController*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SBCMainMenu() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	TSharedRef<SWidget> MakeMenuButton(const char* TitleKey, const char* DescKey, TFunction<void()> OnClick, bool bFirst = false);
	TSharedRef<SWidget> MakeProfileHeader();
	TSharedRef<SWidget> MakeCarCard();
	void HandleProfileChanged();
	void Open(EBCScreen Screen);

	// Selected car of the active profile (nullptr if none).
	const crash::VehicleBuild* SelectedBuild() const;
	const crash::VehicleDef* SelectedDef() const;

	TWeakObjectPtr<ABCPlayerController> PC;
	TWeakObjectPtr<UBCProfileSubsystem> ProfileSub;
	FDelegateHandle ProfileHandle;
	TSharedPtr<SButton> FirstButton;
	TSharedPtr<SEditableTextBox> NameBox;
	FString ConnectionError;
	bool bEditingName = false;
	bool bFocusDone = false;
};

void SBCMainMenu::Construct(const FArguments& InArgs)
{
	PC = InArgs._Owner;
	ABCPlayerController* P = PC.Get();
	if (UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(P))
	{
		ProfileSub = Profile;
		ProfileHandle = Profile->OnChanged.AddSP(this, &SBCMainMenu::HandleProfileChanged);
		if (P)
		{
			P->PreviewBuild(Profile->SelectedBuild());
		}
	}

	// A failed connection / travel dropped us back here: show why (once).
	if (UBCGameInstance* GI = P ? Cast<UBCGameInstance>(P->GetGameInstance()) : nullptr)
	{
		ConnectionError = GI->PendingError;
		GI->PendingError.Reset();
	}

	ChildSlot
	[
		SNew(SOverlay)
		// Left darkening so the logo and buttons read well over the showroom.
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		[
			SNew(SBox)
			.WidthOverride(640.0f)
			[
				SNew(SBorder).BorderImage(BCStyle::SolidBrush(BCStyle::Hex(0x05, 0x06, 0x08, 0.84f))).Padding(0.0f)
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.Padding(640.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(3.0f)
			[
				SNew(SBorder).BorderImage(BCStyle::SolidBrush(BCStyle::WithAlpha(BCStyle::Accent(), 0.7f))).Padding(0.0f)
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(SBox)
				.WidthOverride(640.0f)
				.Padding(FMargin(64.0f, 52.0f, 48.0f, 36.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("BIGCAR")))
						.Font(BCStyle::FontOutlined(96, true, 4))
						.ColorAndOpacity(BCStyle::Accent())
						.ShadowOffset(FVector2D(0.0f, 5.0f))
						.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(4.0f, 0.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(BCStyle::Txt(P, "ui.menu.subtitle"))
						.Font(BCStyle::Font(20, true))
						.ColorAndOpacity(BCStyle::TextSecondary())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 36.0f, 0.0f, 0.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							MakeMenuButton("ui.menu.campaign", "ui.menu.campaign.desc", [this]() { Open(EBCScreen::Campaign); }, true)
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							MakeMenuButton("ui.menu.quick_play", "ui.menu.quick_play.desc", [this]() { Open(EBCScreen::QuickPlay); })
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							MakeMenuButton("ui.menu.online", "ui.menu.online.desc", [this]() { Open(EBCScreen::Online); })
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							MakeMenuButton("ui.menu.garage", "ui.menu.garage.desc", [this]() { Open(EBCScreen::Garage); })
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							MakeMenuButton("ui.menu.store", "ui.menu.store.desc", [this]() { Open(EBCScreen::Store); })
						]
						+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)
						[
							MakeMenuButton("ui.menu.settings", "ui.menu.settings.desc", [this]() { Open(EBCScreen::Settings); })
						]
						+ SVerticalBox::Slot().AutoHeight()
						[
							MakeMenuButton("ui.menu.quit", "ui.menu.quit.desc", [this]() {
								if (ABCPlayerController* Owner = PC.Get())
								{
									Owner->QuitGame();
								}
							})
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					[
						SNew(SSpacer)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(STextBlock)
						.Text(BCStyle::Txt(P, "ui.menu.hint"))
						.Font(BCStyle::Font(13))
						.ColorAndOpacity(BCStyle::TextMuted())
					]
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				.Padding(0.0f, 32.0f, 40.0f, 0.0f)
				[
					MakeProfileHeader()
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SSpacer)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Right)
				.Padding(0.0f, 0.0f, 48.0f, 48.0f)
				[
					MakeCarCard()
				]
			]
		]
		// Connection error banner.
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Bottom)
		.Padding(640.0f, 0.0f, 0.0f, 40.0f)
		[
			SNew(SBox)
			.MaxDesiredWidth(720.0f)
			.Visibility_Lambda([this]() -> EVisibility { return ConnectionError.IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				SNew(SBorder)
				.BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x3A, 0x0C, 0x0A, 0.94f), 8.0f, BCStyle::Danger(), 1.0f))
				.Padding(FMargin(18.0f, 12.0f))
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
							SNew(STextBlock).Text(BCStyle::Txt(P, "ui.menu.connection_lost")).Font(BCStyle::Font(17, true)).ColorAndOpacity(BCStyle::Danger())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Font(BCStyle::Font(14))
							.ColorAndOpacity(BCStyle::TextSecondary())
							.AutoWrapText(true)
							.Text_Lambda([this]() -> FText { return FText::FromString(ConnectionError); })
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(16.0f, 0.0f, 0.0f, 0.0f)
					[
						BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.common.ok"), [this]() { ConnectionError.Reset(); }, true, 90.0f, 15)
					]
				]
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(0.0f, 120.0f, 0.0f, 0.0f)
		[
			BCStyle::ToastLayer(P)
		]
	];
}

SBCMainMenu::~SBCMainMenu()
{
	if (UBCProfileSubsystem* Profile = ProfileSub.Get())
	{
		Profile->OnChanged.Remove(ProfileHandle);
	}
}

void SBCMainMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// Give the first button keyboard/gamepad focus once so the menu is navigable without a mouse.
	if (!bFocusDone && FirstButton.IsValid() && FSlateApplication::IsInitialized())
	{
		bFocusDone = true;
		FSlateApplication::Get().SetKeyboardFocus(FirstButton, EFocusCause::SetDirectly);
	}
}

void SBCMainMenu::Open(EBCScreen Screen)
{
	if (ABCPlayerController* P = PC.Get())
	{
		P->OpenScreen(Screen);
	}
}

void SBCMainMenu::HandleProfileChanged()
{
	ABCPlayerController* P = PC.Get();
	const UBCProfileSubsystem* Profile = ProfileSub.Get();
	if (P && Profile)
	{
		P->PreviewBuild(Profile->SelectedBuild());
	}
}

const crash::VehicleBuild* SBCMainMenu::SelectedBuild() const
{
	const UBCProfileSubsystem* Profile = ProfileSub.Get();
	return Profile ? Profile->Active().SelectedBuild() : nullptr;
}

const crash::VehicleDef* SBCMainMenu::SelectedDef() const
{
	const crash::VehicleBuild* Build = SelectedBuild();
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
	return (Build && Data) ? Data->Db().FindVehicle(Build->vehicleId) : nullptr;
}

TSharedRef<SWidget> SBCMainMenu::MakeMenuButton(const char* TitleKey, const char* DescKey, TFunction<void()> OnClick, bool bFirst)
{
	ABCPlayerController* P = PC.Get();
	TSharedRef<TWeakPtr<SButton>> Self = MakeShared<TWeakPtr<SButton>>();
	TSharedPtr<SButton> Button;
	SAssignNew(Button, SButton)
		.ButtonStyle(BCStyle::MenuButtonStyle())
		.ContentPadding(FMargin(24.0f, 10.0f, 24.0f, 12.0f))
		.OnClicked_Lambda([OnClick]() -> FReply {
			if (OnClick)
			{
				OnClick();
			}
			return FReply::Handled();
		})
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(BCStyle::Txt(P, TitleKey))
				.Font(BCStyle::Font(26, true))
				.ColorAndOpacity_Lambda([Self]() -> FSlateColor {
					const TSharedPtr<SButton> B = Self->Pin();
					return (B.IsValid() && (B->IsHovered() || B->HasKeyboardFocus())) ? FSlateColor(BCStyle::TextOnAccent())
																					: FSlateColor(BCStyle::TextPrimary());
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(BCStyle::Txt(P, DescKey))
				.Font(BCStyle::Font(14))
				.ColorAndOpacity_Lambda([Self]() -> FSlateColor {
					const TSharedPtr<SButton> B = Self->Pin();
					return (B.IsValid() && (B->IsHovered() || B->HasKeyboardFocus())) ? FSlateColor(BCStyle::Hex(0x2A, 0x16, 0x08))
																					: FSlateColor(BCStyle::TextMuted());
				})
			]
		];
	*Self = Button;
	if (bFirst)
	{
		FirstButton = Button;
	}
	return Button.ToSharedRef();
}

TSharedRef<SWidget> SBCMainMenu::MakeProfileHeader()
{
	ABCPlayerController* P = PC.Get();
	TWeakObjectPtr<ABCPlayerController> WeakPC = PC;

	auto XpFraction = [WeakPC]() -> TOptional<float> {
		const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(WeakPC.Get());
		const UBCDataSubsystem* Data = UBCDataSubsystem::Get(WeakPC.Get());
		if (!Profile || !Data)
		{
			return 0.0f;
		}
		const crash::Profile& Pr = Profile->Active();
		const crash::ProgressionDef& Prog = Data->Db().Progression();
		if (Pr.level >= Prog.maxLevel)
		{
			return 1.0f;
		}
		const int64 Need = crash::XpToNextLevel(Prog, Pr.level);
		return Need > 0 ? FMath::Clamp(static_cast<float>(static_cast<double>(Pr.xp) / static_cast<double>(Need)), 0.0f, 1.0f) : 0.0f;
	};

	TSharedRef<SWidget> NameRow =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.Visibility_Lambda([this]() -> EVisibility { return bEditingName ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				SNew(SButton)
				.ButtonStyle(BCStyle::GhostButtonStyle())
				.ContentPadding(FMargin(4.0f, 0.0f))
				.ToolTipText(BCStyle::Txt(P, "ui.menu.edit_name"))
				.OnClicked_Lambda([this]() -> FReply {
					bEditingName = true;
					const UBCProfileSubsystem* Profile = ProfileSub.Get();
					if (NameBox.IsValid())
					{
						NameBox->SetText(FText::FromString(Profile ? BC::ToFString(Profile->Active().displayName) : FString()));
						if (FSlateApplication::IsInitialized())
						{
							FSlateApplication::Get().SetKeyboardFocus(NameBox, EFocusCause::SetDirectly);
						}
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock)
					.Font(BCStyle::Font(24, true))
					.ColorAndOpacity(BCStyle::TextPrimary())
					.Text_Lambda([WeakPC]() -> FText {
						const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(WeakPC.Get());
						return FText::FromString(Profile ? BC::ToFString(Profile->Active().displayName) : FString(TEXT("Driver")));
					})
				]
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(240.0f)
			.Visibility_Lambda([this]() -> EVisibility { return bEditingName ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				SAssignNew(NameBox, SEditableTextBox)
				.Font(BCStyle::Font(18, true))
				.HintText(BCStyle::Txt(P, "ui.menu.name_hint"))
				.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type CommitType) {
					if (CommitType == ETextCommit::OnEnter)
					{
						if (UBCProfileSubsystem* Profile = ProfileSub.Get())
						{
							const FString Trimmed = NewText.ToString().TrimStartAndEnd();
							if (Trimmed.Len() >= 2)
							{
								Profile->SetDisplayName(Trimmed);
							}
							else if (ABCPlayerController* Owner = PC.Get())
							{
								Owner->ShowToast(BCStyle::Txt(Owner, "ui.menu.name_invalid"));
							}
						}
					}
					bEditingName = false;
				})
			]
		];

	TSharedRef<SWidget> OnlineStatus =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 6.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(FText::FromString(TEXT("\u25CF")))
			.Font(BCStyle::Font(12))
			.ColorAndOpacity_Lambda([WeakPC]() -> FSlateColor {
				const UBCOnlineSubsystem* Online = UBCOnlineSubsystem::Get(WeakPC.Get());
				if (!Online)
				{
					return FSlateColor(BCStyle::TextMuted());
				}
				switch (Online->State())
				{
				case EBCOnlineState::LoggedIn:
					return FSlateColor(BCStyle::Success());
				case EBCOnlineState::Connecting:
					return FSlateColor(BCStyle::Warning());
				case EBCOnlineState::Error:
					return FSlateColor(BCStyle::Danger());
				default:
					return FSlateColor(BCStyle::TextMuted());
				}
			})
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(BCStyle::Font(14))
			.ColorAndOpacity(BCStyle::TextSecondary())
			.Text_Lambda([WeakPC]() -> FText {
				const ABCPlayerController* Owner = WeakPC.Get();
				const UBCOnlineSubsystem* Online = UBCOnlineSubsystem::Get(Owner);
				if (!Online)
				{
					return BCStyle::Txt(Owner, "ui.online.status.offline");
				}
				switch (Online->State())
				{
				case EBCOnlineState::LoggedIn:
					return BCStyle::Fmt(Owner, "ui.online.status.logged_in",
										{Online->Username().IsEmpty() ? BCStyle::Txt(Owner, "ui.online.guest").ToString() : Online->Username()});
				case EBCOnlineState::Connecting:
					return BCStyle::Txt(Owner, "ui.online.status.connecting");
				case EBCOnlineState::Error:
					return BCStyle::Txt(Owner, "ui.online.status.error");
				default:
					return BCStyle::Txt(Owner, "ui.online.status.offline");
				}
			})
		];

	return BCStyle::Panel(
		SNew(SHorizontalBox)
		// Level emblem.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 16.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(64.0f)
			.HeightOverride(64.0f)
			[
				SNew(SBorder)
				.BorderImage(BCStyle::RoundedBrush(BCStyle::Accent(), 10.0f))
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock).Text(BCStyle::Txt(P, "ui.common.level")).Font(BCStyle::Font(11, true)).ColorAndOpacity(BCStyle::TextOnAccent())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Font(BCStyle::Font(26, true))
						.ColorAndOpacity(BCStyle::TextOnAccent())
						.Text_Lambda([WeakPC]() -> FText {
							const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(WeakPC.Get());
							return FText::AsNumber(Profile ? Profile->Active().level : 1);
						})
					]
				]
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(300.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[NameRow]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.0f, 4.0f, 0.0f, 2.0f)
				[
					BCStyle::Bar(BCStyle::Bind<TOptional<float>>(XpFraction), FSlateColor(BCStyle::XpColor()), 6.0f)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(4.0f, 0.0f, 0.0f, 4.0f)
				[
					SNew(STextBlock)
					.Font(BCStyle::Font(13))
					.ColorAndOpacity(BCStyle::TextMuted())
					.Text_Lambda([WeakPC]() -> FText {
						const ABCPlayerController* Owner = WeakPC.Get();
						const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(Owner);
						const UBCDataSubsystem* Data = UBCDataSubsystem::Get(Owner);
						if (!Profile || !Data)
						{
							return FText::GetEmpty();
						}
						const crash::Profile& Pr = Profile->Active();
						if (Pr.level >= Data->Db().Progression().maxLevel)
						{
							return BCStyle::Txt(Owner, "ui.menu.xp_max");
						}
						return BCStyle::Fmt(Owner, "ui.menu.xp",
											{BCStyle::Num(Pr.xp), BCStyle::Num(crash::XpToNextLevel(Data->Db().Progression(), Pr.level))});
					})
				]
				+ SVerticalBox::Slot().AutoHeight().Padding(4.0f, 0.0f, 0.0f, 0.0f)[OnlineStatus]
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(20.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				SNew(STextBlock)
				.Font(BCStyle::Font(20, true))
				.ColorAndOpacity(BCStyle::CreditsColor())
				.Text_Lambda([WeakPC]() -> FText {
					const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(WeakPC.Get());
					return BCStyle::CurrencyText(WeakPC.Get(), crash::Currency::Credits, Profile ? Profile->Active().credits : 0);
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(BCStyle::Font(20, true))
				.ColorAndOpacity(BCStyle::GoldColor())
				.Text_Lambda([WeakPC]() -> FText {
					const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(WeakPC.Get());
					return BCStyle::CurrencyText(WeakPC.Get(), crash::Currency::Gold, Profile ? Profile->Active().gold : 0);
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([WeakPC]() -> EVisibility {
					const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(WeakPC.Get());
					return Profile && Profile->IsOnline() ? EVisibility::Visible : EVisibility::Collapsed;
				})
				[
					BCStyle::Badge(BCStyle::Txt(P, "ui.common.online_save"), BCStyle::Success(), 11)
				]
			]
		],
		FMargin(16.0f, 14.0f));
}

TSharedRef<SWidget> SBCMainMenu::MakeCarCard()
{
	ABCPlayerController* P = PC.Get();
	TWeakObjectPtr<ABCPlayerController> WeakPC = PC;
	return SNew(SBox)
		.WidthOverride(460.0f)
		[
			BCStyle::Panel(
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock).Text(BCStyle::Txt(P, "ui.menu.current_car")).Font(BCStyle::Font(14, true)).ColorAndOpacity(BCStyle::Accent())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Font(BCStyle::FontOutlined(30, true, 1))
					.ColorAndOpacity(BCStyle::TextPrimary())
					.AutoWrapText(true)
					.Text_Lambda([this]() -> FText {
						const crash::VehicleDef* Def = SelectedDef();
						const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
						return (Def && Data) ? Data->VehicleName(*Def) : BCStyle::Txt(PC.Get(), "ui.menu.no_car");
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						BCStyle::Badge(BCStyle::Bind<FText>([this]() -> FText {
										   const crash::VehicleDef* Def = SelectedDef();
										   return Def ? BCStyle::TierText(Def->tier) : FText::GetEmpty();
									   }),
									   BCStyle::Accent(), 13)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(BCStyle::Font(16))
						.ColorAndOpacity(BCStyle::TextSecondary())
						.Text_Lambda([this]() -> FText {
							const crash::VehicleDef* Def = SelectedDef();
							if (!Def)
							{
								return FText::GetEmpty();
							}
							return FText::FromString(BCStyle::Txt(PC.Get(), BCStyle::ClassKey(Def->vehicleClass)).ToString() + TEXT("  \u00B7  ") +
													 BCStyle::Txt(PC.Get(), BCStyle::DrivetrainKey(Def->drivetrain)).ToString());
						})
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 12.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(BCStyle::Font(22, true))
						.ColorAndOpacity(BCStyle::Warning())
						.Text_Lambda([this]() -> FText {
							const crash::VehicleBuild* Build = SelectedBuild();
							const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
							if (!Build || !Data || !Data->Db().FindVehicle(Build->vehicleId))
							{
								return FText::GetEmpty();
							}
							return BCStyle::Fmt(PC.Get(), "ui.menu.combat_rating", {FString::FromInt(crash::ComputeCombatRating(Data->Db(), *Build))});
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.menu.to_garage"), [WeakPC]() {
							if (ABCPlayerController* Owner = WeakPC.Get())
							{
								Owner->OpenScreen(EBCScreen::Garage);
							}
						})
					]
				],
				FMargin(22.0f, 18.0f))
		];
}

TSharedRef<SWidget> BCUI::MakeMainMenu(ABCPlayerController* PC)
{
	return SNew(SBCMainMenu).Owner(PC);
}
