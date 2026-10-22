// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/AIBrain.h"

#include <cmath>

namespace crash
{
namespace
{
float AngleTo(const AICarInfo& self, const Vec3& point)
{
	const Vec3 to = point - self.position;
	const float x = Dot(to, self.forward);
	const float y = Dot(to, self.right);
	return std::atan2(y, x);
}

const AICarInfo* FindCar(const AIPerception& p, int id)
{
	for (const AICarInfo& c : p.others)
	{
		if (c.id == id)
		{
			return &c;
		}
	}
	return nullptr;
}
} // namespace

AIBrain::AIBrain(const AIProfileDef& profile, uint64_t seed) : profile_(profile), rng_(seed)
{
}

bool AIBrain::NearEdge(const AIPerception& p, float margin, Vec3* inward) const
{
	const Vec3 rel = (p.self.position - p.arena.center).Flat();
	if (p.arena.circular)
	{
		const float r = rel.Length();
		if (inward)
		{
			*inward = r > kSmallNumber ? -rel / r : Vec3::Forward();
		}
		return p.arena.halfX - r < margin;
	}
	const float dx = p.arena.halfX - std::fabs(rel.x);
	const float dy = p.arena.halfY - std::fabs(rel.y);
	if (inward)
	{
		*inward = dx < dy ? Vec3{-Sign(rel.x), 0.0f, 0.0f} : Vec3{0.0f, -Sign(rel.y), 0.0f};
	}
	return std::min(dx, dy) < margin;
}

int AIBrain::SelectTarget(const AIPerception& p)
{
	if (p.forcedTarget >= 0)
	{
		if (const AICarInfo* forced = FindCar(p, p.forcedTarget))
		{
			if (!forced->wrecked)
			{
				return forced->id;
			}
		}
	}
	int best = -1;
	float bestScore = -1.0e9f;
	for (const AICarInfo& c : p.others)
	{
		if (c.wrecked || (c.team >= 0 && c.team == p.self.team))
		{
			continue;
		}
		const float dist = Distance(c.position, p.self.position);
		float score = -dist / 50.0f;
		score += (1.0f - c.health) * 0.8f * profile_.aggression;
		score += c.isHuman ? profile_.focusPlayer : 0.0f;
		score += c.isBounty ? 1.5f : 0.0f;
		score += c.id == target_ ? 0.4f : 0.0f;
		score += std::cos(AngleTo(p.self, c.position)) * 0.3f;
		if (p.arena.hasHill && !p.arena.hillOwnedByMyTeam)
		{
			// Prefer enemies sitting on the hill.
			score += Distance(c.position, p.arena.hillCenter) < p.arena.hillRadius ? 0.8f : 0.0f;
		}
		score += rng_.Range(-0.5f, 0.5f) * (1.0f - profile_.skill);
		if (score > bestScore)
		{
			bestScore = score;
			best = c.id;
		}
	}
	return best;
}

Vec3 AIBrain::PlanAimPoint(const AIPerception& p, const AICarInfo& target) const
{
	const AICarInfo& me = p.self;
	const float dist = Distance(me.position, target.position);
	const float mySpeed = std::max(8.0f, me.velocity.Length());
	const float t = Clamp(dist / mySpeed, 0.0f, 2.5f) * profile_.aimLead;
	Vec3 predicted = target.position + target.velocity * t;

	if (dist > 12.0f)
	{
		// Line up a T-bone: if we are in line with the target's nose or tail, swing out to its side.
		const Vec3 fromTarget = (me.position - target.position).Flat().Normalized();
		const float alongAxis = std::fabs(Dot(fromTarget, target.forward));
		const float side = Dot(fromTarget, target.right) >= 0.0f ? 1.0f : -1.0f;
		const float offset = (3.0f + 0.15f * dist) * profile_.flankBias * alongAxis;
		predicted += target.right * (side * offset);
	}
	if (p.arena.ringOut)
	{
		// Come from the center side so the hit pushes the target off the edge.
		const Vec3 outward = (target.position - p.arena.center).Flat().Normalized();
		predicted -= outward * 2.0f;
	}
	return predicted;
}

VehicleControls AIBrain::SteerTowards(const AIPerception& p, const Vec3& point, float desiredThrottle) const
{
	VehicleControls c;
	const AICarInfo& me = p.self;
	const float angle = AngleTo(me, point);
	const float speed = Dot(me.velocity, me.forward);
	const float gain = 1.0f / (0.45f + 0.25f * (1.0f - profile_.skill));
	c.steer = Clamp(angle * gain, -1.0f, 1.0f);
	const float absAngle = std::fabs(angle);
	if (absAngle > 1.9f && speed < 6.0f)
	{
		// Target behind us at low speed: reverse-turn.
		c.brake = 1.0f;
		c.throttle = 0.0f;
		c.steer = -Sign(angle);
		return c;
	}
	c.throttle = desiredThrottle;
	if (absAngle > 1.0f && speed > 16.0f)
	{
		c.throttle = 0.25f;
		c.handbrake = profile_.skill > 0.45f;
	}
	else if (absAngle > 0.6f && speed > 25.0f)
	{
		c.throttle = 0.5f;
	}
	return c;
}

AIOutput AIBrain::Think(const AIPerception& p, float dt)
{
	AIOutput out;
	const AICarInfo& me = p.self;
	const float now = p.time;
	const float speed = me.velocity.Length();

	// ---- Recovery: upside down for a while => ask the host to reset us.
	if (p.upsideDown)
	{
		flipTimer_ += dt;
		if (flipTimer_ > 2.0f)
		{
			out.requestReset = true;
			flipTimer_ = 0.0f;
		}
		out.state = AIState::Recover;
		return out;
	}
	flipTimer_ = 0.0f;

	if (now < reverseUntil_)
	{
		out.controls.brake = 1.0f;
		out.controls.steer = reverseSteer_;
		out.state = AIState::Recover;
		state_ = AIState::Recover;
		return out;
	}

	// ---- Periodic re-plan (reaction time).
	if (now >= nextReplan_)
	{
		target_ = SelectTarget(p);
		nextReplan_ = now + profile_.reaction * rng_.Range(0.8f, 1.2f);
		if (rng_.Chance(profile_.mistakeRate * 0.3f))
		{
			mistakeUntil_ = now + rng_.Range(0.2f, 0.6f);
			mistakeSteer_ = rng_.Range(-0.7f, 0.7f);
		}
	}

	// ---- Ring-out edges.
	Vec3 inward;
	if (p.arena.ringOut && NearEdge(p, 10.0f + speed * 0.5f, &inward) && Dot(me.velocity.Flat(), inward) < 2.0f)
	{
		out.controls = SteerTowards(p, p.arena.center, 0.7f);
		if (Dot(me.forward, inward) < -0.3f)
		{
			out.controls.brake = 1.0f;
			out.controls.throttle = 0.0f;
		}
		out.state = AIState::ReturnToArena;
		state_ = out.state;
		return out;
	}

	// ---- Evade when hurt and someone is lining us up.
	if (me.health < profile_.retreatHealth)
	{
		for (const AICarInfo& c : p.others)
		{
			if (c.wrecked || (c.team >= 0 && c.team == me.team))
			{
				continue;
			}
			const Vec3 toMe = me.position - c.position;
			const float dist = toMe.Length();
			if (dist < 35.0f && dist > kSmallNumber && Dot(c.forward, toMe / dist) > 0.85f && Dot(c.velocity, toMe / dist) > 6.0f)
			{
				// Turn perpendicular to the threat and run.
				const Vec3 escape = Cross(Vec3::Up(), (toMe / dist)).Flat().Normalized();
				const float side = Dot(escape, me.forward) >= 0.0f ? 1.0f : -1.0f;
				out.controls = SteerTowards(p, me.position + escape * (side * 20.0f), 1.0f);
				out.controls.boost = p.boostFraction > 0.15f;
				out.useAbility = p.abilityReady && (p.ability == AbilityType::Brace || p.ability == AbilityType::HydraulicJump ||
				                                    p.ability == AbilityType::FieldRepair);
				out.state = AIState::Evade;
				state_ = out.state;
				return out;
			}
		}
	}

	// ---- King of the hill.
	if (p.arena.hasHill && !p.arena.hillOwnedByMyTeam && Distance(me.position, p.arena.hillCenter) > p.arena.hillRadius * 0.6f &&
	    profile_.aggression < 0.85f)
	{
		const AICarInfo* t = FindCar(p, target_);
		const bool targetOnHill = t && Distance(t->position, p.arena.hillCenter) < p.arena.hillRadius * 1.5f;
		if (!targetOnHill)
		{
			out.controls = SteerTowards(p, p.arena.hillCenter, 1.0f);
			out.state = AIState::HoldHill;
			state_ = out.state;
			return out;
		}
	}

	const AICarInfo* target = FindCar(p, target_);
	if (!target || target->wrecked)
	{
		// ---- Wander.
		if (now > wanderUntil_ || Distance(me.position, wanderPoint_) < 8.0f)
		{
			wanderPoint_ = p.arena.center + Vec3{rng_.Range(-0.6f, 0.6f) * p.arena.halfX, rng_.Range(-0.6f, 0.6f) * p.arena.halfY, 0.0f};
			wanderUntil_ = now + 6.0f;
		}
		out.controls = SteerTowards(p, wanderPoint_, 0.6f);
		out.state = AIState::Wander;
		state_ = out.state;
		return out;
	}

	// ---- Hunt / ram.
	aim_ = PlanAimPoint(p, *target);
	const float dist = Distance(me.position, target->position);
	out.controls = SteerTowards(p, aim_, 1.0f);
	out.targetId = target->id;
	out.aimPoint = aim_;
	out.state = dist < 25.0f ? AIState::Ram : AIState::Hunt;

	const float angle = std::fabs(AngleTo(me, aim_));
	const bool aligned = angle < 0.2f;
	if (aligned && dist > 8.0f && dist < 70.0f && p.boostFraction > 0.2f)
	{
		// boostUse decides how often we commit boost; deterministic per re-plan window via rng.
		out.controls.boost = rng_.NextFloat() < profile_.boostUse;
	}

	// Obstacle feelers.
	if (p.arena.clearCenter < 0.5f && dist > 15.0f)
	{
		out.controls.steer = Clamp(out.controls.steer + (p.arena.clearLeft > p.arena.clearRight ? -0.8f : 0.8f), -1.0f, 1.0f);
	}

	// Abilities.
	if (p.abilityReady && rng_.NextFloat() < profile_.abilityUse * dt * 4.0f)
	{
		switch (p.ability)
		{
		case AbilityType::RamCharge:
		case AbilityType::ShockBumper:
			out.useAbility = aligned && dist < 18.0f;
			break;
		case AbilityType::NitroBurst:
			out.useAbility = aligned && dist > 15.0f && dist < 50.0f;
			break;
		case AbilityType::Brace:
			out.useAbility = dist < 10.0f;
			break;
		case AbilityType::FieldRepair:
			out.useAbility = me.health < 0.45f && dist > 20.0f;
			break;
		case AbilityType::HydraulicJump:
			out.useAbility = dist < 9.0f && Dot(target->velocity - me.velocity, (me.position - target->position).Normalized()) > 8.0f;
			break;
		default:
			break;
		}
	}

	// Human-like mistakes.
	if (now < mistakeUntil_)
	{
		out.controls.steer = Clamp(out.controls.steer + mistakeSteer_, -1.0f, 1.0f);
	}

	// ---- Stuck detection.
	if (out.controls.throttle > 0.5f && speed < 1.0f)
	{
		stuckTimer_ += dt;
		if (stuckTimer_ > 1.3f)
		{
			reverseUntil_ = now + 1.0f;
			reverseSteer_ = out.controls.steer > 0.0f ? -1.0f : 1.0f;
			stuckTimer_ = 0.0f;
		}
	}
	else
	{
		stuckTimer_ = 0.0f;
	}
	state_ = out.state;
	return out;
}

} // namespace crash
