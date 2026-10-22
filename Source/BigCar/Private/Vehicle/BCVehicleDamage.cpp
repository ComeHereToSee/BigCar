// Copyright BigCar Team. All Rights Reserved.

#include "Vehicle/BCVehicleDamage.h"

#include "GameFramework/Actor.h"
#include "Net/UnrealNetwork.h"

UBCVehicleDamage::UBCVehicleDamage()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(true);
}

void UBCVehicleDamage::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(UBCVehicleDamage, Rep);
}

void UBCVehicleDamage::Setup(const crash::EffectiveStats& Stats)
{
	DamageState.Init(Stats);
	PushRep();
	OnChanged.Broadcast();
}

crash::DamageEvent UBCVehicleDamage::ApplyDamage(const crash::ZoneDamage& Damage)
{
	const crash::DamageEvent Event = DamageState.Apply(Damage);
	if (Event.applied > 0.0f || Event.becameWrecked)
	{
		PushRep();
		OnChanged.Broadcast();
	}
	return Event;
}

crash::DamageEvent UBCVehicleDamage::TickDamage(float DeltaSeconds, float RepairPerSec)
{
	const crash::DamageEvent Event = DamageState.Tick(DeltaSeconds, RepairPerSec);
	RepTimer += DeltaSeconds;
	// Fire / repair change hp continuously; replicate at 4 Hz to save bandwidth.
	if (Event.becameWrecked || ((DamageState.IsOnFire() || RepairPerSec > 0.0f) && RepTimer > 0.25f))
	{
		RepTimer = 0.0f;
		PushRep();
		OnChanged.Broadcast();
	}
	return Event;
}

void UBCVehicleDamage::Repair(float Fraction)
{
	DamageState.Repair(Fraction);
	PushRep();
	OnChanged.Broadcast();
}

void UBCVehicleDamage::RepairFull()
{
	Repair(1.0f);
}

float UBCVehicleDamage::LostFraction() const
{
	const float Max = DamageState.TotalMax();
	return Max > 0.0f ? FMath::Clamp(DamageState.TotalLost() / Max, 0.0f, 1.0f) : 0.0f;
}

void UBCVehicleDamage::PushRep()
{
	const crash::DamageSnapshot S = DamageState.Snapshot();
	Rep.Values.SetNum(crash::kZoneCount + 1 + crash::kWheelCount);
	int32 i = 0;
	for (const float Z : S.zones)
	{
		Rep.Values[i++] = static_cast<uint8>(FMath::RoundToInt(Z * 255.0f));
	}
	Rep.Values[i++] = static_cast<uint8>(FMath::RoundToInt(S.engine * 255.0f));
	for (const float W : S.wheels)
	{
		Rep.Values[i++] = static_cast<uint8>(FMath::RoundToInt(W * 255.0f));
	}
	Rep.bWrecked = S.wrecked;
	Rep.bOnFire = S.onFire;
}

void UBCVehicleDamage::OnRep_Damage()
{
	if (Rep.Values.Num() != crash::kZoneCount + 1 + crash::kWheelCount)
	{
		return;
	}
	crash::DamageSnapshot S;
	int32 i = 0;
	for (float& Z : S.zones)
	{
		Z = Rep.Values[i++] / 255.0f;
	}
	S.engine = Rep.Values[i++] / 255.0f;
	for (float& W : S.wheels)
	{
		W = Rep.Values[i++] / 255.0f;
	}
	S.wrecked = Rep.bWrecked;
	S.onFire = Rep.bOnFire;
	DamageState.ApplySnapshot(S);
	OnChanged.Broadcast();
}
