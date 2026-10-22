// Copyright BigCar Team. All Rights Reserved.
//
// Shared enums and their stable string ids (the ids are part of the data/save/network contract).
#pragma once

#include "CrashCore/Export.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace crash
{
enum class VehicleClass : uint8_t
{
	Compact,
	Hatchback,
	Sedan,
	Wagon,
	Coupe,
	Muscle,
	Sports,
	Supercar,
	Hypercar,
	Luxury,
	Suv,
	Pickup,
	Offroad,
	Van,
	Truck,
	Count
};

enum class Drivetrain : uint8_t
{
	FWD,
	RWD,
	AWD,
	Count
};

enum class EngineLayout : uint8_t
{
	Front,
	Mid,
	Rear,
	Count
};

// Damage zones of a vehicle body (local frame: front = +X, right = +Y, top = +Z).
enum class Zone : uint8_t
{
	Front,
	Rear,
	Left,
	Right,
	Top,
	Bottom,
	Count
};
constexpr int kZoneCount = static_cast<int>(Zone::Count);
constexpr int kWheelCount = 4; // FL, FR, RL, RR

enum class EquipSlot : uint8_t
{
	Front,
	Rear,
	Side,
	Roof,
	Wheels,
	Core,
	Ability,
	Count
};
constexpr int kEquipSlotCount = static_cast<int>(EquipSlot::Count);

enum class AbilityType : uint8_t
{
	None,
	NitroBurst,
	HydraulicJump,
	RamCharge,
	Brace,
	FieldRepair,
	ShockBumper,
	Count
};

enum class GeometryShape : uint8_t
{
	None,
	RamBar,
	Wedge,
	Plow,
	Spikes,
	Cage,
	SidePlates,
	SideBlades,
	RearBumper,
	RearSpikes,
	RoofArmor,
	WheelSpikes,
	Ballast,
	Count
};

enum class Rarity : uint8_t
{
	Common,
	Rare,
	Epic,
	Legendary,
	Count
};

enum class UpgradeCategory : uint8_t
{
	Engine,
	Transmission,
	Suspension,
	Tires,
	Brakes,
	Chassis,
	Armor,
	Weight,
	Nitro,
	Count
};
constexpr int kUpgradeCategoryCount = static_cast<int>(UpgradeCategory::Count);
constexpr int kMaxUpgradeLevel = 5;

enum class ImpactKind : uint8_t
{
	None,
	HeadOn,
	TBone,
	SideSwipe,
	RearEnd,
	Launch,
	Crush,
	PIT,
	Glance,
	Count
};
constexpr int kImpactKindCount = static_cast<int>(ImpactKind::Count);

enum class Currency : uint8_t
{
	Credits,
	Gold,
	Count
};

enum class ItemType : uint8_t
{
	Vehicle,
	Equipment,
	Paint,
	Wheels,
	Sparks,
	Horn,
	Title,
	Banner,
	Currency,
	Count
};

enum class WinCondition : uint8_t
{
	LastStanding,
	Score,
	Takedowns,
	HillTime,
	Survive,
	Objective,
	Count
};

enum class ObjectiveType : uint8_t
{
	Win,
	Takedowns,
	TimeUnder,
	NoWreck,
	DamageUnder,
	Score,
	HitsOfKind,
	Survive,
	DestroyProps,
	HillTime,
	Collect,
	Boss,
	Count
};

enum class ArenaShape : uint8_t
{
	Rect,
	Circle,
	Octagon,
	Count
};

enum class PropType : uint8_t
{
	Ramp,
	Block,
	Cylinder,
	JumpPad,
	BoostPad,
	Spinner,
	Crusher,
	Pit,
	CrateStack,
	Barrel,
	Wreck,
	Container,
	Fence,
	HillZone,
	Light,
	Count
};

// ---------------------------------------------------------------------------------------------
// String ids. Each table lists the ids in enum order (excluding Count).

template <typename E> struct EnumNames;

#define CRASH_ENUM_NAMES(EnumType, ...)                                                                                                    \
	template <> struct EnumNames<EnumType>                                                                                                 \
	{                                                                                                                                      \
		static constexpr std::array<std::string_view, static_cast<size_t>(EnumType::Count)> names = {__VA_ARGS__};                         \
	};

CRASH_ENUM_NAMES(VehicleClass, "compact", "hatchback", "sedan", "wagon", "coupe", "muscle", "sports", "supercar", "hypercar", "luxury",
                 "suv", "pickup", "offroad", "van", "truck")
CRASH_ENUM_NAMES(Drivetrain, "fwd", "rwd", "awd")
CRASH_ENUM_NAMES(EngineLayout, "front", "mid", "rear")
CRASH_ENUM_NAMES(Zone, "front", "rear", "left", "right", "top", "bottom")
CRASH_ENUM_NAMES(EquipSlot, "front", "rear", "side", "roof", "wheels", "core", "ability")
CRASH_ENUM_NAMES(AbilityType, "none", "nitro_burst", "hydraulic_jump", "ram_charge", "brace", "field_repair", "shock_bumper")
CRASH_ENUM_NAMES(GeometryShape, "none", "ram_bar", "wedge", "plow", "spikes", "cage", "side_plates", "side_blades", "rear_bumper",
                 "rear_spikes", "roof_armor", "wheel_spikes", "ballast")
CRASH_ENUM_NAMES(Rarity, "common", "rare", "epic", "legendary")
CRASH_ENUM_NAMES(UpgradeCategory, "engine", "transmission", "suspension", "tires", "brakes", "chassis", "armor", "weight", "nitro")
CRASH_ENUM_NAMES(ImpactKind, "none", "head_on", "t_bone", "side_swipe", "rear_end", "launch", "crush", "pit", "glance")
CRASH_ENUM_NAMES(Currency, "credits", "gold")
CRASH_ENUM_NAMES(ItemType, "vehicle", "equipment", "paint", "wheels", "sparks", "horn", "title", "banner", "currency")
CRASH_ENUM_NAMES(WinCondition, "last_standing", "score", "takedowns", "hill_time", "survive", "objective")
CRASH_ENUM_NAMES(ObjectiveType, "win", "takedowns", "time_under", "no_wreck", "damage_under", "score", "hits_of_kind", "survive",
                 "destroy_props", "hill_time", "collect", "boss")
CRASH_ENUM_NAMES(ArenaShape, "rect", "circle", "octagon")
CRASH_ENUM_NAMES(PropType, "ramp", "block", "cylinder", "jump_pad", "boost_pad", "spinner", "crusher", "pit", "crate_stack", "barrel",
                 "wreck", "container", "fence", "hill_zone", "light")

#undef CRASH_ENUM_NAMES

template <typename E> constexpr std::string_view ToString(E value)
{
	const size_t index = static_cast<size_t>(value);
	return index < EnumNames<E>::names.size() ? EnumNames<E>::names[index] : std::string_view("unknown");
}

template <typename E> bool FromString(std::string_view text, E& out)
{
	const auto& names = EnumNames<E>::names;
	for (size_t i = 0; i < names.size(); ++i)
	{
		if (names[i] == text)
		{
			out = static_cast<E>(i);
			return true;
		}
	}
	return false;
}

template <typename E> constexpr int ToIndex(E value)
{
	return static_cast<int>(value);
}

// The engine zone(s) that host the engine for each layout, as weights over zones.
struct EngineZoneWeights
{
	std::array<float, kZoneCount> weights{};
};
CRASHCORE_EXPORT EngineZoneWeights GetEngineZoneWeights(EngineLayout layout);

// Score/stat key for an impact kind (same as ToString).
inline std::string ImpactKindId(ImpactKind kind)
{
	return std::string(ToString(kind));
}

} // namespace crash
