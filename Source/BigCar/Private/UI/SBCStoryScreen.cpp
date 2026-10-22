// Copyright BigCar Team. All Rights Reserved.
//
// Visual-novel style campaign dialogue: letterbox bars, speaker + typewriter line in a bottom panel. Click, Enter,
// Space or gamepad A advances (first press completes the line), Esc / skip button ends the scene.

#include "Core/BCDataSubsystem.h"
#include "Core/BigCarTypes.h"
#include "Framework/Application/SlateApplication.h"
#include "Gameplay/BCPlayerController.h"
#include "Input/Events.h"
#include "InputCoreTypes.h"
#include "UI/BCUI.h"
#include "UI/BCUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

class SBCStoryScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBCStoryScreen) : _Owner(nullptr) {}
		SLATE_ARGUMENT(ABCPlayerController*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;
	virtual bool SupportsKeyboardFocus() const override { return true; }
	virtual FReply OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent) override;
	virtual FReply OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;

private:
	void LoadLine();
	int32 VisibleChars() const;
	bool IsLineComplete() const;
	void Advance();
	void Finish();

	static constexpr float CharsPerSecond = 38.0f;

	TWeakObjectPtr<ABCPlayerController> PC;
	TArray<FString> Lines;
	int32 Index = 0;
	FText Speaker;
	FString LineText;
	double LineStart = 0.0;
	bool bFinished = false;
	int32 FocusAttempts = 0;
};

void SBCStoryScreen::Construct(const FArguments& InArgs)
{
	PC = InArgs._Owner;
	ABCPlayerController* P = PC.Get();
	if (P)
	{
		Lines = P->StoryLines();
	}
	LoadLine();

	ChildSlot
	[
		SNew(SOverlay)
		// Letterbox.
		+ SOverlay::Slot()
		.VAlign(VAlign_Top)
		[
			SNew(SBox)
			.HeightOverride(86.0f)
			[
				SNew(SBorder).BorderImage(BCStyle::SolidBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.92f))).Padding(0.0f)
			]
		]
		+ SOverlay::Slot()
		.VAlign(VAlign_Bottom)
		[
			SNew(SBox)
			.HeightOverride(290.0f)
			[
				SNew(SBorder)
				.BorderImage(BCStyle::SolidBrush(FLinearColor(0.0f, 0.0f, 0.0f, 0.88f)))
				.Padding(FMargin(140.0f, 26.0f, 140.0f, 22.0f))
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						.Visibility_Lambda([this]() -> EVisibility { return Speaker.IsEmpty() ? EVisibility::Hidden : EVisibility::Visible; })
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						.Padding(0.0f, 0.0f, 12.0f, 0.0f)
						[
							SNew(SBox)
							.WidthOverride(6.0f)
							.HeightOverride(30.0f)
							[
								SNew(SBorder).BorderImage(BCStyle::SolidBrush(BCStyle::Accent())).Padding(0.0f)
							]
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Font(BCStyle::FontOutlined(28, true, 1))
							.ColorAndOpacity(BCStyle::Accent())
							.Text_Lambda([this]() -> FText { return Speaker; })
						]
					]
					+ SVerticalBox::Slot()
					.FillHeight(1.0f)
					.Padding(0.0f, 14.0f, 0.0f, 0.0f)
					[
						SNew(STextBlock)
						.Font(BCStyle::Font(26))
						.ColorAndOpacity(BCStyle::TextPrimary())
						.AutoWrapText(true)
						.LineHeightPercentage(1.15f)
						.Text_Lambda([this]() -> FText { return FText::FromString(LineText.Left(VisibleChars())); })
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.AutoWidth()
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Font(BCStyle::Font(14, true))
							.ColorAndOpacity(BCStyle::TextMuted())
							.Text_Lambda([this]() -> FText {
								return FText::FromString(FString::Printf(TEXT("%d / %d"), FMath::Min(Index + 1, Lines.Num()), Lines.Num()));
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
							.Text(BCStyle::Txt(P, "ui.story.continue"))
							.ColorAndOpacity_Lambda([this]() -> FSlateColor {
								// Pulses once the line is fully shown.
								if (!IsLineComplete())
								{
									return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.0f));
								}
								const float Pulse = 0.55f + 0.45f * FMath::Sin(static_cast<float>(FPlatformTime::Seconds()) * 5.0f);
								return FSlateColor(BCStyle::WithAlpha(BCStyle::Accent(), Pulse));
							})
						]
					]
				]
			]
		]
		// Skip.
		+ SOverlay::Slot()
		.HAlign(HAlign_Right)
		.VAlign(VAlign_Top)
		.Padding(0.0f, 22.0f, 40.0f, 0.0f)
		[
			SNew(SButton)
			.ButtonStyle(BCStyle::GhostButtonStyle())
			.IsFocusable(false)
			.ContentPadding(FMargin(16.0f, 8.0f))
			.OnClicked_Lambda([this]() -> FReply {
				Finish();
				return FReply::Handled();
			})
			[
				SNew(STextBlock).Text(BCStyle::Txt(P, "ui.story.skip")).Font(BCStyle::Font(18, true)).ColorAndOpacity(BCStyle::TextSecondary())
			]
		]
	];
}

void SBCStoryScreen::LoadLine()
{
	Speaker = FText::GetEmpty();
	LineText.Reset();
	LineStart = FPlatformTime::Seconds();
	if (!Lines.IsValidIndex(Index))
	{
		return;
	}
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
	if (!Data)
	{
		LineText = Lines[Index];
		return;
	}
	FText Line;
	Data->Dialogue(BC::ToStd(Lines[Index]), Speaker, Line);
	LineText = Line.ToString();
}

int32 SBCStoryScreen::VisibleChars() const
{
	const double Elapsed = FPlatformTime::Seconds() - LineStart;
	return FMath::Clamp(static_cast<int32>(Elapsed * CharsPerSecond), 0, LineText.Len());
}

bool SBCStoryScreen::IsLineComplete() const
{
	return VisibleChars() >= LineText.Len();
}

void SBCStoryScreen::Advance()
{
	if (bFinished)
	{
		return;
	}
	if (!IsLineComplete())
	{
		// First press reveals the rest of the line.
		LineStart = FPlatformTime::Seconds() - 1000.0;
		return;
	}
	++Index;
	if (Index >= Lines.Num())
	{
		Finish();
		return;
	}
	LoadLine();
}

void SBCStoryScreen::Finish()
{
	if (bFinished)
	{
		return;
	}
	bFinished = true;
	if (ABCPlayerController* P = PC.Get())
	{
		P->FinishStory();
	}
}

void SBCStoryScreen::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bFinished)
	{
		return;
	}
	if (Lines.Num() == 0)
	{
		Finish();
		return;
	}
	// Keep keyboard focus so Enter / Space / gamepad A reach OnKeyDown (retry for a while after opening).
	if (!HasKeyboardFocus() && FocusAttempts < 600 && FSlateApplication::IsInitialized())
	{
		++FocusAttempts;
		FSlateApplication::Get().SetKeyboardFocus(SharedThis(this), EFocusCause::SetDirectly);
	}
}

FReply SBCStoryScreen::OnKeyDown(const FGeometry& MyGeometry, const FKeyEvent& InKeyEvent)
{
	const FKey Key = InKeyEvent.GetKey();
	if (Key == EKeys::Enter || Key == EKeys::SpaceBar || Key == EKeys::Gamepad_FaceButton_Bottom || Key == EKeys::Virtual_Accept)
	{
		Advance();
		return FReply::Handled();
	}
	if (Key == EKeys::Escape || Key == EKeys::Gamepad_Special_Right)
	{
		Finish();
		return FReply::Handled();
	}
	return SCompoundWidget::OnKeyDown(MyGeometry, InKeyEvent);
}

FReply SBCStoryScreen::OnMouseButtonDown(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	if (MouseEvent.GetEffectingButton() == EKeys::LeftMouseButton)
	{
		Advance();
		return FReply::Handled();
	}
	return SCompoundWidget::OnMouseButtonDown(MyGeometry, MouseEvent);
}

TSharedRef<SWidget> BCUI::MakeStory(ABCPlayerController* PC)
{
	return SNew(SBCStoryScreen).Owner(PC);
}
