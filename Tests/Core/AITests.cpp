// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/AIBrain.h"
#include "CrashCore/HeadlessSim.h"
#include "TestData.h"
#include "TestFramework.h"

using namespace crash;
using namespace bigcar_test;

namespace
{
AIPerception Basic()
{
	AIPerception p;
	p.time = 0.0f;
	p.self.id = 1;
	p.self.team = 0;
	p.self.position = {0, 0, 0.8f};
	p.self.forward = {1, 0, 0};
	p.self.right = {0, 1, 0};
	p.arena.halfX = 60.0f;
	p.arena.halfY = 60.0f;
	return p;
}
} // namespace

TEST_CASE("ai: targets enemies, never teammates, and steers toward them")
{
	AIBrain brain(*FixtureDb().FindAIProfile("elite"), 3);
	AIPerception p = Basic();
	AICarInfo mate;
	mate.id = 2;
	mate.team = 0;
	mate.position = {5, 0, 0.8f};
	AICarInfo enemy;
	enemy.id = 3;
	enemy.team = 1;
	enemy.position = {0, 30, 0.8f}; // to the right
	enemy.forward = {0, 1, 0};
	enemy.right = {-1, 0, 0};
	p.others = {mate, enemy};
	const AIOutput out = brain.Think(p, 0.016f);
	CHECK_EQ(out.targetId, 3);
	CHECK(out.controls.steer > 0.3f);
	CHECK(out.controls.throttle > 0.5f);
}

TEST_CASE("ai: returns to the middle near a ring-out edge")
{
	AIBrain brain(*FixtureDb().FindAIProfile("regular"), 5);
	AIPerception p = Basic();
	p.arena.ringOut = true;
	p.arena.circular = true;
	p.arena.halfX = 40.0f;
	p.self.position = {36, 0, 0.8f};
	p.self.velocity = {10, 0, 0}; // heading off the edge
	AICarInfo enemy;
	enemy.id = 9;
	enemy.team = 1;
	enemy.position = {39, 5, 0.8f};
	p.others = {enemy};
	const AIOutput out = brain.Think(p, 0.016f);
	CHECK(out.state == AIState::ReturnToArena);
	CHECK(out.controls.brake > 0.5f || std::fabs(out.controls.steer) > 0.5f);
}

TEST_CASE("ai: reverses out when stuck and asks for a reset when flipped")
{
	AIBrain brain(*FixtureDb().FindAIProfile("veteran"), 7);
	AIPerception p = Basic();
	AICarInfo enemy;
	enemy.id = 4;
	enemy.team = 1;
	enemy.position = {40, 0, 0.8f};
	p.others = {enemy};
	bool reversed = false;
	for (int i = 0; i < 200; ++i)
	{
		p.time = static_cast<float>(i) * 0.016f;
		const AIOutput out = brain.Think(p, 0.016f);
		reversed = reversed || out.state == AIState::Recover;
	}
	CHECK(reversed);
	AIBrain flipper(*FixtureDb().FindAIProfile("veteran"), 8);
	AIPerception up = Basic();
	up.upsideDown = true;
	bool reset = false;
	for (int i = 0; i < 200 && !reset; ++i)
	{
		reset = flipper.Think(up, 0.016f).requestReset;
	}
	CHECK(reset);
}

TEST_CASE("ai: two AI cars actually find and hit each other")
{
	HeadlessConfig c;
	c.seed = 11;
	HeadlessWorld world(FixtureDb(), c);
	VehicleBuild a;
	a.vehicleId = "test_sedan";
	VehicleBuild b;
	b.vehicleId = "test_hatch";
	world.AddCar(a, {-30, 0, 0}, 90.0f, 0, FixtureDb().FindAIProfile("elite"));
	world.AddCar(b, {30, 10, 0}, -90.0f, 1, FixtureDb().FindAIProfile("elite"));
	for (int i = 0; i < 120 * 30 && world.ImpactCount() == 0; ++i)
	{
		world.Step();
	}
	CHECK_MSG(world.ImpactCount() > 0, "no impact after " << world.Time() << "s");
}
