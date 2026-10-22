// Copyright BigCar Team. All Rights Reserved.

#include "Core/BCGameInstance.h"

#include "Core/BigCarTypes.h"
#include "Engine/Engine.h"

void UBCGameInstance::Init()
{
	Super::Init();
	if (GEngine)
	{
		NetworkFailureHandle = GEngine->OnNetworkFailure().AddUObject(this, &UBCGameInstance::OnNetworkFailure);
		TravelFailureHandle = GEngine->OnTravelFailure().AddUObject(this, &UBCGameInstance::OnTravelFailure);
	}
	UE_LOG(LogBigCar, Log, TEXT("BigCar starting (%s)"), IsDedicatedServerInstance() ? TEXT("dedicated server") : TEXT("client"));
}

void UBCGameInstance::Shutdown()
{
	if (GEngine)
	{
		GEngine->OnNetworkFailure().Remove(NetworkFailureHandle);
		GEngine->OnTravelFailure().Remove(TravelFailureHandle);
	}
	Super::Shutdown();
}

void UBCGameInstance::OnNetworkFailure(UWorld* World, UNetDriver* NetDriver, ENetworkFailure::Type FailureType, const FString& Error)
{
	PendingError = Error;
	UE_LOG(LogBigCar, Warning, TEXT("Network failure: %s"), *Error);
}

void UBCGameInstance::OnTravelFailure(UWorld* World, ETravelFailure::Type FailureType, const FString& Error)
{
	PendingError = Error;
	UE_LOG(LogBigCar, Warning, TEXT("Travel failure: %s"), *Error);
}
