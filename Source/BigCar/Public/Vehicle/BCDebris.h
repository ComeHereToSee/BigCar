// Copyright BigCar Team. All Rights Reserved.
//
// Cosmetic, client-side debris and sparks (engine basic shapes, short lifespan, never collide with cars).
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "BCDebris.generated.h"

class UStaticMeshComponent;

UCLASS()
class BIGCAR_API ABCDebris : public AActor
{
	GENERATED_BODY()

public:
	ABCDebris();

	// Spawns `Count` pieces flying out from Location along Direction (cone). Shape: 0 cube, 1 cylinder.
	static void Burst(UWorld* World, const FVector& Location, const FVector& Direction, const FLinearColor& Color, int32 Count,
					  float SizeCm, float Speed, float LifeSeconds, int32 Shape = 0);
	// Number of live debris pieces (capped by the "debris detail" graphics setting).
	static int32 LiveCount;

	virtual void EndPlay(const EEndPlayReason::Type Reason) override;

	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Mesh;
};
