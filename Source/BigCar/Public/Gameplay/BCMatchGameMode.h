// Copyright BigCar Team. All Rights Reserved.
//
// Server-side referee for every match type (quick play, LAN, online/dedicated, campaign via subclass).
// Builds the arena, spawns vehicles and bots, turns physics contacts into CrashCore impacts (damage, scoring,
// deformation), runs CrashCore MatchRules (rounds, respawns, ring-outs, hill, bounty) and reports results.
#pragma once

#include "Core/BigCarTypes.h"
#include "CoreMinimal.h"
#include "CrashCore/Match.h"
#include "GameFramework/GameModeBase.h"

#include "BCMatchGameMode.generated.h"

class ABCArenaBuilder;
class ABCVehiclePawn;
class ABCPlayerState;
class ABCPlayerController;
class ABCBreakableProp;
class ABCCollectibleActor;
class ABCSecretZone;
class ABCGameState;
struct FBCAssignedPlayer;

UCLASS()
class BIGCAR_API ABCMatchGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABCMatchGameMode();

	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PreLogin(const FString& Options, const FString& Address, const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	virtual FString InitNewPlayer(APlayerController* NewPlayerController, const FUniqueNetIdRepl& UniqueId, const FString& Options,
								  const FString& Portal) override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;

	// ---- Called by pawns / props (server).
	void HandleVehicleHit(ABCVehiclePawn* Vehicle, AActor* Other, UPrimitiveComponent* OtherComp, const FHitResult& Hit, const FVector& NormalImpulse);
	void NotifyVehicleWrecked(ABCVehiclePawn* Vehicle, bool bRingOut);
	void NotifyAirtime(ABCVehiclePawn* Vehicle, float Seconds);
	void NotifyPropDestroyed(ABCBreakableProp* Prop, ABCVehiclePawn* By);
	void Explosion(const FVector& Center, float RadiusCm, float PushMs, float Damage);
	virtual void OnCollectiblePicked(ABCCollectibleActor* Collectible, ABCVehiclePawn* By) {}
	virtual void OnSecretFound(ABCSecretZone* Secret, ABCVehiclePawn* By) {}

	// ---- Player requests.
	void RequestRestart(APlayerController* PC);
	virtual void PlayerReady(APlayerController* PC) {}
	bool IsOfflineMatch() const;
	bool IsMatchRunning() const { return Rules.IsValid() && !bFinished; }

	ABCArenaBuilder* GetArena() const { return Arena; }
	const crash::MatchRules* GetRules() const { return Rules.Get(); }
	const FString& GetModeId() const { return ModeId; }
	ABCVehiclePawn* VehicleOf(int32 ParticipantId) const;
	ABCPlayerState* StateOf(int32 ParticipantId) const;

protected:
	// Decides mode/arena/bots (campaign overrides).
	virtual void ConfigureMatch();
	virtual void StartMatch();
	virtual void SpawnBots();
	virtual void OnMatchFinished();
	virtual void OnWreckedHook(ABCVehiclePawn* Victim, const crash::TakedownInfo& Info) {}
	virtual void TickMode(float DeltaSeconds) {}
	virtual bool AllowRespawn(int32 ParticipantId) const { return true; }
	virtual FBCVehicleBuild BuildForPlayer(APlayerController* PC) const;
	// Campaign holds the countdown until the intro dialogue is closed.
	virtual bool DelayCountdown() const { return false; }
	virtual int32 TeamForNewPlayer() const;

	AController* AddBot(const FBCVehicleBuild& Build, int32 Team, const FString& AiProfile, const FString& Name, bool bBoss);
	ABCVehiclePawn* SpawnVehicleFor(AController* Controller);
	FTransform PickSpawn(int32 Team, const FBCVehicleBuild& Build);
	int32 RegisterParticipant(AController* Controller, int32 Team, bool bBot);
	FBCVehicleBuild RandomBotBuild(int32 AroundTier, float Skill) const;
	void SyncState();
	void ProcessScoreEvents();
	void CheckOutOfBounds();
	void UpdateHill(float DeltaSeconds);
	void HandleRespawns();
	void FinishAndReport();
	void SendPersonalResults(const TMap<FString, crash::JsonValue>& BackendRewards);
	FString NameOf(int32 ParticipantId) const;
	void SlowMotion(float RealSeconds, float Dilation);

	FString ModeId = TEXT("derby");
	FString ArenaId;
	FString BotAi = TEXT("regular");
	int32 BotCount = 5;
	float TimeLimitOverride = 0.0f;
	bool bStarted = false;
	bool bFinished = false;
	bool bWaitingForAssignment = false;
	bool bWaitingForPlayers = false;
	double WaitingSince = 0.0;
	float FinishedAt = 0.0f;
	double SlowMoUntil = 0.0;

	TUniquePtr<crash::MatchRules> Rules;
	const crash::ModeDef* Mode = nullptr;

	UPROPERTY()
	TObjectPtr<ABCArenaBuilder> Arena;

	TMap<int32, TWeakObjectPtr<AController>> Participants;
	TSet<int32> WreckHandled;
	int32 NextParticipantId = 1;
	TMap<uint64, float> PairCooldowns;
	TArray<TPair<TWeakObjectPtr<ABCVehiclePawn>, float>> PendingDestroy;
};
