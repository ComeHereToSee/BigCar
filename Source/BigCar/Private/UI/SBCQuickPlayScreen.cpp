// Copyright BigCar Team. All Rights Reserved.
//
// Quick play / LAN: mode, arena (filtered to compatible arenas), bots, AI difficulty, time limit, host-LAN toggle,
// plus "join by address".

#include "Core/BCDataSubsystem.h"
#include "Core/BigCarTypes.h"
#include "CrashCore/Database.h"
#include "Gameplay/BCPlayerController.h"
#include "UI/BCUI.h"
#include "UI/BCUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SEditableTextBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

namespace BCQuickPlayState
{
// Last choices, kept for the session.
FString ModeId;
FString ArenaId; // empty = random
int32 Bots = 5;
FString Ai = TEXT("regular");
int32 TimeIndex = 0;
bool bHostLan = false;
FString Address;

const float TimeOptions[] = {0.0f, 180.0f, 300.0f, 480.0f, 600.0f};
constexpr int32 NumTimeOptions = 5;
} // namespace BCQuickPlayState

class SBCQuickPlayScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBCQuickPlayScreen) : _Owner(nullptr) {}
		SLATE_ARGUMENT(ABCPlayerController*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);

private:
	const crash::GameDatabase* GetDb() const;
	TArray<const crash::ModeDef*> Modes() const;
	TArray<const crash::ArenaDef*> ArenasFor(const crash::ModeDef* Mode) const;
	TArray<const crash::AIProfileDef*> AiProfiles() const;
	const crash::ModeDef* CurrentMode() const;
	const crash::ArenaDef* CurrentArena() const;
	int32 MaxBots() const;
	void Validate();
	void CycleMode(int32 Dir);
	void CycleArena(int32 Dir);
	void CycleAi(int32 Dir);
	bool CanStart() const;
	void Start();
	void Join();

	TWeakObjectPtr<ABCPlayerController> PC;
	TSharedPtr<SEditableTextBox> AddressBox;
};

void SBCQuickPlayScreen::Construct(const FArguments& InArgs)
{
	PC = InArgs._Owner;
	ABCPlayerController* P = PC.Get();
	Validate();

	TSharedRef<SWidget> Settings =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 10.0f)
		[
			BCStyle::SectionHeader(BCStyle::Txt(P, "ui.quick.match_settings"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			BCStyle::SettingRow(
				BCStyle::Txt(P, "ui.quick.mode"),
				BCStyle::CycleSelector(BCStyle::Bind<FText>([this]() -> FText {
										   const crash::ModeDef* Mode = CurrentMode();
										   return Mode ? BCStyle::TxtStd(PC.Get(), Mode->name) : FText::GetEmpty();
									   }),
									   [this]() { CycleMode(-1); }, [this]() { CycleMode(1); }),
				BCStyle::Bind<FText>([this]() -> FText {
					const crash::ModeDef* Mode = CurrentMode();
					return Mode ? BCStyle::TxtStd(PC.Get(), Mode->description) : FText::GetEmpty();
				}))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			BCStyle::SettingRow(
				BCStyle::Txt(P, "ui.quick.arena"),
				BCStyle::CycleSelector(BCStyle::Bind<FText>([this]() -> FText {
										   const crash::ArenaDef* Arena = CurrentArena();
										   return Arena ? BCStyle::TxtStd(PC.Get(), Arena->name) : BCStyle::Txt(PC.Get(), "ui.quick.random_arena");
									   }),
									   [this]() { CycleArena(-1); }, [this]() { CycleArena(1); }),
				BCStyle::Bind<FText>([this]() -> FText {
					const crash::ArenaDef* Arena = CurrentArena();
					return Arena ? BCStyle::TxtStd(PC.Get(), Arena->description) : BCStyle::Txt(PC.Get(), "ui.quick.random_arena.desc");
				}))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			BCStyle::SettingRow(BCStyle::Txt(P, "ui.quick.bots"),
								BCStyle::CycleSelector(BCStyle::Bind<FText>([this]() -> FText {
														   return BCQuickPlayState::Bots <= 0 ? BCStyle::Txt(PC.Get(), "ui.quick.no_bots")
																							  : FText::AsNumber(BCQuickPlayState::Bots);
													   }),
													   [this]() {
														   const int32 Max = MaxBots();
														   BCQuickPlayState::Bots = BCQuickPlayState::Bots <= 0 ? Max : BCQuickPlayState::Bots - 1;
													   },
													   [this]() {
														   const int32 Max = MaxBots();
														   BCQuickPlayState::Bots = BCQuickPlayState::Bots >= Max ? 0 : BCQuickPlayState::Bots + 1;
													   }))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			BCStyle::SettingRow(BCStyle::Txt(P, "ui.quick.ai"),
								BCStyle::CycleSelector(BCStyle::Bind<FText>([this]() -> FText {
														   const crash::GameDatabase* Db = GetDb();
														   const crash::AIProfileDef* Profile =
															   Db ? Db->FindAIProfile(BC::ToStd(BCQuickPlayState::Ai)) : nullptr;
														   return Profile ? BCStyle::TxtStd(PC.Get(), Profile->name) : FText::FromString(BCQuickPlayState::Ai);
													   }),
													   [this]() { CycleAi(-1); }, [this]() { CycleAi(1); },
													   BCStyle::Bind<bool>([]() { return BCQuickPlayState::Bots > 0; })))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			BCStyle::SettingRow(BCStyle::Txt(P, "ui.quick.time_limit"),
								BCStyle::CycleSelector(BCStyle::Bind<FText>([this]() -> FText {
														   const float Seconds = BCQuickPlayState::TimeOptions[BCQuickPlayState::TimeIndex];
														   if (Seconds <= 0.0f)
														   {
															   const crash::ModeDef* Mode = CurrentMode();
															   const float Default = Mode ? Mode->timeLimit : 0.0f;
															   return Default > 0.0f ? BCStyle::Fmt(PC.Get(), "ui.quick.time_default", {BCStyle::Clock(Default)})
																					 : BCStyle::Txt(PC.Get(), "ui.quick.time_unlimited");
														   }
														   return BCStyle::Fmt(PC.Get(), "ui.quick.minutes", {FString::FromInt(FMath::RoundToInt(Seconds / 60.0f))});
													   }),
													   []() {
														   BCQuickPlayState::TimeIndex = (BCQuickPlayState::TimeIndex + BCQuickPlayState::NumTimeOptions - 1) %
																						 BCQuickPlayState::NumTimeOptions;
													   },
													   []() { BCQuickPlayState::TimeIndex = (BCQuickPlayState::TimeIndex + 1) % BCQuickPlayState::NumTimeOptions; }))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			BCStyle::ToggleRow(P, BCStyle::Txt(P, "ui.quick.host_lan"), []() { return BCQuickPlayState::bHostLan; },
							   [](bool bOn) { BCQuickPlayState::bHostLan = bOn; }, true, BCStyle::Txt(P, "ui.quick.host_lan.desc"))
		];

	auto Fact = [this](const char* NameKey, TFunction<FText()> Value) -> TSharedRef<SWidget> {
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				SNew(STextBlock).Text(BCStyle::Txt(PC.Get(), NameKey)).Font(BCStyle::Font(15)).ColorAndOpacity(BCStyle::TextSecondary())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text_Lambda([Value]() -> FText { return Value(); })
				.Font(BCStyle::Font(15, true))
				.ColorAndOpacity(BCStyle::TextPrimary())
			];
	};

	TSharedRef<SWidget> ModeCard = BCStyle::Panel(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Font(BCStyle::FontOutlined(30, true, 1))
			.ColorAndOpacity(BCStyle::Accent())
			.Text_Lambda([this]() -> FText {
				const crash::ModeDef* Mode = CurrentMode();
				return Mode ? BCStyle::TxtStd(PC.Get(), Mode->name) : FText::GetEmpty();
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 12.0f)
		[
			SNew(STextBlock)
			.Font(BCStyle::Font(16, true))
			.ColorAndOpacity(BCStyle::TextSecondary())
			.Text_Lambda([this]() -> FText {
				const crash::ArenaDef* Arena = CurrentArena();
				return Arena ? BCStyle::TxtStd(PC.Get(), Arena->name) : BCStyle::Txt(PC.Get(), "ui.quick.random_arena");
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
		[
			Fact("ui.quick.fact.teams", [this]() -> FText {
				const crash::ModeDef* Mode = CurrentMode();
				if (!Mode || Mode->teams < 2)
				{
					return BCStyle::Txt(PC.Get(), "ui.quick.ffa");
				}
				return BCStyle::Fmt(PC.Get(), "ui.quick.teams_n", {FString::FromInt(Mode->teams)});
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
		[
			Fact("ui.quick.fact.win", [this]() -> FText {
				const crash::ModeDef* Mode = CurrentMode();
				if (!Mode)
				{
					return FText::GetEmpty();
				}
				switch (Mode->win)
				{
				case crash::WinCondition::Score:
					return BCStyle::Txt(PC.Get(), "ui.win.score");
				case crash::WinCondition::Takedowns:
					return BCStyle::Txt(PC.Get(), "ui.win.takedowns");
				case crash::WinCondition::HillTime:
					return BCStyle::Txt(PC.Get(), "ui.win.hill_time");
				case crash::WinCondition::Survive:
					return BCStyle::Txt(PC.Get(), "ui.win.survive");
				case crash::WinCondition::Objective:
					return BCStyle::Txt(PC.Get(), "ui.win.objective");
				default:
					return BCStyle::Txt(PC.Get(), "ui.win.last_standing");
				}
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
		[
			Fact("ui.quick.fact.respawn", [this]() -> FText {
				const crash::ModeDef* Mode = CurrentMode();
				if (!Mode)
				{
					return FText::GetEmpty();
				}
				if (Mode->ringOut && Mode->lives > 1)
				{
					return BCStyle::Fmt(PC.Get(), "ui.quick.lives_n", {FString::FromInt(Mode->lives)});
				}
				return BCStyle::Txt(PC.Get(), Mode->respawn ? "ui.common.yes" : "ui.common.no");
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
		[
			Fact("ui.quick.fact.rounds", [this]() -> FText {
				const crash::ModeDef* Mode = CurrentMode();
				if (!Mode || Mode->rounds <= 1)
				{
					return BCStyle::Txt(PC.Get(), "ui.quick.single_round");
				}
				return BCStyle::Fmt(PC.Get(), "ui.quick.best_of", {FString::FromInt(Mode->rounds), FString::FromInt(Mode->roundsToWin)});
			})
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 2.0f)
		[
			Fact("ui.quick.fact.players", [this]() -> FText {
				const crash::ModeDef* Mode = CurrentMode();
				return Mode ? FText::FromString(FString::Printf(TEXT("%d - %d"), Mode->minPlayers, Mode->maxPlayers)) : FText::GetEmpty();
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 18.0f, 0.0f, 0.0f)
		[
			BCStyle::PrimaryButton(
				BCStyle::Bind<FText>([this]() -> FText {
					return BCStyle::Txt(PC.Get(), BCQuickPlayState::bHostLan ? "ui.quick.start_lan" : "ui.quick.start");
				}),
				[this]() { Start(); }, BCStyle::Bind<bool>([this]() { return CanStart(); }), 0.0f, 24)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Font(BCStyle::Font(13))
			.ColorAndOpacity(BCStyle::Warning())
			.AutoWrapText(true)
			.Visibility_Lambda([this]() -> EVisibility { return CanStart() ? EVisibility::Collapsed : EVisibility::Visible; })
			.Text_Lambda([this]() -> FText {
				const crash::ModeDef* Mode = CurrentMode();
				return BCStyle::Fmt(PC.Get(), "ui.quick.need_opponents", {FString::FromInt(Mode ? FMath::Max(1, Mode->minPlayers - 1) : 1)});
			})
		],
		FMargin(22.0f, 18.0f));

	TSharedRef<SWidget> JoinCard = BCStyle::Panel(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			BCStyle::SectionHeader(BCStyle::Txt(P, "ui.quick.join_title"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			BCStyle::Label(BCStyle::Txt(P, "ui.quick.join_desc"), 14, BCStyle::TextSecondary(), false, true)
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SAssignNew(AddressBox, SEditableTextBox)
				.Font(BCStyle::Font(17))
				.Text(FText::FromString(BCQuickPlayState::Address))
				.HintText(BCStyle::Txt(P, "ui.quick.address_hint"))
				.OnTextChanged_Lambda([](const FText& NewText) { BCQuickPlayState::Address = NewText.ToString().TrimStartAndEnd(); })
				.OnTextCommitted_Lambda([this](const FText& NewText, ETextCommit::Type CommitType) {
					BCQuickPlayState::Address = NewText.ToString().TrimStartAndEnd();
					if (CommitType == ETextCommit::OnEnter)
					{
						Join();
					}
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.quick.join"), [this]() { Join(); },
										 BCStyle::Bind<bool>([]() { return !BCQuickPlayState::Address.IsEmpty(); }), 110.0f)
			]
		],
		FMargin(22.0f, 18.0f));

	TSharedRef<SWidget> Body =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				Settings
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.Padding(28.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(480.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ModeCard]
				+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 16.0f, 0.0f, 0.0f)[JoinCard]
			]
		];

	ChildSlot
	[
		BCStyle::ScreenFrame(P, BCStyle::Txt(P, "ui.quick.title"), Body)
	];
}

const crash::GameDatabase* SBCQuickPlayScreen::GetDb() const
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
	return Data ? &Data->Db() : nullptr;
}

TArray<const crash::ModeDef*> SBCQuickPlayScreen::Modes() const
{
	TArray<const crash::ModeDef*> Out;
	if (const crash::GameDatabase* Db = GetDb())
	{
		for (const crash::ModeDef& M : Db->Modes())
		{
			// Campaign-only modes (survival, destruction, boss, explore) are not playable here.
			if (M.online && M.win != crash::WinCondition::Objective && M.win != crash::WinCondition::Survive)
			{
				Out.Add(&M);
			}
		}
	}
	return Out;
}

TArray<const crash::ArenaDef*> SBCQuickPlayScreen::ArenasFor(const crash::ModeDef* Mode) const
{
	TArray<const crash::ArenaDef*> Out;
	const crash::GameDatabase* Db = GetDb();
	if (!Db || !Mode)
	{
		return Out;
	}
	for (const crash::ArenaDef& A : Db->Arenas())
	{
		if (A.hub)
		{
			continue;
		}
		if (Mode->ringOut && A.walls)
		{
			continue;
		}
		if (Mode->hill && A.FindHill() == nullptr)
		{
			continue;
		}
		Out.Add(&A);
	}
	return Out;
}

TArray<const crash::AIProfileDef*> SBCQuickPlayScreen::AiProfiles() const
{
	TArray<const crash::AIProfileDef*> Out;
	if (const crash::GameDatabase* Db = GetDb())
	{
		for (const crash::AIProfileDef& A : Db->AIProfiles())
		{
			Out.Add(&A);
		}
	}
	return Out;
}

const crash::ModeDef* SBCQuickPlayScreen::CurrentMode() const
{
	for (const crash::ModeDef* M : Modes())
	{
		if (BC::ToFString(M->id) == BCQuickPlayState::ModeId)
		{
			return M;
		}
	}
	return nullptr;
}

const crash::ArenaDef* SBCQuickPlayScreen::CurrentArena() const
{
	if (BCQuickPlayState::ArenaId.IsEmpty())
	{
		return nullptr;
	}
	const crash::GameDatabase* Db = GetDb();
	return Db ? Db->FindArena(BC::ToStd(BCQuickPlayState::ArenaId)) : nullptr;
}

int32 SBCQuickPlayScreen::MaxBots() const
{
	const crash::ModeDef* Mode = CurrentMode();
	return FMath::Clamp(Mode ? Mode->maxPlayers - 1 : 11, 0, 11);
}

void SBCQuickPlayScreen::Validate()
{
	const TArray<const crash::ModeDef*> AllModes = Modes();
	if (AllModes.Num() == 0)
	{
		return;
	}
	if (!CurrentMode())
	{
		BCQuickPlayState::ModeId = BC::ToFString(AllModes[0]->id);
	}
	const TArray<const crash::ArenaDef*> Arenas = ArenasFor(CurrentMode());
	bool bArenaOk = BCQuickPlayState::ArenaId.IsEmpty();
	for (const crash::ArenaDef* A : Arenas)
	{
		bArenaOk |= BC::ToFString(A->id) == BCQuickPlayState::ArenaId;
	}
	if (!bArenaOk)
	{
		BCQuickPlayState::ArenaId = Arenas.Num() > 0 ? BC::ToFString(Arenas[0]->id) : FString();
	}
	BCQuickPlayState::Bots = FMath::Clamp(BCQuickPlayState::Bots, 0, MaxBots());
	BCQuickPlayState::TimeIndex = FMath::Clamp(BCQuickPlayState::TimeIndex, 0, BCQuickPlayState::NumTimeOptions - 1);
	const crash::GameDatabase* Db = GetDb();
	if (Db && !Db->FindAIProfile(BC::ToStd(BCQuickPlayState::Ai)) && !Db->AIProfiles().empty())
	{
		BCQuickPlayState::Ai = BC::ToFString(Db->AIProfiles().front().id);
	}
}

void SBCQuickPlayScreen::CycleMode(int32 Dir)
{
	const TArray<const crash::ModeDef*> AllModes = Modes();
	if (AllModes.Num() == 0)
	{
		return;
	}
	int32 Index = 0;
	for (int32 i = 0; i < AllModes.Num(); ++i)
	{
		if (BC::ToFString(AllModes[i]->id) == BCQuickPlayState::ModeId)
		{
			Index = i;
		}
	}
	Index = (Index + Dir + AllModes.Num()) % AllModes.Num();
	BCQuickPlayState::ModeId = BC::ToFString(AllModes[Index]->id);
	Validate();
}

void SBCQuickPlayScreen::CycleArena(int32 Dir)
{
	const TArray<const crash::ArenaDef*> Arenas = ArenasFor(CurrentMode());
	// Index 0 = random, then the compatible arenas.
	const int32 Count = Arenas.Num() + 1;
	int32 Index = 0;
	for (int32 i = 0; i < Arenas.Num(); ++i)
	{
		if (BC::ToFString(Arenas[i]->id) == BCQuickPlayState::ArenaId)
		{
			Index = i + 1;
		}
	}
	Index = (Index + Dir + Count) % Count;
	BCQuickPlayState::ArenaId = Index == 0 ? FString() : BC::ToFString(Arenas[Index - 1]->id);
}

void SBCQuickPlayScreen::CycleAi(int32 Dir)
{
	const TArray<const crash::AIProfileDef*> Profiles = AiProfiles();
	if (Profiles.Num() == 0)
	{
		return;
	}
	int32 Index = 0;
	for (int32 i = 0; i < Profiles.Num(); ++i)
	{
		if (BC::ToFString(Profiles[i]->id) == BCQuickPlayState::Ai)
		{
			Index = i;
		}
	}
	Index = (Index + Dir + Profiles.Num()) % Profiles.Num();
	BCQuickPlayState::Ai = BC::ToFString(Profiles[Index]->id);
}

bool SBCQuickPlayScreen::CanStart() const
{
	const crash::ModeDef* Mode = CurrentMode();
	if (!Mode)
	{
		return false;
	}
	// Hosting a LAN game waits for human opponents; otherwise bots must fill the minimum.
	return BCQuickPlayState::bHostLan || BCQuickPlayState::Bots + 1 >= Mode->minPlayers;
}

void SBCQuickPlayScreen::Start()
{
	ABCPlayerController* P = PC.Get();
	if (!P || !CanStart())
	{
		return;
	}
	FString Arena = BCQuickPlayState::ArenaId;
	if (Arena.IsEmpty())
	{
		const TArray<const crash::ArenaDef*> Arenas = ArenasFor(CurrentMode());
		if (Arenas.Num() == 0)
		{
			return;
		}
		Arena = BC::ToFString(Arenas[FMath::RandRange(0, Arenas.Num() - 1)]->id);
	}
	P->StartQuickPlay(BCQuickPlayState::ModeId, Arena, BCQuickPlayState::Bots, BCQuickPlayState::Ai,
					  BCQuickPlayState::TimeOptions[BCQuickPlayState::TimeIndex], BCQuickPlayState::bHostLan);
}

void SBCQuickPlayScreen::Join()
{
	ABCPlayerController* P = PC.Get();
	FString Address = AddressBox.IsValid() ? AddressBox->GetText().ToString().TrimStartAndEnd() : BCQuickPlayState::Address;
	if (!P || Address.IsEmpty())
	{
		return;
	}
	BCQuickPlayState::Address = Address;
	P->ShowToast(BCStyle::Fmt(P, "ui.quick.joining", {Address}));
	P->JoinAddress(Address);
}

TSharedRef<SWidget> BCUI::MakeQuickPlay(ABCPlayerController* PC)
{
	return SNew(SBCQuickPlayScreen).Owner(PC);
}
