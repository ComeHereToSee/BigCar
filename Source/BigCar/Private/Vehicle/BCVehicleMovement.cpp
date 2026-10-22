// Copyright BigCar Team. All Rights Reserved.

#include "Vehicle/BCVehicleMovement.h"

#include "Components/PrimitiveComponent.h"
#include "Core/BigCarTypes.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

UBCVehicleMovement::UBCVehicleMovement()
{
	PrimaryComponentTick.bCanEverTick = false; // stepped by the pawn in TG_PrePhysics
}

void UBCVehicleMovement::Setup(const crash::EffectiveStats& Stats, UPrimitiveComponent* InBody)
{
	Body = InBody;
	Sim.Setup(crash::MakeSimSetup(Stats));
	AbilityStateData.Setup(Stats.ability);
	ResetState();
}

void UBCVehicleMovement::ResetState()
{
	Sim.Reset();
	AbilityStateData.Reset();
	Output = crash::SimOutput();
	Controls = crash::VehicleControls();
	Airtime = 0.0f;
	if (Body)
	{
		LastLocation = Body->GetComponentLocation();
	}
}

bool UBCVehicleMovement::IsUpsideDown() const
{
	return Body && crash::VehicleSimulator::IsUpsideDown(BC::FrameFromQuat(Body->GetComponentQuat()));
}

void UBCVehicleMovement::StepVehicle(float DeltaSeconds, bool bApplyForces)
{
	if (!Body || DeltaSeconds <= 0.0f)
	{
		return;
	}
	UWorld* World = GetWorld();
	const FTransform Xf = Body->GetComponentTransform();
	crash::SimBodyState State;
	State.position = BC::ToCrash(Xf.GetLocation());
	State.frame = BC::FrameFromQuat(Xf.GetRotation());
	State.linearVelocity = BC::ToCrash(Body->GetPhysicsLinearVelocity());
	State.angularVelocity = BC::DirToCrash(Body->GetPhysicsAngularVelocityInRadians());

	// One ray per wheel from the suspension mount down the car's -up axis.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BCWheelTrace), false, GetOwner());
	Params.bReturnPhysicalMaterial = false;
	std::array<crash::WheelContact, crash::kWheelCount> Contacts{};
	const float RayLengthCm = Sim.RayLength() * BC::CmPerM;
	const FVector Down = -Xf.GetRotation().GetUpVector();
	for (int32 i = 0; i < crash::kWheelCount; ++i)
	{
		const FVector Mount = BC::ToUE(Sim.MountWorld(i, State));
		FHitResult Hit;
		if (World->LineTraceSingleByChannel(Hit, Mount, Mount + Down * RayLengthCm, ECC_Visibility, Params))
		{
			crash::WheelContact& C = Contacts[static_cast<size_t>(i)];
			C.hit = true;
			C.distance = Hit.Distance / BC::CmPerM;
			C.point = BC::ToCrash(Hit.ImpactPoint);
			C.normal = BC::DirToCrash(Hit.ImpactNormal);
			C.pointVelocity = BC::ToCrash(Body->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint));
			// Driving on top of another moving car: use relative velocity so tires grip the roof.
			if (const UPrimitiveComponent* Other = Hit.GetComponent())
			{
				if (Other->IsSimulatingPhysics())
				{
					C.pointVelocity = C.pointVelocity - BC::ToCrash(Other->GetPhysicsLinearVelocityAtPoint(Hit.ImpactPoint));
				}
			}
			C.friction = SurfaceFriction;
		}
	}

	const crash::AbilityEffects Fx = AbilityStateData.Effects();
	Sim.SetDriveMultiplier(Fx.driveMultiplier);
	Sim.SetBoostMultiplier(Fx.boostMultiplier);
	Sim.Step(DeltaSeconds, Controls, State, Contacts, Output);
	AbilityStateData.Tick(DeltaSeconds);

	if (bApplyForces && Body->IsSimulatingPhysics())
	{
		for (int32 i = 0; i < crash::kWheelCount; ++i)
		{
			const size_t Idx = static_cast<size_t>(i);
			if (!Output.suspension[Idx].force.IsNearlyZero(1.0f))
			{
				Body->AddForceAtLocation(BC::ForceToUE(Output.suspension[Idx].force), BC::ToUE(Output.suspension[Idx].point));
			}
			if (!Output.tire[Idx].force.IsNearlyZero(1.0f))
			{
				Body->AddForceAtLocation(BC::ForceToUE(Output.tire[Idx].force), BC::ToUE(Output.tire[Idx].point));
			}
		}
		if (!Output.centralForce.IsNearlyZero(1.0f))
		{
			Body->AddForce(BC::ForceToUE(Output.centralForce));
		}
		if (!Output.angularAcceleration.IsNearlyZero(1.0e-4f))
		{
			// Mass independent: rad/s^2 (Unreal: bAccelChange = true).
			Body->AddTorqueInRadians(BC::DirToUE(Output.angularAcceleration), NAME_None, true);
		}
	}

	// Airtime tracking.
	if (Output.groundedWheels == 0)
	{
		Airtime += DeltaSeconds;
	}
	else
	{
		if (Airtime > 0.0f && OnLanded)
		{
			OnLanded(Airtime);
		}
		Airtime = 0.0f;
	}
	const FVector Loc = Xf.GetLocation();
	Distance += FVector::Dist2D(Loc, LastLocation);
	LastLocation = Loc;
}

FVector UBCVehicleMovement::WheelMountLocal(int32 Wheel) const
{
	return BC::ToUE(Sim.GetSetup().wheels[static_cast<size_t>(Wheel)].mountLocal);
}

float UBCVehicleMovement::WheelDropCm(int32 Wheel) const
{
	return Sim.WheelCenterDrop(Wheel) * BC::CmPerM;
}

float UBCVehicleMovement::WheelSpin(int32 Wheel) const
{
	return Sim.GetWheel(Wheel).spinAngle;
}

float UBCVehicleMovement::WheelSteer(int32 Wheel) const
{
	return Sim.GetWheel(Wheel).steerAngle;
}

float UBCVehicleMovement::WheelRadiusCm() const
{
	return Sim.GetSetup().wheelRadius * BC::CmPerM;
}
