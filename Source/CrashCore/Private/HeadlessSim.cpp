// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/HeadlessSim.h"

#include <cmath>

namespace crash
{
// ---------------------------------------------------------------------------------------------
// RigidBody

Vec3 RigidBody::InvInertiaWorld(const Vec3& v) const
{
	const Vec3 local = rotation.Unrotate(v);
	const Vec3 scaled{local.x / std::max(inertia.x, 1.0f), local.y / std::max(inertia.y, 1.0f), local.z / std::max(inertia.z, 1.0f)};
	return rotation.Rotate(scaled);
}

void RigidBody::AddForceAtPoint(const Vec3& f, const Vec3& worldPoint)
{
	force += f;
	torque += Cross(worldPoint - ComWorld(), f);
}

void RigidBody::ApplyImpulseAtPoint(const Vec3& impulse, const Vec3& worldPoint)
{
	velocity += impulse / mass;
	angularVelocity += InvInertiaWorld(Cross(worldPoint - ComWorld(), impulse));
}

void RigidBody::Integrate(float dt)
{
	velocity += (force / mass + Vec3{0.0f, 0.0f, -kGravity}) * dt;
	angularVelocity += InvInertiaWorld(torque) * dt;
	angularVelocity *= std::max(0.0f, 1.0f - 0.05f * dt);
	angularVelocity = ClampLength(angularVelocity, 25.0f);
	Vec3 com = ComWorld();
	com += velocity * dt;
	rotation = rotation.Integrated(angularVelocity, dt);
	position = com - rotation.Rotate(comLocal);
	force = Vec3{};
	torque = Vec3{};
}

// ---------------------------------------------------------------------------------------------

namespace
{
struct Obb2D
{
	Vec3 center;
	Vec3 axisX; // unit, flat
	Vec3 axisY;
	float hx = 1.0f;
	float hy = 1.0f;

	void Corners(Vec3 out[4]) const
	{
		out[0] = center + axisX * hx + axisY * hy;
		out[1] = center + axisX * hx - axisY * hy;
		out[2] = center - axisX * hx - axisY * hy;
		out[3] = center - axisX * hx + axisY * hy;
	}
	bool Contains(const Vec3& p) const
	{
		const Vec3 d = (p - center).Flat();
		return std::fabs(Dot(d, axisX)) <= hx && std::fabs(Dot(d, axisY)) <= hy;
	}
	float ProjectRadius(const Vec3& axis) const { return hx * std::fabs(Dot(axisX, axis)) + hy * std::fabs(Dot(axisY, axis)); }
};

Obb2D MakeObb(const SimCar& car)
{
	const Frame f = car.body.GetFrame();
	Obb2D o;
	o.center = car.body.position.Flat();
	o.axisX = f.forward.Flat().Normalized();
	if (o.axisX.IsNearlyZero())
	{
		o.axisX = Vec3::Forward();
	}
	o.axisY = Cross(Vec3::Up(), o.axisX);
	o.hx = car.sim.GetSetup().bodyHalfExtents.x;
	o.hy = car.sim.GetSetup().bodyHalfExtents.y;
	return o;
}

// Returns false if separated. normal points from a to b.
bool SatOverlap(const Obb2D& a, const Obb2D& b, Vec3& normal, float& depth)
{
	const Vec3 axes[4] = {a.axisX, a.axisY, b.axisX, b.axisY};
	const Vec3 d = (b.center - a.center).Flat();
	depth = 1.0e9f;
	for (const Vec3& axis : axes)
	{
		const float dist = Dot(d, axis);
		const float overlap = a.ProjectRadius(axis) + b.ProjectRadius(axis) - std::fabs(dist);
		if (overlap <= 0.0f)
		{
			return false;
		}
		if (overlap < depth)
		{
			depth = overlap;
			normal = dist >= 0.0f ? axis : -axis;
		}
	}
	return true;
}
} // namespace

// ---------------------------------------------------------------------------------------------
// HeadlessWorld

HeadlessWorld::HeadlessWorld(const GameDatabase& db, const HeadlessConfig& config) : db_(db), config_(config)
{
}

void HeadlessWorld::UseArena(const ArenaDef& arena)
{
	config_.arenaHalfX = arena.HalfX();
	config_.arenaHalfY = arena.HalfY();
	config_.circular = arena.shape != ArenaShape::Rect;
	config_.walls = arena.walls;
	config_.ringOut = !arena.walls;
	config_.killZ = arena.killZ - arena.elevation;
}

bool HeadlessWorld::HasGround(const Vec3& p) const
{
	if (!config_.ringOut)
	{
		return true;
	}
	if (config_.circular)
	{
		return p.Length2D() <= config_.arenaHalfX;
	}
	return std::fabs(p.x) <= config_.arenaHalfX && std::fabs(p.y) <= config_.arenaHalfY;
}

int HeadlessWorld::AddCar(const VehicleBuild& build, const Vec3& position, float yawDeg, int team, const AIProfileDef* ai)
{
	auto car = std::make_unique<SimCar>();
	car->id = nextId_++;
	car->team = team;
	car->build = build;
	car->stats = ComputeEffectiveStats(db_, build);
	car->sim.Setup(MakeSimSetup(car->stats));
	car->damage.Init(car->stats);
	car->ability.Setup(car->stats.ability);
	const VehicleSimSetup& setup = car->sim.GetSetup();
	car->body.mass = setup.mass;
	car->body.inertia = setup.inertiaDiagonal;
	car->body.comLocal = setup.comLocal;
	car->spawnPosition = position;
	car->spawnYawDeg = yawDeg;
	if (ai)
	{
		car->brain = std::make_unique<AIBrain>(*ai, config_.seed * 7919u + static_cast<uint64_t>(car->id));
	}
	ResetCar(*car, position, yawDeg);
	const int id = car->id;
	cars_.push_back(std::move(car));
	return id;
}

void HeadlessWorld::ResetCar(SimCar& car, const Vec3& position, float yawDeg)
{
	const VehicleSimSetup& setup = car.sim.GetSetup();
	car.body.rotation = Quat::FromYaw(yawDeg * kDegToRad);
	car.body.position = Vec3{position.x, position.y, setup.bodyCenterHeight + 0.05f};
	car.body.velocity = Vec3{};
	car.body.angularVelocity = Vec3{};
	car.sim.Reset();
	car.ability.Reset();
	car.wrecked = false;
	car.airtime = 0.0f;
}

SimCar* HeadlessWorld::Car(int id)
{
	for (auto& c : cars_)
	{
		if (c->id == id)
		{
			return c.get();
		}
	}
	return nullptr;
}

ImpactBody HeadlessWorld::MakeImpactBody(const SimCar& car, const Vec3& velocity) const
{
	ImpactBody b;
	b.id = car.id;
	b.isVehicle = true;
	b.position = car.body.position;
	b.frame = car.body.GetFrame();
	b.velocity = velocity;
	b.mass = car.body.mass;
	b.halfExtents = car.sim.GetSetup().bodyHalfExtents;
	b.stats = &car.stats;
	const AbilityEffects fx = car.ability.Effects();
	b.ramCharge = fx.ramCharge;
	b.brace = fx.brace;
	b.shockBumper = fx.shockArmed;
	return b;
}

AIPerception HeadlessWorld::Perceive(const SimCar& car) const
{
	AIPerception p;
	p.time = time_;
	const Frame f = car.body.GetFrame();
	p.self.id = car.id;
	p.self.team = car.team;
	p.self.position = car.body.position;
	p.self.velocity = car.body.velocity;
	p.self.forward = f.forward;
	p.self.right = f.right;
	p.self.health = car.damage.Integrity();
	p.self.mass = car.body.mass;
	p.boostFraction = car.sim.BoostFraction();
	p.abilityReady = car.ability.Ready();
	p.ability = car.ability.Type();
	p.upsideDown = VehicleSimulator::IsUpsideDown(f);
	for (const auto& other : cars_)
	{
		if (other->id == car.id)
		{
			continue;
		}
		AICarInfo info;
		const Frame of = other->body.GetFrame();
		info.id = other->id;
		info.team = other->team;
		info.position = other->body.position;
		info.velocity = other->body.velocity;
		info.forward = of.forward;
		info.right = of.right;
		info.health = other->damage.Integrity();
		info.mass = other->body.mass;
		info.wrecked = other->wrecked;
		info.isBounty = match_ && match_->BountyTarget() == other->id;
		p.others.push_back(info);
	}
	p.arena.halfX = config_.arenaHalfX;
	p.arena.halfY = config_.arenaHalfY;
	p.arena.circular = config_.circular;
	p.arena.ringOut = config_.ringOut;
	return p;
}

void HeadlessWorld::Wreck(SimCar& car, bool ringOut)
{
	if (car.wrecked)
	{
		return;
	}
	car.wrecked = true;
	TakedownInfo info;
	info.victim = car.id;
	info.ringOut = ringOut;
	if (match_)
	{
		info = match_->OnWrecked(car.id, time_, ringOut);
	}
	if (onWrecked)
	{
		onWrecked(car.id, info);
	}
}

void HeadlessWorld::ApplyAbility(SimCar& car)
{
	if (car.wantsAbility)
	{
		const AbilityTrigger t = car.ability.TryActivate();
		if (t.activated)
		{
			car.body.velocity.z += t.jumpVelocity;
			car.sim.AddBoost(t.addBoost);
		}
	}
	car.wantsAbility = false;
	const AbilityEffects fx = car.ability.Effects();
	car.sim.SetDriveMultiplier(fx.driveMultiplier);
	car.sim.SetBoostMultiplier(fx.boostMultiplier);
}

void HeadlessWorld::StepCar(SimCar& car, float dt)
{
	const VehicleSimSetup& setup = car.sim.GetSetup();
	SimBodyState state;
	state.position = car.body.position;
	state.frame = car.body.GetFrame();
	state.linearVelocity = car.body.velocity;
	state.angularVelocity = car.body.angularVelocity;

	VehicleControls controls = car.controls;
	if (car.wrecked || (match_ && !match_->CanDrive()))
	{
		controls = VehicleControls();
		controls.brake = car.wrecked ? 0.0f : 1.0f;
		controls.handbrake = true;
	}
	else if (car.brain)
	{
		const AIOutput ai = car.brain->Think(Perceive(car), dt);
		controls = ai.controls;
		car.wantsAbility = ai.useAbility;
		car.wantsReset = ai.requestReset;
	}
	car.controls = controls;

	std::array<WheelContact, kWheelCount> contacts{};
	const float rayLength = car.sim.RayLength();
	for (int i = 0; i < kWheelCount; ++i)
	{
		const Vec3 mount = car.sim.MountWorld(i, state);
		const Vec3 dir = -state.frame.up;
		WheelContact& c = contacts[static_cast<size_t>(i)];
		if (dir.z < -0.05f && HasGround(mount))
		{
			const float t = mount.z / -dir.z;
			if (t >= 0.0f && t <= rayLength)
			{
				c.hit = true;
				c.distance = t;
				c.point = mount + dir * t;
				c.normal = Vec3::Up();
				c.pointVelocity = car.body.PointVelocity(c.point);
			}
		}
	}

	car.sim.SetDamage(car.damage.DrivingEffects());
	DrivingAssists assists;
	assists.suppressed = time_ - car.lastImpactTime < 1.5f;
	car.sim.SetAssists(assists);
	ApplyAbility(car);

	SimOutput out;
	car.sim.Step(dt, controls, state, contacts, out);
	car.groundedWheels = out.groundedWheels;
	for (int i = 0; i < kWheelCount; ++i)
	{
		const size_t idx = static_cast<size_t>(i);
		car.body.AddForceAtPoint(out.suspension[idx].force, out.suspension[idx].point);
		car.body.AddForceAtPoint(out.tire[idx].force, out.tire[idx].point);
	}
	car.body.AddForceAtPoint(out.centralForce, car.body.ComWorld());
	car.body.angularVelocity += out.angularAcceleration * dt;
	const Vec3 before = car.body.position;
	car.body.Integrate(dt);
	car.distance += (car.body.position - before).Length2D();

	GroundBody(car);

	// Airtime.
	const bool airborne = out.groundedWheels == 0 && car.body.position.z > setup.bodyCenterHeight + 0.3f;
	if (airborne)
	{
		car.airtime += dt;
	}
	else if (car.airtime > 0.0f)
	{
		if (match_ && !car.wrecked)
		{
			match_->Scores().OnAirtime(car.id, car.airtime);
		}
		car.airtime = 0.0f;
	}

	// Damage over time, abilities, resets.
	car.ability.Tick(dt);
	const AbilityEffects fx = car.ability.Effects();
	const DamageEvent burn = car.damage.Tick(dt, car.stats.repairPerSec + fx.repairPerSec);
	if (burn.becameWrecked)
	{
		Wreck(car, false);
	}
	if (car.wantsReset && !car.wrecked)
	{
		const Vec3 p = car.body.position;
		const Frame f = car.body.GetFrame();
		const float yaw = std::atan2(f.forward.y, f.forward.x) * kRadToDeg;
		ResetCar(car, p, yaw);
		car.wantsReset = false;
	}
	if (car.body.position.z < config_.killZ && !car.wrecked)
	{
		Wreck(car, true);
	}
}

void HeadlessWorld::GroundBody(SimCar& car)
{
	const Vec3 h = car.sim.GetSetup().bodyHalfExtents;
	const Frame f = car.body.GetFrame();
	float maxPenetration = 0.0f;
	for (int sx = -1; sx <= 1; sx += 2)
	{
		for (int sy = -1; sy <= 1; sy += 2)
		{
			for (int sz = -1; sz <= 1; sz += 2)
			{
				const Vec3 corner = car.body.position +
				                    f.ToWorld({h.x * static_cast<float>(sx), h.y * static_cast<float>(sy), h.z * static_cast<float>(sz)});
				if (corner.z >= 0.0f || !HasGround(corner))
				{
					continue;
				}
				maxPenetration = std::max(maxPenetration, -corner.z);
				const Vec3 v = car.body.PointVelocity(corner);
				if (v.z >= 0.0f)
				{
					continue;
				}
				const Vec3 n = Vec3::Up();
				const Vec3 r = corner - car.body.ComWorld();
				const float k = 1.0f / car.body.mass + Dot(n, Cross(car.body.InvInertiaWorld(Cross(r, n)), r));
				const float j = -(1.0f + 0.1f) * v.z / std::max(k, 1.0e-6f) * 0.25f; // shared across up to 4 corners
				car.body.ApplyImpulseAtPoint(n * j, corner);
				const Vec3 vt = v.Flat();
				if (!vt.IsNearlyZero())
				{
					const Vec3 t = vt.Normalized();
					const float kt = 1.0f / car.body.mass + Dot(t, Cross(car.body.InvInertiaWorld(Cross(r, t)), r));
					const float jt = std::min(vt.Length() / std::max(kt, 1.0e-6f) * 0.25f, 0.6f * j);
					car.body.ApplyImpulseAtPoint(-t * jt, corner);
				}
			}
		}
	}
	if (maxPenetration > 0.0f)
	{
		car.body.position.z += maxPenetration * 0.8f;
	}
}

void HeadlessWorld::CollideArena(SimCar& car)
{
	if (!config_.walls)
	{
		return;
	}
	const Obb2D box = MakeObb(car);
	Vec3 corners[4];
	box.Corners(corners);
	for (const Vec3& c : corners)
	{
		Vec3 outward;
		float penetration = 0.0f;
		if (config_.circular)
		{
			const float r = c.Length2D();
			if (r > config_.arenaHalfX)
			{
				outward = c.Flat().Normalized();
				penetration = r - config_.arenaHalfX;
			}
		}
		else
		{
			const float px = std::fabs(c.x) - config_.arenaHalfX;
			const float py = std::fabs(c.y) - config_.arenaHalfY;
			if (px > 0.0f && px >= py)
			{
				outward = {Sign(c.x), 0.0f, 0.0f};
				penetration = px;
			}
			else if (py > 0.0f)
			{
				outward = {0.0f, Sign(c.y), 0.0f};
				penetration = py;
			}
		}
		if (penetration <= 0.0f)
		{
			continue;
		}
		const Vec3 contact{c.x, c.y, car.body.position.z};
		const Vec3 preVelocity = car.body.velocity;
		car.body.position -= outward * penetration;
		const Vec3 v = car.body.PointVelocity(contact);
		const float vn = Dot(v, outward);
		if (vn > 0.0f)
		{
			const Vec3 r = contact - car.body.ComWorld();
			const float k = 1.0f / car.body.mass + Dot(outward, Cross(car.body.InvInertiaWorld(Cross(r, outward)), r));
			const float j = (1.0f + 0.25f) * vn / std::max(k, 1.0e-6f);
			car.body.ApplyImpulseAtPoint(-outward * j, contact);
			bool cooling = false;
			for (const PairCooldown& pc : cooldowns_)
			{
				cooling = cooling || (pc.a == car.id && pc.b == -1 && pc.until > time_);
			}
			if (!cooling)
			{
				ImpactInput in;
				in.a = MakeImpactBody(car, preVelocity);
				in.b.isVehicle = false;
				in.contactPoint = contact;
				in.normal = outward;
				const ImpactResult result = ResolveImpact(in, tuning_);
				if (result.valid)
				{
					cooldowns_.push_back({car.id, -1, time_ + 0.4f});
					const DamageEvent e = car.damage.Apply(result.a.damage);
					if (match_)
					{
						match_->Scores().OnDamageTaken(car.id, e.applied);
					}
					if (e.becameWrecked)
					{
						Wreck(car, false);
					}
				}
			}
		}
		return; // one wall contact per step is enough
	}
}

void HeadlessWorld::Collide(SimCar& a, SimCar& b)
{
	const Vec3 ha = a.sim.GetSetup().bodyHalfExtents;
	const Vec3 hb = b.sim.GetSetup().bodyHalfExtents;
	if (std::fabs(a.body.position.z - b.body.position.z) > ha.z + hb.z)
	{
		return;
	}
	const Obb2D oa = MakeObb(a);
	const Obb2D ob = MakeObb(b);
	Vec3 normal;
	float depth = 0.0f;
	if (!SatOverlap(oa, ob, normal, depth))
	{
		return;
	}
	// Contact point: average of corners inside the other box.
	Vec3 ca[4];
	Vec3 cb[4];
	oa.Corners(ca);
	ob.Corners(cb);
	Vec3 sum;
	int count = 0;
	for (const Vec3& c : ca)
	{
		if (ob.Contains(c))
		{
			sum += c;
			++count;
		}
	}
	for (const Vec3& c : cb)
	{
		if (oa.Contains(c))
		{
			sum += c;
			++count;
		}
	}
	Vec3 contact = count > 0 ? sum / static_cast<float>(count) : (oa.center + ob.center) * 0.5f;
	const float low = std::max(a.body.position.z - ha.z, b.body.position.z - hb.z);
	const float high = std::min(a.body.position.z + ha.z, b.body.position.z + hb.z);
	contact.z = 0.5f * (low + high);
	if (a.stats.HasShape(GeometryShape::Wedge) || b.stats.HasShape(GeometryShape::Wedge))
	{
		contact.z = low + 0.15f; // wedges scoop low
	}

	const Vec3 preA = a.body.velocity;
	const Vec3 preB = b.body.velocity;

	// Positional correction.
	const float invA = 1.0f / a.body.mass;
	const float invB = 1.0f / b.body.mass;
	const float correction = depth * 0.8f / (invA + invB);
	a.body.position -= normal * (correction * invA);
	b.body.position += normal * (correction * invB);

	// Impulse with restitution and friction.
	const Vec3 ra = contact - a.body.ComWorld();
	const Vec3 rb = contact - b.body.ComWorld();
	const Vec3 vRel = b.body.PointVelocity(contact) - a.body.PointVelocity(contact);
	const float vn = Dot(vRel, normal);
	if (vn < 0.0f)
	{
		const float k = invA + invB + Dot(normal, Cross(a.body.InvInertiaWorld(Cross(ra, normal)), ra)) +
		                Dot(normal, Cross(b.body.InvInertiaWorld(Cross(rb, normal)), rb));
		const float j = -(1.0f + 0.15f) * vn / std::max(k, 1.0e-9f);
		a.body.ApplyImpulseAtPoint(-normal * j, contact);
		b.body.ApplyImpulseAtPoint(normal * j, contact);
		const Vec3 vt = (vRel - normal * vn).Flat();
		if (!vt.IsNearlyZero())
		{
			const Vec3 t = vt.Normalized();
			const float kt = invA + invB + Dot(t, Cross(a.body.InvInertiaWorld(Cross(ra, t)), ra)) +
			                 Dot(t, Cross(b.body.InvInertiaWorld(Cross(rb, t)), rb));
			const float jt = std::min(vt.Length() / std::max(kt, 1.0e-9f), 0.4f * j);
			a.body.ApplyImpulseAtPoint(t * jt, contact);
			b.body.ApplyImpulseAtPoint(-t * jt, contact);
		}
	}

	// Gameplay resolution (debounced per pair).
	for (const PairCooldown& pc : cooldowns_)
	{
		if (((pc.a == a.id && pc.b == b.id) || (pc.a == b.id && pc.b == a.id)) && pc.until > time_)
		{
			return;
		}
	}
	ImpactInput in;
	in.a = MakeImpactBody(a, preA);
	in.b = MakeImpactBody(b, preB);
	in.contactPoint = contact;
	in.normal = normal;
	const ImpactResult result = ResolveImpact(in, tuning_);
	if (match_ && result.attackerId >= 0)
	{
		match_->Scores().OnTouched(result.attackerId, result.victimId, time_);
	}
	if (!result.valid)
	{
		return;
	}
	cooldowns_.push_back({a.id, b.id, time_ + 0.35f});
	++impactCount_;
	a.lastImpactTime = time_;
	b.lastImpactTime = time_;
	a.body.ApplyImpulseAtPoint(result.a.extraImpulse, contact);
	b.body.ApplyImpulseAtPoint(result.b.extraImpulse, contact);
	if (in.a.shockBumper)
	{
		a.ability.ConsumeShock();
	}
	if (in.b.shockBumper)
	{
		b.ability.ConsumeShock();
	}
	const DamageEvent ea = a.damage.Apply(result.a.damage);
	const DamageEvent eb = b.damage.Apply(result.b.damage);
	if (match_)
	{
		match_->Scores().OnImpact(result, time_);
	}
	if (onImpact)
	{
		onImpact(result);
	}
	if (eb.becameWrecked)
	{
		Wreck(b, false);
	}
	if (ea.becameWrecked)
	{
		Wreck(a, false);
	}
}

void HeadlessWorld::Step()
{
	const float dt = config_.dt;
	if (match_)
	{
		match_->Update(time_);
		if (match_->TakeRoundReset())
		{
			for (auto& car : cars_)
			{
				ResetCar(*car, car->spawnPosition, car->spawnYawDeg);
				car->damage.Init(car->stats);
			}
		}
		for (const int id : match_->TakeRespawns(time_))
		{
			if (SimCar* car = Car(id))
			{
				Pcg32 rng(config_.seed + static_cast<uint64_t>(time_ * 1000.0f) + static_cast<uint64_t>(id));
				const Vec3 p{rng.Range(-0.6f, 0.6f) * config_.arenaHalfX, rng.Range(-0.6f, 0.6f) * config_.arenaHalfY, 0.0f};
				ResetCar(*car, p, rng.Range(-180.0f, 180.0f));
				car->damage.Init(car->stats);
				match_->OnRespawned(id);
			}
		}
	}
	for (auto& car : cars_)
	{
		StepCar(*car, dt);
	}
	for (size_t i = 0; i < cars_.size(); ++i)
	{
		CollideArena(*cars_[i]);
		for (size_t j = i + 1; j < cars_.size(); ++j)
		{
			Collide(*cars_[i], *cars_[j]);
		}
	}
	cooldowns_.erase(std::remove_if(cooldowns_.begin(), cooldowns_.end(), [this](const PairCooldown& pc) { return pc.until <= time_; }),
	                 cooldowns_.end());
	time_ += dt;
}

} // namespace crash
