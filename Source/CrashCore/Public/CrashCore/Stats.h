// Copyright BigCar Team. All Rights Reserved.
//
// Vehicle builds (base car + upgrades + equipment + cosmetics) and the effective stats they produce.
#pragma once

#include "CrashCore/Database.h"
#include "CrashCore/Export.h"

#include <array>
#include <string>
#include <vector>

namespace crash
{
struct VehicleBuild
{
	std::string vehicleId;
	std::array<int, kUpgradeCategoryCount> upgrades{};
	std::array<std::string, kEquipSlotCount> equipment;
	std::string paint;
	std::string wheels;

	int UpgradeLevel(UpgradeCategory c) const { return upgrades[static_cast<size_t>(c)]; }
	const std::string& Equipped(EquipSlot s) const { return equipment[static_cast<size_t>(s)]; }
	CRASHCORE_EXPORT int TotalUpgradeLevels() const;

	CRASHCORE_EXPORT JsonValue ToJson() const;
	static CRASHCORE_EXPORT bool FromJson(const JsonValue& json, VehicleBuild& out);
	CRASHCORE_EXPORT bool operator==(const VehicleBuild& o) const;
};

// A physical add-on that the game spawns on the car (real collision geometry).
struct Attachment
{
	EquipSlot slot = EquipSlot::Front;
	GeometryShape shape = GeometryShape::None;
	float length = 0.0f;
	float angleDeg = 0.0f;
	Color color;
};

struct EffectiveStats
{
	const VehicleDef* def = nullptr;

	// Physics
	float massKg = 1400.0f;
	float powerW = 100000.0f;
	float torqueNm = 200.0f;
	float topSpeed = 50.0f; // m/s
	float grip = 1.0f;
	float brakeDecel = 9.0f; // m/s^2
	float steerLockRad = 0.6f;
	float cgHeight = 0.5f;
	float dragCoefficient = 0.32f;
	float downforce = 0.0f;
	float suspensionFrequency = 1.5f;
	float suspensionDamping = 0.4f;
	float suspensionTravel = 0.15f;
	float antiRoll = 0.6f;
	float shiftTime = 0.2f;

	// Durability
	std::array<float, kZoneCount> zoneHp{};
	float engineHp = 500.0f;
	float wheelHp = 200.0f;
	std::array<float, kZoneCount> armor{};

	// Combat
	float ramPower = 1.0f;
	float sideDamage = 1.0f;
	float rearDamage = 1.0f;
	float selfDamage = 1.0f;
	float launchPower = 0.0f;
	float tireDamage = 0.0f;
	float knockback = 0.0f;
	float repairPerSec = 0.0f;

	// Boost / ability
	float boostCapacity = 100.0f;
	float boostAccel = 6.0f;
	float boostRegen = 6.0f;
	AbilityType ability = AbilityType::None;

	std::vector<Attachment> attachments;
	int combatRating = 100;

	CRASHCORE_EXPORT float TotalHp() const;
	CRASHCORE_EXPORT bool HasShape(GeometryShape shape) const;
};

// Values 0..10 shown as bars in the garage.
struct DisplayRatings
{
	float speed = 0.0f;
	float acceleration = 0.0f;
	float handling = 0.0f;
	float weight = 0.0f;
	float durability = 0.0f;
	float ramming = 0.0f;
};

CRASHCORE_EXPORT EffectiveStats ComputeEffectiveStats(const GameDatabase& db, const VehicleBuild& build);
CRASHCORE_EXPORT int ComputeCombatRating(const GameDatabase& db, const VehicleBuild& build);
CRASHCORE_EXPORT DisplayRatings ComputeDisplayRatings(const EffectiveStats& stats);
// Zone hit points before upgrades/equipment (docs/DATA_SCHEMA.md formula).
CRASHCORE_EXPORT float BaseZoneHp(const VehicleDef& def, Zone zone);

// Returns an empty string if the build is legal, otherwise an error code
// (unknown_vehicle, invalid_level, unknown_item, invalid_slot, class_not_allowed).
CRASHCORE_EXPORT std::string ValidateBuild(const GameDatabase& db, const VehicleBuild& build);
// Default build for a vehicle (no upgrades, factory paint).
CRASHCORE_EXPORT VehicleBuild MakeDefaultBuild(const GameDatabase& db, const std::string& vehicleId);

} // namespace crash
