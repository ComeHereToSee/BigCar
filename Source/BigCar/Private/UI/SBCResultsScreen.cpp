// Copyright BigCar Team. All Rights Reserved.
//
// Match / level results: win-lose banner, placement, campaign stars (NEW badges), rewards (credits, XP, items,
// level-ups, ranked rating change) and the final scoreboard. Continue, play again (offline) and next level (campaign).

#include "Core/BCDataSubsystem.h"
#include "Core/BCProfileSubsystem.h"
#include "Core/BigCarTypes.h"
#include "CrashCore/Campaign.h"
#include "CrashCore/Profile.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Gameplay/BCGameState.h"
#include "Gameplay/BCPlayerController.h"
#include "Gameplay/BCPlayerState.h"
#include "UI/BCUI.h"
#include "UI/BCUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

class SBCResultsScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBCResultsScreen) : _Owner(nullptr) {}
		SLATE_ARGUMENT(ABCPlayerController*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	const ABCGameState* GetGS() const;
	const crash::LevelDef* GetLevel() const;
	const crash::LevelDef* FindNextLevel() const;
	FText Subtitle() const;
	TSharedRef<SWidget> MakeBanner();
	TSharedRef<SWidget> MakeStars();
	TSharedRef<SWidget> MakeRewards();
	TSharedRef<SWidget> MakeButtons();
	void RebuildScoreboard();

	TWeakObjectPtr<ABCPlayerController> PC;
	FBCPersonalResult Result;
	TSharedPtr<SVerticalBox> ScoreRows;
	TSharedPtr<SButton> ContinueButton;
	int32 LastRowCount = -1;
	double OpenedAt = 0.0;
	bool bFocusDone = false;
};

void SBCResultsScreen::Construct(const FArguments& InArgs)
{
	PC = InArgs._Owner;
	ABCPlayerController* P = PC.Get();
	if (P)
	{
		Result = P->LastResult();
	}
	OpenedAt = FPlatformTime::Seconds();

	auto Header = [](const FText& Text, float Width, bool bFill) -> TSharedRef<SWidget> {
		return SNew(SBox)
			.WidthOverride(bFill ? FOptionalSize() : FOptionalSize(Width))
			[
				SNew(STextBlock)
				.Text(Text)
				.Font(BCStyle::Font(13, true))
				.ColorAndOpacity(BCStyle::TextMuted())
				.Justification(bFill ? ETextJustify::Left : ETextJustify::Right)
			];
	};

	TSharedRef<SWidget> Scoreboard = BCStyle::Panel(
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 8.0f)
		[
			BCStyle::SectionHeader(BCStyle::Txt(P, "ui.results.scoreboard"))
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(18.0f, 0.0f, 10.0f, 6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)[Header(BCStyle::Txt(P, "ui.results.col.name"), 0.0f, true)]
			+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.results.col.team"), 90.0f, false)]
			+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.results.col.score"), 90.0f, false)]
			+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.results.col.takedowns"), 70.0f, false)]
			+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.results.col.assists"), 70.0f, false)]
			+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.results.col.wrecked"), 70.0f, false)]
			+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.results.col.damage"), 90.0f, false)]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(ScoreRows, SVerticalBox)
			]
		],
		FMargin(18.0f, 14.0f));

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(BCStyle::SolidBrush(BCStyle::Hex(0x04, 0x05, 0x07, 0.84f))).Padding(0.0f)
		]
		+ SOverlay::Slot()
		.Padding(FMargin(56.0f, 36.0f, 56.0f, 32.0f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				MakeBanner()
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(0.0f, 20.0f, 0.0f, 20.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				[
					SNew(SBox)
					.WidthOverride(500.0f)
					[
						SNew(SScrollBox)
						+ SScrollBox::Slot()
						[
							SNew(SVerticalBox)
							+ SVerticalBox::Slot().AutoHeight()[MakeStars()]
							+ SVerticalBox::Slot().AutoHeight()[MakeRewards()]
						]
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(24.0f, 0.0f, 0.0f, 0.0f)
				[
					Scoreboard
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			[
				MakeButtons()
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(0.0f, 80.0f, 0.0f, 0.0f)
		[
			BCStyle::ToastLayer(P)
		]
	];
	RebuildScoreboard();
}

void SBCResultsScreen::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// Results replicate separately from the personal result RPC: refresh the table when rows arrive.
	const ABCGameState* GS = GetGS();
	const int32 Rows = GS ? GS->Results.Num() : 0;
	if (Rows != LastRowCount)
	{
		RebuildScoreboard();
	}
	if (!bFocusDone && ContinueButton.IsValid() && FSlateApplication::IsInitialized())
	{
		bFocusDone = true;
		FSlateApplication::Get().SetKeyboardFocus(ContinueButton, EFocusCause::SetDirectly);
	}
}

const ABCGameState* SBCResultsScreen::GetGS() const
{
	const ABCPlayerController* P = PC.Get();
	const UWorld* World = P ? P->GetWorld() : nullptr;
	return World ? World->GetGameState<ABCGameState>() : nullptr;
}

const crash::LevelDef* SBCResultsScreen::GetLevel() const
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
	return (Data && !Result.LevelId.IsEmpty()) ? Data->Db().FindLevel(BC::ToStd(Result.LevelId)) : nullptr;
}

const crash::LevelDef* SBCResultsScreen::FindNextLevel() const
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
	const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(PC.Get());
	const crash::LevelDef* Level = GetLevel();
	if (!Data || !Profile || !Level)
	{
		return nullptr;
	}
	const crash::GameDatabase& Db = Data->Db();
	const std::vector<crash::ChapterDef>& Chapters = Db.Campaign().chapters;
	for (size_t c = 0; c < Chapters.size(); ++c)
	{
		const std::vector<crash::LevelDef>& Levels = Chapters[c].levels;
		for (size_t l = 0; l < Levels.size(); ++l)
		{
			if (Levels[l].id != Level->id)
			{
				continue;
			}
			const crash::LevelDef* Next = nullptr;
			if (l + 1 < Levels.size())
			{
				Next = &Levels[l + 1];
			}
			else if (c + 1 < Chapters.size() && !Chapters[c + 1].levels.empty())
			{
				Next = &Chapters[c + 1].levels.front();
			}
			return (Next && crash::IsLevelUnlocked(Db, Profile->Local(), *Next)) ? Next : nullptr;
		}
	}
	return nullptr;
}

FText SBCResultsScreen::Subtitle() const
{
	const ABCPlayerController* P = PC.Get();
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(P);
	if (!Data)
	{
		return FText::GetEmpty();
	}
	if (const crash::LevelDef* Level = GetLevel())
	{
		return BCStyle::TxtStd(P, Level->name);
	}
	const ABCGameState* GS = GetGS();
	if (!GS)
	{
		return FText::GetEmpty();
	}
	const crash::ModeDef* Mode = Data->Db().FindMode(BC::ToStd(GS->ModeId));
	const crash::ArenaDef* Arena = Data->Db().FindArena(BC::ToStd(GS->ArenaId));
	FString Out = Mode ? BCStyle::TxtStd(P, Mode->name).ToString() : FString();
	if (Arena)
	{
		Out += (Out.IsEmpty() ? FString() : FString(TEXT("  \u00B7  "))) + BCStyle::TxtStd(P, Arena->name).ToString();
	}
	return FText::FromString(Out);
}

TSharedRef<SWidget> SBCResultsScreen::MakeBanner()
{
	ABCPlayerController* P = PC.Get();
	const bool bWon = Result.bWon;
	const char* TitleKey = Result.bCampaign ? (bWon ? "ui.results.level_complete" : "ui.results.level_failed")
											: (bWon ? "ui.results.victory" : "ui.results.defeat");
	const FLinearColor Color = bWon ? BCStyle::Accent() : BCStyle::Danger();
	const ABCGameState* GS = GetGS();

	FText Extra = FText::GetEmpty();
	if (GS && GS->IsTeamMode() && GS->WinnerTeam >= 0)
	{
		Extra = BCStyle::Fmt(P, "ui.results.team_wins", {BCStyle::TeamName(P, GS->WinnerTeam).ToString()});
	}
	else if (Result.Placement > 0)
	{
		Extra = BCStyle::Fmt(P, "ui.results.placement", {FString::FromInt(Result.Placement)});
	}

	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 20.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(10.0f)
			.HeightOverride(110.0f)
			[
				SNew(SBorder).BorderImage(BCStyle::SolidBrush(Color)).Padding(0.0f)
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(BCStyle::Txt(P, TitleKey))
				.Font_Lambda([this]() -> FSlateFontInfo {
					// Slam-in: starts big and settles.
					const float T = FMath::Clamp(static_cast<float>(FPlatformTime::Seconds() - OpenedAt) / 0.35f, 0.0f, 1.0f);
					const int32 Size = 80 + 4 * FMath::RoundToInt((1.0f - T) * 6.0f);
					return BCStyle::FontOutlined(Size, true, 4);
				})
				.ColorAndOpacity(Color)
				.ShadowOffset(FVector2D(0.0f, 5.0f))
				.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.7f))
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(BCStyle::Font(22, true))
				.ColorAndOpacity(BCStyle::TextSecondary())
				.Text_Lambda([this]() -> FText { return Subtitle(); })
			]
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
			SNew(STextBlock).Text(Extra).Font(BCStyle::FontOutlined(36, true, 2)).ColorAndOpacity(BCStyle::TextPrimary())
		];
}

TSharedRef<SWidget> SBCResultsScreen::MakeStars()
{
	ABCPlayerController* P = PC.Get();
	const crash::LevelDef* Level = GetLevel();
	if (!Result.bCampaign || !Level)
	{
		return SNullWidget::NullWidget;
	}
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[BCStyle::SectionHeader(BCStyle::Txt(P, "ui.results.stars"))];
	for (int32 s = 0; s < 3; ++s)
	{
		const bool bEarned = Result.Stars.IsValidIndex(s) && Result.Stars[s];
		const bool bNew = Result.NewStars.IsValidIndex(s) && Result.NewStars[s];
		Box->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 8.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 12.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TEXT("\u2605")))
					.Font(BCStyle::FontOutlined(40, true, 2))
					.ColorAndOpacity(bEarned ? BCStyle::GoldColor() : BCStyle::Hex(0x33, 0x37, 0x3E))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					BCStyle::Label(BCStyle::ObjectiveText(P, Level->stars[static_cast<size_t>(s)]), 16,
								   bEarned ? BCStyle::TextPrimary() : BCStyle::TextMuted(), bEarned, true)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(8.0f, 0.0f, 0.0f, 0.0f)
				[
					bNew ? BCStyle::Badge(BCStyle::Txt(P, "ui.results.new"), BCStyle::Danger(), 13) : SNullWidget::NullWidget
				]
			];
	}
	return BCStyle::Panel(Box, FMargin(18.0f, 14.0f));
}

TSharedRef<SWidget> SBCResultsScreen::MakeRewards()
{
	ABCPlayerController* P = PC.Get();
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(P);
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 10.0f)[BCStyle::SectionHeader(BCStyle::Txt(P, "ui.results.rewards"))];

	Box->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 24.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("+") + BCStyle::CurrencyText(P, crash::Currency::Credits, Result.Credits).ToString()))
				.Font(BCStyle::Font(24, true))
				.ColorAndOpacity(BCStyle::CreditsColor())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("+") + BCStyle::Fmt(P, "ui.common.xp_amount", {BCStyle::Num(Result.Xp)}).ToString()))
				.Font(BCStyle::Font(24, true))
				.ColorAndOpacity(BCStyle::XpColor())
			]
		];

	if (Result.LevelsGained > 0)
	{
		const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(P);
		const int32 NowLevel = Profile ? (Result.bOnline ? Profile->Active().level : Profile->Local().level) : 0;
		Box->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 6.0f)
			[
				SNew(SBorder)
				.BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x24, 0x18, 0x40, 0.9f), 6.0f, BCStyle::XpColor(), 1.0f))
				.Padding(FMargin(14.0f, 8.0f))
				[
					SNew(STextBlock)
					.Text(Result.bOnline || NowLevel <= 0 ? BCStyle::Fmt(P, "ui.results.level_up_plain", {FString::FromInt(Result.LevelsGained)})
														 : BCStyle::Fmt(P, "ui.results.level_up", {FString::FromInt(NowLevel)}))
					.Font(BCStyle::Font(20, true))
					.ColorAndOpacity(BCStyle::XpColor())
				]
			];
	}

	if (Result.Items.Num() > 0 && Data)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 4.0f)[BCStyle::Label(BCStyle::Txt(P, "ui.results.items"), 15, BCStyle::Accent(), true)];
		for (const FString& Entry : Result.Items)
		{
			FString TypeStr;
			FString IdStr;
			if (!Entry.Split(TEXT(":"), &TypeStr, &IdStr))
			{
				continue;
			}
			crash::ItemRef Item;
			if (!crash::FromString(BC::ToStd(TypeStr), Item.type))
			{
				continue;
			}
			Item.id = BC::ToStd(IdStr);
			Box->AddSlot()
				.AutoHeight()
				.Padding(0.0f, 2.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 8.0f, 0.0f)
					[
						BCStyle::Badge(BCStyle::Txt(P, BCStyle::ItemTypeKey(Item.type)), BCStyle::TextSecondary(), 11, false)
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						BCStyle::Label(Data->ItemName(Item), 17, BCStyle::TextPrimary(), true)
					]
				];
		}
	}

	// Ranked rating change (online).
	if (Result.bOnline && (Result.RatingBefore > 0.0f || Result.RatingAfter > 0.0f) && Data)
	{
		const int32 Before = FMath::RoundToInt(Result.RatingBefore);
		const int32 After = FMath::RoundToInt(Result.RatingAfter);
		const int32 Delta = After - Before;
		const std::pair<const crash::RankDef*, int> Rank = crash::RankForRating(Data->Db().Progression(), Result.RatingAfter);
		Box->AddSlot().AutoHeight().Padding(0.0f, 12.0f, 0.0f, 6.0f)[BCStyle::SectionHeader(BCStyle::Txt(P, "ui.results.rating"))];
		Box->AddSlot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 16.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("%d \u2192 %d"), Before, After)))
					.Font(BCStyle::Font(24, true))
					.ColorAndOpacity(BCStyle::TextPrimary())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 16.0f, 0.0f)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("%+d"), Delta)))
					.Font(BCStyle::Font(24, true))
					.ColorAndOpacity(Delta >= 0 ? BCStyle::Success() : BCStyle::Danger())
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(BCStyle::RankText(P, Rank.first, Rank.second))
					.Font(BCStyle::Font(20, true))
					.ColorAndOpacity(Rank.first ? BCStyle::FromCrash(Rank.first->color) : BCStyle::TextSecondary())
				]
			];
	}

	if (!Result.bOnline && !Result.bCampaign)
	{
		Box->AddSlot().AutoHeight().Padding(0.0f, 10.0f, 0.0f, 0.0f)[BCStyle::Label(BCStyle::Txt(P, "ui.results.offline_note"), 13, BCStyle::TextMuted(), false, true)];
	}
	return SNew(SBox).Padding(FMargin(0.0f, 12.0f, 0.0f, 0.0f))[BCStyle::Panel(Box, FMargin(18.0f, 14.0f))];
}

TSharedRef<SWidget> SBCResultsScreen::MakeButtons()
{
	ABCPlayerController* P = PC.Get();
	TWeakObjectPtr<ABCPlayerController> WeakPC = PC;
	const bool bOffline = P && P->GetNetMode() == NM_Standalone;
	const crash::LevelDef* Next = (Result.bCampaign && Result.bWon) ? FindNextLevel() : nullptr;
	const FString NextId = Next ? BC::ToFString(Next->id) : FString();

	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	if (bOffline)
	{
		Row->AddSlot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.results.retry"),
										 [WeakPC]() {
											 if (ABCPlayerController* Owner = WeakPC.Get())
											 {
												 Owner->RestartMatch();
											 }
										 },
										 true, 200.0f, 19)
			];
	}
	if (!NextId.IsEmpty())
	{
		Row->AddSlot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 12.0f, 0.0f)
			[
				BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.results.next_level"),
										 [WeakPC, NextId]() {
											 if (ABCPlayerController* Owner = WeakPC.Get())
											 {
												 Owner->StartCampaignLevel(NextId);
											 }
										 },
										 true, 200.0f, 19)
			];
	}
	Row->AddSlot()
		.AutoWidth()
		[
			SNew(SBox)
			.MinDesiredWidth(240.0f)
			[
				SAssignNew(ContinueButton, SButton)
				.ButtonStyle(BCStyle::PrimaryButtonStyle())
				.HAlign(HAlign_Center)
				.ContentPadding(FMargin(24.0f, 12.0f))
				.OnClicked_Lambda([WeakPC]() -> FReply {
					if (ABCPlayerController* Owner = WeakPC.Get())
					{
						Owner->ReturnToMenu();
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(BCStyle::Txt(P, "ui.results.continue")).Font(BCStyle::Font(21, true)).ColorAndOpacity(BCStyle::TextOnAccent())
				]
			]
		];
	return Row;
}

void SBCResultsScreen::RebuildScoreboard()
{
	if (!ScoreRows.IsValid())
	{
		return;
	}
	ScoreRows->ClearChildren();
	ABCPlayerController* P = PC.Get();
	const ABCGameState* GS = GetGS();
	LastRowCount = GS ? GS->Results.Num() : 0;
	if (!GS || GS->Results.Num() == 0)
	{
		ScoreRows->AddSlot().AutoHeight().Padding(10.0f)[BCStyle::Label(BCStyle::Txt(P, "ui.common.loading"), 15, BCStyle::TextMuted())];
		return;
	}
	TArray<FBCResultRow> Rows = GS->Results;
	const bool bTeams = GS->IsTeamMode();
	Rows.Sort([bTeams](const FBCResultRow& A, const FBCResultRow& B) {
		if (bTeams && A.Team != B.Team)
		{
			return A.Team < B.Team;
		}
		return A.Score > B.Score;
	});
	const ABCPlayerState* MyState = P ? P->GetPlayerState<ABCPlayerState>() : nullptr;
	const FString MyName = MyState ? MyState->GetPlayerName() : FString();

	auto Cell = [](const FString& Text, float Width, const FLinearColor& Color, bool bBold) -> TSharedRef<SWidget> {
		return SNew(SBox)
			.WidthOverride(Width)
			[
				SNew(STextBlock).Text(FText::FromString(Text)).Font(BCStyle::Font(16, bBold)).ColorAndOpacity(Color).Justification(ETextJustify::Right)
			];
	};
	int32 Index = 0;
	for (const FBCResultRow& R : Rows)
	{
		const bool bMe = !MyName.IsEmpty() && R.Name == MyName && !R.bBot;
		const FLinearColor StripeColor = bTeams ? BCStyle::TeamColor(R.Team) : (bMe ? BCStyle::Accent() : BCStyle::TextMuted());
		const FLinearColor RowColor = bMe ? BCStyle::Hex(0x3A, 0x22, 0x0C, 0.95f)
										  : ((Index % 2 == 0) ? BCStyle::Hex(0x1A, 0x1E, 0x24, 0.85f) : BCStyle::Hex(0x14, 0x17, 0x1C, 0.85f));
		FString Name = R.Name;
		if (R.bBot)
		{
			Name += TEXT("  [") + BCStyle::Txt(P, "ui.common.bot").ToString() + TEXT("]");
		}
		ScoreRows->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SBorder)
				.BorderImage(BCStyle::RoundedBrush(RowColor, 4.0f))
				.Padding(FMargin(0.0f, 7.0f, 10.0f, 7.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 0.0f, 12.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(4.0f)
						[
							SNew(SBorder).BorderImage(BCStyle::RoundedBrush(StripeColor, 2.0f)).Padding(0.0f)
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Text(FText::FromString(FString::Printf(TEXT("%d.  "), Index + 1) + Name))
						.Font(BCStyle::Font(17, true))
						.ColorAndOpacity(bMe ? BCStyle::Accent() : BCStyle::TextPrimary())
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						Cell(bTeams ? BCStyle::TeamName(P, R.Team).ToString() : FString(TEXT("-")), 90.0f, StripeColor, true)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell(BCStyle::Num(R.Score), 90.0f, BCStyle::TextPrimary(), true)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell(FString::FromInt(R.Takedowns), 70.0f, BCStyle::TextPrimary(), false)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell(FString::FromInt(R.Assists), 70.0f, BCStyle::TextSecondary(), false)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell(FString::FromInt(R.Wrecked), 70.0f, BCStyle::TextSecondary(), false)]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell(BCStyle::Num(R.Damage), 90.0f, BCStyle::TextSecondary(), false)]
				]
			];
		++Index;
	}
}

TSharedRef<SWidget> BCUI::MakeResults(ABCPlayerController* PC)
{
	return SNew(SBCResultsScreen).Owner(PC);
}
