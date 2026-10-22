// Copyright BigCar Team. All Rights Reserved.
//
// Collision → gameplay. The physics engine resolves contact; this module decides what kind of hit it was
// (head-on, T-bone, launch…), who the attacker is, how much damage each car takes per zone, and which extra
// gameplay impulses (wedge launch, PIT spin, shock bumper) to add on top of the physical response.
#pragma once

#include "CrashCore/Damage.h"
#include "CrashCore/Export.h"
#include "CrashCore/Stats.h"

namespace crash
{
struct ImpactBody
{
	int id = -1;
	bool isVehicle = true;  // false = static world / prop
	bool softWorld = false; // tire walls etc. (world only)
	Vec3 position;          // body origin
	Frame frame;
	Vec3 velocity; // pre-impact linear velocity (m/s)
	float mass = 1.0e9f;
	Vec3 halfExtents{2.2f, 0.9f, 0.6f};
	const EffectiveStats* stats = nullptr;
	bool ramCharge = false;   // ability active
	bool brace = false;       // ability active
	bool shockBumper = false; // ability armed
};

struct ImpactInput
{
	ImpactBody a;
	ImpactBody b;
	Vec3 contactPoint;
	Vec3 normal; // unit, pointing from A into B
};

struct ImpactTuning
{
	float joulesPerHp = 240.0f;   // kinetic energy that becomes one point of damage
	float minClosingSpeed = 2.5f; // m/s below which nothing is damaged
	float worldDamageScale = 0.55f;
	float softWorldDamageScale = 0.15f;
	float maxLaunchSpeed = 12.0f;
};

struct ImpactSide
{
	Zone zone = Zone::Front;
	ZoneDamage damage;
	Vec3 extraImpulse; // N*s, apply at ImpactResult::contactPoint
	bool attacker = false;
};

struct ImpactResult
{
	bool valid = false; // false: below damage threshold (still reports attacker for "last touched")
	ImpactKind kind = ImpactKind::None;
	int attackerId = -1;
	int victimId = -1;
	float closingSpeed = 0.0f;
	float energy = 0.0f;   // joules
	float severity = 0.0f; // 0..1 for camera shake / VFX / audio
	Vec3 contactPoint;
	ImpactSide a;
	ImpactSide b;
	bool aIsAttacker = false;

	const ImpactSide& Attacker() const { return aIsAttacker ? a : b; }
	const ImpactSide& Victim() const { return aIsAttacker ? b : a; }
};

CRASHCORE_EXPORT ImpactResult ResolveImpact(const ImpactInput& input, const ImpactTuning& tuning = ImpactTuning());

// Which zone of `body` contains `worldPoint`.
CRASHCORE_EXPORT Zone ClassifyZone(const ImpactBody& body, const Vec3& worldPoint);

// Victim / attacker damage multipliers per impact kind (exposed for tests and the design doc).
CRASHCORE_EXPORT float VictimMultiplier(ImpactKind kind);
CRASHCORE_EXPORT float AttackerMultiplier(ImpactKind kind);

} // namespace crash
