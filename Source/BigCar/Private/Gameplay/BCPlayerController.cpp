// Copyright BigCar Team. All Rights Reserved.

#include "Gameplay/BCPlayerController.h"

#include "Core/BCDataSubsystem.h"
#include "Core/BCProfileSubsystem.h"
#include "Core/BCSettingsSubsystem.h"
#include "CrashCore/Json.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "Engine/LocalPlayer.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "EnhancedPlayerInput.h"
#include "Gameplay/BCFrontEndGameMode.h"
#include "Gameplay/BCMatchGameMode.h"
#include "Gameplay/BCPlayerState.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Misc/Base64.h"
#include "UI/BCUI.h"
#include "Vehicle/BCVehicleMovement.h"
#include "Vehicle/BCVehiclePawn.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"

namespace BCInputDetail
{
UInputAction* MakeAction(UObject* Outer, const TCHAR* Name, EInputActionValueType Type)
{
	UInputAction* A = NewObject<UInputAction>(Outer, FName(Name));
	A->ValueType = Type;
	return A;
}

void Map(UObject* Outer, UInputMappingContext* Ctx, UInputAction* Action, const FKey& Key, bool bNegate = false)
{
	FEnhancedActionKeyMapping& M = Ctx->MapKey(Action, Key);
	if (bNegate)
	{
		M.Modifiers.Add(NewObject<UInputModifierNegate>(Outer));
	}
}
} // namespace BCInputDetail

ABCPlayerController::ABCPlayerController()
{
	bShowMouseCursor = false;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
}

bool ABCPlayerController::IsInFrontEnd() const
{
	return GetWorld() && GetWorld()->GetAuthGameMode<ABCFrontEndGameMode>() != nullptr;
}

ABCVehiclePawn* ABCPlayerController::GetVehicle() const
{
	return Cast<ABCVehiclePawn>(GetPawn());
}

void ABCPlayerController::BeginPlay()
{
	Super::BeginPlay();
	if (!IsLocalController())
	{
		return;
	}
	BuildInput();
	RefreshRoot();
	if (IsInFrontEnd())
	{
		for (TActorIterator<ABCShowroom> It(GetWorld()); It; ++It)
		{
			Showroom = *It;
			break;
		}
		OpenScreen(EBCScreen::MainMenu);
	}
	else
	{
		OpenScreen(EBCScreen::None);
	}
}

void ABCPlayerController::EndPlay(const EEndPlayReason::Type Reason)
{
	if (Root.IsValid() && GEngine && GEngine->GameViewport)
	{
		GEngine->GameViewport->RemoveViewportWidgetContent(Root.ToSharedRef());
	}
	Root.Reset();
	Super::EndPlay(Reason);
}

void ABCPlayerController::RefreshRoot()
{
	if (!GEngine || !GEngine->GameViewport || Root.IsValid())
	{
		return;
	}
	SAssignNew(Root, SOverlay)
	+ SOverlay::Slot()
	[
		SAssignNew(HudSlot, SBox)
	]
	+ SOverlay::Slot()
	[
		SAssignNew(ScreenSlot, SBox)
	];
	GEngine->GameViewport->AddViewportWidgetContent(Root.ToSharedRef(), 10);
	if (!IsInFrontEnd())
	{
		HudSlot->SetContent(BCUI::MakeHud(this));
	}
}

// ---------------------------------------------------------------------------------------------
// Input

void ABCPlayerController::BuildInput()
{
	using namespace BCInputDetail;
	Mapping = NewObject<UInputMappingContext>(this, TEXT("BigCarDriving"));
	ThrottleAction = MakeAction(this, TEXT("Throttle"), EInputActionValueType::Axis1D);
	BrakeAction = MakeAction(this, TEXT("Brake"), EInputActionValueType::Axis1D);
	SteerAction = MakeAction(this, TEXT("Steer"), EInputActionValueType::Axis1D);
	HandbrakeAction = MakeAction(this, TEXT("Handbrake"), EInputActionValueType::Boolean);
	BoostAction = MakeAction(this, TEXT("Boost"), EInputActionValueType::Boolean);
	AbilityAction = MakeAction(this, TEXT("Ability"), EInputActionValueType::Boolean);
	ResetAction = MakeAction(this, TEXT("Reset"), EInputActionValueType::Boolean);
	LookBackAction = MakeAction(this, TEXT("LookBack"), EInputActionValueType::Boolean);
	HornAction = MakeAction(this, TEXT("Horn"), EInputActionValueType::Boolean);
	PauseAction = MakeAction(this, TEXT("Pause"), EInputActionValueType::Boolean);
	ScoreboardAction = MakeAction(this, TEXT("Scoreboard"), EInputActionValueType::Boolean);

	// Keyboard.
	Map(this, Mapping, ThrottleAction, EKeys::W);
	Map(this, Mapping, ThrottleAction, EKeys::Up);
	Map(this, Mapping, BrakeAction, EKeys::S);
	Map(this, Mapping, BrakeAction, EKeys::Down);
	Map(this, Mapping, SteerAction, EKeys::D);
	Map(this, Mapping, SteerAction, EKeys::Right);
	Map(this, Mapping, SteerAction, EKeys::A, true);
	Map(this, Mapping, SteerAction, EKeys::Left, true);
	Map(this, Mapping, HandbrakeAction, EKeys::SpaceBar);
	Map(this, Mapping, BoostAction, EKeys::LeftShift);
	Map(this, Mapping, AbilityAction, EKeys::E);
	Map(this, Mapping, AbilityAction, EKeys::Q);
	Map(this, Mapping, ResetAction, EKeys::R);
	Map(this, Mapping, LookBackAction, EKeys::C);
	Map(this, Mapping, HornAction, EKeys::H);
	Map(this, Mapping, PauseAction, EKeys::Escape);
	Map(this, Mapping, PauseAction, EKeys::P);
	Map(this, Mapping, ScoreboardAction, EKeys::Tab);
	// Gamepad.
	Map(this, Mapping, ThrottleAction, EKeys::Gamepad_RightTriggerAxis);
	Map(this, Mapping, BrakeAction, EKeys::Gamepad_LeftTriggerAxis);
	Map(this, Mapping, SteerAction, EKeys::Gamepad_LeftX);
	Map(this, Mapping, HandbrakeAction, EKeys::Gamepad_FaceButton_Left);
	Map(this, Mapping, BoostAction, EKeys::Gamepad_FaceButton_Bottom);
	Map(this, Mapping, AbilityAction, EKeys::Gamepad_FaceButton_Right);
	Map(this, Mapping, ResetAction, EKeys::Gamepad_FaceButton_Top);
	Map(this, Mapping, LookBackAction, EKeys::Gamepad_RightShoulder);
	Map(this, Mapping, HornAction, EKeys::Gamepad_LeftShoulder);
	Map(this, Mapping, PauseAction, EKeys::Gamepad_Special_Right);
	Map(this, Mapping, ScoreboardAction, EKeys::Gamepad_Special_Left);

	if (ULocalPlayer* LP = GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Sub = LP->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Sub->ClearAllMappings();
			Sub->AddMappingContext(Mapping, 0);
		}
	}
	if (UEnhancedInputComponent* EIC = Cast<UEnhancedInputComponent>(InputComponent))
	{
		EIC->BindAction(AbilityAction, ETriggerEvent::Started, this, &ABCPlayerController::OnAbilityPressed);
		EIC->BindAction(ResetAction, ETriggerEvent::Started, this, &ABCPlayerController::OnResetPressed);
		EIC->BindAction(PauseAction, ETriggerEvent::Started, this, &ABCPlayerController::OnPausePressed);
		EIC->BindAction(ScoreboardAction, ETriggerEvent::Started, this, &ABCPlayerController::OnScoreboardPressed);
		EIC->BindAction(ScoreboardAction, ETriggerEvent::Completed, this, &ABCPlayerController::OnScoreboardReleased);
	}
}

void ABCPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();
}

void ABCPlayerController::PlayerTick(float DeltaTime)
{
	Super::PlayerTick(DeltaTime);
	const double Now = FPlatformTime::Seconds();
	PopupQueue.RemoveAll([Now](const FBCPopup& P) { return Now - P.ShownAt > 2.2; });
	ToastQueue.RemoveAll([Now](const FBCToast& T) { return Now - T.ShownAt > 4.0; });

	ABCVehiclePawn* Vehicle = GetVehicle();
	UEnhancedPlayerInput* Input = Cast<UEnhancedPlayerInput>(PlayerInput);
	if (!Vehicle || !Input || !ThrottleAction)
	{
		return;
	}
	const bool bMenuOpen = Screen != EBCScreen::None;
	crash::VehicleControls C;
	if (!bMenuOpen)
	{
		C.throttle = FMath::Clamp(Input->GetActionValue(ThrottleAction).Get<float>(), 0.0f, 1.0f);
		C.brake = FMath::Clamp(Input->GetActionValue(BrakeAction).Get<float>(), 0.0f, 1.0f);
		const float SteerTarget = FMath::Clamp(Input->GetActionValue(SteerAction).Get<float>(), -1.0f, 1.0f);
		// Keyboard steering is digital: ramp it so small corrections are possible.
		SmoothedSteer = FMath::FInterpConstantTo(SmoothedSteer, SteerTarget, DeltaTime, FMath::Abs(SteerTarget) < 0.01f ? 7.0f : 4.5f);
		C.steer = SmoothedSteer;
		C.handbrake = Input->GetActionValue(HandbrakeAction).Get<bool>();
		C.boost = Input->GetActionValue(BoostAction).Get<bool>();
	}
	const bool bHorn = !bMenuOpen && Input->GetActionValue(HornAction).Get<bool>();
	const bool bLookBack = !bMenuOpen && Input->GetActionValue(LookBackAction).Get<bool>();
	Vehicle->SetDriverInput(C, bHorn, bLookBack);
}

void ABCPlayerController::OnAbilityPressed()
{
	if (ABCVehiclePawn* V = GetVehicle(); V && Screen == EBCScreen::None)
	{
		V->RequestAbility();
	}
}

void ABCPlayerController::OnResetPressed()
{
	if (ABCVehiclePawn* V = GetVehicle(); V && Screen == EBCScreen::None)
	{
		V->RequestReset();
	}
}

void ABCPlayerController::OnPausePressed()
{
	if (IsInFrontEnd())
	{
		Back();
		return;
	}
	if (Screen == EBCScreen::None)
	{
		OpenScreen(EBCScreen::Pause);
	}
	else if (Screen == EBCScreen::Pause || Screen == EBCScreen::Settings)
	{
		Back();
	}
}

void ABCPlayerController::OnScoreboardPressed()
{
	bScoreboardHeld = true;
}

void ABCPlayerController::OnScoreboardReleased()
{
	bScoreboardHeld = false;
}

void ABCPlayerController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);
}

// ---------------------------------------------------------------------------------------------
// UI

void ABCPlayerController::OpenScreen(EBCScreen NewScreen)
{
	if (!IsLocalController())
	{
		return;
	}
	RefreshRoot();
	if (!ScreenSlot.IsValid())
	{
		return;
	}
	if (Screen != EBCScreen::None && Screen != NewScreen && Screen != EBCScreen::Results && Screen != EBCScreen::Story)
	{
		History.Add(Screen);
	}
	Screen = NewScreen;
	TSharedPtr<SWidget> Content;
	switch (NewScreen)
	{
	case EBCScreen::MainMenu:
		History.Reset();
		Content = BCUI::MakeMainMenu(this);
		break;
	case EBCScreen::Campaign:
		Content = BCUI::MakeCampaign(this);
		break;
	case EBCScreen::QuickPlay:
		Content = BCUI::MakeQuickPlay(this);
		break;
	case EBCScreen::Online:
		Content = BCUI::MakeOnline(this);
		break;
	case EBCScreen::Garage:
		Content = BCUI::MakeGarage(this);
		break;
	case EBCScreen::Store:
		Content = BCUI::MakeStore(this);
		break;
	case EBCScreen::Settings:
		Content = BCUI::MakeSettings(this);
		break;
	case EBCScreen::Pause:
		Content = BCUI::MakePause(this);
		break;
	case EBCScreen::Results:
		Content = BCUI::MakeResults(this);
		break;
	case EBCScreen::Story:
		Content = BCUI::MakeStory(this);
		break;
	default:
		break;
	}
	if (Content.IsValid())
	{
		ScreenSlot->SetContent(Content.ToSharedRef());
	}
	else
	{
		ScreenSlot->SetContent(SNullWidget::NullWidget);
	}
	ApplyInputMode();
	// Single player pauses under menus (never online).
	if (GetNetMode() == NM_Standalone && !IsInFrontEnd())
	{
		SetPause(NewScreen == EBCScreen::Pause || NewScreen == EBCScreen::Settings);
	}
}

void ABCPlayerController::Back()
{
	if (History.Num() > 0)
	{
		const EBCScreen Prev = History.Pop();
		Screen = EBCScreen::None; // so OpenScreen does not push the screen we are leaving
		OpenScreen(Prev);
		return;
	}
	Screen = EBCScreen::None;
	OpenScreen(IsInFrontEnd() ? EBCScreen::MainMenu : EBCScreen::None);
}

void ABCPlayerController::ApplyInputMode()
{
	const bool bMenu = Screen != EBCScreen::None;
	if (bMenu)
	{
		FInputModeGameAndUI Mode;
		Mode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
		Mode.SetHideCursorDuringCapture(false);
		SetInputMode(Mode);
		bShowMouseCursor = true;
	}
	else
	{
		SetInputMode(FInputModeGameOnly());
		bShowMouseCursor = false;
	}
}

void ABCPlayerController::ShowToast(const FText& Text)
{
	FBCToast T;
	T.Text = Text;
	T.ShownAt = FPlatformTime::Seconds();
	ToastQueue.Add(T);
}

void ABCPlayerController::PreviewBuild(const FBCVehicleBuild& Build)
{
	if (ABCShowroom* S = Showroom.Get())
	{
		S->ShowBuild(Build);
	}
}

// ---------------------------------------------------------------------------------------------
// Match flow

FString ABCPlayerController::TravelOptionsForBuild() const
{
	const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this);
	if (!Profile)
	{
		return FString();
	}
	const crash::VehicleBuild* Build = Profile->Active().SelectedBuild();
	if (!Build)
	{
		return FString();
	}
	const FString Json = BC::ToFString(Build->ToJson().Dump());
	FString Name = BC::ToFString(Profile->Active().displayName).Replace(TEXT("?"), TEXT("")).Replace(TEXT("="), TEXT(""));
	return FString::Printf(TEXT("?Build=%s?Name=%s"), *FBase64::Encode(Json), *Name);
}

void ABCPlayerController::StartQuickPlay(const FString& ModeId, const FString& ArenaId, int32 Bots, const FString& Ai, float TimeLimit, bool bHostLan)
{
	if (UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this))
	{
		Profile->SetPreferOnline(false);
	}
	FString Options = FString::Printf(TEXT("game=/Script/BigCar.BCMatchGameMode?Mode=%s?Arena=%s?Bots=%d?AI=%s"), *ModeId, *ArenaId, Bots, *Ai);
	if (TimeLimit > 0.0f)
	{
		Options += FString::Printf(TEXT("?Time=%d"), FMath::RoundToInt(TimeLimit));
	}
	if (bHostLan)
	{
		Options += TEXT("?listen");
	}
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), true, Options);
}

void ABCPlayerController::StartCampaignLevel(const FString& LevelId)
{
	if (UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this))
	{
		Profile->SetPreferOnline(false);
	}
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), true,
								FString::Printf(TEXT("game=/Script/BigCar.BCCampaignGameMode?Level=%s"), *LevelId));
}

void ABCPlayerController::StartHub(const FString& ChapterId)
{
	if (UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this))
	{
		Profile->SetPreferOnline(false);
	}
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), true,
								FString::Printf(TEXT("game=/Script/BigCar.BCCampaignGameMode?Hub=%s"), *ChapterId));
}

void ABCPlayerController::JoinAddress(const FString& Address)
{
	if (Address.IsEmpty())
	{
		return;
	}
	ClientTravel(Address + TravelOptionsForBuild(), TRAVEL_Absolute);
}

void ABCPlayerController::ReturnToMenu()
{
	UGameplayStatics::SetGlobalTimeDilation(this, 1.0f);
	UGameplayStatics::OpenLevel(this, FName(TEXT("/Engine/Maps/Entry")), true, TEXT("game=/Script/BigCar.BCFrontEndGameMode"));
}

void ABCPlayerController::RestartMatch()
{
	if (GetNetMode() == NM_Standalone)
	{
		SetPause(false);
		ServerRestart();
	}
}

void ABCPlayerController::ServerRestart_Implementation()
{
	if (ABCMatchGameMode* GM = GetWorld()->GetAuthGameMode<ABCMatchGameMode>())
	{
		GM->RequestRestart(this);
	}
}

void ABCPlayerController::QuitGame()
{
	UKismetSystemLibrary::QuitGame(this, this, EQuitPreference::Quit, false);
}

// ---------------------------------------------------------------------------------------------
// RPCs

void ABCPlayerController::ClientScorePopup_Implementation(const FBCScorePopup& Popup)
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	if (!Data)
	{
		return;
	}
	FBCPopup P;
	P.ShownAt = FPlatformTime::Seconds();
	const crash::ScoreEventType Type = static_cast<crash::ScoreEventType>(Popup.Type);
	FString TitleKey;
	switch (Type)
	{
	case crash::ScoreEventType::Impact:
		TitleKey = TEXT("ui.hit.") + BC::ToFString(std::string(crash::ToString(static_cast<crash::ImpactKind>(Popup.Kind))));
		P.Color = FLinearColor(1.0f, 0.55f, 0.1f);
		break;
	case crash::ScoreEventType::Takedown:
		TitleKey = TEXT("ui.hit.takedown");
		P.Color = FLinearColor(1.0f, 0.15f, 0.1f);
		P.Scale = 1.4f;
		break;
	case crash::ScoreEventType::RingOut:
		TitleKey = TEXT("ui.hit.ring_out");
		P.Color = FLinearColor(1.0f, 0.15f, 0.1f);
		P.Scale = 1.4f;
		break;
	case crash::ScoreEventType::Assist:
		TitleKey = TEXT("ui.hit.assist");
		P.Color = FLinearColor(0.6f, 0.8f, 1.0f);
		break;
	case crash::ScoreEventType::Revenge:
		TitleKey = TEXT("ui.hit.revenge");
		P.Color = FLinearColor(0.9f, 0.2f, 0.9f);
		break;
	case crash::ScoreEventType::MultiTakedown:
		TitleKey = TEXT("ui.hit.multi");
		P.Color = FLinearColor(1.0f, 0.85f, 0.1f);
		P.Scale = 1.3f;
		break;
	case crash::ScoreEventType::Airtime:
		TitleKey = TEXT("ui.hit.airtime");
		P.Color = FLinearColor(0.3f, 0.9f, 1.0f);
		break;
	case crash::ScoreEventType::Bounty:
		TitleKey = TEXT("ui.hit.bounty");
		P.Color = FLinearColor(1.0f, 0.85f, 0.1f);
		break;
	case crash::ScoreEventType::PropDestroyed:
		TitleKey = TEXT("ui.hit.prop");
		P.Color = FLinearColor(0.8f, 0.8f, 0.8f);
		P.Scale = 0.8f;
		break;
	case crash::ScoreEventType::Collectible:
		TitleKey = TEXT("ui.hit.collectible");
		P.Color = FLinearColor(1.0f, 0.8f, 0.2f);
		break;
	default:
		TitleKey = TEXT("ui.hit.bonus");
		break;
	}
	P.Title = Data->Text(TitleKey);
	FString Detail = FString::Printf(TEXT("+%d"), Popup.Points);
	if (Popup.Multiplier > 1.05f)
	{
		Detail += FString::Printf(TEXT("  x%.1f"), Popup.Multiplier);
	}
	if (!Popup.Other.IsEmpty())
	{
		Detail += TEXT("  ") + Popup.Other;
	}
	P.Detail = FText::FromString(Detail);
	PopupQueue.Add(P);
	if (PopupQueue.Num() > 4)
	{
		PopupQueue.RemoveAt(0);
	}
}

void ABCPlayerController::ClientShowResults_Implementation(const FBCPersonalResult& InResult)
{
	Result = InResult;
	// Offline / LAN matches pay out into the local profile (online rewards come from the backend).
	if (!Result.bOnline && !Result.bCampaign && GetNetMode() != NM_DedicatedServer)
	{
		if (UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this))
		{
			crash::LevelUpResult LevelUp;
			Profile->MutableLocal().credits += Result.Credits;
			LevelUp = crash::AddXp(UBCDataSubsystem::Get(this)->Db(), Profile->MutableLocal(), Result.Xp);
			Result.LevelsGained = LevelUp.levelsGained;
			const ABCPlayerState* PS = GetPlayerState<ABCPlayerState>();
			Profile->RecordMatchStats(Result.bWon, PS ? PS->Takedowns : 0, PS ? PS->Wrecked : 0, PS ? static_cast<float>(PS->DamageDealt) : 0.0f,
									 PS ? PS->ImpactCounts : TArray<int32>(),
									 GetVehicle() ? GetVehicle()->GetVehicleMovement()->DistanceKm() : 0.0f);
			Profile->SaveLocal();
		}
	}
	if (UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this))
	{
		Profile->OnChanged.Broadcast();
	}
	OpenScreen(EBCScreen::Results);
}

void ABCPlayerController::ClientCrashCam_Implementation(AActor* Focus, float Duration)
{
	if (ABCVehiclePawn* V = GetVehicle())
	{
		V->SetCrashCam(Duration, Focus);
		V->AddCameraShake(0.6f);
	}
}

void ABCPlayerController::ClientToast_Implementation(const FString& LocKey, const FString& Arg)
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	if (Data)
	{
		ShowToast(Arg.IsEmpty() ? Data->Text(LocKey) : Data->Format(LocKey, {Arg}));
	}
}

void ABCPlayerController::ClientStory_Implementation(const TArray<FString>& LineKeys, bool bIntro)
{
	PendingStory = LineKeys;
	bStoryIntro = bIntro;
	if (LineKeys.Num() == 0)
	{
		FinishStory();
		return;
	}
	OpenScreen(EBCScreen::Story);
}

void ABCPlayerController::FinishStory()
{
	const bool bWasIntro = bStoryIntro;
	PendingStory.Reset();
	if (bWasIntro)
	{
		Screen = EBCScreen::None;
		OpenScreen(EBCScreen::None);
		ServerStoryDone();
	}
	else
	{
		OpenScreen(EBCScreen::Results);
	}
}

void ABCPlayerController::ServerStoryDone_Implementation()
{
	if (ABCMatchGameMode* GM = GetWorld()->GetAuthGameMode<ABCMatchGameMode>())
	{
		GM->PlayerReady(this);
	}
}
