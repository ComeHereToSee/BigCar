// Copyright BigCar Team. All Rights Reserved.
//
// End-to-end AI battles in the headless world: physics + AI + impact + damage + scoring + match rules together.

#include "CrashCore/HeadlessSim.h"
#include "TestData.h"
#include "TestFramework.h"

using namespace crash;
using namespace bigcar_test;

namespace
{
struct Result
{
	bool finished = false;
	float time = 0.0f;
	int impacts = 0;
	int takedowns = 0;
	int winner = -1;
	bool finite = true;
};

Result Battle(const char* modeId, const std::vector<std::pair<const char*, int>>& cars, uint64_t seed, float limit, bool ringArena = false,
              float maxTotal = 0.0f)
{
	const GameDatabase& db = FixtureDb();
	HeadlessConfig c;
	c.seed = seed;
	c.arenaHalfX = 45.0f;
	c.arenaHalfY = 45.0f;
	HeadlessWorld world(db, c);
	if (ringArena)
	{
		world.UseArena(*db.FindArena("test_ring"));
	}
	MatchRules match(*db.FindMode(modeId), db.Progression().score, limit);
	world.SetMatch(&match);
	Result r;
	world.onWrecked = [&r](int, const TakedownInfo& info) { r.takedowns += info.killer >= 0 ? 1 : 0; };
	const int n = static_cast<int>(cars.size());
	for (int i = 0; i < n; ++i)
	{
		VehicleBuild b;
		b.vehicleId = cars[static_cast<size_t>(i)].first;
		const float a = kTwoPi * static_cast<float>(i) / static_cast<float>(n);
		const int id = world.AddCar(b, {std::cos(a) * 25.0f, std::sin(a) * 25.0f, 0}, a * kRadToDeg + 180.0f,
		                            cars[static_cast<size_t>(i)].second, db.FindAIProfile("veteran"));
		match.AddParticipant(id, cars[static_cast<size_t>(i)].second, true);
	}
	match.Start(0.0f, 0.5f);
	const float hardStop = maxTotal > 0.0f ? maxTotal : limit + 5.0f;
	while (match.Phase() != MatchPhase::Finished && world.Time() < hardStop)
	{
		world.Step();
	}
	for (const auto& car : world.Cars())
	{
		r.finite = r.finite && car->body.position.IsFinite();
	}
	r.finished = match.Phase() == MatchPhase::Finished;
	r.time = world.Time();
	r.impacts = world.ImpactCount();
	r.winner = match.Outcome().winnerTeam;
	return r;
}
} // namespace

TEST_CASE("battle: 1v1 derby produces crashes and a winner")
{
	const Result r = Battle("derby", {{"test_sedan", 0}, {"test_truck", 1}}, 21, 150.0f);
	CHECK(r.finite);
	CHECK(r.finished);
	CHECK_MSG(r.impacts >= 3, "impacts " << r.impacts);
	CHECK_MSG(r.winner >= 0, "no winner after " << r.time << "s, impacts " << r.impacts);
}

TEST_CASE("battle: 6-car free-for-all derby wrecks cars")
{
	// The battle is chaotic: a different compiler or libm changes which car wrecks first, so judge several seeds together.
	int takedowns = 0;
	int impacts = 0;
	for (const uint64_t seed : {33u, 34u, 35u, 36u})
	{
		const Result r =
		    Battle("derby", {{"test_hatch", 0}, {"test_sedan", 1}, {"test_super", 2}, {"test_truck", 3}, {"test_ev", 4}, {"test_sedan", 5}},
		           seed, 150.0f);
		CHECK(r.finite);
		CHECK(r.finished);
		CHECK_MSG(r.impacts >= 10, "seed " << seed << " impacts " << r.impacts);
		takedowns += r.takedowns;
		impacts += r.impacts;
	}
	CHECK_MSG(takedowns >= 4, "takedowns " << takedowns << " impacts " << impacts << " over 4 matches");
}

TEST_CASE("battle: 3v3 team elimination finishes")
{
	const Result r = Battle("team_elimination",
	                        {{"test_sedan", 0}, {"test_truck", 0}, {"test_hatch", 0}, {"test_sedan", 1}, {"test_truck", 1}, {"test_ev", 1}},
	                        44, 60.0f, false, 5.0f * 70.0f); // up to five rounds of 60 s
	CHECK(r.finite);
	CHECK(r.finished);
	CHECK(r.impacts > 5);
}

TEST_CASE("battle: ring-out arena pushes cars off the edge")
{
	const Result r = Battle("ring_out", {{"test_truck", 0}, {"test_hatch", 1}, {"test_hatch", 2}, {"test_sedan", 3}}, 55, 120.0f, true);
	CHECK(r.finite);
	CHECK(r.finished);
	CHECK(r.impacts > 0);
}
