// Copyright BigCar Team. All Rights Reserved.
//
// Active abilities granted by ability-slot equipment. All are melee / self buffs (no ranged effects by design).
#pragma once

#include "CrashCore/Types.h"

namespace crash
{
struct AbilityTuning
{
	float cooldown = 10.0f;
	float duration = 0.0f;
};

AbilityTuning GetAbilityTuning(AbilityType type);

// Continuous effects while an ability is active.
struct AbilityEffects
{
	bool ramCharge = false;
	bool brace = false;
	bool shockArmed = false;
	float repairPerSec = 0.0f; // fraction of max hp per second
	float driveMultiplier = 1.0f;
	float boostMultiplier = 1.0f;
};

// One-shot effects at activation.
struct AbilityTrigger
{
	bool activated = false;
	float jumpVelocity = 0.0f; // m/s upward
	float addBoost = 0.0f;     // boost units
};

class AbilityState
{
public:
	void Setup(AbilityType type);
	AbilityTrigger TryActivate();
	void Tick(float dt);
	void ConsumeShock(); // the armed shock bumper fired on contact
	void Reset();

	AbilityEffects Effects() const;
	AbilityType Type() const { return type_; }
	bool Ready() const { return type_ != AbilityType::None && cooldown_ <= 0.0f; }
	bool Active() const { return active_ > 0.0f; }
	float CooldownRemaining() const { return cooldown_; }
	float CooldownFraction() const; // 1 = just used, 0 = ready

private:
	AbilityType type_ = AbilityType::None;
	float cooldown_ = 0.0f;
	float active_ = 0.0f;
};

} // namespace crash
