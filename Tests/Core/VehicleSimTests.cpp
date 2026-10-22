// Copyright BigCar Team. All Rights Reserved.
//
// Physics plausibility checks of the vehicle model on flat ground (headless rigid body integrator).

#include "CrashCore/HeadlessSim.h"
#include "TestData.h"
#include "TestFramework.h"

using namespace crash;
using namespace bigcar_test;

namespace
{
HeadlessConfig OpenField()
{
	HeadlessConfig c;
	c.arenaHalfX = 5000.0f;
	c.arenaHalfY = 5000.0f;
	c.walls = false;
	c.ringOut = false;
	return c;
}

SimCar* Spawn(HeadlessWorld& world, const char* vehicleId, float yawDeg = 0.0f)
{
	VehicleBuild b;
	b.vehicleId = vehicleId;
	const int id = world.AddCar(b, {0, 0, 0}, yawDeg, 0, nullptr);
	return world.Car(id);
}

void Run(HeadlessWorld& world, float seconds)
{
	const int steps = static_cast<int>(seconds / world.Config().dt);
	for (int i = 0; i < steps; ++i)
	{
		world.Step();
	}
}

float TimeTo(HeadlessWorld& world, SimCar& car, float speedMs, float limit)
{
	car.controls.throttle = 1.0f;
	while (world.Time() < limit)
	{
		world.Step();
		if (Dot(car.body.velocity, car.body.GetFrame().forward) >= speedMs)
		{
			return world.Time();
		}
	}
	return 999.0f;
}
} // namespace

TEST_CASE("vehicle: settles at ride height, upright and still")
{
	HeadlessWorld world(FixtureDb(), OpenField());
	SimCar* car = Spawn(world, "test_sedan");
	REQUIRE(car);
	Run(world, 3.0f);
	const float expected = car->sim.GetSetup().bodyCenterHeight;
	CHECK_NEAR(car->body.position.z, expected, 0.06f);
	CHECK(car->body.velocity.Length() < 0.05f);
	CHECK(Dot(car->body.GetFrame().up, Vec3::Up()) > 0.99f);
	for (int i = 0; i < kWheelCount; ++i)
	{
		CHECK(car->sim.GetWheel(i).grounded);
	}
}

TEST_CASE("vehicle: 0-100 km/h times are plausible per class")
{
	struct Case
	{
		const char* id;
		float lo;
		float hi;
	};
	// Real references: hatch ~9.5s, sedan ~5.5s, supercar ~2.9s, pickup ~6s, EV ~5.5s.
	const Case cases[] = {{"test_hatch", 6.5f, 13.0f},
	                      {"test_sedan", 4.0f, 8.0f},
	                      {"test_super", 2.2f, 4.2f},
	                      {"test_truck", 4.5f, 10.0f},
	                      {"test_ev", 3.5f, 8.5f}};
	for (const Case& c : cases)
	{
		HeadlessWorld world(FixtureDb(), OpenField());
		SimCar* car = Spawn(world, c.id);
		REQUIRE(car);
		Run(world, 1.0f);
		const float start = world.Time();
		const float t = TimeTo(world, *car, 100.0f / 3.6f, 40.0f) - start;
		CHECK_MSG(t >= c.lo && t <= c.hi, c.id << " 0-100 in " << t << "s");
		CHECK(Dot(car->body.GetFrame().up, Vec3::Up()) > 0.95f);
	}
}

TEST_CASE("vehicle: top speed stays near the spec")
{
	HeadlessWorld world(FixtureDb(), OpenField());
	SimCar* car = Spawn(world, "test_hatch");
	REQUIRE(car);
	car->controls.throttle = 1.0f;
	Run(world, 70.0f);
	const float kmh = car->body.velocity.Length() * 3.6f;
	CHECK_MSG(kmh > 150.0f && kmh < 200.0f, "top speed " << kmh);
}

TEST_CASE("vehicle: braking from 100 km/h")
{
	HeadlessWorld world(FixtureDb(), OpenField());
	SimCar* car = Spawn(world, "test_sedan");
	REQUIRE(car);
	Run(world, 0.5f);
	TimeTo(world, *car, 100.0f / 3.6f, 30.0f);
	const Vec3 start = car->body.position;
	car->controls.throttle = 0.0f;
	car->controls.brake = 1.0f;
	float t = 0.0f;
	while (Dot(car->body.velocity, car->body.GetFrame().forward) > 0.3f && t < 10.0f)
	{
		world.Step();
		t += world.Config().dt;
	}
	const float distance = (car->body.position - start).Length2D();
	CHECK_MSG(distance > 30.0f && distance < 60.0f, "stopping distance " << distance);
	CHECK(Dot(car->body.GetFrame().up, Vec3::Up()) > 0.95f);
}

TEST_CASE("vehicle: steering right turns right, steady turn stays upright")
{
	HeadlessWorld world(FixtureDb(), OpenField());
	SimCar* car = Spawn(world, "test_sedan");
	REQUIRE(car);
	Run(world, 0.5f);
	TimeTo(world, *car, 15.0f, 20.0f);
	car->controls.throttle = 0.5f;
	car->controls.steer = 1.0f;
	float minUp = 1.0f;
	for (int i = 0; i < 600; ++i)
	{
		world.Step();
		minUp = std::min(minUp, Dot(car->body.GetFrame().up, Vec3::Up()));
	}
	const Frame f = car->body.GetFrame();
	CHECK(car->body.position.y > 5.0f);
	CHECK(f.forward.y > 0.3f || f.forward.x < 0.0f); // heading rotated clockwise (toward +Y)
	CHECK_MSG(minUp > 0.85f, "min up " << minUp);
	// Tall truck should not roll over in the same turn either.
	HeadlessWorld world2(FixtureDb(), OpenField());
	SimCar* truck = Spawn(world2, "test_truck");
	Run(world2, 0.5f);
	TimeTo(world2, *truck, 18.0f, 20.0f);
	truck->controls.steer = 1.0f;
	truck->controls.throttle = 0.6f;
	float truckMinUp = 1.0f;
	for (int i = 0; i < 600; ++i)
	{
		world2.Step();
		truckMinUp = std::min(truckMinUp, Dot(truck->body.GetFrame().up, Vec3::Up()));
	}
	CHECK_MSG(truckMinUp > 0.8f, "truck min up " << truckMinUp);
}

TEST_CASE("vehicle: brake at standstill engages reverse")
{
	HeadlessWorld world(FixtureDb(), OpenField());
	SimCar* car = Spawn(world, "test_hatch");
	REQUIRE(car);
	Run(world, 1.0f);
	car->controls.brake = 1.0f;
	Run(world, 3.0f);
	CHECK(car->sim.Gear() == -1);
	CHECK(Dot(car->body.velocity, car->body.GetFrame().forward) < -2.0f);
}

TEST_CASE("vehicle: handbrake turn rotates the car faster than steering alone")
{
	auto yawAfter = [](bool handbrake)
	{
		HeadlessWorld world(FixtureDb(), OpenField());
		SimCar* car = Spawn(world, "test_sedan");
		Run(world, 0.5f);
		TimeTo(world, *car, 20.0f, 20.0f);
		car->controls.steer = 1.0f;
		car->controls.throttle = 0.3f;
		car->controls.handbrake = handbrake;
		Run(world, 1.0f);
		const Vec3 fwd = car->body.GetFrame().forward;
		return std::atan2(fwd.y, fwd.x);
	};
	const float normal = yawAfter(false);
	const float drift = yawAfter(true);
	CHECK_MSG(drift > normal, "drift yaw " << drift << " vs " << normal);
}

TEST_CASE("vehicle: engine damage reduces acceleration, lost wheel pulls")
{
	HeadlessWorld healthy(FixtureDb(), OpenField());
	SimCar* a = Spawn(healthy, "test_sedan");
	Run(healthy, 0.5f);
	const float tHealthy = TimeTo(healthy, *a, 20.0f, 30.0f);

	HeadlessWorld damaged(FixtureDb(), OpenField());
	SimCar* b = Spawn(damaged, "test_sedan");
	ZoneDamage d;
	d.engine = b->stats.engineHp * 0.9f;
	b->damage.Apply(d);
	Run(damaged, 0.5f);
	const float tDamaged = TimeTo(damaged, *b, 20.0f, 30.0f);
	CHECK_MSG(tDamaged > tHealthy * 1.2f, "healthy " << tHealthy << " damaged " << tDamaged);
}

TEST_CASE("vehicle: sim outputs stay finite under extreme inputs")
{
	HeadlessWorld world(FixtureDb(), OpenField());
	SimCar* car = Spawn(world, "test_super");
	REQUIRE(car);
	Pcg32 rng(42);
	for (int i = 0; i < 4000; ++i)
	{
		car->controls.throttle = rng.NextFloat();
		car->controls.brake = rng.Chance(0.2f) ? 1.0f : 0.0f;
		car->controls.steer = rng.Range(-1.0f, 1.0f);
		car->controls.handbrake = rng.Chance(0.1f);
		car->controls.boost = rng.Chance(0.3f);
		if (i % 700 == 0)
		{
			car->body.velocity.z += 8.0f; // throw it in the air
			car->body.angularVelocity += Vec3{rng.Range(-3, 3), rng.Range(-3, 3), rng.Range(-3, 3)};
		}
		world.Step();
		REQUIRE(car->body.position.IsFinite());
		REQUIRE(car->body.velocity.IsFinite());
	}
	CHECK(car->body.position.z > -1.0f);
}
