// Copyright BigCar Team. All Rights Reserved.
//
// Local player: Enhanced Input (keyboard + gamepad, created in code), the Slate UI stack (menus, HUD, results,
// story dialogue), match-flow actions (quick play, campaign, LAN, online travel) and client RPCs from the server.
#pragma once

#include "Core/BigCarTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"

#include "BCPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;
class SWidget;
class SOverlay;
class SBox;
class ABCVehiclePawn;
class ABCShowroom;

UENUM()
enum class EBCScreen : uint8
{
	None,
	MainMenu,
	Campaign,
	QuickPlay,
	Online,
	Garage,
	Store,
	Settings,
	Pause,
	Results,
	Story
};

// Big score text shown over the HUD ("T-BONE! +480").
struct FBCPopup
{
	FText Title;
	FText Detail;
	FLinearColor Color = FLinearColor::White;
	double ShownAt = 0.0;
	float Scale = 1.0f;
};

struct FBCToast
{
	FText Text;
	double ShownAt = 0.0;
};

UCLASS()
class BIGCAR_API ABCPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	ABCPlayerController();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void SetupInputComponent() override;
	virtual void PlayerTick(float DeltaTime) override;
	virtual void OnPossess(APawn* InPawn) override;

	// ---- UI navigation (local only).
	void OpenScreen(EBCScreen Screen);
	void Back();
	EBCScreen CurrentScreen() const { return Screen; }
	bool IsInFrontEnd() const;
	void ShowToast(const FText& Text);
	const TArray<FBCPopup>& Popups() const { return PopupQueue; }
	const TArray<FBCToast>& Toasts() const { return ToastQueue; }
	bool IsScoreboardHeld() const { return bScoreboardHeld; }
	const FBCPersonalResult& LastResult() const { return Result; }

	// ---- Match flow.
	void StartQuickPlay(const FString& ModeId, const FString& ArenaId, int32 Bots, const FString& Ai, float TimeLimit, bool bHostLan);
	void StartCampaignLevel(const FString& LevelId);
	void StartHub(const FString& ChapterId);
	void JoinAddress(const FString& Address);
	void ReturnToMenu();
	void RestartMatch();
	void QuitGame();
	void PreviewBuild(const FBCVehicleBuild& Build);

	ABCVehiclePawn* GetVehicle() const;

	// ---- Server -> client.
	UFUNCTION(Client, Unreliable)
	void ClientScorePopup(const FBCScorePopup& Popup);

	UFUNCTION(Client, Reliable)
	void ClientShowResults(const FBCPersonalResult& InResult);

	UFUNCTION(Client, Unreliable)
	void ClientCrashCam(AActor* Focus, float Duration);

	UFUNCTION(Client, Reliable)
	void ClientToast(const FString& LocKey, const FString& Arg);

	// Campaign story dialogue; the server waits for ServerStoryDone before the countdown.
	UFUNCTION(Client, Reliable)
	void ClientStory(const TArray<FString>& LineKeys, bool bIntro);

	UFUNCTION(Server, Reliable)
	void ServerStoryDone();

	UFUNCTION(Server, Reliable)
	void ServerRestart();

	// Story screen callback (local).
	void FinishStory();
	const TArray<FString>& StoryLines() const { return PendingStory; }

protected:
	void BuildInput();
	void ApplyInputMode();
	void RefreshRoot();
	void OnAbilityPressed();
	void OnResetPressed();
	void OnPausePressed();
	void OnScoreboardPressed();
	void OnScoreboardReleased();
	FString TravelOptionsForBuild() const;

	UPROPERTY()
	TObjectPtr<UInputMappingContext> Mapping;

	UPROPERTY()
	TObjectPtr<UInputAction> ThrottleAction;

	UPROPERTY()
	TObjectPtr<UInputAction> BrakeAction;

	UPROPERTY()
	TObjectPtr<UInputAction> SteerAction;

	UPROPERTY()
	TObjectPtr<UInputAction> HandbrakeAction;

	UPROPERTY()
	TObjectPtr<UInputAction> BoostAction;

	UPROPERTY()
	TObjectPtr<UInputAction> AbilityAction;

	UPROPERTY()
	TObjectPtr<UInputAction> ResetAction;

	UPROPERTY()
	TObjectPtr<UInputAction> LookBackAction;

	UPROPERTY()
	TObjectPtr<UInputAction> HornAction;

	UPROPERTY()
	TObjectPtr<UInputAction> PauseAction;

	UPROPERTY()
	TObjectPtr<UInputAction> ScoreboardAction;

private:
	TSharedPtr<SOverlay> Root;
	TSharedPtr<SBox> ScreenSlot;
	TSharedPtr<SBox> HudSlot;
	EBCScreen Screen = EBCScreen::None;
	TArray<EBCScreen> History;
	TArray<FBCPopup> PopupQueue;
	TArray<FBCToast> ToastQueue;
	TArray<FString> PendingStory;
	bool bStoryIntro = false;
	bool bScoreboardHeld = false;
	FBCPersonalResult Result;
	float SmoothedSteer = 0.0f;
	TWeakObjectPtr<ABCShowroom> Showroom;
};
