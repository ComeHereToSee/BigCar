// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Impact.h"
#include "TestData.h"
#include "TestFramework.h"

using namespace crash;
using namespace bigcar_test;

namespace
{
struct Car
{
	EffectiveStats stats;
	ImpactBody body;
};

Car MakeCar(const char* vehicleId, const Vec3& pos, float yawDeg, const Vec3& velocity, const char* front = nullptr)
{
	const GameDatabase& db = FixtureDb();
	VehicleBuild b;
	b.vehicleId = vehicleId;
	if (front)
	{
		b.equipment[static_cast<size_t>(EquipSlot::Front)] = front;
	}
	Car c;
	c.stats = ComputeEffectiveStats(db, b);
	c.body.id = vehicleId[5] == 's' ? 1 : 2;
	c.body.position = pos;
	c.body.frame = Frame::FromQuat(Quat::FromYaw(yawDeg * kDegToRad));
	c.body.velocity = velocity;
	c.body.mass = c.stats.massKg;
	const VehicleDimensions& d = c.stats.def->dimensions;
	c.body.halfExtents = {d.length * 0.5f, d.width * 0.5f, (d.height - d.groundClearance) * 0.5f};
	return c;
}

ImpactResult Hit(Car& a, Car& b, const Vec3& contact, const Vec3& normal)
{
	a.body.stats = &a.stats;
	b.body.stats = &b.stats;
	a.body.id = 1;
	b.body.id = 2;
	ImpactInput in;
	in.a = a.body;
	in.b = b.body;
	in.contactPoint = contact;
	in.normal = normal;
	return ResolveImpact(in);
}
} // namespace

TEST_CASE("impact: zone classification")
{
	Car c = MakeCar("test_sedan", {0, 0, 0.8f}, 0.0f, {});
	const float hx = c.body.halfExtents.x;
	const float hy = c.body.halfExtents.y;
	CHECK(ClassifyZone(c.body, {hx, 0, 0.8f}) == Zone::Front);
	CHECK(ClassifyZone(c.body, {-hx, 0.2f, 0.8f}) == Zone::Rear);
	CHECK(ClassifyZone(c.body, {0, hy, 0.8f}) == Zone::Right);
	CHECK(ClassifyZone(c.body, {0.5f, -hy, 0.8f}) == Zone::Left);
	CHECK(ClassifyZone(c.body, {0, 0, 0.8f + c.body.halfExtents.z}) == Zone::Top);
	// Rotated 90 degrees: world +Y is now the car's front.
	Car r = MakeCar("test_sedan", {0, 0, 0.8f}, 90.0f, {});
	CHECK(ClassifyZone(r.body, {0, hx, 0.8f}) == Zone::Front);
}

TEST_CASE("impact: equal head-on is symmetric and heavy")
{
	Car a = MakeCar("test_sedan", {-2.44f, 0, 0.8f}, 0.0f, {13.9f, 0, 0});
	Car b = MakeCar("test_sedan", {2.44f, 0, 0.8f}, 180.0f, {-13.9f, 0, 0});
	const ImpactResult r = Hit(a, b, {0, 0, 0.8f}, {1, 0, 0});
	REQUIRE(r.valid);
	CHECK(r.kind == ImpactKind::HeadOn);
	CHECK_NEAR(r.closingSpeed, 27.8f, 0.01f);
	CHECK_NEAR(r.a.damage.zone[0], r.b.damage.zone[0], 1.0f);
	// Two 1600 kg cars at 50 km/h each: front loses well over half its hit points.
	CHECK_MSG(r.a.damage.zone[0] > 0.5f * a.stats.zoneHp[0], r.a.damage.zone[0] << " of " << a.stats.zoneHp[0]);
	CHECK(r.a.damage.engine > 0.0f); // front engine
	CHECK(r.severity > 0.5f);
}

TEST_CASE("impact: T-bone hurts the victim far more than the attacker")
{
	Car attacker = MakeCar("test_sedan", {0, -3.3f, 0.8f}, 90.0f, {0, 16.0f, 0}); // driving +Y
	Car victim = MakeCar("test_hatch", {0, 0, 0.8f}, 0.0f, {2.0f, 0, 0});
	const ImpactResult r = Hit(attacker, victim, {0, -0.9f, 0.8f}, {0, 1, 0});
	REQUIRE(r.valid);
	CHECK(r.kind == ImpactKind::TBone);
	CHECK(r.aIsAttacker);
	CHECK(r.b.zone == Zone::Left);
	CHECK_MSG(r.b.damage.BodyTotal() > 2.5f * r.a.damage.BodyTotal(), r.b.damage.BodyTotal() << " vs " << r.a.damage.BodyTotal());
	CHECK(r.b.extraImpulse.y > 0.0f); // pushed away from the attacker
	CHECK(r.attackerId == 1);
	CHECK(r.victimId == 2);
}

TEST_CASE("impact: rear-end, PIT and side swipe")
{
	Car a = MakeCar("test_sedan", {-4.9f, 0, 0.8f}, 0.0f, {20.0f, 0, 0});
	Car b = MakeCar("test_sedan", {0, 0, 0.8f}, 0.0f, {10.0f, 0, 0});
	ImpactResult r = Hit(a, b, {-2.44f, 0.1f, 0.8f}, {1, 0, 0});
	CHECK(r.kind == ImpactKind::RearEnd);
	CHECK(r.valid);

	// PIT: nudge the rear quarter while moving sideways relative to the victim.
	Car p = MakeCar("test_sedan", {-3.5f, 1.4f, 0.8f}, -10.0f, {18.0f, -4.0f, 0});
	Car q = MakeCar("test_sedan", {0, 0, 0.8f}, 0.0f, {15.0f, 0, 0});
	r = Hit(p, q, {-1.9f, 0.92f, 0.8f}, {0.2f, -0.98f, 0});
	CHECK_MSG(r.kind == ImpactKind::PIT, "kind " << ToString(r.kind));
	CHECK(std::fabs(r.b.extraImpulse.y) > 0.0f);

	// Side swipe: parallel cars rubbing doors.
	Car s1 = MakeCar("test_sedan", {0, -1.9f, 0.8f}, 0.0f, {20.0f, 2.0f, 0});
	Car s2 = MakeCar("test_sedan", {0, 0, 0.8f}, 0.0f, {20.0f, 0, 0});
	r = Hit(s1, s2, {0, -0.92f, 0.8f}, {0, 1, 0});
	CHECK(r.kind == ImpactKind::SideSwipe);
	CHECK(!r.valid); // only 2 m/s closing: no damage, but the attacker is still known for ring-out credit
	CHECK(r.attackerId == 1);
}

TEST_CASE("impact: wedge launches the victim, crush from above")
{
	Car a = MakeCar("test_sedan", {0, -3.4f, 0.6f}, 90.0f, {0, 15.0f, 0}, "front_wedge");
	Car b = MakeCar("test_hatch", {0, 0, 0.8f}, 0.0f, {});
	ImpactResult r = Hit(a, b, {0, -0.9f, 0.5f}, {0, 1, 0});
	CHECK_MSG(r.kind == ImpactKind::Launch, "kind " << ToString(r.kind));
	CHECK(r.b.extraImpulse.z / b.stats.massKg > 3.0f); // at least 3 m/s upward
	// Same hit without a wedge is a plain T-bone and no big launch.
	Car c = MakeCar("test_sedan", {0, -3.4f, 0.6f}, 90.0f, {0, 15.0f, 0});
	r = Hit(c, b, {0, -0.9f, 0.5f}, {0, 1, 0});
	CHECK(r.kind == ImpactKind::TBone);
	CHECK(r.b.extraImpulse.z <= 0.0f);

	Car top = MakeCar("test_truck", {0, 0, 2.6f}, 0.0f, {2.0f, 0, -7.0f});
	Car below = MakeCar("test_hatch", {0, 0, 0.8f}, 0.0f, {});
	r = Hit(top, below, {0, 0, 1.45f}, {0, 0, -1});
	CHECK_MSG(r.kind == ImpactKind::Crush, "kind " << ToString(r.kind));
	CHECK(r.b.zone == Zone::Top);
	CHECK(r.b.damage.zone[static_cast<size_t>(Zone::Top)] > r.a.damage.BodyTotal() * 3.0f);
}

TEST_CASE("impact: armor, brace, ram power and mass matter")
{
	auto victimDamage = [](const char* attackerId, bool brace, const char* front)
	{
		Car a = MakeCar(attackerId, {0, -3.4f, 0.8f}, 90.0f, {0, 15.0f, 0}, front);
		Car b = MakeCar("test_sedan", {0, 0, 0.8f}, 0.0f, {});
		b.body.brace = brace;
		return Hit(a, b, {0, -0.92f, 0.8f}, {0, 1, 0}).b.damage.BodyTotal();
	};
	const float base = victimDamage("test_sedan", false, nullptr);
	CHECK_NEAR(victimDamage("test_sedan", true, nullptr), base * 0.5f, base * 0.01f);
	CHECK(victimDamage("test_sedan", false, "front_ram") > base * 1.1f);
	CHECK(victimDamage("test_truck", false, nullptr) > base * 1.3f); // heavy + ram power 1.5

	// Low speed bumps do nothing.
	Car a = MakeCar("test_sedan", {-4.9f, 0, 0.8f}, 0.0f, {2.0f, 0, 0});
	Car b = MakeCar("test_sedan", {0, 0, 0.8f}, 0.0f, {});
	CHECK(!Hit(a, b, {-2.44f, 0, 0.8f}, {1, 0, 0}).valid);
}

TEST_CASE("impact: walls damage only the car and less than cars")
{
	Car a = MakeCar("test_sedan", {0, 0, 0.8f}, 0.0f, {20.0f, 0, 0});
	a.body.stats = &a.stats;
	ImpactInput in;
	in.a = a.body;
	in.b.isVehicle = false;
	in.contactPoint = {2.44f, 0, 0.8f};
	in.normal = {1, 0, 0};
	const ImpactResult wall = ResolveImpact(in);
	CHECK(wall.valid);
	CHECK(wall.kind == ImpactKind::None);
	CHECK(wall.a.zone == Zone::Front);
	CHECK(wall.a.damage.zone[0] > 0.0f);
	in.b.softWorld = true;
	CHECK(ResolveImpact(in).a.damage.zone[0] < wall.a.damage.zone[0] * 0.5f);
}

TEST_CASE("impact: side blades shred tires")
{
	const GameDatabase& db = FixtureDb();
	VehicleBuild bladed;
	bladed.vehicleId = "test_sedan";
	bladed.equipment[static_cast<size_t>(EquipSlot::Side)] = "side_blades";
	Car a = MakeCar("test_sedan", {0, -1.9f, 0.8f}, 0.0f, {20.0f, 9.0f, 0});
	a.stats = ComputeEffectiveStats(db, bladed);
	Car b = MakeCar("test_sedan", {0, 0, 0.8f}, 0.0f, {20.0f, 0, 0});
	const ImpactResult r = Hit(a, b, {1.4f, -0.92f, 0.8f}, {0, 1, 0});
	REQUIRE(r.valid);
	CHECK(r.kind == ImpactKind::SideSwipe);
	CHECK(r.b.damage.wheels[0] > 100.0f); // front-left wheel of the victim
}
