// Copyright BigCar Team. All Rights Reserved.
//
// Server-authoritative damage (CrashCore DamageState) replicated to clients as a compact byte snapshot.
#pragma once

#include "Components/ActorComponent.h"
#include "Core/BigCarTypes.h"
#include "CoreMinimal.h"
#include "CrashCore/Damage.h"

#include "BCVehicleDamage.generated.h"

UCLASS(ClassGroup = (BigCar))
class BIGCAR_API UBCVehicleDamage : public UActorComponent
{
	GENERATED_BODY()

public:
	UBCVehicleDamage();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Setup(const crash::EffectiveStats& Stats);
	// Authority only.
	crash::DamageEvent ApplyDamage(const crash::ZoneDamage& Damage);
	crash::DamageEvent TickDamage(float DeltaSeconds, float RepairPerSec);
	void Repair(float Fraction);
	void RepairFull();

	const crash::DamageState& State() const { return DamageState; }
	float Integrity() const { return DamageState.Integrity(); }
	bool IsWrecked() const { return DamageState.IsWrecked(); }
	bool IsOnFire() const { return DamageState.IsOnFire(); }
	float LostFraction() const;

	FSimpleMulticastDelegate OnChanged;

private:
	void PushRep();

	UFUNCTION()
	void OnRep_Damage();

	UPROPERTY(ReplicatedUsing = OnRep_Damage)
	FBCDamageRep Rep;

	crash::DamageState DamageState;
	float RepTimer = 0.0f;
};
