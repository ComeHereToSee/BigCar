// Copyright BigCar Team. All Rights Reserved.

#include "Core/BigCarTypes.h"

DEFINE_LOG_CATEGORY(LogBigCar);

FBCControls FBCControls::FromCrash(const crash::VehicleControls& In, bool bHorn, bool bLookBack)
{
	FBCControls C;
	C.Throttle = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(In.throttle, 0.0f, 1.0f) * 255.0f));
	C.Brake = static_cast<uint8>(FMath::RoundToInt(FMath::Clamp(In.brake, 0.0f, 1.0f) * 255.0f));
	C.Steer = static_cast<int8>(FMath::RoundToInt(FMath::Clamp(In.steer, -1.0f, 1.0f) * 127.0f));
	C.Flags = (In.handbrake ? 1 : 0) | (In.boost ? 2 : 0) | (bHorn ? 4 : 0) | (bLookBack ? 8 : 0);
	return C;
}

crash::VehicleControls FBCControls::ToCrash() const
{
	crash::VehicleControls C;
	C.throttle = Throttle / 255.0f;
	C.brake = Brake / 255.0f;
	C.steer = Steer / 127.0f;
	C.handbrake = (Flags & 1) != 0;
	C.boost = (Flags & 2) != 0;
	return C;
}

crash::VehicleBuild FBCVehicleBuild::ToCrash() const
{
	crash::VehicleBuild B;
	B.vehicleId = BC::ToStd(VehicleId);
	for (int32 i = 0; i < Upgrades.Num() && i < crash::kUpgradeCategoryCount; ++i)
	{
		B.upgrades[static_cast<size_t>(i)] = FMath::Clamp<int32>(Upgrades[i], 0, crash::kMaxUpgradeLevel);
	}
	for (int32 i = 0; i < Equipment.Num() && i < crash::kEquipSlotCount; ++i)
	{
		B.equipment[static_cast<size_t>(i)] = BC::ToStd(Equipment[i]);
	}
	B.paint = BC::ToStd(Paint);
	B.wheels = BC::ToStd(Wheels);
	return B;
}

FBCVehicleBuild FBCVehicleBuild::FromCrash(const crash::VehicleBuild& Build)
{
	FBCVehicleBuild B;
	B.VehicleId = BC::ToFString(Build.vehicleId);
	for (const int Level : Build.upgrades)
	{
		B.Upgrades.Add(static_cast<uint8>(Level));
	}
	for (const std::string& Item : Build.equipment)
	{
		B.Equipment.Add(BC::ToFString(Item));
	}
	B.Paint = BC::ToFString(Build.paint);
	B.Wheels = BC::ToFString(Build.wheels);
	return B;
}
