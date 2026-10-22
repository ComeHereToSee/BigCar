// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Impact.h"

#include <cmath>

namespace crash
{
namespace
{
struct KindMultipliers
{
	float victim;
	float attacker;
};

KindMultipliers MultipliersFor(ImpactKind kind)
{
	switch (kind)
	{
	case ImpactKind::HeadOn:
		return {1.0f, 1.0f};
	case ImpactKind::TBone:
		return {1.4f, 0.7f};
	case ImpactKind::SideSwipe:
		return {0.5f, 0.5f};
	case ImpactKind::RearEnd:
		return {0.9f, 0.6f};
	case ImpactKind::Launch:
		return {0.8f, 0.4f};
	case ImpactKind::Crush:
		return {1.6f, 0.3f};
	case ImpactKind::PIT:
		return {0.7f, 0.4f};
	case ImpactKind::Glance:
		return {0.3f, 0.3f};
	default:
		return {1.0f, 1.0f};
	}
}

// How hard a zone hits back. Fronts are built to ram; sides crumple.
float ZoneAggression(const ImpactBody& body, Zone zone)
{
	const EffectiveStats* s = body.stats;
	switch (zone)
	{
	case Zone::Front:
		return (s ? s->ramPower : 1.0f) * (body.ramCharge ? 1.6f : 1.0f);
	case Zone::Rear:
		return 0.6f * (s ? s->rearDamage : 1.0f);
	case Zone::Left:
	case Zone::Right:
		return 0.5f * (s ? s->sideDamage : 1.0f);
	case Zone::Top:
		return 0.35f;
	case Zone::Bottom:
		return 0.45f;
	default:
		return 0.5f;
	}
}

float ArmorOf(const ImpactBody& body, Zone zone)
{
	return body.stats ? body.stats->armor[static_cast<size_t>(zone)] : 0.05f;
}

Vec3 LocalPoint(const ImpactBody& body, const Vec3& worldPoint)
{
	return body.frame.ToLocal(worldPoint - body.position);
}

// Builds a ZoneDamage for `body` taking `amount` on `zone`, including engine and wheel shares.
ZoneDamage BuildDamage(const ImpactBody& body, Zone zone, float amount, const Vec3& contactPoint, float extraTireDamage)
{
	ZoneDamage d;
	d.zone[static_cast<size_t>(zone)] = amount;
	if (body.stats && body.stats->def)
	{
		const EngineZoneWeights engine = GetEngineZoneWeights(body.stats->def->engineLayout);
		d.engine = amount * 0.5f * engine.weights[static_cast<size_t>(zone)];

		// Wheels near the contact take a share (and spikes shred them).
		const VehicleDimensions& dim = body.stats->def->dimensions;
		const Vec3 local = LocalPoint(body, contactPoint);
		for (int i = 0; i < kWheelCount; ++i)
		{
			const bool front = i < 2;
			const bool left = (i % 2) == 0;
			const float wx = (front ? 0.5f : -0.5f) * dim.wheelbase;
			const bool sideMatches = left ? local.y < -0.35f * body.halfExtents.y : local.y > 0.35f * body.halfExtents.y;
			const bool nearAxle = std::fabs(local.x - wx) < dim.wheelRadius * 1.8f;
			if (sideMatches && nearAxle)
			{
				d.wheels[static_cast<size_t>(i)] += amount * 0.4f + extraTireDamage;
			}
		}
	}
	return d;
}
} // namespace

float VictimMultiplier(ImpactKind kind)
{
	return MultipliersFor(kind).victim;
}

float AttackerMultiplier(ImpactKind kind)
{
	return MultipliersFor(kind).attacker;
}

Zone ClassifyZone(const ImpactBody& body, const Vec3& worldPoint)
{
	const Vec3 local = LocalPoint(body, worldPoint);
	const Vec3 h{std::max(body.halfExtents.x, 0.1f), std::max(body.halfExtents.y, 0.1f), std::max(body.halfExtents.z, 0.1f)};
	// Normalize by extents; the side with the largest relative coordinate wins. Vertical faces get a small bias so
	// bumper-height hits on low cars are not classified as roof hits.
	const float nx = local.x / h.x;
	const float ny = local.y / h.y;
	const float nz = local.z / h.z * 0.85f;
	const float ax = std::fabs(nx);
	const float ay = std::fabs(ny);
	const float az = std::fabs(nz);
	if (az > ax && az > ay)
	{
		return nz > 0.0f ? Zone::Top : Zone::Bottom;
	}
	if (ax >= ay)
	{
		return nx > 0.0f ? Zone::Front : Zone::Rear;
	}
	return ny > 0.0f ? Zone::Right : Zone::Left;
}

ImpactResult ResolveImpact(const ImpactInput& input, const ImpactTuning& tuning)
{
	ImpactResult result;
	result.contactPoint = input.contactPoint;
	const ImpactBody& A = input.a;
	const ImpactBody& B = input.b;
	const Vec3 n = input.normal.Normalized();
	if (n.IsNearlyZero())
	{
		return result;
	}

	const float aToward = Dot(A.velocity, n);
	const float bToward = B.isVehicle ? -Dot(B.velocity, n) : 0.0f;
	const float closing = aToward + bToward;
	result.closingSpeed = closing;
	result.a.zone = ClassifyZone(A, input.contactPoint);
	result.b.zone = B.isVehicle ? ClassifyZone(B, input.contactPoint) : Zone::Front;

	// ---------------- World impact: only A takes damage, no attacker.
	if (!B.isVehicle)
	{
		if (closing < tuning.minClosingSpeed)
		{
			return result;
		}
		const float energy = 0.5f * A.mass * closing * closing;
		result.energy = energy;
		result.severity = Clamp01(energy / 400000.0f);
		float amount = energy / tuning.joulesPerHp * (B.softWorld ? tuning.softWorldDamageScale : tuning.worldDamageScale);
		amount *= (1.0f - ArmorOf(A, result.a.zone)) * (A.stats ? A.stats->selfDamage : 1.0f) * (A.brace ? 0.5f : 1.0f);
		result.a.damage = BuildDamage(A, result.a.zone, amount, input.contactPoint, 0.0f);
		result.valid = true;
		return result;
	}

	// ---------------- Vehicle vs vehicle.
	const bool aAttacks = std::max(aToward, 0.0f) >= std::max(bToward, 0.0f);
	result.aIsAttacker = aAttacks;
	const ImpactBody& X = aAttacks ? A : B; // attacker
	const ImpactBody& Y = aAttacks ? B : A; // victim
	const Zone zx = aAttacks ? result.a.zone : result.b.zone;
	const Zone zy = aAttacks ? result.b.zone : result.a.zone;
	const Vec3 nXY = aAttacks ? n : -n; // from attacker into victim
	result.attackerId = X.id;
	result.victimId = Y.id;
	result.a.attacker = aAttacks;
	result.b.attacker = !aAttacks;

	if (closing < 0.5f)
	{
		return result; // touching, not hitting
	}

	// ---- Classification.
	const float xToward = aAttacks ? aToward : bToward;
	const float yToward = aAttacks ? bToward : aToward;
	const Vec3 yLocalContact = LocalPoint(Y, input.contactPoint);
	const float forwardDot = Dot(X.frame.forward, Y.frame.forward);
	const bool xReversing = Dot(X.velocity, X.frame.forward) < -2.0f;
	const bool xHitsWithFront = zx == Zone::Front || (zx == Zone::Rear && xReversing);
	const float relLateral = Dot(X.velocity - Y.velocity, Y.frame.right);
	const bool wedge = X.stats && X.stats->launchPower > 0.0f && zx == Zone::Front;

	ImpactKind kind = ImpactKind::Glance;
	if ((X.position.z - Y.position.z) > Y.halfExtents.z * 1.2f && X.velocity.z < -2.0f && zy == Zone::Top)
	{
		kind = ImpactKind::Crush;
	}
	else if (zx == Zone::Front && zy == Zone::Front && forwardDot < -0.5f && xToward > 1.0f && yToward > 1.0f)
	{
		kind = ImpactKind::HeadOn;
	}
	else if (xHitsWithFront &&
	         (zy == Zone::Bottom || (wedge && yLocalContact.z < 0.0f) || (zx == Zone::Front && Dot(nXY, Vec3::Up()) > 0.35f)))
	{
		kind = ImpactKind::Launch;
	}
	else if (xHitsWithFront && (zy == Zone::Left || zy == Zone::Right))
	{
		const bool rearQuarter = yLocalContact.x < -0.55f * Y.halfExtents.x;
		if (rearQuarter && std::fabs(forwardDot) > 0.5f)
		{
			kind = ImpactKind::PIT;
		}
		else if (std::fabs(Dot(zx == Zone::Front ? X.frame.forward : -X.frame.forward, Y.frame.right)) > 0.5f)
		{
			kind = ImpactKind::TBone;
		}
		else
		{
			kind = ImpactKind::SideSwipe;
		}
	}
	else if (xHitsWithFront && zy == Zone::Rear)
	{
		const bool offset = std::fabs(yLocalContact.y) > 0.45f * Y.halfExtents.y;
		kind = (offset && std::fabs(relLateral) > 2.0f) ? ImpactKind::PIT : ImpactKind::RearEnd;
	}
	else if (xHitsWithFront && zy == Zone::Front)
	{
		kind = ImpactKind::HeadOn;
	}
	else if ((zx == Zone::Left || zx == Zone::Right) && (zy == Zone::Left || zy == Zone::Right))
	{
		kind = ImpactKind::SideSwipe;
	}
	result.kind = kind;

	if (closing < tuning.minClosingSpeed)
	{
		return result;
	}

	// ---- Damage.
	const float reducedMass = X.mass * Y.mass / (X.mass + Y.mass);
	const float energy = 0.5f * reducedMass * closing * closing;
	result.energy = energy;
	result.severity = Clamp01(energy / 400000.0f);
	const float pool = energy / tuning.joulesPerHp;

	const float aggX = ZoneAggression(X, zx);
	const float aggY = ZoneAggression(Y, zy);
	const float shareToY = aggX / std::max(0.01f, aggX + aggY);
	const KindMultipliers mult = MultipliersFor(kind);
	const float massToY = Clamp(std::sqrt(2.0f * X.mass / (X.mass + Y.mass)), 0.6f, 1.4f);
	const float massToX = Clamp(std::sqrt(2.0f * Y.mass / (X.mass + Y.mass)), 0.6f, 1.4f);

	const float toY = pool * shareToY * mult.victim * massToY * (1.0f - ArmorOf(Y, zy)) * (Y.brace ? 0.5f : 1.0f);
	const float toX = pool * (1.0f - shareToY) * mult.attacker * massToX * (1.0f - ArmorOf(X, zx)) *
	                  (X.stats ? X.stats->selfDamage : 1.0f) * (X.brace ? 0.5f : 1.0f);

	const float severityScale = Clamp01(closing / 10.0f);
	const float tireToY = (X.stats ? X.stats->tireDamage : 0.0f) * 120.0f * severityScale *
	                      ((kind == ImpactKind::SideSwipe || kind == ImpactKind::TBone || kind == ImpactKind::PIT) ? 1.0f : 0.3f);
	const float tireToX = (Y.stats ? Y.stats->tireDamage : 0.0f) * 120.0f * severityScale * ((kind == ImpactKind::SideSwipe) ? 1.0f : 0.2f);

	ImpactSide& sideX = aAttacks ? result.a : result.b;
	ImpactSide& sideY = aAttacks ? result.b : result.a;
	sideY.damage = BuildDamage(Y, zy, toY, input.contactPoint, tireToY);
	sideX.damage = BuildDamage(X, zx, toX, input.contactPoint, tireToX);

	// ---- Gameplay impulses (on top of the physical contact response).
	const float massRatio = Clamp(X.mass / Y.mass, 0.3f, 1.5f);
	if (kind == ImpactKind::Launch)
	{
		const float launchPower = X.stats ? X.stats->launchPower : 0.0f;
		float launchSpeed = launchPower > 0.0f ? 2.5f + 2.2f * launchPower + 0.12f * closing : 1.2f + 0.05f * closing;
		launchSpeed = std::min(launchSpeed * massRatio, tuning.maxLaunchSpeed);
		const Vec3 push = X.frame.forward.Flat().Normalized() * 1.5f;
		sideY.extraImpulse += (Vec3::Up() * launchSpeed + push) * Y.mass;
	}
	else if (kind == ImpactKind::TBone)
	{
		sideY.extraImpulse += nXY.Flat().Normalized() * (Y.mass * std::min(0.15f * closing, 4.0f) * massRatio);
	}
	else if (kind == ImpactKind::PIT)
	{
		const float dir = relLateral >= 0.0f ? 1.0f : -1.0f;
		sideY.extraImpulse += Y.frame.right * (dir * Y.mass * 2.5f * massRatio);
	}
	if (X.shockBumper)
	{
		const float knock = 7.0f + (X.stats ? X.stats->knockback : 0.0f);
		sideY.extraImpulse += nXY * (Y.mass * knock * massRatio);
	}
	if (Y.shockBumper && kind != ImpactKind::Crush)
	{
		const float knock = 7.0f + (Y.stats ? Y.stats->knockback : 0.0f);
		sideX.extraImpulse += -nXY * (X.mass * knock * Clamp(Y.mass / X.mass, 0.3f, 1.5f));
	}
	result.valid = true;
	return result;
}

} // namespace crash
