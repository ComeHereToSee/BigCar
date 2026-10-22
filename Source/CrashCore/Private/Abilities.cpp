// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Abilities.h"

#include <algorithm>

namespace crash
{
AbilityTuning GetAbilityTuning(AbilityType type)
{
	switch (type)
	{
	case AbilityType::NitroBurst:
		return {14.0f, 2.0f};
	case AbilityType::HydraulicJump:
		return {8.0f, 0.0f};
	case AbilityType::RamCharge:
		return {12.0f, 2.5f};
	case AbilityType::Brace:
		return {15.0f, 3.0f};
	case AbilityType::FieldRepair:
		return {30.0f, 5.0f};
	case AbilityType::ShockBumper:
		return {10.0f, 3.0f};
	default:
		return {0.0f, 0.0f};
	}
}

void AbilityState::Setup(AbilityType type)
{
	type_ = type;
	Reset();
}

void AbilityState::Reset()
{
	cooldown_ = 0.0f;
	active_ = 0.0f;
}

AbilityTrigger AbilityState::TryActivate()
{
	AbilityTrigger t;
	if (!Ready())
	{
		return t;
	}
	const AbilityTuning tuning = GetAbilityTuning(type_);
	cooldown_ = tuning.cooldown;
	active_ = tuning.duration;
	t.activated = true;
	if (type_ == AbilityType::HydraulicJump)
	{
		t.jumpVelocity = 6.5f;
	}
	else if (type_ == AbilityType::NitroBurst)
	{
		t.addBoost = 50.0f;
	}
	return t;
}

void AbilityState::Tick(float dt)
{
	cooldown_ = std::max(0.0f, cooldown_ - dt);
	active_ = std::max(0.0f, active_ - dt);
}

void AbilityState::ConsumeShock()
{
	if (type_ == AbilityType::ShockBumper)
	{
		active_ = 0.0f;
	}
}

AbilityEffects AbilityState::Effects() const
{
	AbilityEffects e;
	if (!Active())
	{
		return e;
	}
	switch (type_)
	{
	case AbilityType::NitroBurst:
		e.boostMultiplier = 1.4f;
		break;
	case AbilityType::RamCharge:
		e.ramCharge = true;
		e.driveMultiplier = 1.3f;
		break;
	case AbilityType::Brace:
		e.brace = true;
		break;
	case AbilityType::FieldRepair:
		e.repairPerSec = 0.25f / GetAbilityTuning(type_).duration;
		break;
	case AbilityType::ShockBumper:
		e.shockArmed = true;
		break;
	default:
		break;
	}
	return e;
}

float AbilityState::CooldownFraction() const
{
	const float cd = GetAbilityTuning(type_).cooldown;
	return cd > 0.0f ? std::clamp(cooldown_ / cd, 0.0f, 1.0f) : 0.0f;
}

} // namespace crash
