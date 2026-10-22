// Copyright BigCar Team. All Rights Reserved.
//
// Drives the vehicle's Chaos rigid body with CrashCore's VehicleSimulator: one ray per wheel, suspension + tire
// forces applied at the contact points, aero/boost at the center of mass, air control as angular acceleration.
// Runs on every machine (server authoritative; clients simulate with replicated controls and get corrected).
#pragma once

#include "Components/ActorComponent.h"
#include "CoreMinimal.h"
#include "CrashCore/Abilities.h"
#include "CrashCore/VehicleSim.h"

#include "BCVehicleMovement.generated.h"

class UPrimitiveComponent;

UCLASS(ClassGroup = (BigCar))
class BIGCAR_API UBCVehicleMovement : public UActorComponent
{
	GENERATED_BODY()

public:
	UBCVehicleMovement();

	void Setup(const crash::EffectiveStats& Stats, UPrimitiveComponent* InBody);
	bool IsSetUp() const { return Body != nullptr; }
	// Called by the pawn every frame before physics.
	void StepVehicle(float DeltaSeconds, bool bApplyForces);
	void ResetState();

	void SetControls(const crash::VehicleControls& InControls) { Controls = InControls; }
	const crash::VehicleControls& GetControls() const { return Controls; }
	void SetDrivingDamage(const crash::DrivingDamage& Damage) { Sim.SetDamage(Damage); }
	void SetAssists(const crash::DrivingAssists& Assists) { Sim.SetAssists(Assists); }
	void SetSurfaceFriction(float Friction) { SurfaceFriction = Friction; }

	const crash::VehicleSimulator& GetSim() const { return Sim; }
	crash::VehicleSimulator& GetSimMutable() { return Sim; }
	const crash::SimOutput& LastOutput() const { return Output; }
	crash::AbilityState& Ability() { return AbilityStateData; }
	const crash::AbilityState& Ability() const { return AbilityStateData; }

	float SpeedKmh() const { return FMath::Abs(Output.forwardSpeed) * 3.6f; }
	float ForwardSpeed() const { return Output.forwardSpeed; }
	int32 Gear() const { return Output.gear; }
	float Rpm() const { return Output.engineRpm; }
	float BoostFraction() const { return Sim.BoostFraction(); }
	bool IsBoosting() const { return Output.boosting; }
	int32 GroundedWheels() const { return Output.groundedWheels; }
	bool IsUpsideDown() const;
	float CurrentAirtime() const { return Airtime; }
	float MaxSlip() const { return Output.maxSlip; }
	float DistanceKm() const { return Distance / 100000.0f; }

	// Wheel visuals (local space, cm / radians).
	FVector WheelMountLocal(int32 Wheel) const;
	float WheelDropCm(int32 Wheel) const;
	float WheelSpin(int32 Wheel) const;
	float WheelSteer(int32 Wheel) const;
	float WheelRadiusCm() const;

	// Fired when the car lands after a jump (seconds airborne).
	TFunction<void(float)> OnLanded;

private:
	UPROPERTY()
	TObjectPtr<UPrimitiveComponent> Body;

	crash::VehicleSimulator Sim;
	crash::SimOutput Output;
	crash::VehicleControls Controls;
	crash::AbilityState AbilityStateData;
	float SurfaceFriction = 1.0f;
	float Airtime = 0.0f;
	float Distance = 0.0f;
	FVector LastLocation = FVector::ZeroVector;
};
