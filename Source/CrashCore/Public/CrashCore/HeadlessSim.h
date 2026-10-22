// Copyright BigCar Team. All Rights Reserved.
//
// Headless battle simulator: rigid-body cars on a flat arena driven by the same VehicleSimulator, AIBrain, impact,
// damage, scoring and match rules the game uses. Runs thousands of AI matches in CI for balance and regression
// testing without Unreal. Collision detection is simplified (oriented boxes in the ground plane + height overlap),
// which is plenty for statistics; the shipped game uses Chaos for the real contact physics.
#pragma once

#include "CrashCore/AIBrain.h"
#include "CrashCore/Abilities.h"
#include "CrashCore/Damage.h"
#include "CrashCore/Export.h"
#include "CrashCore/Impact.h"
#include "CrashCore/Match.h"
#include "CrashCore/VehicleSim.h"

#include <functional>
#include <memory>
#include <vector>

namespace crash
{
struct RigidBody
{
	float mass = 1400.0f;
	Vec3 inertia{1000.0f, 2500.0f, 3000.0f}; // local diagonal
	Vec3 comLocal;
	Vec3 position; // body origin
	Quat rotation;
	Vec3 velocity;        // of the center of mass
	Vec3 angularVelocity; // world
	Vec3 force;
	Vec3 torque;

	Frame GetFrame() const { return Frame::FromQuat(rotation); }
	Vec3 ComWorld() const { return position + rotation.Rotate(comLocal); }
	Vec3 PointVelocity(const Vec3& worldPoint) const { return velocity + Cross(angularVelocity, worldPoint - ComWorld()); }
	CRASHCORE_EXPORT Vec3 InvInertiaWorld(const Vec3& v) const;
	CRASHCORE_EXPORT void AddForceAtPoint(const Vec3& f, const Vec3& worldPoint);
	CRASHCORE_EXPORT void ApplyImpulseAtPoint(const Vec3& impulse, const Vec3& worldPoint);
	CRASHCORE_EXPORT void Integrate(float dt);
};

struct SimCar
{
	int id = -1;
	int team = -1;
	VehicleBuild build;
	EffectiveStats stats;
	VehicleSimulator sim;
	DamageState damage;
	RigidBody body;
	std::unique_ptr<AIBrain> brain;
	VehicleControls controls;
	bool wrecked = false;
	float airtime = 0.0f;
	float lastImpactTime = -100.0f;
	float distance = 0.0f;
	AbilityState ability;
	Vec3 spawnPosition;
	float spawnYawDeg = 0.0f;
	bool wantsAbility = false;
	bool wantsReset = false;
	int groundedWheels = 0;
};

struct HeadlessConfig
{
	float dt = 1.0f / 120.0f;
	float arenaHalfX = 60.0f;
	float arenaHalfY = 60.0f;
	bool circular = false;
	bool walls = true;
	bool ringOut = false;
	float killZ = -15.0f;
	uint64_t seed = 1;
};

class HeadlessWorld
{
public:
	CRASHCORE_EXPORT HeadlessWorld(const GameDatabase& db, const HeadlessConfig& config);

	// Configures arena bounds from data.
	CRASHCORE_EXPORT void UseArena(const ArenaDef& arena);
	CRASHCORE_EXPORT int AddCar(const VehicleBuild& build, const Vec3& position, float yawDeg, int team, const AIProfileDef* ai);
	CRASHCORE_EXPORT SimCar* Car(int id);
	const std::vector<std::unique_ptr<SimCar>>& Cars() const { return cars_; }

	CRASHCORE_EXPORT void Step();
	float Time() const { return time_; }
	const HeadlessConfig& Config() const { return config_; }

	// Optional match rules (derby, takedown…). When set, wrecks/impacts are reported to it.
	void SetMatch(MatchRules* match) { match_ = match; }

	std::function<void(const ImpactResult&)> onImpact;
	std::function<void(int victim, const TakedownInfo&)> onWrecked;

	int ImpactCount() const { return impactCount_; }

private:
	CRASHCORE_EXPORT void StepCar(SimCar& car, float dt);
	CRASHCORE_EXPORT void Collide(SimCar& a, SimCar& b);
	CRASHCORE_EXPORT void CollideArena(SimCar& car);
	CRASHCORE_EXPORT void GroundBody(SimCar& car);
	CRASHCORE_EXPORT void ApplyAbility(SimCar& car);
	CRASHCORE_EXPORT void ResetCar(SimCar& car, const Vec3& position, float yawDeg);
	CRASHCORE_EXPORT void Wreck(SimCar& car, bool ringOut);
	CRASHCORE_EXPORT ImpactBody MakeImpactBody(const SimCar& car, const Vec3& velocity) const;
	CRASHCORE_EXPORT AIPerception Perceive(const SimCar& car) const;
	CRASHCORE_EXPORT bool HasGround(const Vec3& p) const;

	const GameDatabase& db_;
	HeadlessConfig config_;
	std::vector<std::unique_ptr<SimCar>> cars_;
	MatchRules* match_ = nullptr;
	float time_ = 0.0f;
	int nextId_ = 1;
	int impactCount_ = 0;
	ImpactTuning tuning_;
	struct PairCooldown
	{
		int a;
		int b;
		float until;
	};
	std::vector<PairCooldown> cooldowns_;
};

} // namespace crash
