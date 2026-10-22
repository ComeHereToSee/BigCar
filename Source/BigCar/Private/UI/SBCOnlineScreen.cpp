// Copyright BigCar Team. All Rights Reserved.
//
// Online hub: login (guest / account), profile + ranked summary, online-save toggle, party (create / join by code /
// ready / leave), matchmaking queues with a searching panel, and the ranked ladder.

#include "Core/BCDataSubsystem.h"
#include "Core/BCProfileSubsystem.h"
#include "Core/BigCarTypes.h"
#include "CrashCore/Profile.h"
#include "Gameplay/BCPlayerController.h"
#include "Online/BCOnlineSubsystem.h"
#include "UI/BCUI.h"
#include "UI/BCUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
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

namespace BCOnlineScreenState
{
int32 LadderQueue = 2; // index into RankedQueues (3v3 by default)

const TCHAR* const RankedQueues[] = {TEXT("ranked_1v1"), TEXT("ranked_2v2"), TEXT("ranked_3v3")};
constexpr int32 NumRankedQueues = 3;

struct FQueueInfo
{
	const TCHAR* Id;
	int32 TeamSize;
	bool bRanked;
};
const FQueueInfo Queues[] = {
	{TEXT("casual_ffa"), 1, false},
	{TEXT("casual_teams"), 3, false},
	{TEXT("ranked_1v1"), 1, true},
	{TEXT("ranked_2v2"), 2, true},
	{TEXT("ranked_3v3"), 3, true},
};
constexpr int32 NumQueues = 5;
} // namespace BCOnlineScreenState

class SBCOnlineScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBCOnlineScreen) : _Owner(nullptr) {}
		SLATE_ARGUMENT(ABCPlayerController*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SBCOnlineScreen() override;

private:
	UBCOnlineSubsystem* Online() const { return OnlineSub.Get(); }
	bool IsLoggedIn() const;
	int32 PartySize() const;
	// Empty when the queue can be joined, otherwise the reason (ui key).
	const char* QueueBlockReason(int32 QueueIndex) const;

	TSharedRef<SWidget> MakeLoginPanel();
	TSharedRef<SWidget> MakeLoggedInPanel();
	TSharedRef<SWidget> MakeProfilePanel();
	TSharedRef<SWidget> MakePartyPanel();
	TSharedRef<SWidget> MakeQueuePanel();
	TSharedRef<SWidget> MakeLadderPanel();

	void RebuildRanked();
	void RebuildParty();
	void RebuildLadder();
	void RefreshAll();
	void DoLogin(bool bRegister);

	void HandleState();
	void HandleProfile();
	void HandleParty();
	void HandleTicket();
	void HandleLadder();

	TWeakObjectPtr<ABCPlayerController> PC;
	TWeakObjectPtr<UBCOnlineSubsystem> OnlineSub;
	TWeakObjectPtr<UBCProfileSubsystem> ProfileSub;
	FDelegateHandle StateHandle;
	FDelegateHandle ProfileHandle;
	FDelegateHandle PartyHandle;
	FDelegateHandle TicketHandle;
	FDelegateHandle LadderHandle;

	TSharedPtr<SEditableTextBox> UserBox;
	TSharedPtr<SEditableTextBox> PassBox;
	TSharedPtr<SEditableTextBox> DisplayNameBox;
	TSharedPtr<SEditableTextBox> PartyCodeBox;
	TSharedPtr<SVerticalBox> RankedBox;
	TSharedPtr<SVerticalBox> PartyMembersBox;
	TSharedPtr<SVerticalBox> LadderBox;

	bool bWasLoggedIn = false;
	double TicketUpdatedAt = 0.0;
	float TicketWait = 0.0f;
};

void SBCOnlineScreen::Construct(const FArguments& InArgs)
{
	PC = InArgs._Owner;
	ABCPlayerController* P = PC.Get();
	ProfileSub = UBCProfileSubsystem::Get(P);
	if (UBCOnlineSubsystem* Sub = UBCOnlineSubsystem::Get(P))
	{
		OnlineSub = Sub;
		StateHandle = Sub->OnStateChanged.AddSP(this, &SBCOnlineScreen::HandleState);
		ProfileHandle = Sub->OnProfileChanged.AddSP(this, &SBCOnlineScreen::HandleProfile);
		PartyHandle = Sub->OnPartyChanged.AddSP(this, &SBCOnlineScreen::HandleParty);
		TicketHandle = Sub->OnTicketChanged.AddSP(this, &SBCOnlineScreen::HandleTicket);
		LadderHandle = Sub->OnLadderChanged.AddSP(this, &SBCOnlineScreen::HandleLadder);
		TicketWait = Sub->Ticket().WaitSec;
	}
	TicketUpdatedAt = FPlatformTime::Seconds();
	bWasLoggedIn = IsLoggedIn();

	TSharedRef<SWidget> Body =
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() -> EVisibility { return IsLoggedIn() ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				MakeLoginPanel()
			]
		]
		+ SOverlay::Slot()
		[
			SNew(SBox)
			.Visibility_Lambda([this]() -> EVisibility { return IsLoggedIn() ? EVisibility::Visible : EVisibility::Collapsed; })
			[
				MakeLoggedInPanel()
			]
		];

	ChildSlot
	[
		BCStyle::ScreenFrame(P, BCStyle::Txt(P, "ui.online.title"), Body)
	];

	RebuildRanked();
	RebuildParty();
	RebuildLadder();
	if (IsLoggedIn())
	{
		RefreshAll();
	}
}

SBCOnlineScreen::~SBCOnlineScreen()
{
	if (UBCOnlineSubsystem* Sub = OnlineSub.Get())
	{
		Sub->OnStateChanged.Remove(StateHandle);
		Sub->OnProfileChanged.Remove(ProfileHandle);
		Sub->OnPartyChanged.Remove(PartyHandle);
		Sub->OnTicketChanged.Remove(TicketHandle);
		Sub->OnLadderChanged.Remove(LadderHandle);
	}
}

bool SBCOnlineScreen::IsLoggedIn() const
{
	const UBCOnlineSubsystem* Sub = Online();
	return Sub && Sub->IsLoggedIn();
}

int32 SBCOnlineScreen::PartySize() const
{
	const UBCOnlineSubsystem* Sub = Online();
	if (!Sub || !Sub->Party().bInParty)
	{
		return 1;
	}
	return FMath::Max(1, Sub->Party().Members.Num());
}

const char* SBCOnlineScreen::QueueBlockReason(int32 QueueIndex) const
{
	const UBCOnlineSubsystem* Sub = Online();
	if (!Sub || !Sub->IsLoggedIn())
	{
		return "ui.online.block.offline";
	}
	const BCOnlineScreenState::FQueueInfo& Q = BCOnlineScreenState::Queues[QueueIndex];
	if (PartySize() > Q.TeamSize)
	{
		return "ui.online.block.party_too_big";
	}
	const FBCPartyState& Party = Sub->Party();
	if (Party.bInParty)
	{
		if (!Sub->IsPartyLeader())
		{
			return "ui.online.block.not_leader";
		}
		for (const FBCPartyMember& M : Party.Members)
		{
			if (!M.bReady && M.PlayerId != Party.LeaderId)
			{
				return "ui.online.block.not_ready";
			}
		}
	}
	return nullptr;
}

void SBCOnlineScreen::RefreshAll()
{
	if (UBCOnlineSubsystem* Sub = Online(); Sub && Sub->IsLoggedIn())
	{
		Sub->RefreshProfile();
		Sub->RefreshParty();
		Sub->FetchLadder(BCOnlineScreenState::RankedQueues[BCOnlineScreenState::LadderQueue]);
	}
}

void SBCOnlineScreen::DoLogin(bool bRegister)
{
	UBCOnlineSubsystem* Sub = Online();
	ABCPlayerController* P = PC.Get();
	if (!Sub || !P || !UserBox.IsValid() || !PassBox.IsValid())
	{
		return;
	}
	const FString User = UserBox->GetText().ToString().TrimStartAndEnd().ToLower();
	const FString Pass = PassBox->GetText().ToString();
	if (User.Len() < 3 || User.Len() > 24)
	{
		P->ShowToast(BCStyle::Txt(P, "ui.online.invalid_username"));
		return;
	}
	if (Pass.Len() < (bRegister ? 8 : 1))
	{
		P->ShowToast(BCStyle::Txt(P, "ui.online.invalid_password"));
		return;
	}
	if (bRegister)
	{
		FString Display = DisplayNameBox.IsValid() ? DisplayNameBox->GetText().ToString().TrimStartAndEnd() : FString();
		if (Display.Len() < 2)
		{
			Display = User;
		}
		Sub->Register(User, Pass, Display.Left(16), BCStyle::Feedback(P, "ui.online.welcome"));
	}
	else
	{
		Sub->Login(User, Pass, BCStyle::Feedback(P, "ui.online.welcome"));
	}
}

// ---------------------------------------------------------------------------------------------
// Login

TSharedRef<SWidget> SBCOnlineScreen::MakeLoginPanel()
{
	ABCPlayerController* P = PC.Get();
	auto Busy = [this]() {
		const UBCOnlineSubsystem* Sub = Online();
		return Sub && Sub->State() == EBCOnlineState::Connecting;
	};
	return SNew(SBox)
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
						SNew(STextBlock).Text(BCStyle::Txt(P, "ui.online.connect_title")).Font(BCStyle::FontOutlined(28, true, 1)).ColorAndOpacity(BCStyle::TextPrimary())
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Font(BCStyle::Font(15, true))
						.AutoWrapText(true)
						.ColorAndOpacity_Lambda([this]() -> FSlateColor {
							const UBCOnlineSubsystem* Sub = Online();
							if (Sub && Sub->State() == EBCOnlineState::Error)
							{
								return FSlateColor(BCStyle::Danger());
							}
							return FSlateColor(Sub && Sub->State() == EBCOnlineState::Connecting ? BCStyle::Warning() : BCStyle::TextSecondary());
						})
						.Text_Lambda([this]() -> FText {
							const ABCPlayerController* Owner = PC.Get();
							const UBCOnlineSubsystem* Sub = Online();
							if (!Sub)
							{
								return BCStyle::Txt(Owner, "ui.online.status.offline");
							}
							switch (Sub->State())
							{
							case EBCOnlineState::Connecting:
								return BCStyle::Txt(Owner, "ui.online.status.connecting");
							case EBCOnlineState::Error:
								return BCStyle::Fmt(Owner, "ui.online.login_failed", {BCStyle::ErrorText(Owner, Sub->LastError()).ToString()});
							default:
								return BCStyle::Txt(Owner, "ui.online.status.offline");
							}
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 2.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Font(BCStyle::Font(12))
						.ColorAndOpacity(BCStyle::TextMuted())
						.Text_Lambda([this]() -> FText {
							const UBCOnlineSubsystem* Sub = Online();
							return BCStyle::Fmt(PC.Get(), "ui.online.server", {Sub ? Sub->BackendUrl() : FString()});
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 20.0f, 0.0f, 0.0f)
					[
						BCStyle::PrimaryButton(BCStyle::Txt(P, "ui.online.guest_login"),
											   [this]() {
												   if (UBCOnlineSubsystem* Sub = Online())
												   {
													   Sub->LoginGuest(BCStyle::Feedback(PC.Get(), "ui.online.welcome"));
												   }
											   },
											   BCStyle::Bind<bool>([Busy]() { return !Busy(); }), 0.0f, 22)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						BCStyle::Label(BCStyle::Txt(P, "ui.online.guest_hint"), 13, BCStyle::TextMuted(), false, true)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 22.0f, 0.0f, 12.0f)
					[
						BCStyle::SectionHeader(BCStyle::Txt(P, "ui.online.or_account"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SAssignNew(UserBox, SEditableTextBox).Font(BCStyle::Font(17)).HintText(BCStyle::Txt(P, "ui.online.username"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SAssignNew(PassBox, SEditableTextBox)
						.Font(BCStyle::Font(17))
						.IsPassword(true)
						.HintText(BCStyle::Txt(P, "ui.online.password"))
						.OnTextCommitted_Lambda([this](const FText&, ETextCommit::Type CommitType) {
							if (CommitType == ETextCommit::OnEnter)
							{
								DoLogin(false);
							}
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 12.0f)
					[
						SAssignNew(DisplayNameBox, SEditableTextBox).Font(BCStyle::Font(17)).HintText(BCStyle::Txt(P, "ui.online.display_name"))
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(0.0f, 0.0f, 6.0f, 0.0f)
						[
							BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.online.login"), [this]() { DoLogin(false); },
													 BCStyle::Bind<bool>([Busy]() { return !Busy(); }))
						]
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						.Padding(6.0f, 0.0f, 0.0f, 0.0f)
						[
							BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.online.register"), [this]() { DoLogin(true); },
													 BCStyle::Bind<bool>([Busy]() { return !Busy(); }))
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 8.0f, 0.0f, 0.0f)
					[
						BCStyle::Label(BCStyle::Txt(P, "ui.online.register_rules"), 12, BCStyle::TextMuted(), false, true)
					],
					FMargin(28.0f, 24.0f))
			]
		];
}

// ---------------------------------------------------------------------------------------------
// Logged in

TSharedRef<SWidget> SBCOnlineScreen::MakeLoggedInPanel()
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(380.0f)
			[
				MakeProfilePanel()
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(20.0f, 0.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[MakePartyPanel()]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)[MakeQueuePanel()]
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(500.0f)
			[
				MakeLadderPanel()
			]
		];
}

TSharedRef<SWidget> SBCOnlineScreen::MakeProfilePanel()
{
	ABCPlayerController* P = PC.Get();
	return BCStyle::Panel(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Font(BCStyle::FontOutlined(26, true, 1))
			.ColorAndOpacity(BCStyle::TextPrimary())
			.Text_Lambda([this]() -> FText {
				const UBCOnlineSubsystem* Sub = Online();
				return (Sub && Sub->HasProfile()) ? FText::FromString(BC::ToFString(Sub->Profile().displayName)) : FText::GetEmpty();
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Font(BCStyle::Font(15, true))
			.ColorAndOpacity(BCStyle::XpColor())
			.Text_Lambda([this]() -> FText {
				const UBCOnlineSubsystem* Sub = Online();
				if (!Sub || !Sub->HasProfile())
				{
					return BCStyle::Txt(PC.Get(), "ui.online.loading_profile");
				}
				return BCStyle::Fmt(PC.Get(), "ui.common.level_short", {FString::FromInt(Sub->Profile().level)});
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Font(BCStyle::Font(12))
			.ColorAndOpacity(BCStyle::TextMuted())
			.Text_Lambda([this]() -> FText {
				const UBCOnlineSubsystem* Sub = Online();
				if (!Sub)
				{
					return FText::GetEmpty();
				}
				const FString Account = Sub->Username().IsEmpty() ? BCStyle::Txt(PC.Get(), "ui.online.guest").ToString() : Sub->Username();
				return FText::FromString(Account + TEXT("  \u00B7  ") + Sub->PlayerId());
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 14.0f, 0.0f, 0.0f)
		[
			BCStyle::ToggleRow(
				P, BCStyle::Txt(P, "ui.online.use_online_save"),
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
					const UBCOnlineSubsystem* Sub = Online();
					return Sub && Sub->IsLoggedIn() && Sub->HasProfile();
				}),
				BCStyle::Txt(P, "ui.online.use_online_save.desc"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 16.0f, 0.0f, 8.0f)
		[
			BCStyle::SectionHeader(BCStyle::Txt(P, "ui.online.ranked"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SAssignNew(RankedBox, SVerticalBox)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSpacer)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 16.0f, 0.0f, 0.0f)
		[
			BCStyle::Button(BCStyle::EButtonKind::Danger, BCStyle::Txt(P, "ui.online.logout"), [this]() {
				if (UBCProfileSubsystem* Profile = ProfileSub.Get())
				{
					Profile->SetPreferOnline(false);
				}
				if (UBCOnlineSubsystem* Sub = Online())
				{
					Sub->Logout();
				}
			})
		],
		FMargin(20.0f, 18.0f));
}

void SBCOnlineScreen::RebuildRanked()
{
	if (!RankedBox.IsValid())
	{
		return;
	}
	RankedBox->ClearChildren();
	ABCPlayerController* P = PC.Get();
	const UBCOnlineSubsystem* Sub = Online();
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(P);
	if (!Sub || !Data || !Sub->HasProfile())
	{
		return;
	}
	const crash::ProgressionDef& Prog = Data->Db().Progression();
	for (int32 q = 0; q < BCOnlineScreenState::NumRankedQueues; ++q)
	{
		const FString QueueId = BCOnlineScreenState::RankedQueues[q];
		const auto It = Sub->Profile().ranked.find(BC::ToStd(QueueId));
		crash::RankedEntry Entry;
		const bool bHasEntry = It != Sub->Profile().ranked.end();
		if (bHasEntry)
		{
			Entry = It->second;
		}
		std::pair<const crash::RankDef*, int> Rank = crash::RankForRating(Prog, Entry.rating);
		// The backend's tier is authoritative (e.g. crash_king also needs a top-100 ladder spot).
		for (const crash::RankDef& R : Prog.ranks)
		{
			if (bHasEntry && R.id == Entry.tier)
			{
				Rank = {&R, Entry.division};
			}
		}
		const bool bPlacement = !bHasEntry || Entry.placementsLeft > 0;
		const FLinearColor RankColor = (Rank.first && !bPlacement) ? BCStyle::FromCrash(Rank.first->color) : BCStyle::TextMuted();
		const FText RankLabel = bPlacement ? BCStyle::Fmt(P, "ui.online.placement", {FString::FromInt(bHasEntry ? Entry.placementsLeft : 5)})
										   : BCStyle::RankText(P, Rank.first, Rank.second);

		RankedBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SBorder)
				.BorderImage(BCStyle::CardBrush())
				.Padding(FMargin(12.0f, 8.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 12.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(8.0f)
						.HeightOverride(36.0f)
						[
							SNew(SBorder).BorderImage(BCStyle::RoundedBrush(RankColor, 3.0f)).Padding(0.0f)
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock).Text(BCStyle::Txt(P, BCStyle::QueueKey(QueueId))).Font(BCStyle::Font(13)).ColorAndOpacity(BCStyle::TextSecondary())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock).Text(RankLabel).Font(BCStyle::Font(18, true)).ColorAndOpacity(RankColor)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						[
							SNew(STextBlock)
							.Text(FText::AsNumber(FMath::RoundToInt(static_cast<float>(Entry.rating))))
							.Font(BCStyle::Font(18, true))
							.ColorAndOpacity(BCStyle::TextPrimary())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Right)
						[
							SNew(STextBlock)
							.Text(BCStyle::Fmt(P, "ui.online.win_loss", {FString::FromInt(Entry.wins), FString::FromInt(Entry.losses)}))
							.Font(BCStyle::Font(12))
							.ColorAndOpacity(BCStyle::TextMuted())
						]
					]
				]
			];
	}
}

// ---------------------------------------------------------------------------------------------
// Party

TSharedRef<SWidget> SBCOnlineScreen::MakePartyPanel()
{
	ABCPlayerController* P = PC.Get();
	auto InParty = [this]() {
		const UBCOnlineSubsystem* Sub = Online();
		return Sub && Sub->Party().bInParty;
	};
	return BCStyle::Panel(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			BCStyle::SectionHeader(BCStyle::Txt(P, "ui.online.party"))
		]
		// Not in a party.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([InParty]() -> EVisibility { return InParty() ? EVisibility::Collapsed : EVisibility::Visible; })
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 10.0f)
			[
				BCStyle::Label(BCStyle::Txt(P, "ui.online.party_desc"), 14, BCStyle::TextSecondary(), false, true)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 16.0f, 0.0f)
				[
					BCStyle::PrimaryButton(BCStyle::Txt(P, "ui.online.party_create"), [this]() {
						if (UBCOnlineSubsystem* Sub = Online())
						{
							Sub->CreateParty(BCStyle::Feedback(PC.Get()));
						}
					})
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 8.0f, 0.0f)
				[
					SAssignNew(PartyCodeBox, SEditableTextBox)
					.Font(BCStyle::Font(18, true))
					.HintText(BCStyle::Txt(P, "ui.online.party_code_hint"))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.online.party_join"), [this]() {
						UBCOnlineSubsystem* Sub = Online();
						const FString Code = PartyCodeBox.IsValid() ? PartyCodeBox->GetText().ToString().TrimStartAndEnd().ToUpper() : FString();
						if (Sub && Code.Len() >= 4)
						{
							Sub->JoinParty(Code, BCStyle::Feedback(PC.Get()));
						}
						else if (ABCPlayerController* Owner = PC.Get())
						{
							Owner->ShowToast(BCStyle::Txt(Owner, "ui.online.party_code_invalid"));
						}
					})
				]
			]
		]
		// In a party.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SVerticalBox)
			.Visibility_Lambda([InParty]() -> EVisibility { return InParty() ? EVisibility::Visible : EVisibility::Collapsed; })
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 16.0f, 0.0f)
				[
					SNew(STextBlock).Text(BCStyle::Txt(P, "ui.online.party_code")).Font(BCStyle::Font(15)).ColorAndOpacity(BCStyle::TextSecondary())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(BCStyle::FontOutlined(40, true, 2))
					.ColorAndOpacity(BCStyle::Accent())
					.Text_Lambda([this]() -> FText {
						const UBCOnlineSubsystem* Sub = Online();
						if (!Sub)
						{
							return FText::GetEmpty();
						}
						// Spread the letters so the code is easy to read out loud.
						FString Spaced;
						for (const TCHAR C : Sub->Party().Code)
						{
							if (!Spaced.IsEmpty())
							{
								Spaced.AppendChar(TEXT(' '));
							}
							Spaced.AppendChar(C);
						}
						return FText::FromString(Spaced);
					})
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Font(BCStyle::Font(15, true))
					.ColorAndOpacity(BCStyle::TextSecondary())
					.Text_Lambda([this]() -> FText { return FText::FromString(FString::Printf(TEXT("%d / 3"), PartySize())); })
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 10.0f, 0.0f, 10.0f)
			[
				SAssignNew(PartyMembersBox, SVerticalBox)
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 10.0f, 0.0f)
				[
					SNew(SBox)
					.Visibility_Lambda([this]() -> EVisibility {
						const UBCOnlineSubsystem* Sub = Online();
						return (Sub && !Sub->IsPartyLeader()) ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						BCStyle::PrimaryButton(BCStyle::Bind<FText>([this]() -> FText {
												   const UBCOnlineSubsystem* Sub = Online();
												   bool bReady = false;
												   if (Sub)
												   {
													   for (const FBCPartyMember& M : Sub->Party().Members)
													   {
														   bReady |= M.PlayerId == Sub->PlayerId() && M.bReady;
													   }
												   }
												   return BCStyle::Txt(PC.Get(), bReady ? "ui.online.unready" : "ui.online.ready");
											   }),
											   [this]() {
												   UBCOnlineSubsystem* Sub = Online();
												   if (!Sub)
												   {
													   return;
												   }
												   bool bReady = false;
												   for (const FBCPartyMember& M : Sub->Party().Members)
												   {
													   bReady |= M.PlayerId == Sub->PlayerId() && M.bReady;
												   }
												   Sub->SetReady(!bReady, BCStyle::Feedback(PC.Get()));
											   })
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					BCStyle::Button(BCStyle::EButtonKind::Danger, BCStyle::Txt(P, "ui.online.party_leave"), [this]() {
						if (UBCOnlineSubsystem* Sub = Online())
						{
							Sub->LeaveParty(BCStyle::Feedback(PC.Get()));
						}
					})
				]
			]
		],
		FMargin(20.0f, 16.0f));
}

void SBCOnlineScreen::RebuildParty()
{
	if (!PartyMembersBox.IsValid())
	{
		return;
	}
	PartyMembersBox->ClearChildren();
	ABCPlayerController* P = PC.Get();
	const UBCOnlineSubsystem* Sub = Online();
	if (!Sub || !Sub->Party().bInParty)
	{
		return;
	}
	const FBCPartyState& Party = Sub->Party();
	for (const FBCPartyMember& M : Party.Members)
	{
		const bool bLeader = M.PlayerId == Party.LeaderId;
		const bool bReady = M.bReady || bLeader;
		const bool bMe = M.PlayerId == Sub->PlayerId();
		PartyMembersBox->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SBorder)
				.BorderImage(BCStyle::CardBrush())
				.Padding(FMargin(12.0f, 8.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("\u25CF")))
						.Font(BCStyle::Font(14))
						.ColorAndOpacity(bReady ? BCStyle::Success() : BCStyle::TextMuted())
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(M.Name.IsEmpty() ? M.PlayerId : M.Name))
						.Font(BCStyle::Font(17, true))
						.ColorAndOpacity(bMe ? BCStyle::Accent() : BCStyle::TextPrimary())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						bLeader ? BCStyle::Badge(BCStyle::Txt(P, "ui.online.leader"), BCStyle::GoldColor(), 11) : SNullWidget::NullWidget
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(BCStyle::Txt(P, bReady ? "ui.online.is_ready" : "ui.online.not_ready"))
						.Font(BCStyle::Font(14, true))
						.ColorAndOpacity(bReady ? BCStyle::Success() : BCStyle::Warning())
					]
				]
			];
	}
}

// ---------------------------------------------------------------------------------------------
// Matchmaking

TSharedRef<SWidget> SBCOnlineScreen::MakeQueuePanel()
{
	ABCPlayerController* P = PC.Get();
	auto Searching = [this]() {
		const UBCOnlineSubsystem* Sub = Online();
		return Sub && (Sub->IsQueued() || Sub->Ticket().bMatched);
	};

	TSharedRef<SUniformGridPanel> Grid = SNew(SUniformGridPanel).SlotPadding(FMargin(5.0f));
	for (int32 q = 0; q < BCOnlineScreenState::NumQueues; ++q)
	{
		const BCOnlineScreenState::FQueueInfo& Info = BCOnlineScreenState::Queues[q];
		const FString QueueId = Info.Id;
		TSharedRef<SWidget> Card =
			SNew(SBorder)
			.BorderImage(BCStyle::CardBrush())
			.Padding(FMargin(14.0f, 12.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(BCStyle::Txt(P, BCStyle::QueueKey(QueueId))).Font(BCStyle::Font(19, true)).ColorAndOpacity(BCStyle::TextPrimary())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						BCStyle::Badge(BCStyle::Txt(P, Info.bRanked ? "ui.online.ranked_badge" : "ui.online.casual_badge"),
									   Info.bRanked ? BCStyle::GoldColor() : BCStyle::CreditsColor(), 11, !Info.bRanked)
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 10.0f)
				[
					BCStyle::Label(BCStyle::Txt(P, BCStyle::QueueDescKey(QueueId)), 13, BCStyle::TextSecondary(), false, true)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					BCStyle::PrimaryButton(BCStyle::Txt(P, "ui.online.find_match"),
										   [this, QueueId]() {
											   if (UBCOnlineSubsystem* Sub = Online())
											   {
												   Sub->StartQueue(QueueId, BCStyle::Feedback(PC.Get()));
											   }
										   },
										   BCStyle::Bind<bool>([this, q]() { return QueueBlockReason(q) == nullptr; }), 0.0f, 16)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Font(BCStyle::Font(12))
					.ColorAndOpacity(BCStyle::Warning())
					.AutoWrapText(true)
					.Visibility_Lambda([this, q]() -> EVisibility { return QueueBlockReason(q) ? EVisibility::Visible : EVisibility::Collapsed; })
					.Text_Lambda([this, q]() -> FText {
						const char* Reason = QueueBlockReason(q);
						return Reason ? BCStyle::Txt(PC.Get(), Reason) : FText::GetEmpty();
					})
				]
			];
		Grid->AddSlot(q % 2, q / 2)[Card];
	}

	return BCStyle::Panel(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			BCStyle::SectionHeader(BCStyle::Txt(P, "ui.online.matchmaking"))
		]
		// Searching / matched.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBorder)
			.Visibility_Lambda([Searching]() -> EVisibility { return Searching() ? EVisibility::Visible : EVisibility::Collapsed; })
			.BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x2A, 0x18, 0x08, 0.92f), 8.0f, BCStyle::Accent(), 1.0f))
			.Padding(FMargin(20.0f, 16.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Font(BCStyle::FontOutlined(26, true, 1))
					.ColorAndOpacity(BCStyle::Accent())
					.Text_Lambda([this]() -> FText {
						const UBCOnlineSubsystem* Sub = Online();
						if (Sub && Sub->Ticket().bMatched)
						{
							return BCStyle::Txt(PC.Get(), "ui.online.match_found");
						}
						const int32 Dots = static_cast<int32>(FPlatformTime::Seconds() * 2.0) % 4;
						return FText::FromString(BCStyle::Txt(PC.Get(), "ui.online.searching").ToString() + FString::ChrN(Dots, TEXT('.')));
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 4.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Font(BCStyle::Font(17, true))
					.ColorAndOpacity(BCStyle::TextPrimary())
					.Text_Lambda([this]() -> FText {
						const UBCOnlineSubsystem* Sub = Online();
						return Sub ? BCStyle::Txt(PC.Get(), BCStyle::QueueKey(Sub->Ticket().Queue)) : FText::GetEmpty();
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Font(BCStyle::Font(16))
					.ColorAndOpacity(BCStyle::TextSecondary())
					.Text_Lambda([this]() -> FText {
						const UBCOnlineSubsystem* Sub = Online();
						if (!Sub)
						{
							return FText::GetEmpty();
						}
						if (Sub->Ticket().bMatched)
						{
							const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
							const crash::ModeDef* Mode = Data ? Data->Db().FindMode(BC::ToStd(Sub->Ticket().Mode)) : nullptr;
							const crash::ArenaDef* Arena = Data ? Data->Db().FindArena(BC::ToStd(Sub->Ticket().Arena)) : nullptr;
							return BCStyle::Fmt(PC.Get(), "ui.online.match_info",
												{Mode ? BCStyle::TxtStd(PC.Get(), Mode->name).ToString() : Sub->Ticket().Mode,
												 Arena ? BCStyle::TxtStd(PC.Get(), Arena->name).ToString() : Sub->Ticket().Arena});
						}
						const float Elapsed = TicketWait + static_cast<float>(FPlatformTime::Seconds() - TicketUpdatedAt);
						const float Estimate = Sub->Ticket().EstimatedWaitSec;
						return BCStyle::Fmt(PC.Get(), "ui.online.wait_time",
											{BCStyle::Clock(Elapsed), Estimate > 0.0f ? BCStyle::Clock(Estimate) : FString(TEXT("--:--"))});
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Left)
				.Padding(0.0f, 12.0f, 0.0f, 0.0f)
				[
					SNew(SBox)
					.Visibility_Lambda([this]() -> EVisibility {
						const UBCOnlineSubsystem* Sub = Online();
						return (Sub && Sub->IsQueued()) ? EVisibility::Visible : EVisibility::Collapsed;
					})
					[
						BCStyle::Button(BCStyle::EButtonKind::Danger, BCStyle::Txt(P, "ui.online.cancel_search"), [this]() {
							if (UBCOnlineSubsystem* Sub = Online())
							{
								Sub->CancelQueue();
							}
						})
					]
				]
			]
		]
		// Last ticket ended without a match.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			SNew(STextBlock)
			.Font(BCStyle::Font(14, true))
			.ColorAndOpacity(BCStyle::Warning())
			.AutoWrapText(true)
			.Visibility_Lambda([this, Searching]() -> EVisibility {
				const UBCOnlineSubsystem* Sub = Online();
				const bool bEnded = Sub && (Sub->Ticket().Status == TEXT("cancelled") || Sub->Ticket().Status == TEXT("expired"));
				return (bEnded && !Searching()) ? EVisibility::Visible : EVisibility::Collapsed;
			})
			.Text_Lambda([this]() -> FText {
				const UBCOnlineSubsystem* Sub = Online();
				if (!Sub)
				{
					return FText::GetEmpty();
				}
				const FString& Reason = Sub->Ticket().Reason;
				const char* ReasonKey = "ui.online.reason.generic";
				if (Reason == TEXT("party_changed"))
				{
					ReasonKey = "ui.online.reason.party_changed";
				}
				else if (Reason == TEXT("server_aborted") || Reason == TEXT("server_lost"))
				{
					ReasonKey = "ui.online.reason.server";
				}
				else if (Sub->Ticket().Status == TEXT("expired"))
				{
					ReasonKey = "ui.online.reason.expired";
				}
				return BCStyle::Txt(PC.Get(), ReasonKey);
			})
		]
		// Queue choice.
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SBox)
			.Visibility_Lambda([Searching]() -> EVisibility { return Searching() ? EVisibility::Collapsed : EVisibility::Visible; })
			[
				Grid
			]
		],
		FMargin(20.0f, 16.0f));
}

// ---------------------------------------------------------------------------------------------
// Ladder

TSharedRef<SWidget> SBCOnlineScreen::MakeLadderPanel()
{
	ABCPlayerController* P = PC.Get();
	TArray<FText> Tabs;
	for (int32 q = 0; q < BCOnlineScreenState::NumRankedQueues; ++q)
	{
		Tabs.Add(BCStyle::Txt(P, BCStyle::QueueKey(BCOnlineScreenState::RankedQueues[q])));
	}
	auto Header = [](const FText& Text, float Width, bool bRight) -> TSharedRef<SWidget> {
		return SNew(SBox)
			.WidthOverride(Width)
			[
				SNew(STextBlock)
				.Text(Text)
				.Font(BCStyle::Font(13, true))
				.ColorAndOpacity(BCStyle::TextMuted())
				.Justification(bRight ? ETextJustify::Right : ETextJustify::Left)
			];
	};
	return BCStyle::Panel(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			BCStyle::SectionHeader(BCStyle::Txt(P, "ui.online.ladder"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			BCStyle::TabRow(
				Tabs, []() { return BCOnlineScreenState::LadderQueue; },
				[this](int32 Index) {
					BCOnlineScreenState::LadderQueue = Index;
					if (UBCOnlineSubsystem* Sub = Online(); Sub && Sub->IsLoggedIn())
					{
						Sub->FetchLadder(BCOnlineScreenState::RankedQueues[Index]);
					}
					RebuildLadder();
				},
				15)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(10.0f, 0.0f, 10.0f, 6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.online.col.rank"), 44.0f, false)]
			+ SHorizontalBox::Slot().FillWidth(1.0f)[Header(BCStyle::Txt(P, "ui.online.col.name"), 120.0f, false)]
			+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.online.col.tier"), 110.0f, false)]
			+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.online.col.rating"), 60.0f, true)]
			+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.online.col.record"), 80.0f, true)]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(LadderBox, SVerticalBox)
			]
		],
		FMargin(18.0f, 16.0f));
}

void SBCOnlineScreen::RebuildLadder()
{
	if (!LadderBox.IsValid())
	{
		return;
	}
	LadderBox->ClearChildren();
	ABCPlayerController* P = PC.Get();
	const UBCOnlineSubsystem* Sub = Online();
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(P);
	const FString Wanted = BCOnlineScreenState::RankedQueues[BCOnlineScreenState::LadderQueue];
	if (!Sub || !Data || Sub->LadderQueue() != Wanted || Sub->Ladder().Num() == 0)
	{
		LadderBox->AddSlot()
			.AutoHeight()
			.Padding(10.0f, 12.0f)
			[
				BCStyle::Label(BCStyle::Txt(P, (Sub && Sub->LadderQueue() == Wanted) ? "ui.online.ladder_empty" : "ui.common.loading"), 15,
							   BCStyle::TextMuted())
			];
		return;
	}
	const crash::ProgressionDef& Prog = Data->Db().Progression();
	int32 Row = 0;
	for (const FBCLadderEntry& E : Sub->Ladder())
	{
		const crash::RankDef* Rank = nullptr;
		for (const crash::RankDef& R : Prog.ranks)
		{
			if (BC::ToFString(R.id) == E.Tier)
			{
				Rank = &R;
			}
		}
		const bool bMe = E.PlayerId == Sub->PlayerId();
		const FLinearColor RankColor = Rank ? BCStyle::FromCrash(Rank->color) : BCStyle::TextSecondary();
		const FLinearColor RowColor = bMe ? BCStyle::Hex(0x3A, 0x22, 0x0C, 0.95f)
										  : ((Row % 2 == 0) ? BCStyle::Hex(0x1A, 0x1E, 0x24, 0.8f) : BCStyle::Hex(0x14, 0x17, 0x1C, 0.8f));
		LadderBox->AddSlot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(BCStyle::RoundedBrush(RowColor, 4.0f))
				.Padding(FMargin(10.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(44.0f)
						[
							SNew(STextBlock)
							.Text(FText::AsNumber(E.Rank))
							.Font(BCStyle::Font(16, true))
							.ColorAndOpacity(E.Rank <= 3 ? BCStyle::GoldColor() : BCStyle::TextSecondary())
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(E.Name.IsEmpty() ? E.PlayerId : E.Name))
						.Font(BCStyle::Font(16, bMe))
						.ColorAndOpacity(bMe ? BCStyle::Accent() : BCStyle::TextPrimary())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(110.0f)
						[
							SNew(STextBlock)
							.Text(Rank ? BCStyle::RankText(P, Rank, E.Division) : FText::FromString(E.Tier))
							.Font(BCStyle::Font(15, true))
							.ColorAndOpacity(RankColor)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(60.0f)
						[
							SNew(STextBlock)
							.Text(FText::AsNumber(FMath::RoundToInt(static_cast<float>(E.Rating))))
							.Font(BCStyle::Font(15, true))
							.Justification(ETextJustify::Right)
							.ColorAndOpacity(BCStyle::TextPrimary())
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(80.0f)
						[
							SNew(STextBlock)
							.Text(FText::FromString(FString::Printf(TEXT("%d-%d"), E.Wins, E.Losses)))
							.Font(BCStyle::Font(14))
							.Justification(ETextJustify::Right)
							.ColorAndOpacity(BCStyle::TextSecondary())
						]
					]
				]
			];
		++Row;
	}
}

// ---------------------------------------------------------------------------------------------
// Events

void SBCOnlineScreen::HandleState()
{
	const bool bNow = IsLoggedIn();
	if (bNow && !bWasLoggedIn)
	{
		RefreshAll();
	}
	bWasLoggedIn = bNow;
	RebuildRanked();
	RebuildParty();
	RebuildLadder();
}

void SBCOnlineScreen::HandleProfile()
{
	RebuildRanked();
}

void SBCOnlineScreen::HandleParty()
{
	RebuildParty();
}

void SBCOnlineScreen::HandleTicket()
{
	if (const UBCOnlineSubsystem* Sub = Online())
	{
		TicketWait = Sub->Ticket().WaitSec;
	}
	TicketUpdatedAt = FPlatformTime::Seconds();
}

void SBCOnlineScreen::HandleLadder()
{
	RebuildLadder();
}

TSharedRef<SWidget> BCUI::MakeOnline(ABCPlayerController* PC)
{
	return SNew(SBCOnlineScreen).Owner(PC);
}
