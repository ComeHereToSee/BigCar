// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Stats.h"

#include <cmath>

namespace crash
{
namespace
{
constexpr float kZoneWeights[kZoneCount] = {1.0f, 0.9f, 0.8f, 0.8f, 0.7f, 0.8f};
constexpr float kEngineWeight = 0.7f;
constexpr float kWheelWeight = 0.35f;

float HpBase(const VehicleDef& def)
{
	return def.toughness * (400.0f + 0.35f * def.massKg);
}
} // namespace

int VehicleBuild::TotalUpgradeLevels() const
{
	int total = 0;
	for (const int level : upgrades)
	{
		total += level;
	}
	return total;
}

JsonValue VehicleBuild::ToJson() const
{
	JsonValue v = JsonValue::MakeObject();
	v.Set("vehicleId", JsonValue(vehicleId));
	JsonValue up = JsonValue::MakeObject();
	for (int i = 0; i < kUpgradeCategoryCount; ++i)
	{
		if (upgrades[static_cast<size_t>(i)] > 0)
		{
			up.Set(ToString(static_cast<UpgradeCategory>(i)), JsonValue(upgrades[static_cast<size_t>(i)]));
		}
	}
	v.Set("upgrades", std::move(up));
	JsonValue eq = JsonValue::MakeObject();
	for (int i = 0; i < kEquipSlotCount; ++i)
	{
		if (!equipment[static_cast<size_t>(i)].empty())
		{
			eq.Set(ToString(static_cast<EquipSlot>(i)), JsonValue(equipment[static_cast<size_t>(i)]));
		}
	}
	v.Set("equipment", std::move(eq));
	v.Set("paint", paint.empty() ? JsonValue() : JsonValue(paint));
	v.Set("wheels", wheels.empty() ? JsonValue() : JsonValue(wheels));
	return v;
}

bool VehicleBuild::FromJson(const JsonValue& json, VehicleBuild& out)
{
	if (!json.IsObject() || !json["vehicleId"].IsString())
	{
		return false;
	}
	out = VehicleBuild();
	out.vehicleId = json["vehicleId"].AsString();
	for (const auto& [key, value] : json["upgrades"].Members())
	{
		UpgradeCategory cat;
		if (FromString(key, cat))
		{
			out.upgrades[static_cast<size_t>(cat)] = std::clamp(value.AsInt(0), 0, kMaxUpgradeLevel);
		}
	}
	for (const auto& [key, value] : json["equipment"].Members())
	{
		EquipSlot slot;
		if (FromString(key, slot) && value.IsString())
		{
			out.equipment[static_cast<size_t>(slot)] = value.AsString();
		}
	}
	out.paint = json["paint"].AsString("");
	out.wheels = json["wheels"].AsString("");
	return true;
}

bool VehicleBuild::operator==(const VehicleBuild& o) const
{
	return vehicleId == o.vehicleId && upgrades == o.upgrades && equipment == o.equipment && paint == o.paint && wheels == o.wheels;
}

float EffectiveStats::TotalHp() const
{
	float total = engineHp;
	for (const float hp : zoneHp)
	{
		total += hp;
	}
	return total;
}

bool EffectiveStats::HasShape(GeometryShape shape) const
{
	for (const Attachment& a : attachments)
	{
		if (a.shape == shape)
		{
			return true;
		}
	}
	return false;
}

float BaseZoneHp(const VehicleDef& def, Zone zone)
{
	return HpBase(def) * kZoneWeights[static_cast<size_t>(zone)];
}

EffectiveStats ComputeEffectiveStats(const GameDatabase& db, const VehicleBuild& build)
{
	EffectiveStats s;
	const VehicleDef* def = db.FindVehicle(build.vehicleId);
	if (!def)
	{
		return s;
	}
	s.def = def;

	StatMods mods;
	for (int i = 0; i < kUpgradeCategoryCount; ++i)
	{
		const int level = std::clamp(build.upgrades[static_cast<size_t>(i)], 0, kMaxUpgradeLevel);
		const UpgradeCategoryDef& cat = db.Upgrade(static_cast<UpgradeCategory>(i));
		if (level > 0 && static_cast<size_t>(level) <= cat.levels.size())
		{
			mods.Combine(cat.levels[static_cast<size_t>(level - 1)].mods);
		}
	}
	for (int i = 0; i < kEquipSlotCount; ++i)
	{
		const std::string& itemId = build.equipment[static_cast<size_t>(i)];
		if (itemId.empty())
		{
			continue;
		}
		const EquipmentDef* item = db.FindEquipment(itemId);
		if (!item || static_cast<int>(item->slot) != i)
		{
			continue;
		}
		mods.Combine(item->mods);
		if (item->ability != AbilityType::None)
		{
			s.ability = item->ability;
		}
		if (item->geometry.shape != GeometryShape::None)
		{
			Attachment a;
			a.slot = item->slot;
			a.shape = item->geometry.shape;
			a.length = item->geometry.length;
			a.angleDeg = item->geometry.angleDeg;
			a.color = item->geometry.color;
			s.attachments.push_back(a);
		}
	}

	s.massKg = std::max(300.0f, def->massKg * mods.massMul + mods.massAddKg);
	s.powerW = def->powerKw * 1000.0f * mods.powerMul;
	s.torqueNm = def->torqueNm * mods.torqueMul;
	s.topSpeed = def->topSpeedKmh / 3.6f * mods.topSpeedMul;
	s.grip = def->grip * mods.gripMul;
	s.brakeDecel = def->brakeG * kGravity * mods.brakeMul;
	s.steerLockRad = def->steerLockDeg * kDegToRad * mods.steerMul;
	s.cgHeight = std::max(def->dimensions.groundClearance + 0.12f, def->cgHeight + mods.cgHeightAdd);
	s.dragCoefficient = def->dragCoefficient * mods.dragMul;
	s.downforce = def->downforce * mods.downforceMul;
	s.suspensionFrequency = Clamp(def->suspensionFrequency * mods.suspensionMul, 0.6f, 4.0f);
	s.suspensionDamping = def->suspensionDamping;
	s.suspensionTravel = def->suspensionTravel;
	s.antiRoll = def->antiRoll * mods.suspensionMul;
	s.shiftTime = std::max(0.06f, 0.25f - 0.035f * static_cast<float>(build.UpgradeLevel(UpgradeCategory::Transmission)));

	const float hpBase = HpBase(*def) * mods.toughnessMul;
	for (int z = 0; z < kZoneCount; ++z)
	{
		s.zoneHp[static_cast<size_t>(z)] = hpBase * kZoneWeights[z];
		s.armor[static_cast<size_t>(z)] = Clamp(def->armor[static_cast<size_t>(z)] + mods.armor[static_cast<size_t>(z)], 0.0f, 0.75f);
	}
	s.engineHp = hpBase * kEngineWeight * mods.engineHpMul;
	s.wheelHp = hpBase * kWheelWeight * mods.wheelHpMul;

	s.ramPower = def->ramPower * mods.ramPowerMul;
	s.sideDamage = mods.sideDamageMul;
	s.rearDamage = mods.rearDamageMul;
	s.selfDamage = mods.selfDamageMul;
	s.launchPower = mods.launchPower;
	s.tireDamage = mods.tireDamage;
	s.knockback = mods.knockback;
	s.repairPerSec = mods.repairPerSec;

	s.boostCapacity = std::max(0.0f, def->boostCapacity + mods.boostCapacityAdd);
	s.boostAccel = def->boostAccel * mods.boostAccelMul;
	s.boostRegen = def->boostRegenPerSec * mods.boostRegenMul;

	s.combatRating = ComputeCombatRating(db, build);
	return s;
}

int ComputeCombatRating(const GameDatabase& db, const VehicleBuild& build)
{
	const VehicleDef* def = db.FindVehicle(build.vehicleId);
	if (!def)
	{
		return 0;
	}
	int cr = 100 * def->tier + 10 * build.TotalUpgradeLevels();
	for (int i = 0; i < kEquipSlotCount; ++i)
	{
		const std::string& itemId = build.equipment[static_cast<size_t>(i)];
		if (const EquipmentDef* item = itemId.empty() ? nullptr : db.FindEquipment(itemId))
		{
			cr += item->cr;
		}
	}
	return cr;
}

DisplayRatings ComputeDisplayRatings(const EffectiveStats& s)
{
	DisplayRatings r;
	r.speed = RemapClamped(s.topSpeed, 40.0f, 115.0f, 1.0f, 10.0f);
	const float powerToWeight = s.powerW / std::max(1.0f, s.massKg);
	r.acceleration = 1.0f + 9.0f * std::sqrt(Clamp01((powerToWeight - 40.0f) / 760.0f));
	const float massFactor = std::sqrt(1500.0f / std::max(500.0f, s.massKg));
	r.handling = RemapClamped(s.grip * massFactor * (0.7f + 0.3f * s.steerLockRad / (35.0f * kDegToRad)), 0.5f, 1.6f, 1.0f, 10.0f);
	r.weight = RemapClamped(std::log(std::max(1.0f, s.massKg)), std::log(900.0f), std::log(9000.0f), 1.0f, 10.0f);
	float avgArmor = 0.0f;
	for (const float a : s.armor)
	{
		avgArmor += a / static_cast<float>(kZoneCount);
	}
	r.durability = RemapClamped(s.TotalHp() * (1.0f + 2.0f * avgArmor), 2500.0f, 20000.0f, 1.0f, 10.0f);
	const float ram = s.ramPower * std::sqrt(s.massKg / 1500.0f) * (1.0f + 0.25f * s.launchPower) + 0.2f * s.knockback;
	r.ramming = RemapClamped(ram, 0.5f, 3.0f, 1.0f, 10.0f);
	return r;
}

std::string ValidateBuild(const GameDatabase& db, const VehicleBuild& build)
{
	const VehicleDef* def = db.FindVehicle(build.vehicleId);
	if (!def)
	{
		return "unknown_vehicle";
	}
	for (const int level : build.upgrades)
	{
		if (level < 0 || level > kMaxUpgradeLevel)
		{
			return "invalid_level";
		}
	}
	for (int i = 0; i < kEquipSlotCount; ++i)
	{
		const std::string& itemId = build.equipment[static_cast<size_t>(i)];
		if (itemId.empty())
		{
			continue;
		}
		const EquipmentDef* item = db.FindEquipment(itemId);
		if (!item)
		{
			return "unknown_item";
		}
		if (static_cast<int>(item->slot) != i)
		{
			return "invalid_slot";
		}
		if (!item->AllowsClass(def->vehicleClass))
		{
			return "class_not_allowed";
		}
	}
	if (!build.paint.empty())
	{
		const CosmeticDef* paint = db.FindCosmetic(build.paint);
		if (!paint || paint->type != ItemType::Paint)
		{
			return "unknown_item";
		}
	}
	if (!build.wheels.empty())
	{
		const CosmeticDef* wheels = db.FindCosmetic(build.wheels);
		if (!wheels || wheels->type != ItemType::Wheels)
		{
			return "unknown_item";
		}
	}
	return std::string();
}

VehicleBuild MakeDefaultBuild(const GameDatabase& db, const std::string& vehicleId)
{
	VehicleBuild build;
	build.vehicleId = vehicleId;
	(void)db;
	return build;
}

} // namespace crash
