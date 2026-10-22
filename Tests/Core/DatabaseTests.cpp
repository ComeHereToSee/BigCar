// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Database.h"
#include "CrashCore/Localization.h"
#include "TestData.h"
#include "TestFramework.h"

#include <filesystem>
#include <regex>
#include <set>

using namespace crash;
using namespace bigcar_test;

TEST_CASE("database: fixture data loads without errors")
{
	const GameDatabase& db = FixtureDb();
	CHECK_MSG(!db.HasErrors(), db.FormatMessages());
	CHECK_EQ(db.Vehicles().size(), 5u);
	CHECK(db.FindVehicle("test_super") != nullptr);
	CHECK(db.FindVehicle("nope") == nullptr);
	CHECK_EQ(db.StarterVehicles().size(), 1u);
	CHECK(db.FindLevel("ch1_l2") != nullptr);
	CHECK_EQ(db.FindLevel("ch1_l2")->indexInChapter, 1);
	CHECK_EQ(db.FindArena("test_ring")->walls, false);
	CHECK(db.FindArena("test_box")->FindHill() != nullptr);
	CHECK(!db.Graphics().options.empty());
}

TEST_CASE("database: upgrade cost formula (roundTo100 of cost * tier multiplier)")
{
	const GameDatabase& db = FixtureDb();
	const VehicleDef* hatch = db.FindVehicle("test_hatch"); // tier 1
	const VehicleDef* sedan = db.FindVehicle("test_sedan"); // tier 2 (x1.6)
	const VehicleDef* super = db.FindVehicle("test_super"); // tier 4 (x4.0)
	REQUIRE(hatch && sedan && super);
	CHECK_EQ(db.UpgradeCost(*hatch, UpgradeCategory::Engine, 1), 3000);
	CHECK_EQ(db.UpgradeCost(*sedan, UpgradeCategory::Engine, 2), 11200); // 7000 * 1.6
	CHECK_EQ(db.UpgradeCost(*super, UpgradeCategory::Armor, 5), 240000); // 60000 * 4
	CHECK_EQ(db.UpgradeCost(*hatch, UpgradeCategory::Engine, 6), -1);
	CHECK_EQ(RoundTo100(1249.0), 1200);
	CHECK_EQ(RoundTo100(1250.0), 1300);
}

TEST_CASE("database: ranged equipment and unknown mods are rejected")
{
	GameDatabase db;
	const char* bad = R"({"version": 1, "equipment": [
		{"id": "rocket", "slot": "roof", "name": "equip.rocket", "contactOnly": false, "range": 50,
		 "geometry": {"shape": "none"}, "mods": {"laserDamage": 3}, "ability": null, "cr": 5, "price": {"credits": 10}}
	]})";
	CHECK(!db.LoadFile("equipment.json", bad));
	const std::string messages = db.FormatMessages();
	CHECK(messages.find("contactOnly") != std::string::npos);
	CHECK(messages.find("range") != std::string::npos);
	CHECK(messages.find("laserDamage") != std::string::npos);
}

TEST_CASE("database: schema errors are reported with context")
{
	GameDatabase db;
	CHECK(!db.LoadFile("vehicles.json", R"({"version": 1, "vehicles": [{"id": "Bad Id", "brand": "x", "name": "n",
		"class": "spaceship", "tier": 9, "price": {"diamonds": 5}, "gears": [1.0, 2.0]}]})"));
	const std::string m = db.FormatMessages();
	CHECK(m.find("invalid id") != std::string::npos);
	CHECK(m.find("class") != std::string::npos);
	CHECK(m.find("tier") != std::string::npos);
	CHECK(m.find("price") != std::string::npos);
	CHECK(m.find("decreasing") != std::string::npos);
	GameDatabase db2;
	CHECK(!db2.LoadFile("modes.json", R"({"version": 2, "modes": []})"));
	GameDatabase db3;
	CHECK(!db3.LoadFile("arenas.json", "{ not json"));
	CHECK(db3.FormatMessages().find("line 1") != std::string::npos);
}

TEST_CASE("database: arena bounds helpers")
{
	ArenaDef rect;
	rect.shape = ArenaShape::Rect;
	rect.sizeX = 100;
	rect.sizeY = 60;
	CHECK(rect.IsInside({49, 29, 0}));
	CHECK(!rect.IsInside({49, 31, 0}));
	CHECK(!rect.IsInside({49, 0, 0}, 2.0f));
	ArenaDef circle;
	circle.shape = ArenaShape::Circle;
	circle.sizeX = 100;
	CHECK(circle.IsInside({35, 35, 0}));
	CHECK(!circle.IsInside({40, 40, 0}));
	ArenaDef oct;
	oct.shape = ArenaShape::Octagon;
	oct.sizeX = 100;
	CHECK(oct.IsInside({45, 0, 0}));
	CHECK(!oct.IsInside({40, 40, 0}));
}

// ---------------------------------------------------------------------------------------------
// Real content (Content/BigCar/Data). These guard the shipped data in CI.

TEST_CASE("content: shipped data loads and cross-references are valid")
{
	if (!RealDataAvailable())
	{
		std::printf("  (skipped: no content data)\n");
		return;
	}
	GameDatabase db;
	const bool ok = db.LoadAll(DirReader(BIGCAR_DATA_DIR));
	CHECK_MSG(ok, "\n" << db.FormatMessages());
	CHECK(db.Vehicles().size() >= 40);
	CHECK(db.Equipment().size() >= 20);
	CHECK(db.Arenas().size() >= 10);
	int levels = 0;
	for (const ChapterDef& ch : db.Campaign().chapters)
	{
		levels += static_cast<int>(ch.levels.size());
	}
	CHECK(levels >= 40);
	// Every slot has at least one item and every ability exists.
	for (int s = 0; s < kEquipSlotCount; ++s)
	{
		bool found = false;
		for (const EquipmentDef& e : db.Equipment())
		{
			found = found || static_cast<int>(e.slot) == s;
		}
		CHECK_MSG(found, "slot " << ToString(static_cast<EquipSlot>(s)));
	}
	for (int a = 1; a < static_cast<int>(AbilityType::Count); ++a)
	{
		bool found = false;
		for (const EquipmentDef& e : db.Equipment())
		{
			found = found || static_cast<int>(e.ability) == a;
		}
		CHECK_MSG(found, "ability " << ToString(static_cast<AbilityType>(a)));
	}
}

TEST_CASE("content: every referenced localization key exists in zh-Hans and en")
{
	if (!RealDataAvailable())
	{
		return;
	}
	GameDatabase db;
	db.LoadAll(DirReader(BIGCAR_DATA_DIR));
	Localization loc;
	std::string error;
	CHECK_MSG(LoadLocalization(BIGCAR_DATA_DIR, loc, error), error);
	const std::vector<std::string> keys = db.ReferencedLocKeys();
	CHECK(!keys.empty());
	for (const char* lang : {"zh-Hans", "en"})
	{
		const std::vector<std::string> missing = loc.MissingKeys(lang, keys);
		std::string list;
		for (size_t i = 0; i < missing.size() && i < 20; ++i)
		{
			list += missing[i] + " ";
		}
		CHECK_MSG(missing.empty(), lang << " missing " << missing.size() << " keys: " << list);
	}
}

TEST_CASE("content: every ui.* key used by the game code exists in zh-Hans and en")
{
	if (!RealDataAvailable())
	{
		return;
	}
	Localization loc;
	std::string error;
	LoadLocalization(BIGCAR_DATA_DIR, loc, error);
	// Scan the Unreal module for BC_LOC("ui.xxx") / Loc("ui.xxx") style literals.
	const std::regex keyPattern("\"(ui\\.[a-z0-9_.]+)\"");
	std::set<std::string> used;
	const std::filesystem::path src = std::filesystem::path(BIGCAR_SOURCE_DIR) / "BigCar";
	if (!std::filesystem::exists(src))
	{
		return;
	}
	for (const auto& entry : std::filesystem::recursive_directory_iterator(src))
	{
		const std::string ext = entry.path().extension().string();
		if (ext != ".cpp" && ext != ".h")
		{
			continue;
		}
		std::string text;
		ReadFile(entry.path().string(), text);
		for (std::sregex_iterator it(text.begin(), text.end(), keyPattern), end; it != end; ++it)
		{
			const std::string key = (*it)[1].str();
			if (key.back() != '.' && key.size() > 5 && key.compare(key.size() - 5, 5, ".json") != 0)
			{
				used.insert(key);
			}
		}
	}
	for (const char* lang : {"zh-Hans", "en"})
	{
		std::string list;
		int missing = 0;
		for (const std::string& key : used)
		{
			if (!loc.HasIn(lang, key))
			{
				++missing;
				if (missing <= 25)
				{
					list += key + " ";
				}
			}
		}
		CHECK_MSG(missing == 0, lang << " missing " << missing << " ui keys: " << list);
	}
}
