// Copyright BigCar Team. All Rights Reserved.
//
// Main menu / garage: a lit showroom with a turntable showing the selected car behind the Slate menus.
#pragma once

#include "Core/BigCarTypes.h"
#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"

#include "BCFrontEndGameMode.generated.h"

class ABCVehiclePawn;
class UCameraComponent;

UCLASS()
class BIGCAR_API ABCShowroom : public AActor
{
	GENERATED_BODY()

public:
	ABCShowroom();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	void ShowBuild(const FBCVehicleBuild& Build);
	UCameraComponent* GetCamera() const { return Camera; }

private:
	UPROPERTY()
	TObjectPtr<UCameraComponent> Camera;

	UPROPERTY()
	TObjectPtr<ABCVehiclePawn> Display;

	float Spin = 0.0f;
};

UCLASS()
class BIGCAR_API ABCFrontEndGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ABCFrontEndGameMode();
	virtual void BeginPlay() override;
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;

private:
	UPROPERTY()
	TObjectPtr<ABCShowroom> Showroom;
};
