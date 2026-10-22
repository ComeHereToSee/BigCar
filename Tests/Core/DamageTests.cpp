// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Damage.h"
#include "TestData.h"
#include "TestFramework.h"

using namespace crash;
using namespace bigcar_test;

namespace
{
EffectiveStats Stats(const char* id)
{
	VehicleBuild b;
	b.vehicleId = id;
	return ComputeEffectiveStats(FixtureDb(), b);
}
} // namespace

TEST_CASE("damage: zone hp follows the documented formula")
{
	const EffectiveStats s = Stats("test_sedan"); // 1600 kg, toughness 1
	const float base = 400.0f + 0.35f * 1600.0f;
	CHECK_NEAR(s.zoneHp[0], base * 1.0f, 0.01f);
	CHECK_NEAR(s.zoneHp[2], base * 0.8f, 0.01f);
	CHECK_NEAR(s.engineHp, base * 0.7f, 0.01f);
	CHECK_NEAR(s.wheelHp, base * 0.35f, 0.01f);
}

TEST_CASE("damage: broken zones spill into engine and frame, then wreck")
{
	const EffectiveStats s = Stats("test_sedan");
	DamageState d;
	d.Init(s);
	CHECK_NEAR(d.Integrity(), 1.0f, 1e-5f);
	ZoneDamage hit;
	hit.zone[static_cast<size_t>(Zone::Left)] = s.zoneHp[2] * 1.2f;
	const DamageEvent e = d.Apply(hit);
	CHECK(e.zoneBroken);
	CHECK_NEAR(d.ZoneFraction(Zone::Left), 0.0f, 1e-5f);
	CHECK(d.EngineFraction() < 1.0f);
	CHECK(d.ZoneFraction(Zone::Front) < 1.0f);
	CHECK(!d.IsWrecked());
	int hits = 1;
	while (!d.IsWrecked() && hits < 20)
	{
		const DamageEvent again = d.Apply(hit);
		++hits;
		if (again.becameWrecked)
		{
			CHECK(d.IsWrecked());
		}
	}
	CHECK(d.IsWrecked());
	CHECK(hits >= 2); // a single huge side hit is not enough to wreck instantly
	const DamageEvent after = d.Apply(hit);
	CHECK_NEAR(after.applied, 0.0f, 1e-5f);
}

TEST_CASE("damage: engine fire burns down, repair stops it")
{
	const EffectiveStats s = Stats("test_hatch");
	DamageState d;
	d.Init(s);
	ZoneDamage hit;
	hit.engine = s.engineHp * 0.8f;
	const DamageEvent e = d.Apply(hit);
	CHECK(e.fireStarted);
	CHECK(d.IsOnFire());
	const float before = d.EngineFraction();
	d.Tick(2.0f, 0.0f);
	CHECK(d.EngineFraction() < before);
	d.Repair(0.25f);
	CHECK(!d.IsOnFire());
	DamageState burning;
	burning.Init(s);
	burning.Apply(hit);
	bool wrecked = false;
	for (int i = 0; i < 200 && !wrecked; ++i)
	{
		wrecked = burning.Tick(0.1f, 0.0f).becameWrecked;
	}
	CHECK(wrecked);
}

TEST_CASE("damage: wheels detach and affect driving")
{
	const EffectiveStats s = Stats("test_sedan");
	DamageState d;
	d.Init(s);
	ZoneDamage hit;
	hit.wheels[1] = s.wheelHp * 2.0f;
	const DamageEvent e = d.Apply(hit);
	CHECK(e.wheelLost[1]);
	CHECK(d.IsWheelDetached(1));
	const DrivingDamage fx = d.DrivingEffects();
	CHECK(fx.wheelMissing[1]);
	CHECK(fx.steerPullRad > 0.0f);
	d.Repair(0.5f);
	CHECK(d.IsWheelDetached(1)); // partial repair does not bring a lost wheel back
	d.RepairFull();
	CHECK(!d.IsWheelDetached(1));
}

TEST_CASE("damage: snapshot round trip")
{
	const EffectiveStats s = Stats("test_truck");
	DamageState d;
	d.Init(s);
	ZoneDamage hit;
	hit.zone[0] = 300.0f;
	hit.engine = 100.0f;
	hit.wheels[2] = 50.0f;
	d.Apply(hit);
	DamageState mirror;
	mirror.Init(s);
	mirror.ApplySnapshot(d.Snapshot());
	CHECK_NEAR(mirror.ZoneFraction(Zone::Front), d.ZoneFraction(Zone::Front), 1e-5f);
	CHECK_NEAR(mirror.EngineFraction(), d.EngineFraction(), 1e-5f);
	CHECK_NEAR(mirror.WheelFraction(2), d.WheelFraction(2), 1e-5f);
	CHECK_NEAR(mirror.Integrity(), d.Integrity(), 1e-5f);
}
