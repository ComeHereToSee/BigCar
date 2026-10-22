// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Campaign.h"
#include "CrashCore/Profile.h"
#include "TestData.h"
#include "TestFramework.h"

using namespace crash;
using namespace bigcar_test;

TEST_CASE("profile: new profile gets starters and credits")
{
	const GameDatabase& db = FixtureDb();
	const Profile p = MakeNewProfile(db, "p1", "Tester");
	CHECK_EQ(p.credits, 25000);
	CHECK_EQ(p.gold, 0);
	REQUIRE(p.vehicles.size() == 1u);
	CHECK_EQ(p.selected, std::string("test_hatch"));
	CHECK(p.OwnsCosmetic(db, "paint_red"));    // free
	CHECK(!p.OwnsCosmetic(db, "paint_gold"));  // paid
	CHECK(p.OwnsEquipment(db, "front_basic")); // free equipment
	CHECK(p.SelectedBuild() != nullptr);
}

TEST_CASE("profile: garage operations and error codes")
{
	const GameDatabase& db = FixtureDb();
	Profile p = MakeNewProfile(db, "p1", "Tester");
	CHECK_EQ(BuyVehicle(db, p, "test_sedan").error, std::string("insufficient_funds"));
	CHECK_EQ(BuyVehicle(db, p, "test_hatch").error, std::string("already_owned"));
	CHECK_EQ(BuyVehicle(db, p, "nope").error, std::string("not_found"));
	p.credits = 1000000;
	CHECK(BuyVehicle(db, p, "test_sedan").ok);
	CHECK_EQ(p.credits, 940000);
	CHECK(SelectVehicle(db, p, "test_sedan").ok);
	CHECK_EQ(SelectVehicle(db, p, "test_super").error, std::string("not_owned"));

	// Upgrades: costs follow the tier multiplier and cap at 5.
	const OpResult up = UpgradeVehicle(db, p, "test_sedan", UpgradeCategory::Engine);
	CHECK(up.ok);
	CHECK_EQ(up.creditsSpent, 4800); // 3000 * 1.6
	for (int i = 0; i < 4; ++i)
	{
		CHECK(UpgradeVehicle(db, p, "test_sedan", UpgradeCategory::Engine).ok);
	}
	CHECK_EQ(UpgradeVehicle(db, p, "test_sedan", UpgradeCategory::Engine).error, std::string("max_level"));
	CHECK_EQ(p.FindVehicle("test_sedan")->UpgradeLevel(UpgradeCategory::Engine), 5);

	// Equipment: must own, must match slot.
	CHECK_EQ(EquipItem(db, p, "test_sedan", EquipSlot::Front, "front_wedge").error, std::string("not_owned"));
	CHECK(BuyEquipment(db, p, "front_wedge").ok);
	CHECK_EQ(BuyEquipment(db, p, "front_wedge").error, std::string("already_owned"));
	CHECK_EQ(EquipItem(db, p, "test_sedan", EquipSlot::Side, "front_wedge").error, std::string("invalid_slot"));
	CHECK(EquipItem(db, p, "test_sedan", EquipSlot::Front, "front_wedge").ok);
	CHECK_EQ(p.FindVehicle("test_sedan")->Equipped(EquipSlot::Front), std::string("front_wedge"));
	CHECK(EquipItem(db, p, "test_sedan", EquipSlot::Front, "").ok);
	CHECK(p.FindVehicle("test_sedan")->Equipped(EquipSlot::Front).empty());

	// Cosmetics.
	CHECK_EQ(CustomizeVehicle(db, p, "test_sedan", "paint_gold", "").error, std::string("not_owned"));
	CHECK(BuyCosmetic(db, p, "paint_gold").ok);
	CHECK(CustomizeVehicle(db, p, "test_sedan", "paint_gold", "wheels_stock").ok);
	CHECK_EQ(p.FindVehicle("test_sedan")->paint, std::string("paint_gold"));
	CHECK_EQ(CustomizeVehicle(db, p, "test_sedan", "wheels_stock", "").error, std::string("not_found")); // wrong type
}

TEST_CASE("profile: combat rating and effective stats")
{
	const GameDatabase& db = FixtureDb();
	VehicleBuild b;
	b.vehicleId = "test_sedan";
	CHECK_EQ(ComputeCombatRating(db, b), 200);
	b.upgrades[static_cast<size_t>(UpgradeCategory::Engine)] = 3;
	b.upgrades[static_cast<size_t>(UpgradeCategory::Armor)] = 2;
	b.equipment[static_cast<size_t>(EquipSlot::Front)] = "front_wedge";
	CHECK_EQ(ComputeCombatRating(db, b), 200 + 50 + 25);
	const EffectiveStats s = ComputeEffectiveStats(db, b);
	CHECK_NEAR(s.powerW, 224000.0f * 1.15f, 1.0f);
	CHECK_NEAR(s.massKg, 1600.0f + 40.0f + 80.0f, 0.5f);
	CHECK_NEAR(s.armor[0], 0.05f + 0.06f + 0.1f, 1e-4f);
	CHECK_NEAR(s.launchPower, 1.0f, 1e-5f);
	CHECK(s.HasShape(GeometryShape::Wedge));
	CHECK(ValidateBuild(db, b).empty());
	b.equipment[static_cast<size_t>(EquipSlot::Side)] = "front_ram";
	CHECK_EQ(ValidateBuild(db, b), std::string("invalid_slot"));
	const DisplayRatings r = ComputeDisplayRatings(s);
	for (const float v : {r.speed, r.acceleration, r.handling, r.weight, r.durability, r.ramming})
	{
		CHECK(v >= 1.0f && v <= 10.0f);
	}
	VehicleBuild super;
	super.vehicleId = "test_super";
	VehicleBuild truck;
	truck.vehicleId = "test_truck";
	const DisplayRatings rs = ComputeDisplayRatings(ComputeEffectiveStats(db, super));
	const DisplayRatings rt = ComputeDisplayRatings(ComputeEffectiveStats(db, truck));
	CHECK(rs.speed > rt.speed);
	CHECK(rt.ramming > rs.ramming);
	CHECK(rt.durability > rs.durability);
}

TEST_CASE("profile: xp, level rewards and match rewards")
{
	const GameDatabase& db = FixtureDb();
	Profile p = MakeNewProfile(db, "p1", "Tester");
	CHECK_EQ(XpToNextLevel(db.Progression(), 1), 1000);
	CHECK_EQ(XpToNextLevel(db.Progression(), 2), 1100);
	const LevelUpResult r = AddXp(db, p, 2150);
	CHECK_EQ(r.levelsGained, 2);
	CHECK_EQ(p.level, 3);
	CHECK_EQ(p.xp, 50);
	CHECK_EQ(p.credits, 30000);              // +5000 at level 2
	CHECK(p.OwnsEquipment(db, "front_ram")); // level 3 reward
	const MatchReward win = ComputeMatchReward(db.Progression(), 2000, true, true);
	CHECK_EQ(win.credits, static_cast<int64_t>(std::llround((800 + 2000 * 1.5) * 1.5 * 1.25)));
	CHECK_EQ(win.xp, static_cast<int64_t>(std::llround((150 + 2000 * 0.8) * 1.5 * 1.25)));
	CHECK_EQ(ComputeMatchReward(db.Progression(), 10000000, true, true).credits, 40000);
}

TEST_CASE("profile: duplicate grants convert to credits")
{
	const GameDatabase& db = FixtureDb();
	Profile p = MakeNewProfile(db, "p1", "Tester");
	const int64_t before = p.credits;
	const OpResult r = GrantItems(db, p, {{ItemType::Vehicle, "test_hatch", 0}, {ItemType::Currency, "gold", 25}});
	CHECK_EQ(p.gold, 25);
	CHECK_EQ(p.credits, before); // starter is free -> no refund
	GrantItems(db, p, {{ItemType::Paint, "paint_gold", 0}});
	GrantItems(db, p, {{ItemType::Paint, "paint_gold", 0}});
	CHECK_EQ(p.credits, before + 10000);
	CHECK(!r.granted.empty());
}

TEST_CASE("profile: json round trip and backend compatible layout")
{
	const GameDatabase& db = FixtureDb();
	Profile p = MakeNewProfile(db, "p_123", "车神");
	p.credits = 123456;
	p.gold = 77;
	p.purchases["offer_x"] = 2;
	p.ranked["ranked_3v3"].rating = 1712.5;
	p.stats.impacts["t_bone"] = 4;
	p.campaign.levels["ch1_l1"].completed = true;
	p.campaign.levels["ch1_l1"].stars = {true, false, true};
	p.campaign.levels["ch1_l1"].collectibles = {true, false};
	p.campaign.hubs["ch1"].secrets.push_back("hub_s1");
	p.loadout.title = "title_rookie";
	const JsonValue json = p.ToJson();
	CHECK_EQ(json["wallet"]["credits"].AsInt64(), 123456);
	CHECK(json["garage"]["vehicles"].IsArray());
	CHECK_EQ(json["garage"]["vehicles"][0]["vehicleId"].AsString(), std::string("test_hatch"));
	Profile back;
	REQUIRE(Profile::FromJson(json, back));
	CHECK_EQ(back.ToJson().Dump(), json.Dump());
	CHECK_EQ(back.displayName, std::string("车神"));
	CHECK_NEAR(back.ranked["ranked_3v3"].rating, 1712.5, 1e-9);

	// A backend document (no offline sections) parses too.
	JsonValue server;
	REQUIRE(ParseJson(R"({"version":1,"playerId":"p_x","displayName":"D","level":4,"xp":10,
		"wallet":{"credits":5,"gold":6},"garage":{"selected":"test_hatch","vehicles":[{"vehicleId":"test_hatch",
		"upgrades":{"engine":2},"equipment":{"front":"front_basic"},"paint":"paint_red","wheels":"wheels_stock"}]},
		"inventory":{"equipment":[],"cosmetics":[]},"purchases":{},"seasonPass":{"season":"s1","xp":5,"premium":true,
		"claimedFree":[1],"claimedPremium":[]},"ranked":{},"stats":{"matches":3,"wins":1,"takedowns":2,"wrecked":1,
		"damageDealt":100,"impacts":{}}})",
	                  server));
	Profile sp;
	REQUIRE(Profile::FromJson(server, sp));
	CHECK_EQ(sp.level, 4);
	CHECK_EQ(sp.FindVehicle("test_hatch")->UpgradeLevel(UpgradeCategory::Engine), 2);
	CHECK(sp.seasonPass.premium);
}

TEST_CASE("profile: rank tiers and divisions")
{
	const ProgressionDef& prog = FixtureDb().Progression();
	auto [bronze, d1] = RankForRating(prog, 100.0);
	CHECK_EQ(bronze->id, std::string("bronze"));
	CHECK_EQ(d1, 3);
	auto [gold, d2] = RankForRating(prog, 1690.0);
	CHECK_EQ(gold->id, std::string("gold"));
	CHECK_EQ(d2, 1);
	auto [king, d3] = RankForRating(prog, 2500.0);
	CHECK_EQ(king->id, std::string("crash_king"));
	CHECK_EQ(d3, 1);
}

TEST_CASE("campaign: objectives, stars, rewards and unlocks")
{
	const GameDatabase& db = FixtureDb();
	Profile p = MakeNewProfile(db, "p1", "Tester");
	const LevelDef* l1 = db.FindLevel("ch1_l1");
	const LevelDef* l2 = db.FindLevel("ch1_l2");
	const LevelDef* c2 = db.FindLevel("ch2_l1");
	REQUIRE(l1 && l2 && c2);
	CHECK(IsLevelUnlocked(db, p, *l1));
	CHECK(!IsLevelUnlocked(db, p, *l2));
	CHECK(!IsLevelUnlocked(db, p, *c2));

	LevelRunResult fail;
	fail.won = false;
	fail.collectiblesFound = {1};
	LevelCompletion c = ApplyLevelResult(db, p, *l1, fail);
	CHECK(!c.success);
	CHECK(p.campaign.levels["ch1_l1"].collectibles[1]); // kept despite failing

	LevelRunResult win;
	win.won = true;
	win.takedowns = 1;
	win.time = 100.0f;
	c = ApplyLevelResult(db, p, *l1, win);
	CHECK(c.success);
	CHECK(c.firstClear);
	CHECK(c.newStars[0] && c.newStars[1] && c.newStars[2]);
	CHECK(p.OwnsCosmetic(db, "paint_gold")); // first clear item
	CHECK_EQ(TotalStars(p), 3);
	CHECK(IsLevelUnlocked(db, p, *l2));

	// Replay pays less and grants no new stars.
	const int64_t creditsBefore = p.credits;
	c = ApplyLevelResult(db, p, *l1, win);
	CHECK(!c.firstClear);
	CHECK_EQ(p.credits - creditsBefore, 1000);

	LevelRunResult ringWin;
	ringWin.won = true;
	ringWin.hits[static_cast<size_t>(ImpactKind::TBone)] = 2;
	ringWin.playerWrecked = true;
	c = ApplyLevelResult(db, p, *l2, ringWin);
	CHECK(c.starsThisRun[1]);
	CHECK(!c.starsThisRun[2]);          // no_wreck failed
	CHECK(IsLevelUnlocked(db, p, *c2)); // chapter 1 finale done and 5 stars >= 2
}

TEST_CASE("campaign: exploration percentage")
{
	const GameDatabase& db = FixtureDb();
	Profile p = MakeNewProfile(db, "p1", "Tester");
	ExplorationStats e = ComputeExploration(db, p);
	CHECK_NEAR(e.percent, 0.0f, 1e-4f);
	CHECK_EQ(e.levelsTotal, 3);
	CHECK_EQ(e.collectiblesTotal, 2 + 0 + 1 + 2); // levels + hub collectibles
	CHECK_EQ(e.secretsTotal, 2);
	CHECK(MarkHubSecret(p, "ch1", "hub_s1"));
	CHECK(!MarkHubSecret(p, "ch1", "hub_s1"));
	CHECK(MarkHubCollectible(p, "ch1", 0));
	e = ComputeExploration(db, p);
	CHECK_NEAR(e.percent, 100.0f * (0.4f * 1.0f / 5.0f + 0.1f * 0.5f), 1e-3f);
	const ExplorationStats ch2 = ComputeExploration(db, p, "ch2");
	CHECK_EQ(ch2.levelsTotal, 1);
	CHECK_EQ(ch2.secretsTotal, 0);
}
