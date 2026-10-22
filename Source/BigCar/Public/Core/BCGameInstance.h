// Copyright BigCar Team. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Engine/GameInstance.h"

#include "BCGameInstance.generated.h"

class UNetDriver;

UCLASS()
class BIGCAR_API UBCGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:
	virtual void Init() override;
	virtual void Shutdown() override;

	// Last connection/travel error, shown by the main menu after we fall back to it.
	FString PendingError;

private:
	void OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Error);
	void OnTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error);

	FDelegateHandle NetworkFailureHandle;
	FDelegateHandle TravelFailureHandle;
};
