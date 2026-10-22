// Copyright BigCar Team. All Rights Reserved.
//
// Career: chapter list (left), level grid of the chosen chapter (middle), level briefing with objectives, star
// challenges, intel and rewards (right). Start a level or free-roam the chapter hub.

#include "Core/BCDataSubsystem.h"
#include "Core/BCProfileSubsystem.h"
#include "Core/BigCarTypes.h"
#include "CrashCore/Campaign.h"
#include "CrashCore/Profile.h"
#include "Gameplay/BCPlayerController.h"
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

namespace BCCampaignScreenState
{
// Remembered between visits so returning from a level lands on the same chapter/level.
int32 LastChapter = INDEX_NONE;
int32 LastLevel = INDEX_NONE;
} // namespace BCCampaignScreenState

class SBCCampaignScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBCCampaignScreen) : _Owner(nullptr) {}
		SLATE_ARGUMENT(ABCPlayerController*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SBCCampaignScreen() override;

private:
	const crash::GameDatabase* GetDb() const;
	const crash::Profile* GetLocalProfile() const;
	const crash::ChapterDef* GetChapterDef() const;
	const crash::LevelDef* GetLevelDef() const;
	const crash::LevelProgress* GetProgress(const std::string& LevelId) const;

	void PickInitialSelection();
	void SelectChapter(int32 Index);
	void SelectLevel(int32 Index);
	void HandleProfileChanged();

	void RebuildChapters();
	void RebuildLevels();
	void RebuildDetail();
	TSharedRef<SWidget> MakeChapterCard(int32 Index);
	TSharedRef<SWidget> MakeLevelTile(int32 Index);
	TSharedRef<SWidget> InfoRow(const FText& Name, const FText& Value, const FLinearColor& ValueColor);

	TWeakObjectPtr<ABCPlayerController> PC;
	TWeakObjectPtr<UBCProfileSubsystem> ProfileSub;
	FDelegateHandle ProfileHandle;
	int32 ChapterIndex = 0;
	int32 LevelIndex = 0;
	TSharedPtr<SVerticalBox> ChapterBox;
	TSharedPtr<SUniformGridPanel> LevelGrid;
	TSharedPtr<SVerticalBox> DetailBox;
};

void SBCCampaignScreen::Construct(const FArguments& InArgs)
{
	PC = InArgs._Owner;
	ABCPlayerController* P = PC.Get();
	if (UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(P))
	{
		ProfileSub = Profile;
		ProfileHandle = Profile->OnChanged.AddSP(this, &SBCCampaignScreen::HandleProfileChanged);
	}
	PickInitialSelection();

	TSharedRef<SWidget> HeaderExtra =
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 24.0f, 0.0f)
		[
			SNew(STextBlock)
			.Font(BCStyle::Font(20, true))
			.ColorAndOpacity(BCStyle::GoldColor())
			.Text_Lambda([this]() -> FText {
				const crash::GameDatabase* Db = GetDb();
				const crash::Profile* Pr = GetLocalProfile();
				if (!Db || !Pr)
				{
					return FText::GetEmpty();
				}
				return BCStyle::Fmt(PC.Get(), "ui.campaign.total_stars", {FString::FromInt(crash::TotalStars(*Pr)), FString::FromInt(crash::MaxStars(*Db))});
			})
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock)
			.Font(BCStyle::Font(18, true))
			.ColorAndOpacity(BCStyle::CreditsColor())
			.Text_Lambda([this]() -> FText {
				const crash::GameDatabase* Db = GetDb();
				const crash::Profile* Pr = GetLocalProfile();
				if (!Db || !Pr)
				{
					return FText::GetEmpty();
				}
				const crash::ExplorationStats Stats = crash::ComputeExploration(*Db, *Pr);
				return BCStyle::Fmt(PC.Get(), "ui.campaign.exploration", {FString::FromInt(FMath::RoundToInt(Stats.percent))});
			})
		];

	TSharedRef<SWidget> Body =
		SNew(SHorizontalBox)
		// Chapters.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(380.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					BCStyle::SectionHeader(BCStyle::Txt(P, "ui.campaign.chapters"))
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(ChapterBox, SVerticalBox)
					]
				]
			]
		]
		// Levels.
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.Padding(24.0f, 0.0f)
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Font(BCStyle::FontOutlined(30, true, 1))
				.ColorAndOpacity(BCStyle::TextPrimary())
				.Text_Lambda([this]() -> FText {
					const crash::ChapterDef* Ch = GetChapterDef();
					return Ch ? BCStyle::TxtStd(PC.Get(), Ch->name) : FText::GetEmpty();
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 4.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Font(BCStyle::Font(15))
				.ColorAndOpacity(BCStyle::TextSecondary())
				.AutoWrapText(true)
				.Text_Lambda([this]() -> FText {
					const crash::ChapterDef* Ch = GetChapterDef();
					return Ch ? BCStyle::TxtStd(PC.Get(), Ch->description) : FText::GetEmpty();
				})
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(0.0f, 0.0f, 16.0f, 0.0f)
				[
					SNew(STextBlock)
					.Font(BCStyle::Font(14, true))
					.ColorAndOpacity(BCStyle::CreditsColor())
					.Text_Lambda([this]() -> FText {
						const crash::GameDatabase* Db = GetDb();
						const crash::Profile* Pr = GetLocalProfile();
						const crash::ChapterDef* Ch = GetChapterDef();
						if (!Db || !Pr || !Ch)
						{
							return FText::GetEmpty();
						}
						const crash::ExplorationStats S = crash::ComputeExploration(*Db, *Pr, Ch->id);
						return BCStyle::Fmt(PC.Get(), "ui.campaign.chapter_exploration",
											{FString::FromInt(FMath::RoundToInt(S.percent)), FString::FromInt(S.collectiblesFound), FString::FromInt(S.collectiblesTotal),
											 FString::FromInt(S.secretsFound), FString::FromInt(S.secretsTotal)});
					})
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 14.0f, 0.0f, 10.0f)
			[
				BCStyle::SectionHeader(BCStyle::Txt(P, "ui.campaign.levels"))
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(LevelGrid, SUniformGridPanel).SlotPadding(FMargin(6.0f)).MinDesiredSlotWidth(220.0f)
				]
			]
		]
		// Briefing.
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(SBox)
			.WidthOverride(460.0f)
			[
				BCStyle::Panel(
					SNew(SScrollBox)
					+ SScrollBox::Slot()
					[
						SAssignNew(DetailBox, SVerticalBox)
					],
					FMargin(20.0f, 16.0f))
			]
		];

	ChildSlot
	[
		BCStyle::ScreenFrame(P, BCStyle::Txt(P, "ui.campaign.title"), Body, 0.8f, HeaderExtra)
	];

	RebuildChapters();
	RebuildLevels();
	RebuildDetail();
}

SBCCampaignScreen::~SBCCampaignScreen()
{
	if (UBCProfileSubsystem* Profile = ProfileSub.Get())
	{
		Profile->OnChanged.Remove(ProfileHandle);
	}
}

const crash::GameDatabase* SBCCampaignScreen::GetDb() const
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
	return Data ? &Data->Db() : nullptr;
}

const crash::Profile* SBCCampaignScreen::GetLocalProfile() const
{
	const UBCProfileSubsystem* Profile = ProfileSub.Get();
	return Profile ? &Profile->Local() : nullptr;
}

const crash::ChapterDef* SBCCampaignScreen::GetChapterDef() const
{
	const crash::GameDatabase* Db = GetDb();
	if (!Db)
	{
		return nullptr;
	}
	const std::vector<crash::ChapterDef>& Chapters = Db->Campaign().chapters;
	return ChapterIndex >= 0 && ChapterIndex < static_cast<int32>(Chapters.size()) ? &Chapters[static_cast<size_t>(ChapterIndex)] : nullptr;
}

const crash::LevelDef* SBCCampaignScreen::GetLevelDef() const
{
	const crash::ChapterDef* Ch = GetChapterDef();
	if (!Ch)
	{
		return nullptr;
	}
	return LevelIndex >= 0 && LevelIndex < static_cast<int32>(Ch->levels.size()) ? &Ch->levels[static_cast<size_t>(LevelIndex)] : nullptr;
}

const crash::LevelProgress* SBCCampaignScreen::GetProgress(const std::string& LevelId) const
{
	const crash::Profile* Pr = GetLocalProfile();
	if (!Pr)
	{
		return nullptr;
	}
	const auto It = Pr->campaign.levels.find(LevelId);
	return It != Pr->campaign.levels.end() ? &It->second : nullptr;
}

void SBCCampaignScreen::PickInitialSelection()
{
	const crash::GameDatabase* Db = GetDb();
	const crash::Profile* Pr = GetLocalProfile();
	ChapterIndex = 0;
	LevelIndex = 0;
	if (!Db || !Pr || Db->Campaign().chapters.empty())
	{
		return;
	}
	const std::vector<crash::ChapterDef>& Chapters = Db->Campaign().chapters;
	const int32 NumChapters = static_cast<int32>(Chapters.size());
	if (BCCampaignScreenState::LastChapter >= 0 && BCCampaignScreenState::LastChapter < NumChapters)
	{
		ChapterIndex = BCCampaignScreenState::LastChapter;
		const int32 NumLevels = static_cast<int32>(Chapters[static_cast<size_t>(ChapterIndex)].levels.size());
		LevelIndex = FMath::Clamp(BCCampaignScreenState::LastLevel, 0, FMath::Max(0, NumLevels - 1));
		return;
	}
	// Latest unlocked chapter, first unlocked level that is not completed yet.
	for (int32 c = 0; c < NumChapters; ++c)
	{
		if (crash::IsChapterUnlocked(*Db, *Pr, Chapters[static_cast<size_t>(c)]))
		{
			ChapterIndex = c;
		}
	}
	const crash::ChapterDef& Ch = Chapters[static_cast<size_t>(ChapterIndex)];
	for (int32 l = 0; l < static_cast<int32>(Ch.levels.size()); ++l)
	{
		const crash::LevelDef& Lv = Ch.levels[static_cast<size_t>(l)];
		const crash::LevelProgress* Progress = GetProgress(Lv.id);
		if (crash::IsLevelUnlocked(*Db, *Pr, Lv))
		{
			LevelIndex = l;
			if (!Progress || !Progress->completed)
			{
				break;
			}
		}
	}
}

void SBCCampaignScreen::SelectChapter(int32 Index)
{
	const crash::GameDatabase* Db = GetDb();
	const crash::Profile* Pr = GetLocalProfile();
	ChapterIndex = Index;
	LevelIndex = 0;
	if (const crash::ChapterDef* Ch = GetChapterDef(); Ch && Db && Pr)
	{
		for (int32 l = 0; l < static_cast<int32>(Ch->levels.size()); ++l)
		{
			const crash::LevelDef& Lv = Ch->levels[static_cast<size_t>(l)];
			if (!crash::IsLevelUnlocked(*Db, *Pr, Lv))
			{
				break;
			}
			LevelIndex = l;
			const crash::LevelProgress* Progress = GetProgress(Lv.id);
			if (!Progress || !Progress->completed)
			{
				break;
			}
		}
	}
	BCCampaignScreenState::LastChapter = ChapterIndex;
	BCCampaignScreenState::LastLevel = LevelIndex;
	RebuildLevels();
	RebuildDetail();
}

void SBCCampaignScreen::SelectLevel(int32 Index)
{
	LevelIndex = Index;
	BCCampaignScreenState::LastChapter = ChapterIndex;
	BCCampaignScreenState::LastLevel = LevelIndex;
	RebuildDetail();
}

void SBCCampaignScreen::HandleProfileChanged()
{
	RebuildChapters();
	RebuildLevels();
	RebuildDetail();
}

void SBCCampaignScreen::RebuildChapters()
{
	if (!ChapterBox.IsValid())
	{
		return;
	}
	ChapterBox->ClearChildren();
	const crash::GameDatabase* Db = GetDb();
	if (!Db)
	{
		return;
	}
	for (int32 i = 0; i < static_cast<int32>(Db->Campaign().chapters.size()); ++i)
	{
		ChapterBox->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 8.0f)[MakeChapterCard(i)];
	}
}

TSharedRef<SWidget> SBCCampaignScreen::MakeChapterCard(int32 Index)
{
	ABCPlayerController* P = PC.Get();
	const crash::GameDatabase* Db = GetDb();
	const crash::Profile* Pr = GetLocalProfile();
	if (!Db || !Pr)
	{
		return SNew(SSpacer);
	}
	const crash::ChapterDef& Ch = Db->Campaign().chapters[static_cast<size_t>(Index)];
	const bool bUnlocked = crash::IsChapterUnlocked(*Db, *Pr, Ch);
	int32 Stars = 0;
	for (const crash::LevelDef& Lv : Ch.levels)
	{
		if (const crash::LevelProgress* Progress = GetProgress(Lv.id))
		{
			Stars += Progress->StarCount();
		}
	}
	const int32 MaxChapterStars = static_cast<int32>(Ch.levels.size()) * 3;
	const crash::ExplorationStats Explore = crash::ComputeExploration(*Db, *Pr, Ch.id);
	const FLinearColor ChapterColor = BCStyle::FromCrash(Ch.color);
	const TSharedRef<SWidget> LockBadge =
		bUnlocked ? SNullWidget::NullWidget : BCStyle::Badge(BCStyle::Txt(P, "ui.common.locked"), BCStyle::TextMuted(), 11, false);

	TSharedRef<SWidget> Content =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(BCStyle::Fmt(P, "ui.campaign.chapter_n", {FString::FromInt(Index + 1)}))
				.Font(BCStyle::Font(13, true))
				.ColorAndOpacity(ChapterColor)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				LockBadge
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(BCStyle::TxtStd(P, Ch.name))
			.Font(BCStyle::Font(20, true))
			.ColorAndOpacity(bUnlocked ? BCStyle::TextPrimary() : BCStyle::TextMuted())
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 16.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(FString::Printf(TEXT("\u2605 %d/%d"), Stars, MaxChapterStars)))
				.Font(BCStyle::Font(14, true))
				.ColorAndOpacity(BCStyle::GoldColor())
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(BCStyle::Fmt(P, "ui.campaign.exploration", {FString::FromInt(FMath::RoundToInt(Explore.percent))}))
				.Font(BCStyle::Font(14))
				.ColorAndOpacity(BCStyle::CreditsColor())
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Visibility(bUnlocked ? EVisibility::Collapsed : EVisibility::Visible)
			.Text(BCStyle::Fmt(P, "ui.campaign.requires_stars", {FString::FromInt(Ch.unlockStars)}))
			.Font(BCStyle::Font(13))
			.ColorAndOpacity(BCStyle::Warning())
		];

	return BCStyle::SelectableCard(
		Content, [this, Index]() { return ChapterIndex == Index; }, [this, Index]() { SelectChapter(Index); }, ChapterColor,
		FMargin(14.0f, 10.0f));
}

void SBCCampaignScreen::RebuildLevels()
{
	if (!LevelGrid.IsValid())
	{
		return;
	}
	LevelGrid->ClearChildren();
	const crash::ChapterDef* Ch = GetChapterDef();
	if (!Ch)
	{
		return;
	}
	constexpr int32 Columns = 4;
	for (int32 i = 0; i < static_cast<int32>(Ch->levels.size()); ++i)
	{
		LevelGrid->AddSlot(i % Columns, i / Columns)[MakeLevelTile(i)];
	}
}

TSharedRef<SWidget> SBCCampaignScreen::MakeLevelTile(int32 Index)
{
	ABCPlayerController* P = PC.Get();
	const crash::GameDatabase* Db = GetDb();
	const crash::Profile* Pr = GetLocalProfile();
	const crash::ChapterDef* Ch = GetChapterDef();
	if (!Db || !Pr || !Ch)
	{
		return SNew(SSpacer);
	}
	const crash::LevelDef& Lv = Ch->levels[static_cast<size_t>(Index)];
	const bool bUnlocked = crash::IsLevelUnlocked(*Db, *Pr, Lv);
	const crash::LevelProgress* Progress = GetProgress(Lv.id);
	const std::array<bool, 3> StarFlags = Progress ? Progress->stars : std::array<bool, 3>{};
	const int32 Found = Progress ? Progress->CollectiblesFound() : 0;
	const crash::ModeDef* Mode = Db->FindMode(Lv.mode);
	const crash::ArenaDef* Arena = Db->FindArena(Lv.arena);
	const bool bCompleted = Progress && Progress->completed;
	const FLinearColor ChapterColor = BCStyle::FromCrash(Ch->color);

	TSharedRef<SWidget> Content =
		SNew(SBorder)
		.BorderImage(BCStyle::SolidBrush(FLinearColor::Transparent))
		.Padding(0.0f)
		.ColorAndOpacity(bUnlocked ? FLinearColor::White : FLinearColor(1.0f, 1.0f, 1.0f, 0.45f))
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("%02d"), Index + 1)))
					.Font(BCStyle::Font(30, true))
					.ColorAndOpacity(bCompleted ? ChapterColor : BCStyle::TextMuted())
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(SSpacer)
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Top)
				[
					BCStyle::Badge(Mode ? BCStyle::TxtStd(P, Mode->name) : FText::GetEmpty(), BCStyle::TextSecondary(), 11, false)
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock).Text(BCStyle::TxtStd(P, Lv.name)).Font(BCStyle::Font(17, true)).ColorAndOpacity(BCStyle::TextPrimary())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(STextBlock)
				.Text(Arena ? BCStyle::TxtStd(P, Arena->name) : FText::GetEmpty())
				.Font(BCStyle::Font(13))
				.ColorAndOpacity(BCStyle::TextMuted())
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 8.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					BCStyle::Stars([StarFlags](int32 S) { return S >= 0 && S < 3 && StarFlags[static_cast<size_t>(S)]; }, 3, 20)
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
					.Visibility(Lv.collectibles > 0 ? EVisibility::Visible : EVisibility::Collapsed)
					.Text(BCStyle::Fmt(P, "ui.campaign.collectibles", {FString::Printf(TEXT("%d/%d"), Found, Lv.collectibles)}))
					.Font(BCStyle::Font(13))
					.ColorAndOpacity(Found >= Lv.collectibles ? BCStyle::GoldColor() : BCStyle::TextSecondary())
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			.Padding(0.0f, 6.0f, 0.0f, 0.0f)
			[
				SNew(STextBlock)
				.Visibility(bUnlocked ? EVisibility::Collapsed : EVisibility::Visible)
				.Text(Lv.unlockStars > 0 ? BCStyle::Fmt(P, "ui.campaign.requires_stars", {FString::FromInt(Lv.unlockStars)})
										 : BCStyle::Txt(P, "ui.campaign.level_locked"))
				.Font(BCStyle::Font(13, true))
				.ColorAndOpacity(BCStyle::Warning())
			]
		];

	return BCStyle::SelectableCard(
		Content, [this, Index]() { return LevelIndex == Index; }, [this, Index]() { SelectLevel(Index); },
		bCompleted ? ChapterColor : BCStyle::Hex(0x3A, 0x40, 0x49), FMargin(12.0f, 10.0f));
}

TSharedRef<SWidget> SBCCampaignScreen::InfoRow(const FText& Name, const FText& Value, const FLinearColor& ValueColor)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		[
			SNew(STextBlock).Text(Name).Font(BCStyle::Font(15)).ColorAndOpacity(BCStyle::TextSecondary())
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		[
			SNew(STextBlock).Text(Value).Font(BCStyle::Font(15, true)).ColorAndOpacity(ValueColor)
		];
}

void SBCCampaignScreen::RebuildDetail()
{
	if (!DetailBox.IsValid())
	{
		return;
	}
	DetailBox->ClearChildren();
	ABCPlayerController* P = PC.Get();
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(P);
	const crash::GameDatabase* Db = GetDb();
	const crash::Profile* Pr = GetLocalProfile();
	const crash::ChapterDef* Ch = GetChapterDef();
	const crash::LevelDef* Lv = GetLevelDef();
	if (!Data || !Db || !Pr || !Ch || !Lv)
	{
		DetailBox->AddSlot().AutoHeight()[BCStyle::Label(BCStyle::Txt(P, "ui.campaign.no_levels"), 16, BCStyle::TextMuted())];
		return;
	}
	const bool bUnlocked = crash::IsLevelUnlocked(*Db, *Pr, *Lv);
	const bool bChapterUnlocked = crash::IsChapterUnlocked(*Db, *Pr, *Ch);
	const crash::LevelProgress* Progress = GetProgress(Lv->id);
	const bool bCompleted = Progress && Progress->completed;
	const crash::ModeDef* Mode = Db->FindMode(Lv->mode);
	const crash::ArenaDef* Arena = Db->FindArena(Lv->arena);

	auto AddRow = [this](const TSharedRef<SWidget>& Widget, float Top) {
		DetailBox->AddSlot().AutoHeight().Padding(0.0f, Top, 0.0f, 0.0f)[Widget];
	};

	// ---- Title.
	AddRow(SNew(STextBlock).Text(BCStyle::TxtStd(P, Lv->name)).Font(BCStyle::FontOutlined(28, true, 1)).ColorAndOpacity(BCStyle::TextPrimary()).AutoWrapText(true),
		   0.0f);
	AddRow(SNew(STextBlock)
			   .Text(FText::FromString((Mode ? BCStyle::TxtStd(P, Mode->name).ToString() : FString()) + TEXT("  \u00B7  ") +
									   (Arena ? BCStyle::TxtStd(P, Arena->name).ToString() : FString())))
			   .Font(BCStyle::Font(15, true))
			   .ColorAndOpacity(BCStyle::FromCrash(Ch->color)),
		   2.0f);
	AddRow(BCStyle::Label(BCStyle::TxtStd(P, Lv->description), 15, BCStyle::TextSecondary(), false, true), 8.0f);

	// ---- Objectives.
	AddRow(BCStyle::SectionHeader(BCStyle::Txt(P, "ui.campaign.objective")), 18.0f);
	AddRow(SNew(SHorizontalBox)
			   + SHorizontalBox::Slot()
			   .AutoWidth()
			   .VAlign(VAlign_Center)
			   .Padding(0.0f, 0.0f, 8.0f, 0.0f)
			   [
				   SNew(STextBlock).Text(FText::FromString(TEXT("\u25C6"))).Font(BCStyle::Font(14)).ColorAndOpacity(BCStyle::Accent())
			   ]
			   + SHorizontalBox::Slot()
			   .FillWidth(1.0f)
			   .VAlign(VAlign_Center)
			   [
				   BCStyle::Label(BCStyle::ObjectiveText(P, Lv->objective), 17, BCStyle::TextPrimary(), true, true)
			   ],
		   8.0f);

	AddRow(BCStyle::SectionHeader(BCStyle::Txt(P, "ui.campaign.star_challenges")), 18.0f);
	for (int32 s = 0; s < 3; ++s)
	{
		const bool bEarned = Progress && Progress->stars[static_cast<size_t>(s)];
		AddRow(SNew(SHorizontalBox)
				   + SHorizontalBox::Slot()
				   .AutoWidth()
				   .VAlign(VAlign_Center)
				   .Padding(0.0f, 0.0f, 10.0f, 0.0f)
				   [
					   SNew(STextBlock)
					   .Text(FText::FromString(TEXT("\u2605")))
					   .Font(BCStyle::Font(22, true))
					   .ColorAndOpacity(bEarned ? BCStyle::GoldColor() : BCStyle::Hex(0x3A, 0x3F, 0x47))
				   ]
				   + SHorizontalBox::Slot()
				   .FillWidth(1.0f)
				   .VAlign(VAlign_Center)
				   [
					   BCStyle::Label(BCStyle::ObjectiveText(P, Lv->stars[static_cast<size_t>(s)]), 15,
									  bEarned ? BCStyle::TextPrimary() : BCStyle::TextSecondary(), false, true)
				   ],
			   6.0f);
	}

	// ---- Intel.
	AddRow(BCStyle::SectionHeader(BCStyle::Txt(P, "ui.campaign.intel")), 18.0f);
	bool bBoss = false;
	for (const crash::CampaignCarDef& Car : Lv->opponents)
	{
		bBoss |= Car.boss;
	}
	AddRow(InfoRow(BCStyle::Txt(P, "ui.campaign.opponents"), FText::AsNumber(static_cast<int32>(Lv->opponents.size())), BCStyle::Danger()), 8.0f);
	if (!Lv->allies.empty())
	{
		AddRow(InfoRow(BCStyle::Txt(P, "ui.campaign.allies"), FText::AsNumber(static_cast<int32>(Lv->allies.size())), BCStyle::TeamColor(0)), 4.0f);
	}
	const float TimeLimit = Lv->timeLimit > 0.0f ? Lv->timeLimit : (Mode ? Mode->timeLimit : 0.0f);
	if (TimeLimit > 0.0f)
	{
		AddRow(InfoRow(BCStyle::Txt(P, "ui.campaign.time_limit"), FText::FromString(BCStyle::Clock(TimeLimit)), BCStyle::TextPrimary()), 4.0f);
	}
	if (!Lv->loaner.empty())
	{
		const crash::VehicleDef* Loaner = Db->FindVehicle(Lv->loaner);
		AddRow(InfoRow(BCStyle::Txt(P, "ui.campaign.loaner"), Loaner ? Data->VehicleName(*Loaner) : FText::FromString(BC::ToFString(Lv->loaner)),
					   BCStyle::Accent()),
			   4.0f);
	}
	else
	{
		AddRow(InfoRow(BCStyle::Txt(P, "ui.campaign.max_tier"),
					   Lv->maxTier > 0 ? BCStyle::TierText(Lv->maxTier) : BCStyle::Txt(P, "ui.campaign.any_tier"), BCStyle::TextPrimary()),
			   4.0f);
	}
	if (bBoss)
	{
		AddRow(BCStyle::Badge(BCStyle::Txt(P, "ui.campaign.boss_warning"), BCStyle::Danger(), 13), 8.0f);
	}
	// Selected car too strong for this level -> the game picks a legal owned car automatically.
	if (Lv->loaner.empty() && Lv->maxTier > 0)
	{
		const crash::VehicleBuild* Selected = Pr->SelectedBuild();
		const crash::VehicleDef* SelectedDef = Selected ? Db->FindVehicle(Selected->vehicleId) : nullptr;
		if (SelectedDef && SelectedDef->tier > Lv->maxTier)
		{
			AddRow(SNew(SBorder)
					   .BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x3A, 0x2A, 0x08, 0.9f), 6.0f, BCStyle::Warning(), 1.0f))
					   .Padding(FMargin(12.0f, 8.0f))
					   [
						   BCStyle::Label(BCStyle::Fmt(P, "ui.campaign.tier_warning",
													   {Data->VehicleName(*SelectedDef).ToString(), FString::FromInt(SelectedDef->tier),
														FString::FromInt(Lv->maxTier)}),
										  14, BCStyle::Warning(), false, true)
					   ],
				   10.0f);
		}
	}

	// ---- Rewards.
	AddRow(BCStyle::SectionHeader(BCStyle::Txt(P, bCompleted ? "ui.campaign.rewards_replay" : "ui.campaign.rewards")), 18.0f);
	const int64 Credits = bCompleted ? Lv->rewards.credits / 4 : Lv->rewards.credits;
	const int64 Xp = bCompleted ? Lv->rewards.xp / 2 : Lv->rewards.xp;
	AddRow(SNew(SHorizontalBox)
			   + SHorizontalBox::Slot()
			   .AutoWidth()
			   .Padding(0.0f, 0.0f, 20.0f, 0.0f)
			   [
				   SNew(STextBlock)
				   .Text(BCStyle::CurrencyText(P, crash::Currency::Credits, Credits))
				   .Font(BCStyle::Font(17, true))
				   .ColorAndOpacity(BCStyle::CreditsColor())
			   ]
			   + SHorizontalBox::Slot()
			   .AutoWidth()
			   [
				   SNew(STextBlock)
				   .Text(BCStyle::Fmt(P, "ui.common.xp_amount", {BCStyle::Num(Xp)}))
				   .Font(BCStyle::Font(17, true))
				   .ColorAndOpacity(BCStyle::XpColor())
			   ],
		   8.0f);
	if (!Lv->rewards.items.empty())
	{
		AddRow(BCStyle::Label(BCStyle::Txt(P, bCompleted ? "ui.campaign.first_clear_claimed" : "ui.campaign.first_clear"), 13,
							  bCompleted ? BCStyle::TextMuted() : BCStyle::Accent(), true),
			   8.0f);
		for (const crash::ItemRef& Item : Lv->rewards.items)
		{
			AddRow(SNew(SHorizontalBox)
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
						   BCStyle::Label(Data->ItemName(Item), 15, bCompleted ? BCStyle::TextMuted() : BCStyle::TextPrimary(), true)
					   ],
				   4.0f);
		}
	}
	if (bCompleted && Progress)
	{
		AddRow(InfoRow(BCStyle::Txt(P, "ui.campaign.best_score"), FText::FromString(BCStyle::Num(Progress->bestScore)), BCStyle::TextPrimary()), 10.0f);
		if (Progress->bestTime > 0.0f)
		{
			AddRow(InfoRow(BCStyle::Txt(P, "ui.campaign.best_time"), FText::FromString(BCStyle::Clock(Progress->bestTime)), BCStyle::TextPrimary()),
				   4.0f);
		}
	}

	// ---- Actions.
	TWeakObjectPtr<ABCPlayerController> WeakPC = PC;
	const FString LevelId = BC::ToFString(Lv->id);
	const FString ChapterId = BC::ToFString(Ch->id);
	AddRow(BCStyle::PrimaryButton(BCStyle::Txt(P, bUnlocked ? "ui.campaign.start" : "ui.campaign.locked_button"),
								  [WeakPC, LevelId]() {
									  if (ABCPlayerController* Owner = WeakPC.Get())
									  {
										  Owner->StartCampaignLevel(LevelId);
									  }
								  },
								  bUnlocked, 0.0f, 22),
		   22.0f);
	if (!Ch->hub.empty())
	{
		AddRow(BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.campaign.explore"),
										[WeakPC, ChapterId]() {
											if (ABCPlayerController* Owner = WeakPC.Get())
											{
												Owner->StartHub(ChapterId);
											}
										},
										bChapterUnlocked),
			   8.0f);
		AddRow(BCStyle::Label(BCStyle::Txt(P, "ui.campaign.explore_hint"), 13, BCStyle::TextMuted(), false, true), 4.0f);
	}
}

TSharedRef<SWidget> BCUI::MakeCampaign(ABCPlayerController* PC)
{
	return SNew(SBCCampaignScreen).Owner(PC);
}
