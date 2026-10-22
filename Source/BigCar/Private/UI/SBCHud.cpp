// Copyright BigCar Team. All Rights Reserved.
//
// In-game HUD: speedometer / gear / rpm, boost and ability (bottom-right), damage silhouette + integrity
// (bottom-left), timer / mode / team scores / hill / bounty (top-center), countdown and big state messages (center),
// score popups, takedown feed (top-right), radar (top-left), campaign objective panel (left), toasts and the
// scoreboard overlay while Tab / View is held. Scaled by the HUD scale setting.

#include "Core/BCDataSubsystem.h"
#include "Core/BCSettingsSubsystem.h"
#include "Core/BigCarTypes.h"
#include "CrashCore/Abilities.h"
#include "CrashCore/Damage.h"
#include "Arena/BCArenaProps.h"
#include "Camera/PlayerCameraManager.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/BCGameState.h"
#include "Gameplay/BCPlayerController.h"
#include "Gameplay/BCPlayerState.h"
#include "Rendering/DrawElements.h"
#include "Rendering/SlateLayoutTransform.h"
#include "UI/BCUI.h"
#include "UI/BCUIStyle.h"
#include "Vehicle/BCVehicleDamage.h"
#include "Vehicle/BCVehicleMovement.h"
#include "Vehicle/BCVehiclePawn.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SDPIScaler.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace BCHudDetail
{
struct FFeedItem
{
	FBCFeedEntry Entry;
	double Time = 0.0;
};

constexpr int32 MaxFeed = 5;
constexpr double FeedLifetime = 7.0;
constexpr int32 MaxPopups = 4;
constexpr double PopupLifetime = 2.2;
constexpr float RadarSize = 210.0f;
constexpr float RadarRangeCm = 9000.0f;

ABCGameState* GameStateOf(const ABCPlayerController* PC)
{
	const UWorld* World = PC ? PC->GetWorld() : nullptr;
	return World ? World->GetGameState<ABCGameState>() : nullptr;
}

FText GearText(int32 Gear)
{
	if (Gear < 0)
	{
		return FText::FromString(TEXT("R"));
	}
	if (Gear == 0)
	{
		return FText::FromString(TEXT("N"));
	}
	return FText::AsNumber(Gear);
}
} // namespace BCHudDetail

// ---------------------------------------------------------------------------------------------
// Radar: other cars (team colored), hill zone and collectibles relative to the player, forward = up.

class SBCHudRadar : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBCHudRadar) : _Owner(nullptr) {}
		SLATE_ARGUMENT(ABCPlayerController*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs)
	{
		PC = InArgs._Owner;
		ChildSlot
		[
			SNew(SBox).WidthOverride(BCHudDetail::RadarSize).HeightOverride(BCHudDetail::RadarSize)
		];
	}

	virtual int32 OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
						  FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle,
						  bool bParentEnabled) const override;

private:
	void DrawBox(FSlateWindowElementList& OutDrawElements, int32 Layer, const FGeometry& Geometry, const FVector2D& Center, float Size,
				 const FSlateBrush* Brush, const FLinearColor& Color) const
	{
		FSlateDrawElement::MakeBox(OutDrawElements, Layer,
								   Geometry.ToPaintGeometry(FVector2D(Size, Size), FSlateLayoutTransform(Center - FVector2D(Size * 0.5f, Size * 0.5f))),
								   Brush, ESlateDrawEffect::None, Color);
	}

	TWeakObjectPtr<ABCPlayerController> PC;
};

int32 SBCHudRadar::OnPaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry, const FSlateRect& MyCullingRect,
						   FSlateWindowElementList& OutDrawElements, int32 LayerId, const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const FVector2D Size = AllottedGeometry.GetLocalSize();
	const float Diameter = static_cast<float>(FMath::Min(Size.X, Size.Y));
	const FVector2D Center(Diameter * 0.5f, Diameter * 0.5f);
	const float Radius = Diameter * 0.5f;
	const float Scale = (Radius - 8.0f) / BCHudDetail::RadarRangeCm;
	const FSlateBrush* Disc = BCStyle::RoundedBrush(FLinearColor::White, Diameter * 0.5f);
	const FSlateBrush* Ring = BCStyle::RoundedBrush(FLinearColor::Transparent, Diameter * 0.25f, FLinearColor::White, 1.0f);
	const FSlateBrush* Dot = BCStyle::RoundedBrush(FLinearColor::White, 6.0f);
	const FSlateBrush* Square = BCStyle::WhiteBrush();

	// Background + range ring + crosshair.
	DrawBox(OutDrawElements, LayerId, AllottedGeometry, Center, Diameter, Disc, BCStyle::Hex(0x06, 0x08, 0x0B, 0.62f));
	DrawBox(OutDrawElements, LayerId + 1, AllottedGeometry, Center, Diameter * 0.5f, Ring, FLinearColor(1.0f, 1.0f, 1.0f, 0.12f));
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
							   AllottedGeometry.ToPaintGeometry(FVector2D(1.0f, Diameter - 16.0f), FSlateLayoutTransform(FVector2D(Center.X, 8.0f))),
							   Square, ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, 0.07f));
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 1,
							   AllottedGeometry.ToPaintGeometry(FVector2D(Diameter - 16.0f, 1.0f), FSlateLayoutTransform(FVector2D(8.0f, Center.Y))),
							   Square, ESlateDrawEffect::None, FLinearColor(1.0f, 1.0f, 1.0f, 0.07f));

	const ABCPlayerController* P = PC.Get();
	const UWorld* World = P ? P->GetWorld() : nullptr;
	const ABCVehiclePawn* Me = P ? P->GetVehicle() : nullptr;
	FVector Origin = FVector::ZeroVector;
	FRotator Facing = FRotator::ZeroRotator;
	if (Me)
	{
		Origin = Me->GetActorLocation();
		Facing = Me->GetActorRotation();
	}
	else if (P && P->PlayerCameraManager)
	{
		Origin = P->PlayerCameraManager->GetCameraLocation();
		Facing = P->PlayerCameraManager->GetCameraRotation();
	}
	const FVector Forward = FRotator(0.0f, Facing.Yaw, 0.0f).Vector();
	const FVector Right(-Forward.Y, Forward.X, 0.0f);
	auto ToRadar = [&](const FVector& World3, bool& bClamped) -> FVector2D {
		const FVector Rel = World3 - Origin;
		FVector2D Local(FVector::DotProduct(Rel, Right) * Scale, -FVector::DotProduct(Rel, Forward) * Scale);
		const float MaxLen = Radius - 8.0f;
		bClamped = Local.Size() > MaxLen;
		if (bClamped)
		{
			Local = Local.GetSafeNormal() * MaxLen;
		}
		return Center + Local;
	};

	const ABCGameState* GS = BCHudDetail::GameStateOf(P);
	if (World && GS)
	{
		// Hill zone.
		if (GS->HillRadius > 0.0f)
		{
			bool bClamped = false;
			const FVector2D HillPos = ToRadar(FVector(GS->HillCenter), bClamped);
			const float HillSize = FMath::Clamp(GS->HillRadius * 2.0f * Scale, 10.0f, Diameter * 0.6f);
			FLinearColor HillColor = GS->HillOwnerTeam >= 0 ? BCStyle::TeamColor(GS->HillOwnerTeam) : FLinearColor::White;
			if (GS->bHillContested)
			{
				const float Pulse = 0.5f + 0.5f * FMath::Sin(static_cast<float>(FPlatformTime::Seconds()) * 8.0f);
				HillColor = FLinearColor::LerpUsingHSV(BCStyle::Danger(), BCStyle::Warning(), Pulse);
			}
			const FSlateBrush* HillBrush = BCStyle::RoundedBrush(FLinearColor::White, 64.0f);
			DrawBox(OutDrawElements, LayerId + 2, AllottedGeometry, HillPos, bClamped ? 10.0f : HillSize, HillBrush, BCStyle::WithAlpha(HillColor, 0.35f));
		}
	}
	if (World)
	{
		// Collectibles (campaign).
		for (TActorIterator<ABCCollectibleActor> It(const_cast<UWorld*>(World)); It; ++It)
		{
			if (It->IsHidden())
			{
				continue;
			}
			bool bClamped = false;
			const FVector2D Pos = ToRadar(It->GetActorLocation(), bClamped);
			DrawBox(OutDrawElements, LayerId + 3, AllottedGeometry, Pos, bClamped ? 5.0f : 7.0f, Square, BCStyle::WithAlpha(BCStyle::GoldColor(), bClamped ? 0.6f : 1.0f));
		}
		// Cars.
		const int32 MyTeam = Me ? Me->GetTeam() : -1;
		const bool bTeams = GS && GS->IsTeamMode();
		for (TActorIterator<ABCVehiclePawn> It(const_cast<UWorld*>(World)); It; ++It)
		{
			const ABCVehiclePawn* Car = *It;
			if (!Car || Car == Me || Car->IsDisplayOnly())
			{
				continue;
			}
			bool bClamped = false;
			const FVector2D Pos = ToRadar(Car->GetActorLocation(), bClamped);
			FLinearColor Color = bTeams ? BCStyle::TeamColor(Car->GetTeam()) : BCStyle::Danger();
			if (bTeams && Car->GetTeam() == MyTeam)
			{
				Color = BCStyle::WithAlpha(BCStyle::TeamColor(MyTeam), 0.9f);
			}
			float DotSize = bClamped ? 7.0f : 10.0f;
			if (GS && GS->BountyParticipant >= 0 && Car->GetParticipantId() == GS->BountyParticipant)
			{
				Color = BCStyle::GoldColor();
				DotSize += 4.0f;
			}
			if (Car->IsBossCar())
			{
				DotSize += 4.0f;
			}
			if (Car->IsWrecked())
			{
				Color = FLinearColor(0.35f, 0.35f, 0.35f, 0.8f);
			}
			DrawBox(OutDrawElements, LayerId + 4, AllottedGeometry, Pos, DotSize + 2.0f, Dot, FLinearColor(0.0f, 0.0f, 0.0f, 0.7f));
			DrawBox(OutDrawElements, LayerId + 5, AllottedGeometry, Pos, DotSize, Dot, Color);
		}
	}
	// The player: an accent marker with a "nose" pointing up.
	DrawBox(OutDrawElements, LayerId + 6, AllottedGeometry, Center, 12.0f, Dot, BCStyle::Accent());
	FSlateDrawElement::MakeBox(OutDrawElements, LayerId + 6,
							   AllottedGeometry.ToPaintGeometry(FVector2D(4.0f, 9.0f), FSlateLayoutTransform(Center - FVector2D(2.0f, 14.0f))),
							   Square, ESlateDrawEffect::None, BCStyle::Accent());

	return SCompoundWidget::OnPaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements, LayerId + 7, InWidgetStyle, bParentEnabled);
}

// ---------------------------------------------------------------------------------------------
// HUD

class SBCHud : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBCHud) : _Owner(nullptr) {}
		SLATE_ARGUMENT(ABCPlayerController*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SBCHud() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	ABCGameState* GetGS() const { return BCHudDetail::GameStateOf(PC.Get()); }
	ABCPlayerState* GetPS() const;
	ABCVehiclePawn* GetCar() const;
	const crash::ModeDef* GetMode() const;
	FString MyName() const;
	FLinearColor NameColor(const FString& Name, int32 Team) const;
	const BCHudDetail::FFeedItem* FeedAt(int32 Slot) const;
	const FBCPopup* PopupAt(int32 Slot) const;
	void HandleFeed(const FBCFeedEntry& Entry);
	void RebuildScoreboard();

	TSharedRef<SWidget> MakeSpeedPanel();
	TSharedRef<SWidget> MakeDamagePanel();
	TSharedRef<SWidget> MakeTopCenter();
	TSharedRef<SWidget> MakeCenterMessages();
	TSharedRef<SWidget> MakePopups();
	TSharedRef<SWidget> MakeFeed();
	TSharedRef<SWidget> MakeObjectivePanel();
	TSharedRef<SWidget> MakeScoreboard();
	TSharedRef<SWidget> ZoneBox(crash::Zone Zone, float Width, float Height);
	TSharedRef<SWidget> WheelBox(int32 Wheel);

	TWeakObjectPtr<ABCPlayerController> PC;
	TWeakObjectPtr<ABCGameState> BoundGS;
	FDelegateHandle FeedHandle;
	TArray<BCHudDetail::FFeedItem> FeedItems;
	TSharedPtr<SVerticalBox> ScoreboardRows;
	double LastScoreboardBuild = 0.0;
	EBCMatchPhase LastPhase = EBCMatchPhase::Waiting;
	double GoUntil = 0.0;
};

void SBCHud::Construct(const FArguments& InArgs)
{
	PC = InArgs._Owner;
	ABCPlayerController* P = PC.Get();
	TWeakObjectPtr<ABCPlayerController> WeakPC = PC;

	ChildSlot
	[
		SNew(SDPIScaler)
		.Visibility(EVisibility::HitTestInvisible)
		.DPIScale_Lambda([WeakPC]() -> float {
			const UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(WeakPC.Get());
			return Settings ? FMath::Clamp(Settings->Gameplay().hudScale, 0.6f, 1.5f) : 1.0f;
		})
		[
			SNew(SOverlay)
			.Visibility(EVisibility::SelfHitTestInvisible)
			// Radar + objectives (top-left).
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Top)
			.Padding(28.0f, 24.0f, 0.0f, 0.0f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SBCHudRadar).Owner(P).Visibility(EVisibility::HitTestInvisible)
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 14.0f, 0.0f, 0.0f)
				[
					MakeObjectivePanel()
				]
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Top)
			.Padding(0.0f, 18.0f, 0.0f, 0.0f)
			[
				MakeTopCenter()
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Top)
			.Padding(0.0f, 24.0f, 28.0f, 0.0f)
			[
				MakeFeed()
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 120.0f)
			[
				MakeCenterMessages()
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 360.0f)
			[
				MakePopups()
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Bottom)
			.Padding(28.0f, 0.0f, 0.0f, 28.0f)
			[
				MakeDamagePanel()
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Right)
			.VAlign(VAlign_Bottom)
			.Padding(0.0f, 0.0f, 28.0f, 28.0f)
			[
				MakeSpeedPanel()
			]
			// Toasts (menus show their own).
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Top)
			.Padding(0.0f, 150.0f, 0.0f, 0.0f)
			[
				SNew(SBox)
				.Visibility_Lambda([WeakPC]() -> EVisibility {
					const ABCPlayerController* Owner = WeakPC.Get();
					return (Owner && Owner->CurrentScreen() == EBCScreen::None) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
				[
					BCStyle::ToastLayer(P)
				]
			]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				MakeScoreboard()
			]
		]
	];
}

SBCHud::~SBCHud()
{
	if (ABCGameState* GS = BoundGS.Get())
	{
		GS->OnFeed.Remove(FeedHandle);
	}
}

void SBCHud::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	const double Now = FPlatformTime::Seconds();
	ABCGameState* GS = GetGS();
	// Bind to the takedown feed once the (replicated) game state exists.
	if (GS && BoundGS.Get() != GS)
	{
		if (ABCGameState* Old = BoundGS.Get())
		{
			Old->OnFeed.Remove(FeedHandle);
		}
		BoundGS = GS;
		FeedHandle = GS->OnFeed.AddSP(this, &SBCHud::HandleFeed);
	}
	FeedItems.RemoveAll([Now](const BCHudDetail::FFeedItem& Item) { return Now - Item.Time > BCHudDetail::FeedLifetime; });

	// "GO!" flash when the countdown ends.
	const EBCMatchPhase Phase = GS ? GS->Phase : EBCMatchPhase::Waiting;
	if (Phase == EBCMatchPhase::Playing && LastPhase == EBCMatchPhase::Countdown)
	{
		GoUntil = Now + 1.1;
	}
	LastPhase = Phase;

	const ABCPlayerController* P = PC.Get();
	if (P && P->IsScoreboardHeld() && Now - LastScoreboardBuild > 0.3)
	{
		LastScoreboardBuild = Now;
		RebuildScoreboard();
	}
}

ABCPlayerState* SBCHud::GetPS() const
{
	const ABCPlayerController* P = PC.Get();
	return P ? P->GetPlayerState<ABCPlayerState>() : nullptr;
}

ABCVehiclePawn* SBCHud::GetCar() const
{
	const ABCPlayerController* P = PC.Get();
	return P ? P->GetVehicle() : nullptr;
}

const crash::ModeDef* SBCHud::GetMode() const
{
	const ABCGameState* GS = GetGS();
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
	return (GS && Data) ? Data->Db().FindMode(BC::ToStd(GS->ModeId)) : nullptr;
}

FString SBCHud::MyName() const
{
	const ABCPlayerState* PS = GetPS();
	return PS ? PS->GetPlayerName() : FString();
}

FLinearColor SBCHud::NameColor(const FString& Name, int32 Team) const
{
	const ABCGameState* GS = GetGS();
	if (GS && GS->IsTeamMode() && Team >= 0)
	{
		return BCStyle::TeamColor(Team);
	}
	return (!Name.IsEmpty() && Name == MyName()) ? BCStyle::Accent() : BCStyle::TextPrimary();
}

void SBCHud::HandleFeed(const FBCFeedEntry& Entry)
{
	BCHudDetail::FFeedItem Item;
	Item.Entry = Entry;
	Item.Time = FPlatformTime::Seconds();
	FeedItems.Add(Item);
	while (FeedItems.Num() > BCHudDetail::MaxFeed)
	{
		FeedItems.RemoveAt(0);
	}
}

const BCHudDetail::FFeedItem* SBCHud::FeedAt(int32 Slot) const
{
	// Slot 0 = newest.
	const int32 Index = FeedItems.Num() - 1 - Slot;
	return FeedItems.IsValidIndex(Index) ? &FeedItems[Index] : nullptr;
}

const FBCPopup* SBCHud::PopupAt(int32 Slot) const
{
	const ABCPlayerController* P = PC.Get();
	if (!P)
	{
		return nullptr;
	}
	const TArray<FBCPopup>& Popups = P->Popups();
	const int32 Index = Popups.Num() - 1 - Slot;
	return Popups.IsValidIndex(Index) ? &Popups[Index] : nullptr;
}

// ---------------------------------------------------------------------------------------------
// Bottom-right: speed, gear, rpm, boost, ability

TSharedRef<SWidget> SBCHud::MakeSpeedPanel()
{
	ABCPlayerController* P = PC.Get();
	auto Movement = [this]() -> const UBCVehicleMovement* {
		const ABCVehiclePawn* Car = GetCar();
		return Car ? Car->GetVehicleMovement() : nullptr;
	};
	auto IsMph = [this]() {
		const UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(PC.Get());
		return Settings && Settings->Gameplay().speedUnit == "mph";
	};

	return SNew(SBox)
		.WidthOverride(340.0f)
		.Visibility_Lambda([Movement]() -> EVisibility { return Movement() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(SBorder)
			.BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x06, 0x08, 0x0B, 0.62f), 12.0f))
			.Padding(FMargin(18.0f, 12.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.HAlign(HAlign_Right)
					.VAlign(VAlign_Bottom)
					[
						SNew(STextBlock)
						.Font(BCStyle::FontOutlined(64, true, 2))
						.ColorAndOpacity(BCStyle::TextPrimary())
						.Text_Lambda([Movement, IsMph]() -> FText {
							const UBCVehicleMovement* M = Movement();
							const float Kmh = M ? M->SpeedKmh() : 0.0f;
							return FText::AsNumber(FMath::RoundToInt(IsMph() ? Kmh * 0.621371f : Kmh));
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Bottom)
					.Padding(8.0f, 0.0f, 14.0f, 12.0f)
					[
						SNew(STextBlock)
						.Font(BCStyle::Font(16, true))
						.ColorAndOpacity(BCStyle::TextSecondary())
						.Text_Lambda([this, IsMph]() -> FText { return BCStyle::Txt(PC.Get(), IsMph() ? "ui.hud.mph" : "ui.hud.kmh"); })
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(SBox)
						.WidthOverride(56.0f)
						.HeightOverride(56.0f)
						[
							SNew(SBorder)
							.BorderImage(BCStyle::RoundedBrush(FLinearColor::Transparent, 8.0f, BCStyle::Accent(), 2.0f))
							.HAlign(HAlign_Center)
							.VAlign(VAlign_Center)
							[
								SNew(STextBlock)
								.Font(BCStyle::Font(30, true))
								.ColorAndOpacity(BCStyle::Accent())
								.Text_Lambda([Movement]() -> FText {
									const UBCVehicleMovement* M = Movement();
									return BCHudDetail::GearText(M ? M->Gear() : 0);
								})
							]
						]
					]
				]
				// RPM.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					BCStyle::Bar(BCStyle::Bind<TOptional<float>>([this, Movement]() -> TOptional<float> {
									 const UBCVehicleMovement* M = Movement();
									 const ABCVehiclePawn* Car = GetCar();
									 const float Redline = (Car && Car->GetDef()) ? Car->GetDef()->redlineRpm : 7000.0f;
									 return M ? FMath::Clamp(M->Rpm() / FMath::Max(1000.0f, Redline), 0.0f, 1.0f) : 0.0f;
								 }),
								 BCStyle::Bind<FSlateColor>([this, Movement]() -> FSlateColor {
									 const UBCVehicleMovement* M = Movement();
									 const ABCVehiclePawn* Car = GetCar();
									 const float Redline = (Car && Car->GetDef()) ? Car->GetDef()->redlineRpm : 7000.0f;
									 return (M && M->Rpm() > Redline * 0.9f) ? FSlateColor(BCStyle::Danger()) : FSlateColor(BCStyle::Accent());
								 }),
								 6.0f)
				]
				// Boost.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 10.0f, 0.0f, 0.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 10.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(64.0f)
						[
							SNew(STextBlock)
							.Text(BCStyle::Txt(P, "ui.hud.boost"))
							.Font(BCStyle::Font(14, true))
							.ColorAndOpacity_Lambda([Movement]() -> FSlateColor {
								const UBCVehicleMovement* M = Movement();
								return (M && M->IsBoosting()) ? FSlateColor(BCStyle::Hex(0x9F, 0xF0, 0xFF)) : FSlateColor(BCStyle::TextSecondary());
							})
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						BCStyle::Bar(BCStyle::Bind<TOptional<float>>([Movement]() -> TOptional<float> {
										 const UBCVehicleMovement* M = Movement();
										 return M ? FMath::Clamp(M->BoostFraction(), 0.0f, 1.0f) : 0.0f;
									 }),
									 BCStyle::Bind<FSlateColor>([Movement]() -> FSlateColor {
										 const UBCVehicleMovement* M = Movement();
										 return (M && M->IsBoosting()) ? FSlateColor(BCStyle::Hex(0x9F, 0xF0, 0xFF)) : FSlateColor(BCStyle::Hex(0x3D, 0xC8, 0xFF));
									 }),
									 12.0f)
					]
				]
				// Ability.
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 8.0f, 0.0f, 0.0f)
				[
					SNew(SVerticalBox)
					.Visibility_Lambda([Movement]() -> EVisibility {
						const UBCVehicleMovement* M = Movement();
						return (M && M->Ability().Type() != crash::AbilityType::None) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					+ SVerticalBox::Slot()
					.AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot()
						.FillWidth(1.0f)
						[
							SNew(STextBlock)
							.Font(BCStyle::Font(16, true))
							.ColorAndOpacity(BCStyle::TextPrimary())
							.Text_Lambda([this, Movement]() -> FText {
								const UBCVehicleMovement* M = Movement();
								return M ? BCStyle::Txt(PC.Get(), BCStyle::AbilityKey(M->Ability().Type())) : FText::GetEmpty();
							})
						]
						+ SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(STextBlock)
							.Font(BCStyle::Font(15, true))
							.ColorAndOpacity_Lambda([Movement]() -> FSlateColor {
								const UBCVehicleMovement* M = Movement();
								if (!M)
								{
									return FSlateColor(BCStyle::TextMuted());
								}
								if (M->Ability().Active())
								{
									return FSlateColor(BCStyle::Accent());
								}
								return M->Ability().Ready() ? FSlateColor(BCStyle::Success()) : FSlateColor(BCStyle::TextSecondary());
							})
							.Text_Lambda([this, Movement]() -> FText {
								const UBCVehicleMovement* M = Movement();
								if (!M)
								{
									return FText::GetEmpty();
								}
								if (M->Ability().Active())
								{
									return BCStyle::Txt(PC.Get(), "ui.hud.ability_active");
								}
								if (M->Ability().Ready())
								{
									return BCStyle::Txt(PC.Get(), "ui.hud.ability_ready");
								}
								return BCStyle::Fmt(PC.Get(), "ui.hud.ability_cooldown", {FString::FromInt(FMath::CeilToInt(M->Ability().CooldownRemaining()))});
							})
						]
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 4.0f, 0.0f, 0.0f)
					[
						BCStyle::Bar(BCStyle::Bind<TOptional<float>>([Movement]() -> TOptional<float> {
										 const UBCVehicleMovement* M = Movement();
										 return M ? FMath::Clamp(1.0f - M->Ability().CooldownFraction(), 0.0f, 1.0f) : 0.0f;
									 }),
									 BCStyle::Bind<FSlateColor>([Movement]() -> FSlateColor {
										 const UBCVehicleMovement* M = Movement();
										 return (M && M->Ability().Ready()) ? FSlateColor(BCStyle::Success()) : FSlateColor(BCStyle::Hex(0x80, 0x86, 0x90));
									 }),
									 6.0f)
					]
				]
			]
		];
}

// ---------------------------------------------------------------------------------------------
// Bottom-left: damage silhouette

TSharedRef<SWidget> SBCHud::ZoneBox(crash::Zone Zone, float Width, float Height)
{
	return SNew(SBox)
		.WidthOverride(Width)
		.HeightOverride(Height)
		.Padding(1.5f)
		[
			SNew(SBorder)
			.BorderImage(BCStyle::RoundedBrush(FLinearColor::White, 3.0f))
			.Padding(0.0f)
			.BorderBackgroundColor_Lambda([this, Zone]() -> FSlateColor {
				const ABCVehiclePawn* Car = GetCar();
				const UBCVehicleDamage* Damage = Car ? Car->GetDamage() : nullptr;
				if (!Damage)
				{
					return FSlateColor(BCStyle::Hex(0x30, 0x34, 0x3A));
				}
				const float F = Damage->State().ZoneFraction(Zone);
				return FSlateColor(BCStyle::WithAlpha(BCStyle::HealthColor(F), F <= 0.0f ? 0.35f : 0.9f));
			})
		];
}

TSharedRef<SWidget> SBCHud::WheelBox(int32 Wheel)
{
	return SNew(SBox)
		.WidthOverride(16.0f)
		.HeightOverride(36.0f)
		[
			SNew(SBorder)
			.BorderImage(BCStyle::RoundedBrush(FLinearColor::White, 4.0f))
			.Padding(0.0f)
			.BorderBackgroundColor_Lambda([this, Wheel]() -> FSlateColor {
				const ABCVehiclePawn* Car = GetCar();
				const UBCVehicleDamage* Damage = Car ? Car->GetDamage() : nullptr;
				if (!Damage)
				{
					return FSlateColor(BCStyle::Hex(0x30, 0x34, 0x3A));
				}
				if (Damage->State().IsWheelDetached(Wheel))
				{
					return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, 0.08f));
				}
				return FSlateColor(BCStyle::HealthColor(Damage->State().WheelFraction(Wheel)));
			})
		];
}

TSharedRef<SWidget> SBCHud::MakeDamagePanel()
{
	ABCPlayerController* P = PC.Get();
	auto Integrity = [this]() -> float {
		const ABCVehiclePawn* Car = GetCar();
		const UBCVehicleDamage* Damage = Car ? Car->GetDamage() : nullptr;
		return Damage ? FMath::Clamp(Damage->Integrity(), 0.0f, 1.0f) : 1.0f;
	};
	auto OnFire = [this]() {
		const ABCVehiclePawn* Car = GetCar();
		const UBCVehicleDamage* Damage = Car ? Car->GetDamage() : nullptr;
		return Damage && Damage->IsOnFire();
	};

	// Top view, front up: wheels at the corners around the body zones.
	TSharedRef<SWidget> Silhouette =
		SNew(SBox)
		.WidthOverride(124.0f)
		.HeightOverride(188.0f)
		[
			SNew(SOverlay)
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Top).Padding(0.0f, 26.0f, 0.0f, 0.0f)[WheelBox(0)]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Top).Padding(0.0f, 26.0f, 0.0f, 0.0f)[WheelBox(1)]
			+ SOverlay::Slot().HAlign(HAlign_Left).VAlign(VAlign_Bottom).Padding(0.0f, 0.0f, 0.0f, 26.0f)[WheelBox(2)]
			+ SOverlay::Slot().HAlign(HAlign_Right).VAlign(VAlign_Bottom).Padding(0.0f, 0.0f, 0.0f, 26.0f)[WheelBox(3)]
			+ SOverlay::Slot()
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot().AutoHeight()[ZoneBox(crash::Zone::Front, 84.0f, 34.0f)]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().AutoWidth()[ZoneBox(crash::Zone::Left, 18.0f, 112.0f)]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					[
						SNew(SOverlay)
						+ SOverlay::Slot()[ZoneBox(crash::Zone::Top, 48.0f, 112.0f)]
						// Engine marker (fire flashes).
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						[
							SNew(STextBlock)
							.Font(BCStyle::FontOutlined(13, true, 1))
							.Text(BCStyle::Txt(P, "ui.hud.engine_short"))
							.ColorAndOpacity_Lambda([this, OnFire]() -> FSlateColor {
								const ABCVehiclePawn* Car = GetCar();
								const UBCVehicleDamage* Damage = Car ? Car->GetDamage() : nullptr;
								if (OnFire())
								{
									const float Flash = 0.5f + 0.5f * FMath::Sin(static_cast<float>(FPlatformTime::Seconds()) * 14.0f);
									return FSlateColor(FLinearColor::LerpUsingHSV(BCStyle::Danger(), BCStyle::Warning(), Flash));
								}
								return FSlateColor(Damage ? BCStyle::HealthColor(Damage->State().EngineFraction()) : BCStyle::TextMuted());
							})
						]
					]
					+ SHorizontalBox::Slot().AutoWidth()[ZoneBox(crash::Zone::Right, 18.0f, 112.0f)]
				]
				+ SVerticalBox::Slot().AutoHeight()[ZoneBox(crash::Zone::Rear, 84.0f, 30.0f)]
			]
		];

	return SNew(SBox)
		.Visibility_Lambda([this]() -> EVisibility { return GetCar() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		[
			SNew(SBorder)
			.BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x06, 0x08, 0x0B, 0.62f), 12.0f))
			.Padding(FMargin(16.0f, 12.0f))
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					Silhouette
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(16.0f, 0.0f, 0.0f, 0.0f)
				[
					SNew(SBox)
					.WidthOverride(130.0f)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock).Text(BCStyle::Txt(P, "ui.hud.integrity")).Font(BCStyle::Font(13, true)).ColorAndOpacity(BCStyle::TextSecondary())
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						[
							SNew(STextBlock)
							.Font(BCStyle::FontOutlined(40, true, 2))
							.ColorAndOpacity_Lambda([Integrity]() -> FSlateColor { return FSlateColor(BCStyle::HealthColor(Integrity())); })
							.Text_Lambda([Integrity]() -> FText {
								return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(Integrity() * 100.0f)));
							})
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							BCStyle::Bar(BCStyle::Bind<TOptional<float>>([Integrity]() -> TOptional<float> { return Integrity(); }),
										 BCStyle::Bind<FSlateColor>([Integrity]() -> FSlateColor { return FSlateColor(BCStyle::HealthColor(Integrity())); }),
										 8.0f)
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 8.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Font(BCStyle::Font(14, true))
							.Text(BCStyle::Txt(P, "ui.hud.on_fire"))
							.Visibility_Lambda([OnFire]() -> EVisibility { return OnFire() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
							.ColorAndOpacity_Lambda([]() -> FSlateColor {
								const float Flash = 0.5f + 0.5f * FMath::Sin(static_cast<float>(FPlatformTime::Seconds()) * 14.0f);
								return FSlateColor(FLinearColor::LerpUsingHSV(BCStyle::Danger(), BCStyle::Warning(), Flash));
							})
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.Padding(0.0f, 4.0f, 0.0f, 0.0f)
						[
							SNew(STextBlock)
							.Font(BCStyle::Font(13, true))
							.AutoWrapText(true)
							.ColorAndOpacity(BCStyle::Warning())
							.Text(BCStyle::Txt(P, "ui.hud.flipped"))
							.Visibility_Lambda([this]() -> EVisibility {
								const ABCVehiclePawn* Car = GetCar();
								const UBCVehicleMovement* M = Car ? Car->GetVehicleMovement() : nullptr;
								return (M && M->IsUpsideDown() && !Car->IsWrecked()) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
							})
						]
					]
				]
			]
		];
}

// ---------------------------------------------------------------------------------------------
// Top-center: timer, mode, scores, hill, bounty

TSharedRef<SWidget> SBCHud::MakeTopCenter()
{
	TSharedRef<SHorizontalBox> Teams = SNew(SHorizontalBox);
	for (int32 Team = 0; Team < 4; ++Team)
	{
		Teams->AddSlot()
			.AutoWidth()
			.Padding(4.0f, 0.0f)
			[
				SNew(SBox)
				.MinDesiredWidth(88.0f)
				.Visibility_Lambda([this, Team]() -> EVisibility {
					const ABCGameState* GS = GetGS();
					return (GS && GS->IsTeamMode() && Team < GS->TeamCount) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
				[
					SNew(SBorder)
					.BorderImage(BCStyle::RoundedBrush(BCStyle::WithAlpha(BCStyle::TeamColor(Team), 0.85f), 6.0f))
					.Padding(FMargin(12.0f, 2.0f))
					.HAlign(HAlign_Center)
					[
						SNew(SVerticalBox)
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Font(BCStyle::FontOutlined(28, true, 1))
							.ColorAndOpacity(FLinearColor::White)
							.Text_Lambda([this, Team]() -> FText {
								const ABCGameState* GS = GetGS();
								const crash::ModeDef* Mode = GetMode();
								if (!GS)
								{
									return FText::GetEmpty();
								}
								// Round-based modes show round wins big, score small.
								if (Mode && Mode->rounds > 1)
								{
									return FText::AsNumber(GS->RoundWins.IsValidIndex(Team) ? GS->RoundWins[Team] : 0);
								}
								return FText::AsNumber(GS->TeamScores.IsValidIndex(Team) ? GS->TeamScores[Team] : 0);
							})
						]
						+ SVerticalBox::Slot()
						.AutoHeight()
						.HAlign(HAlign_Center)
						[
							SNew(STextBlock)
							.Font(BCStyle::Font(11, true))
							.ColorAndOpacity(FLinearColor(1.0f, 1.0f, 1.0f, 0.85f))
							.Text_Lambda([this, Team]() -> FText {
								const ABCGameState* GS = GetGS();
								const crash::ModeDef* Mode = GetMode();
								if (GS && Mode && Mode->rounds > 1)
								{
									return BCStyle::Fmt(PC.Get(), "ui.hud.team_score_small",
														{BCStyle::Num(GS->TeamScores.IsValidIndex(Team) ? GS->TeamScores[Team] : 0)});
								}
								return BCStyle::TeamName(PC.Get(), Team);
							})
						]
					]
				]
			];
	}

	return SNew(SVerticalBox)
		.Visibility_Lambda([this]() -> EVisibility { return GetGS() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(SBorder)
			.BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x06, 0x08, 0x0B, 0.66f), 10.0f))
			.Padding(FMargin(22.0f, 4.0f))
			.HAlign(HAlign_Center)
			.Visibility_Lambda([this]() -> EVisibility {
				const ABCGameState* GS = GetGS();
				return (GS && GS->TimeRemaining >= 0.0f) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(STextBlock)
				.Font(BCStyle::FontOutlined(36, true, 2))
				.Text_Lambda([this]() -> FText {
					const ABCGameState* GS = GetGS();
					return FText::FromString(GS ? BCStyle::Clock(GS->TimeRemaining) : FString());
				})
				.ColorAndOpacity_Lambda([this]() -> FSlateColor {
					const ABCGameState* GS = GetGS();
					if (GS && GS->Phase == EBCMatchPhase::Playing && GS->TimeRemaining < 30.0f)
					{
						const bool bFlash = GS->TimeRemaining < 10.0f && FMath::Frac(FPlatformTime::Seconds() * 2.0) < 0.5;
						return FSlateColor(bFlash ? BCStyle::Warning() : BCStyle::Danger());
					}
					return FSlateColor(BCStyle::TextPrimary());
				})
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Font(BCStyle::FontOutlined(15, true, 1))
			.ColorAndOpacity(BCStyle::TextSecondary())
			.Text_Lambda([this]() -> FText {
				const ABCGameState* GS = GetGS();
				const crash::ModeDef* Mode = GetMode();
				if (!GS || !Mode)
				{
					return FText::GetEmpty();
				}
				FString Line = BCStyle::TxtStd(PC.Get(), Mode->name).ToString();
				if (Mode->rounds > 1)
				{
					Line += TEXT("  \u00B7  ") + BCStyle::Fmt(PC.Get(), "ui.hud.round", {FString::FromInt(GS->Round), FString::FromInt(Mode->rounds)}).ToString();
				}
				return FText::FromString(Line);
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.0f, 6.0f, 0.0f, 0.0f)
		[
			Teams
		]
		// FFA: own score.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Font(BCStyle::FontOutlined(20, true, 1))
			.ColorAndOpacity(BCStyle::Accent())
			.Visibility_Lambda([this]() -> EVisibility {
				const ABCGameState* GS = GetGS();
				return (GS && !GS->IsTeamMode() && GetPS() && GS->ModeId != TEXT("explore")) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			.Text_Lambda([this]() -> FText {
				const ABCPlayerState* PS = GetPS();
				return PS ? BCStyle::Fmt(PC.Get(), "ui.hud.my_score", {BCStyle::Num(PS->MatchScore), FString::FromInt(PS->Takedowns)}) : FText::GetEmpty();
			})
		]
		// Hill status.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Font(BCStyle::FontOutlined(17, true, 1))
			.Visibility_Lambda([this]() -> EVisibility {
				const crash::ModeDef* Mode = GetMode();
				return (Mode && Mode->hill) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			.ColorAndOpacity_Lambda([this]() -> FSlateColor {
				const ABCGameState* GS = GetGS();
				if (!GS)
				{
					return FSlateColor(BCStyle::TextPrimary());
				}
				if (GS->bHillContested)
				{
					return FSlateColor(BCStyle::Warning());
				}
				return FSlateColor(GS->HillOwnerTeam >= 0 ? BCStyle::TeamColor(GS->HillOwnerTeam) : BCStyle::TextSecondary());
			})
			.Text_Lambda([this]() -> FText {
				const ABCGameState* GS = GetGS();
				if (!GS)
				{
					return FText::GetEmpty();
				}
				if (GS->bHillContested)
				{
					return BCStyle::Txt(PC.Get(), "ui.hud.hill_contested");
				}
				if (GS->HillOwnerTeam >= 0)
				{
					return BCStyle::Fmt(PC.Get(), "ui.hud.hill_owned", {BCStyle::TeamName(PC.Get(), GS->HillOwnerTeam).ToString()});
				}
				return BCStyle::Txt(PC.Get(), "ui.hud.hill_neutral");
			})
		]
		// Bounty target.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Font(BCStyle::FontOutlined(17, true, 1))
			.ColorAndOpacity(BCStyle::GoldColor())
			.Visibility_Lambda([this]() -> EVisibility {
				const ABCGameState* GS = GetGS();
				return (GS && GS->BountyParticipant >= 0) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			.Text_Lambda([this]() -> FText {
				const ABCGameState* GS = GetGS();
				if (!GS)
				{
					return FText::GetEmpty();
				}
				const ABCPlayerState* Mine = GetPS();
				if (Mine && Mine->ParticipantId == GS->BountyParticipant)
				{
					return BCStyle::Txt(PC.Get(), "ui.hud.bounty_you");
				}
				for (APlayerState* Base : GS->PlayerArray)
				{
					const ABCPlayerState* PS = Cast<ABCPlayerState>(Base);
					if (PS && PS->ParticipantId == GS->BountyParticipant)
					{
						return BCStyle::Fmt(PC.Get(), "ui.hud.bounty_target", {PS->GetPlayerName()});
					}
				}
				return BCStyle::Txt(PC.Get(), "ui.hud.bounty_unknown");
			})
		];
}

// ---------------------------------------------------------------------------------------------
// Center: countdown, GO, waiting, round end, respawn, eliminated

TSharedRef<SWidget> SBCHud::MakeCenterMessages()
{
	auto Phase = [this]() {
		const ABCGameState* GS = GetGS();
		return GS ? GS->Phase : EBCMatchPhase::Waiting;
	};
	auto HasGS = [this]() { return GetGS() != nullptr; };
	return SNew(SVerticalBox)
		// Countdown "3 2 1" then the GO message.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Visibility_Lambda([this, Phase]() -> EVisibility {
				const bool bCountdown = Phase() == EBCMatchPhase::Countdown;
				return (bCountdown || FPlatformTime::Seconds() < GoUntil) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			.Text_Lambda([this, Phase]() -> FText {
				const ABCGameState* GS = GetGS();
				if (Phase() == EBCMatchPhase::Countdown && GS)
				{
					const int32 N = FMath::CeilToInt(GS->CountdownRemaining);
					return N > 0 ? FText::AsNumber(N) : BCStyle::Txt(PC.Get(), "ui.hud.go");
				}
				return BCStyle::Txt(PC.Get(), "ui.hud.go");
			})
			.Font_Lambda([this, Phase]() -> FSlateFontInfo {
				const ABCGameState* GS = GetGS();
				// Each number pops in big and settles.
				float Frac = 0.0f;
				if (Phase() == EBCMatchPhase::Countdown && GS)
				{
					Frac = FMath::Frac(GS->CountdownRemaining);
				}
				else
				{
					Frac = FMath::Clamp(static_cast<float>(GoUntil - FPlatformTime::Seconds()) / 1.1f, 0.0f, 1.0f);
				}
				const int32 Size = 120 + 8 * FMath::RoundToInt(Frac * 4.0f);
				return BCStyle::FontOutlined(Size, true, 5);
			})
			.ColorAndOpacity_Lambda([Phase]() -> FSlateColor {
				return Phase() == EBCMatchPhase::Countdown ? FSlateColor(BCStyle::TextPrimary()) : FSlateColor(BCStyle::Accent());
			})
			.ShadowOffset(FVector2D(0.0f, 6.0f))
			.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f))
		]
		// Waiting / round end / finished.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		[
			SNew(STextBlock)
			.Font(BCStyle::FontOutlined(40, true, 3))
			.ColorAndOpacity(BCStyle::TextPrimary())
			.Visibility_Lambda([Phase, HasGS]() -> EVisibility {
				const EBCMatchPhase Ph = Phase();
				return (HasGS() && (Ph == EBCMatchPhase::Waiting || Ph == EBCMatchPhase::RoundEnd || Ph == EBCMatchPhase::Finished))
						   ? EVisibility::HitTestInvisible
						   : EVisibility::Collapsed;
			})
			.Text_Lambda([this, Phase]() -> FText {
				switch (Phase())
				{
				case EBCMatchPhase::RoundEnd:
					return BCStyle::Txt(PC.Get(), "ui.hud.round_end");
				case EBCMatchPhase::Finished:
					return BCStyle::Txt(PC.Get(), "ui.hud.match_over");
				default:
				{
					const int32 Dots = static_cast<int32>(FPlatformTime::Seconds() * 2.0) % 4;
					return FText::FromString(BCStyle::Txt(PC.Get(), "ui.hud.waiting").ToString() + FString::ChrN(Dots, TEXT('.')));
				}
				}
			})
		]
		// Respawn / eliminated.
		+ SVerticalBox::Slot()
		.AutoHeight()
		.HAlign(HAlign_Center)
		.Padding(0.0f, 10.0f, 0.0f, 0.0f)
		[
			SNew(SBorder)
			.BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x30, 0x08, 0x06, 0.8f), 10.0f, BCStyle::Danger(), 1.0f))
			.Padding(FMargin(28.0f, 10.0f))
			.Visibility_Lambda([this, Phase]() -> EVisibility {
				const ABCPlayerState* PS = GetPS();
				const bool bShow = PS && (PS->bEliminated || PS->RespawnIn > 0.0f) && Phase() == EBCMatchPhase::Playing;
				return bShow ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
			})
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Font(BCStyle::FontOutlined(34, true, 2))
					.ColorAndOpacity(BCStyle::Danger())
					.Text_Lambda([this]() -> FText {
						const ABCPlayerState* PS = GetPS();
						return BCStyle::Txt(PC.Get(), (PS && PS->bEliminated) ? "ui.hud.eliminated" : "ui.hud.wrecked");
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Font(BCStyle::Font(20, true))
					.ColorAndOpacity(BCStyle::TextPrimary())
					.Text_Lambda([this]() -> FText {
						const ABCPlayerState* PS = GetPS();
						if (PS && PS->bEliminated)
						{
							return BCStyle::Txt(PC.Get(), "ui.hud.spectating");
						}
						return BCStyle::Fmt(PC.Get(), "ui.hud.respawn_in", {FString::FromInt(PS ? FMath::CeilToInt(PS->RespawnIn) : 0)});
					})
				]
			]
		];
}

// ---------------------------------------------------------------------------------------------
// Score popups (newest first, fading)

TSharedRef<SWidget> SBCHud::MakePopups()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible);
	for (int32 PopupSlot = 0; PopupSlot < BCHudDetail::MaxPopups; ++PopupSlot)
	{
		auto Alpha = [this, PopupSlot]() -> float {
			const FBCPopup* Popup = PopupAt(PopupSlot);
			if (!Popup)
			{
				return 0.0f;
			}
			const double Age = FPlatformTime::Seconds() - Popup->ShownAt;
			const float Fade = Age > 1.5 ? static_cast<float>((BCHudDetail::PopupLifetime - Age) / (BCHudDetail::PopupLifetime - 1.5)) : 1.0f;
			// Older popups are dimmer.
			return FMath::Clamp(Fade, 0.0f, 1.0f) * (1.0f - 0.18f * static_cast<float>(PopupSlot));
		};
		Box->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 2.0f)
			[
				SNew(SVerticalBox)
				.Visibility_Lambda([this, PopupSlot]() -> EVisibility { return PopupAt(PopupSlot) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Text_Lambda([this, PopupSlot]() -> FText {
						const FBCPopup* Popup = PopupAt(PopupSlot);
						return Popup ? Popup->Title : FText::GetEmpty();
					})
					.Font_Lambda([this, PopupSlot]() -> FSlateFontInfo {
						const FBCPopup* Popup = PopupAt(PopupSlot);
						const float Scale = Popup ? Popup->Scale : 1.0f;
						const double Age = Popup ? FPlatformTime::Seconds() - Popup->ShownAt : 1.0;
						const float Punch = Age < 0.16 ? 1.0f + 0.4f * static_cast<float>(1.0 - Age / 0.16) : 1.0f;
						const float Base = (PopupSlot == 0 ? 40.0f : 28.0f) * Scale * Punch;
						// Quantize sizes so the font cache stays small.
						return BCStyle::FontOutlined(FMath::Max(12, 4 * FMath::RoundToInt(Base / 4.0f)), true, 2);
					})
					.ColorAndOpacity_Lambda([this, PopupSlot, Alpha]() -> FSlateColor {
						const FBCPopup* Popup = PopupAt(PopupSlot);
						return FSlateColor(BCStyle::WithAlpha(Popup ? Popup->Color : FLinearColor::White, Alpha()));
					})
					.ShadowOffset(FVector2D(0.0f, 3.0f))
					.ShadowColorAndOpacity_Lambda([Alpha]() -> FLinearColor { return FLinearColor(0.0f, 0.0f, 0.0f, 0.6f * Alpha()); })
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.HAlign(HAlign_Center)
				[
					SNew(STextBlock)
					.Font(BCStyle::FontOutlined(PopupSlot == 0 ? 22 : 16, true, 1))
					.Visibility_Lambda([this]() -> EVisibility {
						const UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(PC.Get());
						return (!Settings || Settings->Gameplay().damageNumbers) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					.Text_Lambda([this, PopupSlot]() -> FText {
						const FBCPopup* Popup = PopupAt(PopupSlot);
						return Popup ? Popup->Detail : FText::GetEmpty();
					})
					.ColorAndOpacity_Lambda([Alpha]() -> FSlateColor { return FSlateColor(FLinearColor(1.0f, 1.0f, 1.0f, Alpha())); })
				]
			];
	}
	return Box;
}

// ---------------------------------------------------------------------------------------------
// Takedown feed (top-right)

TSharedRef<SWidget> SBCHud::MakeFeed()
{
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible);
	for (int32 FeedSlot = 0; FeedSlot < BCHudDetail::MaxFeed; ++FeedSlot)
	{
		auto Alpha = [this, FeedSlot]() -> float {
			const BCHudDetail::FFeedItem* Item = FeedAt(FeedSlot);
			if (!Item)
			{
				return 0.0f;
			}
			const double Age = FPlatformTime::Seconds() - Item->Time;
			return FMath::Clamp(static_cast<float>((BCHudDetail::FeedLifetime - Age) / 1.0), 0.0f, 1.0f);
		};
		Box->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Right)
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SBorder)
				.BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x06, 0x08, 0x0B, 0.7f), 6.0f))
				.Padding(FMargin(12.0f, 5.0f))
				.Visibility_Lambda([this, FeedSlot]() -> EVisibility { return FeedAt(FeedSlot) ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
				.ColorAndOpacity_Lambda([Alpha]() -> FLinearColor { return FLinearColor(1.0f, 1.0f, 1.0f, Alpha()); })
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(BCStyle::Font(16, true))
						.Text_Lambda([this, FeedSlot]() -> FText {
							const BCHudDetail::FFeedItem* Item = FeedAt(FeedSlot);
							return Item ? FText::FromString(Item->Entry.Attacker) : FText::GetEmpty();
						})
						.ColorAndOpacity_Lambda([this, FeedSlot]() -> FSlateColor {
							const BCHudDetail::FFeedItem* Item = FeedAt(FeedSlot);
							return Item ? FSlateColor(NameColor(Item->Entry.Attacker, Item->Entry.AttackerTeam)) : FSlateColor(BCStyle::TextPrimary());
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(10.0f, 0.0f)
					[
						SNew(STextBlock)
						.Font(BCStyle::Font(14, true))
						.Text_Lambda([this, FeedSlot]() -> FText {
							const BCHudDetail::FFeedItem* Item = FeedAt(FeedSlot);
							if (!Item)
							{
								return FText::GetEmpty();
							}
							const FBCFeedEntry& E = Item->Entry;
							if (E.Attacker.IsEmpty())
							{
								return BCStyle::Txt(PC.Get(), E.bRingOut ? "ui.hud.feed.fell" : "ui.hud.feed.self_wreck");
							}
							if (E.bRingOut)
							{
								return BCStyle::Txt(PC.Get(), "ui.hud.feed.ring_out");
							}
							const crash::ImpactKind Kind = static_cast<crash::ImpactKind>(E.Kind);
							const FString KindName = Kind == crash::ImpactKind::None ? FString() : BCStyle::Txt(PC.Get(), BCStyle::ImpactKey(Kind)).ToString();
							if (E.bTakedown)
							{
								return KindName.IsEmpty() ? BCStyle::Txt(PC.Get(), "ui.hud.feed.takedown_plain")
														  : BCStyle::Fmt(PC.Get(), "ui.hud.feed.takedown", {KindName});
							}
							return FText::FromString(KindName);
						})
						.ColorAndOpacity_Lambda([this, FeedSlot]() -> FSlateColor {
							const BCHudDetail::FFeedItem* Item = FeedAt(FeedSlot);
							return (Item && (Item->Entry.bTakedown || Item->Entry.bRingOut)) ? FSlateColor(BCStyle::Accent())
																						   : FSlateColor(BCStyle::TextSecondary());
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(BCStyle::Font(16, true))
						.Text_Lambda([this, FeedSlot]() -> FText {
							const BCHudDetail::FFeedItem* Item = FeedAt(FeedSlot);
							return Item ? FText::FromString(Item->Entry.Victim) : FText::GetEmpty();
						})
						.ColorAndOpacity_Lambda([this, FeedSlot]() -> FSlateColor {
							const BCHudDetail::FFeedItem* Item = FeedAt(FeedSlot);
							return Item ? FSlateColor(NameColor(Item->Entry.Victim, Item->Entry.VictimTeam)) : FSlateColor(BCStyle::TextPrimary());
						})
					]
				]
			];
	}
	return Box;
}

// ---------------------------------------------------------------------------------------------
// Campaign objective (left)

TSharedRef<SWidget> SBCHud::MakeObjectivePanel()
{
	ABCPlayerController* P = PC.Get();
	auto Level = [this]() -> const crash::LevelDef* {
		const ABCGameState* GS = GetGS();
		const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get());
		return (GS && Data && !GS->LevelId.IsEmpty()) ? Data->Db().FindLevel(BC::ToStd(GS->LevelId)) : nullptr;
	};
	TSharedRef<SVerticalBox> Stars = SNew(SVerticalBox);
	for (int32 s = 0; s < 3; ++s)
	{
		Stars->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 2.0f, 0.0f, 0.0f)
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(0.0f, 0.0f, 6.0f, 0.0f)
				[
					SNew(STextBlock).Text(FText::FromString(TEXT("\u2605"))).Font(BCStyle::Font(13, true)).ColorAndOpacity(BCStyle::GoldColor())
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				[
					SNew(STextBlock)
					.Font(BCStyle::Font(13))
					.ColorAndOpacity(BCStyle::TextSecondary())
					.AutoWrapText(true)
					.Text_Lambda([this, Level, s]() -> FText {
						const crash::LevelDef* L = Level();
						return L ? BCStyle::ObjectiveText(PC.Get(), L->stars[static_cast<size_t>(s)]) : FText::GetEmpty();
					})
				]
			];
	}

	return SNew(SBox)
		.WidthOverride(320.0f)
		.Visibility_Lambda([this]() -> EVisibility {
			const ABCGameState* GS = GetGS();
			return (GS && (!GS->LevelId.IsEmpty() || GS->ModeId == TEXT("explore"))) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
		})
		[
			SNew(SBorder)
			.BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x06, 0x08, 0x0B, 0.62f), 10.0f))
			.Padding(FMargin(14.0f, 10.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock).Text(BCStyle::Txt(P, "ui.hud.objective")).Font(BCStyle::Font(13, true)).ColorAndOpacity(BCStyle::Accent())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Font(BCStyle::Font(16, true))
					.ColorAndOpacity(BCStyle::TextPrimary())
					.AutoWrapText(true)
					.Text_Lambda([this, Level]() -> FText {
						if (const crash::LevelDef* L = Level())
						{
							return BCStyle::ObjectiveText(PC.Get(), L->objective);
						}
						const ABCGameState* GS = GetGS();
						return GS ? BCStyle::TxtF(PC.Get(), GS->ObjectiveText) : FText::GetEmpty();
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 2.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Font(BCStyle::Font(13))
					.ColorAndOpacity(BCStyle::TextSecondary())
					.AutoWrapText(true)
					.Visibility_Lambda([Level]() -> EVisibility { return Level() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					.Text_Lambda([this]() -> FText {
						const ABCGameState* GS = GetGS();
						return GS ? BCStyle::TxtF(PC.Get(), GS->ObjectiveText) : FText::GetEmpty();
					})
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(SBox)
					.Visibility_Lambda([Level]() -> EVisibility { return Level() ? EVisibility::HitTestInvisible : EVisibility::Collapsed; })
					[
						Stars
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 6.0f, 0.0f, 0.0f)
				[
					SNew(STextBlock)
					.Font(BCStyle::Font(15, true))
					.ColorAndOpacity(BCStyle::GoldColor())
					.Visibility_Lambda([this]() -> EVisibility {
						const ABCGameState* GS = GetGS();
						return (GS && GS->CollectiblesTotal > 0) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
					})
					.Text_Lambda([this]() -> FText {
						const ABCGameState* GS = GetGS();
						return GS ? BCStyle::Fmt(PC.Get(), "ui.hud.collectibles",
												 {FString::FromInt(GS->CollectiblesFound), FString::FromInt(GS->CollectiblesTotal)})
								  : FText::GetEmpty();
					})
				]
			]
		];
}

// ---------------------------------------------------------------------------------------------
// Scoreboard overlay (while held)

TSharedRef<SWidget> SBCHud::MakeScoreboard()
{
	ABCPlayerController* P = PC.Get();
	auto Header = [](const FText& Text, float Width) -> TSharedRef<SWidget> {
		return SNew(SBox)
			.WidthOverride(Width)
			[
				SNew(STextBlock).Text(Text).Font(BCStyle::Font(13, true)).ColorAndOpacity(BCStyle::TextMuted()).Justification(ETextJustify::Right)
			];
	};
	return SNew(SBox)
		.WidthOverride(940.0f)
		.Visibility_Lambda([this]() -> EVisibility {
			const ABCPlayerController* Owner = PC.Get();
			return (Owner && Owner->IsScoreboardHeld()) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
		})
		[
			SNew(SBorder)
			.BorderImage(BCStyle::RoundedBrush(BCStyle::Hex(0x08, 0x0A, 0x0D, 0.9f), 12.0f, BCStyle::Hex(0x2A, 0x2F, 0x37), 1.0f))
			.Padding(FMargin(20.0f, 16.0f))
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(0.0f, 0.0f, 0.0f, 10.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					[
						SNew(STextBlock)
						.Font(BCStyle::FontOutlined(24, true, 1))
						.ColorAndOpacity(BCStyle::TextPrimary())
						.Text_Lambda([this]() -> FText {
							const crash::ModeDef* Mode = GetMode();
							return Mode ? BCStyle::TxtStd(PC.Get(), Mode->name) : BCStyle::Txt(PC.Get(), "ui.results.scoreboard");
						})
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock)
						.Font(BCStyle::Font(18, true))
						.ColorAndOpacity(BCStyle::TextSecondary())
						.Text_Lambda([this]() -> FText {
							const ABCGameState* GS = GetGS();
							return (GS && GS->TimeRemaining >= 0.0f) ? FText::FromString(BCStyle::Clock(GS->TimeRemaining)) : FText::GetEmpty();
						})
					]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(16.0f, 0.0f, 10.0f, 6.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(STextBlock).Text(BCStyle::Txt(P, "ui.results.col.name")).Font(BCStyle::Font(13, true)).ColorAndOpacity(BCStyle::TextMuted())]
					+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.results.col.team"), 90.0f)]
					+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.results.col.score"), 90.0f)]
					+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.results.col.takedowns"), 70.0f)]
					+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.results.col.assists"), 70.0f)]
					+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.results.col.wrecked"), 70.0f)]
					+ SHorizontalBox::Slot().AutoWidth()[Header(BCStyle::Txt(P, "ui.results.col.damage"), 90.0f)]
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SAssignNew(ScoreboardRows, SVerticalBox)
				]
			]
		];
}

void SBCHud::RebuildScoreboard()
{
	if (!ScoreboardRows.IsValid())
	{
		return;
	}
	ScoreboardRows->ClearChildren();
	const ABCGameState* GS = GetGS();
	if (!GS)
	{
		return;
	}
	TArray<const ABCPlayerState*> Players;
	for (APlayerState* Base : GS->PlayerArray)
	{
		if (const ABCPlayerState* PS = Cast<ABCPlayerState>(Base))
		{
			Players.Add(PS);
		}
	}
	const bool bTeams = GS->IsTeamMode();
	Players.Sort([bTeams](const ABCPlayerState& A, const ABCPlayerState& B) {
		if (bTeams && A.Team != B.Team)
		{
			return A.Team < B.Team;
		}
		return A.MatchScore > B.MatchScore;
	});
	const ABCPlayerState* Mine = GetPS();
	auto Cell = [](const FString& Text, float Width, const FLinearColor& Color) -> TSharedRef<SWidget> {
		return SNew(SBox)
			.WidthOverride(Width)
			[
				SNew(STextBlock).Text(FText::FromString(Text)).Font(BCStyle::Font(16, true)).ColorAndOpacity(Color).Justification(ETextJustify::Right)
			];
	};
	int32 Row = 0;
	for (const ABCPlayerState* PS : Players)
	{
		const bool bMe = PS == Mine;
		const FLinearColor Stripe = bTeams ? BCStyle::TeamColor(PS->Team) : (bMe ? BCStyle::Accent() : BCStyle::TextMuted());
		const FLinearColor RowColor = bMe ? BCStyle::Hex(0x3A, 0x22, 0x0C, 0.95f) : ((Row % 2 == 0) ? BCStyle::Hex(0x1A, 0x1E, 0x24, 0.85f) : BCStyle::Hex(0x14, 0x17, 0x1C, 0.85f));
		FString Name = PS->GetPlayerName();
		if (PS->bBot)
		{
			Name += TEXT("  [") + BCStyle::Txt(PC.Get(), "ui.common.bot").ToString() + TEXT("]");
		}
		const FLinearColor NameCol = PS->bEliminated ? BCStyle::TextMuted() : (bMe ? BCStyle::Accent() : BCStyle::TextPrimary());
		ScoreboardRows->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 3.0f)
			[
				SNew(SBorder)
				.BorderImage(BCStyle::RoundedBrush(RowColor, 4.0f))
				.Padding(FMargin(0.0f, 6.0f, 10.0f, 6.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.Padding(4.0f, 0.0f, 12.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(4.0f)
						[
							SNew(SBorder).BorderImage(BCStyle::RoundedBrush(Stripe, 2.0f)).Padding(0.0f)
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(FText::FromString(Name)).Font(BCStyle::Font(17, true)).ColorAndOpacity(NameCol)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)
					[
						Cell(bTeams ? BCStyle::TeamName(PC.Get(), PS->Team).ToString() : FString(TEXT("-")), 90.0f, Stripe)
					]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell(BCStyle::Num(PS->MatchScore), 90.0f, BCStyle::TextPrimary())]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell(FString::FromInt(PS->Takedowns), 70.0f, BCStyle::TextPrimary())]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell(FString::FromInt(PS->Assists), 70.0f, BCStyle::TextSecondary())]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell(FString::FromInt(PS->Wrecked), 70.0f, BCStyle::TextSecondary())]
					+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center)[Cell(BCStyle::Num(PS->DamageDealt), 90.0f, BCStyle::TextSecondary())]
				]
			];
		++Row;
	}
}

TSharedRef<SWidget> BCUI::MakeHud(ABCPlayerController* PC)
{
	return SNew(SBCHud).Owner(PC).Visibility(EVisibility::SelfHitTestInvisible);
}
