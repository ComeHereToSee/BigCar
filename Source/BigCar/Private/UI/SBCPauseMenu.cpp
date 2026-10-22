// Copyright BigCar Team. All Rights Reserved.
//
// In-game pause menu: resume, settings, restart (offline only), leave match / leave free-roam.

#include "Core/BCDataSubsystem.h"
#include "Core/BigCarTypes.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "Gameplay/BCGameState.h"
#include "Gameplay/BCPlayerController.h"
#include "UI/BCUI.h"
#include "UI/BCUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

class SBCPauseMenu : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBCPauseMenu) : _Owner(nullptr) {}
		SLATE_ARGUMENT(ABCPlayerController*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	const ABCGameState* GetGS() const;
	bool IsHub() const;
	bool IsOffline() const;
	FText MatchInfo() const;

	TWeakObjectPtr<ABCPlayerController> PC;
	TSharedPtr<SButton> ResumeButton;
	bool bFocusDone = false;
};

void SBCPauseMenu::Construct(const FArguments& InArgs)
{
	PC = InArgs._Owner;
	ABCPlayerController* P = PC.Get();
	TWeakObjectPtr<ABCPlayerController> WeakPC = PC;

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(BCStyle::SolidBrush(BCStyle::Hex(0x03, 0x04, 0x06, 0.66f))).Padding(0.0f)
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(480.0f)
			[
				BCStyle::Panel(
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						BCStyle::BigTitle(BCStyle::Txt(P, "ui.pause.title"), 52)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 2.0f, 0.0f, 18.0f)
					[
						SNew(STextBlock)
						.Font(BCStyle::Font(16, true))
						.ColorAndOpacity(BCStyle::TextSecondary())
						.Justification(ETextJustify::Center)
						.Text_Lambda([this]() -> FText { return MatchInfo(); })
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SAssignNew(ResumeButton, SButton)
						.ButtonStyle(BCStyle::PrimaryButtonStyle())
						.HAlign(HAlign_Center)
						.ContentPadding(FMargin(18.0f, 12.0f))
						.OnClicked_Lambda([WeakPC]() -> FReply {
							if (ABCPlayerController* Owner = WeakPC.Get())
							{
								Owner->Back();
							}
							return FReply::Handled();
						})
						[
							SNew(STextBlock).Text(BCStyle::Txt(P, "ui.pause.resume")).Font(BCStyle::Font(22, true)).ColorAndOpacity(BCStyle::TextOnAccent())
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						BCStyle::SecondaryButton(BCStyle::Txt(P, "ui.pause.settings"),
												 [WeakPC]() {
													 if (ABCPlayerController* Owner = WeakPC.Get())
													 {
														 Owner->OpenScreen(EBCScreen::Settings);
													 }
												 },
												 true, 0.0f, 19)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 0.0f, 0.0f, 8.0f)
					[
						SNew(SBox)
						.Visibility_Lambda([this]() -> EVisibility { return (IsOffline() && !IsHub()) ? EVisibility::Visible : EVisibility::Collapsed; })
						[
							BCStyle::ConfirmButton(P, BCStyle::EButtonKind::Secondary, BCStyle::Txt(P, "ui.pause.restart"),
												   [WeakPC]() {
													   if (ABCPlayerController* Owner = WeakPC.Get())
													   {
														   Owner->RestartMatch();
													   }
												   },
												   true, 0.0f, 19)
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						BCStyle::ConfirmButton(P, BCStyle::EButtonKind::Danger,
											   BCStyle::Bind<FText>([this]() -> FText {
												   return BCStyle::Txt(PC.Get(), IsHub() ? "ui.pause.leave_explore" : "ui.pause.leave_match");
											   }),
											   [WeakPC]() {
												   if (ABCPlayerController* Owner = WeakPC.Get())
												   {
													   Owner->ReturnToMenu();
												   }
											   },
											   true, 0.0f, 19)
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					.Padding(0.0f, 16.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Font(BCStyle::Font(13))
						.ColorAndOpacity(BCStyle::TextMuted())
						.Text_Lambda([this]() -> FText {
							return BCStyle::Txt(PC.Get(), IsOffline() ? "ui.pause.hint" : "ui.pause.online_note");
						})
					],
					FMargin(32.0f, 26.0f))
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
}

void SBCPauseMenu::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (!bFocusDone && ResumeButton.IsValid() && FSlateApplication::IsInitialized())
	{
		bFocusDone = true;
		FSlateApplication::Get().SetKeyboardFocus(ResumeButton, EFocusCause::SetDirectly);
	}
}

const ABCGameState* SBCPauseMenu::GetGS() const
{
	const ABCPlayerController* P = PC.Get();
	const UWorld* World = P ? P->GetWorld() : nullptr;
	return World ? World->GetGameState<ABCGameState>() : nullptr;
}

bool SBCPauseMenu::IsHub() const
{
	const ABCGameState* GS = GetGS();
	return GS && GS->ModeId == TEXT("explore");
}

bool SBCPauseMenu::IsOffline() const
{
	const ABCPlayerController* P = PC.Get();
	return P && P->GetNetMode() == NM_Standalone;
}

FText SBCPauseMenu::MatchInfo() const
{
	const ABCPlayerController* P = PC.Get();
	const ABCGameState* GS = GetGS();
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(P);
	if (!GS || !Data)
	{
		return FText::GetEmpty();
	}
	const crash::GameDatabase& Db = Data->Db();
	if (const crash::LevelDef* Level = GS->LevelId.IsEmpty() ? nullptr : Db.FindLevel(BC::ToStd(GS->LevelId)))
	{
		return BCStyle::TxtStd(P, Level->name);
	}
	const crash::ModeDef* Mode = Db.FindMode(BC::ToStd(GS->ModeId));
	const crash::ArenaDef* Arena = Db.FindArena(BC::ToStd(GS->ArenaId));
	FString Info = Mode ? BCStyle::TxtStd(P, Mode->name).ToString() : FString();
	if (Arena)
	{
		Info += (Info.IsEmpty() ? FString() : FString(TEXT("  \u00B7  "))) + BCStyle::TxtStd(P, Arena->name).ToString();
	}
	return FText::FromString(Info);
}

TSharedRef<SWidget> BCUI::MakePause(ABCPlayerController* PC)
{
	return SNew(SBCPauseMenu).Owner(PC);
}
