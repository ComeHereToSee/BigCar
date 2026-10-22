// Copyright BigCar Team. All Rights Reserved.
//
// Bot driver: builds a perception snapshot (cars, arena, obstacle feelers) each frame and lets CrashCore's AIBrain
// decide steering/throttle/boost/abilities.
#pragma once

#include "AIController.h"
#include "CoreMinimal.h"
#include "CrashCore/AIBrain.h"

#include "BCAIController.generated.h"

UCLASS()
class BIGCAR_API ABCAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABCAIController();
	virtual void Tick(float DeltaSeconds) override;
	void SetProfile(const FString& ProfileId);
	void SetForcedTarget(int32 ParticipantId) { ForcedTarget = ParticipantId; }

private:
	TUniquePtr<crash::AIBrain> Brain;
	float FeelerTimer = 0.0f;
	float ClearLeft = 1.0f;
	float ClearCenter = 1.0f;
	float ClearRight = 1.0f;
	int32 ForcedTarget = -1;
};
