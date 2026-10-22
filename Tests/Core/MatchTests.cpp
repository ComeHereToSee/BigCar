// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Match.h"
#include "TestData.h"
#include "TestFramework.h"

using namespace crash;
using namespace bigcar_test;

namespace
{
ImpactResult FakeHit(int attacker, int victim, ImpactKind kind, float damage, float severity = 0.5f)
{
	ImpactResult r;
	r.valid = true;
	r.kind = kind;
	r.attackerId = attacker;
	r.victimId = victim;
	r.aIsAttacker = true;
	r.severity = severity;
	r.b.damage.zone[0] = damage;
	r.a.damage.zone[0] = damage * 0.2f;
	return r;
}
} // namespace

TEST_CASE("scoring: crash points, combo multiplier, minimum damage")
{
	const ScoreTable& table = FixtureDb().Progression().score;
	ScoreKeeper s(table);
	s.AddParticipant(1, 0);
	s.AddParticipant(2, 1);
	const int first = s.OnImpact(FakeHit(1, 2, ImpactKind::TBone, 300.0f, 0.5f), 1.0f);
	CHECK_EQ(first, 400); // 400 * (0.7 + 0.6 * 0.5)
	const int second = s.OnImpact(FakeHit(1, 2, ImpactKind::TBone, 300.0f, 0.5f), 2.0f);
	CHECK_EQ(second, 480);                                                 // combo x1.2
	CHECK_EQ(s.OnImpact(FakeHit(1, 2, ImpactKind::TBone, 5.0f), 3.0f), 0); // below min damage
	CHECK_EQ(s.Get(1)->impacts[static_cast<size_t>(ImpactKind::TBone)], 2);
	CHECK_EQ(s.Get(1)->bestCombo, 2);
	CHECK_EQ(s.OnImpact(FakeHit(1, 2, ImpactKind::Launch, 300.0f, 1.0f), 20.0f), 650); // combo expired: 500 * 1.3
	CHECK(s.Get(2)->damageTaken > 900.0f);
}

TEST_CASE("scoring: takedown credit, assists, revenge, multi takedowns, friendly fire")
{
	const ScoreTable& table = FixtureDb().Progression().score;
	ScoreKeeper s(table);
	s.AddParticipant(1, 0);
	s.AddParticipant(2, 1);
	s.AddParticipant(3, 2);
	s.AddParticipant(4, 0); // teammate of 1
	s.OnImpact(FakeHit(3, 2, ImpactKind::SideSwipe, 200.0f), 1.0f);
	s.OnImpact(FakeHit(1, 2, ImpactKind::TBone, 400.0f), 2.0f);
	TakedownInfo t = s.OnWrecked(2, 3.0f, false);
	CHECK_EQ(t.killer, 1);
	CHECK_EQ(t.finishingKind, ImpactKind::TBone);
	REQUIRE(t.assists.size() == 1u);
	CHECK_EQ(t.assists[0], 3);
	CHECK_EQ(s.Get(1)->takedowns, 1);
	CHECK_EQ(s.Get(3)->assists, 1);

	// Revenge: 2 takes down 1 after 1 took down 2.
	s.OnImpact(FakeHit(2, 1, ImpactKind::HeadOn, 500.0f), 10.0f);
	t = s.OnWrecked(1, 10.5f, false);
	CHECK(t.revenge);

	// Ring-out without damage: last touch gets the credit.
	s.OnTouched(2, 3, 20.0f);
	t = s.OnWrecked(3, 21.0f, true);
	CHECK_EQ(t.killer, 2);
	CHECK(t.ringOut);
	CHECK_EQ(s.Get(2)->ringOuts, 1);
	CHECK_EQ(t.multiCount, 0); // previous takedown by 2 was 10.5 s ago: not a multi
	s.OnTouched(2, 4, 22.0f);
	t = s.OnWrecked(4, 23.0f, true);
	CHECK_EQ(t.killer, 2);
	CHECK_EQ(t.multiCount, 2); // two takedowns within 6 s
	// Friendly fire never credits.
	s.OnImpact(FakeHit(4, 1, ImpactKind::TBone, 500.0f), 30.0f);
	t = s.OnWrecked(1, 30.5f, false);
	CHECK_EQ(t.killer, -1);
	// Self wreck (fire, falling with nobody around) has no killer.
	t = s.OnWrecked(4, 100.0f, false);
	CHECK_EQ(t.killer, -1);
}

TEST_CASE("match: derby last car standing with placements")
{
	MatchRules m(*FixtureDb().FindMode("derby"), FixtureDb().Progression().score);
	for (int id = 1; id <= 4; ++id)
	{
		m.AddParticipant(id, id, true);
	}
	m.Start(0.0f, 3.0f);
	CHECK(m.Phase() == MatchPhase::Countdown);
	CHECK(!m.CanDrive());
	m.Update(3.1f);
	CHECK(m.Phase() == MatchPhase::Playing);
	m.OnWrecked(2, 10.0f, false);
	m.OnWrecked(4, 20.0f, false);
	CHECK(m.Phase() == MatchPhase::Playing);
	CHECK(m.TakeRespawns(100.0f).empty()); // derby has no respawns
	m.OnWrecked(1, 30.0f, false);
	CHECK(m.Phase() == MatchPhase::Finished);
	const MatchOutcome& o = m.Outcome();
	CHECK_EQ(o.winnerTeam, 3);
	REQUIRE(o.placements.size() == 4u);
	CHECK_EQ(o.placements[0], 3);
	CHECK_EQ(o.placements[1], 1);
	CHECK_EQ(o.placements[2], 4);
	CHECK_EQ(o.placements[3], 2);
}

TEST_CASE("match: team elimination best of five rounds")
{
	MatchRules m(*FixtureDb().FindMode("team_elimination"), FixtureDb().Progression().score);
	m.AddParticipant(1, 0, false);
	m.AddParticipant(2, 0, false);
	m.AddParticipant(3, 1, false);
	m.AddParticipant(4, 1, false);
	m.Start(0.0f, 1.0f);
	float t = 2.0f;
	for (int round = 0; round < 3; ++round)
	{
		m.Update(t);
		REQUIRE(m.Phase() == MatchPhase::Playing);
		m.OnWrecked(3, t + 1.0f, false);
		m.OnWrecked(4, t + 2.0f, false);
		if (round < 2)
		{
			CHECK(m.Phase() == MatchPhase::RoundEnd);
			CHECK_EQ(m.RoundWins(0), round + 1);
			m.Update(t + 2.0f + MatchRules::kRoundEndDelay + 0.1f);
			CHECK(m.TakeRoundReset());
			CHECK(m.Phase() == MatchPhase::Countdown);
			t += 2.0f + MatchRules::kRoundEndDelay + 0.1f + 3.1f;
		}
	}
	CHECK(m.Phase() == MatchPhase::Finished);
	CHECK_EQ(m.Outcome().winnerTeam, 0);
	CHECK_EQ(m.Round(), 3);
}

TEST_CASE("match: takedown mode respawns and time limit decides")
{
	MatchRules m(*FixtureDb().FindMode("takedown"), FixtureDb().Progression().score, 60.0f);
	m.AddParticipant(1, 1, false);
	m.AddParticipant(2, 2, true);
	m.Start(0.0f, 0.0f);
	m.Update(0.0f);
	ImpactResult hit;
	hit.valid = true;
	hit.kind = ImpactKind::TBone;
	hit.attackerId = 1;
	hit.victimId = 2;
	hit.aIsAttacker = true;
	hit.b.damage.zone[0] = 500.0f;
	m.Scores().OnImpact(hit, 5.0f);
	m.OnWrecked(2, 5.5f, false);
	CHECK(m.TakeRespawns(6.0f).empty());
	const std::vector<int> respawn = m.TakeRespawns(9.0f);
	REQUIRE(respawn.size() == 1u);
	CHECK_EQ(respawn[0], 2);
	m.OnRespawned(2);
	CHECK(m.Find(2)->alive);
	m.Update(59.0f);
	CHECK(m.Phase() == MatchPhase::Playing);
	CHECK_NEAR(m.TimeRemaining(59.0f), 1.0f, 0.01f);
	m.Update(60.5f);
	CHECK(m.Phase() == MatchPhase::Finished);
	CHECK_EQ(m.Outcome().winnerTeam, 1);
}

TEST_CASE("match: king of the hill needs uncontested control")
{
	MatchRules m(*FixtureDb().FindMode("king_of_hill"), FixtureDb().Progression().score);
	m.AddParticipant(1, 0, false);
	m.AddParticipant(2, 1, false);
	m.Start(0.0f, 0.0f);
	m.Update(0.0f);
	for (int i = 0; i < 100; ++i)
	{
		m.OnHillOccupants({1, 2}, 0.1f); // contested
	}
	CHECK(m.HillContested());
	CHECK(m.Phase() == MatchPhase::Playing);
	for (int i = 0; i < 601; ++i)
	{
		m.OnHillOccupants({1}, 0.1f);
		m.Update(10.0f + 0.1f * static_cast<float>(i));
	}
	CHECK(m.Phase() == MatchPhase::Finished);
	CHECK_EQ(m.Outcome().winnerTeam, 0);
	CHECK(m.Scores().Get(1)->hillTime >= 60.0f);
}

TEST_CASE("match: bounty transfers to whoever wrecks the target")
{
	MatchRules m(*FixtureDb().FindMode("bounty"), FixtureDb().Progression().score);
	m.AddParticipant(1, 1, false);
	m.AddParticipant(2, 2, false);
	m.AddParticipant(3, 3, false);
	m.Start(0.0f, 0.0f);
	m.Update(0.0f);
	CHECK_EQ(m.BountyTarget(), 1);
	m.Scores().OnTouched(3, 1, 1.0f);
	const TakedownInfo t = m.OnWrecked(1, 2.0f, false);
	CHECK(t.bountyClaimed);
	CHECK_EQ(m.BountyTarget(), 3);
}

TEST_CASE("match: survival is won by surviving the clock")
{
	MatchRules m(*FixtureDb().FindMode("survival"), FixtureDb().Progression().score);
	m.AddParticipant(1, 0, false);
	m.AddParticipant(2, 1, true);
	m.Start(0.0f, 0.0f);
	m.Update(0.0f);
	m.Update(59.0f);
	CHECK(m.Phase() == MatchPhase::Playing);
	m.Update(61.0f);
	CHECK(m.Phase() == MatchPhase::Finished);
	CHECK_EQ(m.Outcome().winnerTeam, 0);
}
