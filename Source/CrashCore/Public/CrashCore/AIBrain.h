// Copyright BigCar Team. All Rights Reserved.
//
// Crash-focused driving AI. Picks targets, plans T-bone / flank approaches with intercept prediction, uses boost and
// abilities, evades when hurt, avoids ring-out edges, contests the hill and recovers when stuck.
// Pure function of a perception snapshot + internal memory, so it runs identically in Unreal and in the headless sim.
#pragma once

#include "CrashCore/Database.h"
#include "CrashCore/Export.h"
#include "CrashCore/Random.h"
#include "CrashCore/VehicleSim.h"

#include <vector>

namespace crash
{
struct AICarInfo
{
	int id = -1;
	int team = -1;
	Vec3 position;
	Vec3 velocity;
	Vec3 forward = Vec3::Forward();
	Vec3 right = Vec3::Right();
	float health = 1.0f; // integrity 0..1
	float mass = 1500.0f;
	bool wrecked = false;
	bool isHuman = false;
	bool isBounty = false;
};

struct AIArenaInfo
{
	Vec3 center;
	float halfX = 50.0f;
	float halfY = 50.0f;
	bool circular = false;
	bool ringOut = false; // falling off the edge eliminates
	bool hasHill = false;
	Vec3 hillCenter;
	float hillRadius = 10.0f;
	bool hillOwnedByMyTeam = false;
	// Optional obstacle probe results (0..1, 1 = clear) for left / center / right feelers.
	float clearLeft = 1.0f;
	float clearCenter = 1.0f;
	float clearRight = 1.0f;
};

struct AIPerception
{
	float time = 0.0f;
	AICarInfo self;
	float boostFraction = 1.0f;
	bool abilityReady = false;
	AbilityType ability = AbilityType::None;
	bool upsideDown = false;
	std::vector<AICarInfo> others;
	AIArenaInfo arena;
	int forcedTarget = -1; // campaign scripts may force a target
};

enum class AIState : uint8_t
{
	Hunt,
	Ram,
	Evade,
	Recover,
	HoldHill,
	ReturnToArena,
	Wander
};

struct AIOutput
{
	VehicleControls controls;
	bool useAbility = false;
	bool requestReset = false;
	int targetId = -1;
	AIState state = AIState::Wander;
	Vec3 aimPoint;
};

class AIBrain
{
public:
	AIBrain() = default;
	CRASHCORE_EXPORT AIBrain(const AIProfileDef& profile, uint64_t seed);

	CRASHCORE_EXPORT AIOutput Think(const AIPerception& perception, float dt);
	const AIProfileDef& Profile() const { return profile_; }

private:
	CRASHCORE_EXPORT int SelectTarget(const AIPerception& p);
	CRASHCORE_EXPORT Vec3 PlanAimPoint(const AIPerception& p, const AICarInfo& target) const;
	CRASHCORE_EXPORT VehicleControls SteerTowards(const AIPerception& p, const Vec3& point, float desiredSpeed) const;
	CRASHCORE_EXPORT bool NearEdge(const AIPerception& p, float margin, Vec3* inward) const;

	AIProfileDef profile_;
	Pcg32 rng_{1u};
	int target_ = -1;
	float nextReplan_ = 0.0f;
	float stuckTimer_ = 0.0f;
	float reverseUntil_ = -1.0f;
	float reverseSteer_ = 0.0f;
	float flipTimer_ = 0.0f;
	float mistakeUntil_ = -1.0f;
	float mistakeSteer_ = 0.0f;
	Vec3 aim_;
	AIState state_ = AIState::Wander;
	Vec3 wanderPoint_;
	float wanderUntil_ = -1.0f;
};

} // namespace crash
