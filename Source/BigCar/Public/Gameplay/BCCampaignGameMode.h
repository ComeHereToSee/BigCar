// Copyright BigCar Team. All Rights Reserved.
//
// Single-player career: levels from campaign.json (scripted opponents, bosses, survival waves, destruction goals,
// three stars, collectibles, intro/outro dialogue) and free-roam chapter hubs with secrets for exploration %.
#pragma once

#include "CoreMinimal.h"
#include "CrashCore/Campaign.h"
#include "Gameplay/BCMatchGameMode.h"

#include "BCCampaignGameMode.generated.h"

UCLASS()
class BIGCAR_API ABCCampaignGameMode : public ABCMatchGameMode
{
	GENERATED_BODY()

public:
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void OnCollectiblePicked(ABCCollectibleActor* Collectible, ABCVehiclePawn* By) override;
	virtual void OnSecretFound(ABCSecretZone* Secret, ABCVehiclePawn* By) override;
	virtual void PlayerReady(APlayerController* PC) override;

	const crash::LevelDef* Level() const { return LevelDef; }
	bool IsHub() const { return !HubChapter.IsEmpty(); }

protected:
	virtual void ConfigureMatch() override;
	virtual void StartMatch() override;
	virtual void SpawnBots() override;
	virtual void OnMatchFinished() override;
	virtual void OnWreckedHook(ABCVehiclePawn* Victim, const crash::TakedownInfo& Info) override;
	virtual void TickMode(float DeltaSeconds) override;
	virtual bool DelayCountdown() const override { return bWaitingForStory; }
	virtual int32 TeamForNewPlayer() const override { return 0; }
	virtual FBCVehicleBuild BuildForPlayer(APlayerController* PC) const override;

private:
	void SpawnCollectibles();
	void SpawnWave();
	bool PrimaryObjectiveMet() const;
	crash::LevelRunResult BuildRun() const;

	FString LevelId;
	FString HubChapter;
	const crash::LevelDef* LevelDef = nullptr;
	bool bWaitingForStory = false;
	bool bBossDefeated = false;
	bool bPlayerOut = false;
	TArray<int32> CollectedThisRun;
	float StartTime = 0.0f;
	float NextWaveAt = 0.0f;
	int32 WaveIndex = 0;
	int32 PropsDestroyedAtStart = 0;
};
