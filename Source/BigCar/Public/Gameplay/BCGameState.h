// Copyright BigCar Team. All Rights Reserved.
//
// Replicated match state mirrored from CrashCore MatchRules by the server game mode.
#pragma once

#include "Core/BigCarTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"

#include "BCGameState.generated.h"

class ABCArenaBuilder;

DECLARE_MULTICAST_DELEGATE_OneParam(FBCFeedEvent, const FBCFeedEntry&);

UCLASS()
class BIGCAR_API ABCGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ABCGameState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated)
	FString ModeId;

	UPROPERTY(ReplicatedUsing = OnRep_ArenaId)
	FString ArenaId;

	UPROPERTY(Replicated)
	FString LevelId; // campaign level, if any

	UPROPERTY(Replicated)
	EBCMatchPhase Phase = EBCMatchPhase::Waiting;

	UPROPERTY(Replicated)
	float TimeRemaining = -1.0f;

	UPROPERTY(Replicated)
	float CountdownRemaining = 0.0f;

	UPROPERTY(Replicated)
	int32 Round = 1;

	UPROPERTY(Replicated)
	int32 TeamCount = 0;

	UPROPERTY(Replicated)
	TArray<int32> TeamScores;

	UPROPERTY(Replicated)
	TArray<int32> RoundWins;

	UPROPERTY(Replicated)
	int32 HillOwnerTeam = -1;

	UPROPERTY(Replicated)
	bool bHillContested = false;

	UPROPERTY(Replicated)
	FVector_NetQuantize HillCenter;

	UPROPERTY(Replicated)
	float HillRadius = 0.0f;

	UPROPERTY(Replicated)
	int32 BountyParticipant = -1;

	UPROPERTY(Replicated)
	bool bNightArena = false;

	UPROPERTY(Replicated)
	int32 WinnerTeam = -1;

	UPROPERTY(Replicated)
	TArray<FBCResultRow> Results;

	UPROPERTY(Replicated)
	FString ObjectiveText; // loc key

	UPROPERTY(Replicated)
	int32 CollectiblesFound = 0;

	UPROPERTY(Replicated)
	int32 CollectiblesTotal = 0;

	bool IsNightArena() const { return bNightArena; }
	bool IsTeamMode() const { return TeamCount >= 2; }

	UFUNCTION(NetMulticast, Unreliable)
	void MulticastFeed(const FBCFeedEntry& Entry);

	FBCFeedEvent OnFeed;
	TArray<FBCFeedEntry> RecentFeed;

	// Clients build their own copy of the (static) arena geometry; the server's builder is not replicated.
	ABCArenaBuilder* GetArena() const { return Arena; }
	// Defined in the .cpp: assigning a raw pointer to TObjectPtr needs the complete ABCArenaBuilder type.
	void SetArena(ABCArenaBuilder* InArena);

private:
	UFUNCTION()
	void OnRep_ArenaId();

	UPROPERTY()
	TObjectPtr<ABCArenaBuilder> Arena;
};
