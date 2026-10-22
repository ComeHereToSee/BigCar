// Copyright BigCar Team. All Rights Reserved.
#pragma once

#include "Core/BigCarTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"

#include "BCPlayerState.generated.h"

UCLASS()
class BIGCAR_API ABCPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(Replicated)
	int32 Team = -1;

	UPROPERTY(Replicated)
	int32 ParticipantId = -1;

	UPROPERTY(Replicated)
	int32 MatchScore = 0;

	UPROPERTY(Replicated)
	int32 Takedowns = 0;

	UPROPERTY(Replicated)
	int32 Assists = 0;

	UPROPERTY(Replicated)
	int32 Wrecked = 0;

	UPROPERTY(Replicated)
	int32 DamageDealt = 0;

	UPROPERTY(Replicated)
	bool bEliminated = false;

	UPROPERTY(Replicated)
	float RespawnIn = 0.0f;

	UPROPERTY(Replicated)
	FBCVehicleBuild Build;

	UPROPERTY(Replicated)
	FString Title;

	UPROPERTY(Replicated)
	bool bBot = false;

	// Server only.
	FString BackendPlayerId;
	TArray<int32> ImpactCounts;
};
