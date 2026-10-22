// Copyright BigCar Team. All Rights Reserved.
//
// crashsim — headless AI battle simulator for balance and regression testing.
//
//   crashsim --data Content/BigCar/Data --smoke                 quick sanity run used by CI
//   crashsim --data Content/BigCar/Data --duels 20 --tier 3     round-robin duels inside a tier, prints win rates
//   crashsim --data Content/BigCar/Data --balance --max-winrate 0.8   fails if a car dominates its tier
//   crashsim --data ... --mode derby --cars 8 --matches 50 --seed 7 --csv out.csv

#include "CrashCore/HeadlessSim.h"
#include "CrashCore/Match.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

using namespace crash;

namespace
{
struct Options
{
	std::string dataDir = "Content/BigCar/Data";
	bool smoke = false;
	bool balance = false;
	int duels = 0;
	int tier = 0;
	std::string mode = "derby";
	int cars = 6;
	int matches = 0;
	uint64_t seed = 1;
	float maxWinRate = 0.85f;
	float maxSeconds = 180.0f;
	std::string csv;
};

bool ReadText(const std::string& path, std::string& out)
{
	std::ifstream in(path, std::ios::binary);
	if (!in)
	{
		return false;
	}
	std::ostringstream ss;
	ss << in.rdbuf();
	out = ss.str();
	return true;
}

struct MatchStats
{
	int winnerTeam = -1;
	float duration = 0.0f;
	int impacts = 0;
	int takedowns = 0;
	bool finished = false;
	bool finite = true;
	std::map<int, int> impactKinds;
};

MatchStats RunMatch(const GameDatabase& db, const ModeDef& mode, const std::vector<std::pair<VehicleBuild, int>>& entrants,
                    const std::string& aiProfile, uint64_t seed, float maxSeconds)
{
	MatchStats stats;
	HeadlessConfig config;
	config.seed = seed;
	config.arenaHalfX = 55.0f;
	config.arenaHalfY = 55.0f;
	config.ringOut = mode.ringOut;
	config.walls = !mode.ringOut;
	config.circular = mode.ringOut;
	HeadlessWorld world(db, config);
	MatchRules match(mode, db.Progression().score, std::min(mode.timeLimit, maxSeconds));
	world.SetMatch(&match);
	const AIProfileDef* ai = db.FindAIProfile(aiProfile);
	const int n = static_cast<int>(entrants.size());
	for (int i = 0; i < n; ++i)
	{
		const float angle = kTwoPi * static_cast<float>(i) / static_cast<float>(n);
		const Vec3 pos{std::cos(angle) * 30.0f, std::sin(angle) * 30.0f, 0.0f};
		const int id = world.AddCar(entrants[static_cast<size_t>(i)].first, pos, angle * kRadToDeg + 180.0f,
		                            entrants[static_cast<size_t>(i)].second, ai);
		match.AddParticipant(id, entrants[static_cast<size_t>(i)].second, true);
	}
	world.onImpact = [&stats](const ImpactResult& r)
	{
		++stats.impacts;
		stats.impactKinds[static_cast<int>(r.kind)] += 1;
	};
	world.onWrecked = [&stats](int, const TakedownInfo& info) { stats.takedowns += info.killer >= 0 ? 1 : 0; };
	match.Start(0.0f, 0.5f);
	const int maxSteps = static_cast<int>((maxSeconds + 10.0f) / config.dt);
	for (int step = 0; step < maxSteps && match.Phase() != MatchPhase::Finished; ++step)
	{
		world.Step();
	}
	for (const auto& car : world.Cars())
	{
		stats.finite = stats.finite && car->body.position.IsFinite() && car->body.velocity.IsFinite();
	}
	if (match.Phase() != MatchPhase::Finished)
	{
		match.ForceFinish(world.Time(), -1);
	}
	stats.finished = true;
	stats.winnerTeam = match.Outcome().winnerTeam;
	stats.duration = world.Time();
	return stats;
}

int Smoke(const GameDatabase& db, const Options& opt)
{
	// A few quick matches across modes with real content. Fails on NaNs, no impacts, or no takedowns at all.
	int failures = 0;
	int totalImpacts = 0;
	int totalTakedowns = 0;
	const std::vector<const VehicleDef*> pool = [&db]
	{
		std::vector<const VehicleDef*> v;
		for (const VehicleDef& def : db.Vehicles())
		{
			v.push_back(&def);
		}
		return v;
	}();
	if (pool.empty())
	{
		std::fprintf(stderr, "no vehicles\n");
		return 1;
	}
	const char* modes[] = {"derby", "takedown", "team_elimination", "ring_out"};
	Pcg32 rng(opt.seed);
	for (int m = 0; m < 4; ++m)
	{
		const ModeDef* mode = db.FindMode(modes[m]);
		if (!mode)
		{
			continue;
		}
		std::vector<std::pair<VehicleBuild, int>> entrants;
		const int count = mode->teams >= 2 ? 6 : 6;
		for (int i = 0; i < count; ++i)
		{
			VehicleBuild b;
			b.vehicleId = pool[rng.NextBounded(static_cast<uint32_t>(pool.size()))]->id;
			b.upgrades[0] = static_cast<int>(rng.NextBounded(4));
			entrants.emplace_back(b, mode->teams >= 2 ? i % 2 : i);
		}
		const MatchStats s = RunMatch(db, *mode, entrants, "regular", opt.seed + static_cast<uint64_t>(m), 90.0f);
		std::printf("smoke %-18s duration %6.1fs impacts %4d takedowns %3d winner team %d%s\n", mode->id.c_str(), s.duration, s.impacts,
		            s.takedowns, s.winnerTeam, s.finite ? "" : "  NaN!");
		totalImpacts += s.impacts;
		totalTakedowns += s.takedowns;
		failures += s.finite ? 0 : 1;
	}
	if (totalImpacts == 0)
	{
		std::fprintf(stderr, "smoke: no impacts happened — AI or collision pipeline is broken\n");
		++failures;
	}
	if (totalTakedowns == 0)
	{
		std::fprintf(stderr, "smoke: no takedowns happened — damage pipeline is broken\n");
		++failures;
	}
	return failures == 0 ? 0 : 1;
}

int Duels(const GameDatabase& db, const Options& opt)
{
	std::vector<const VehicleDef*> cars;
	for (const VehicleDef& v : db.Vehicles())
	{
		if (opt.tier == 0 || v.tier == opt.tier)
		{
			cars.push_back(&v);
		}
	}
	const ModeDef* derby = db.FindMode("derby");
	if (cars.size() < 2 || !derby)
	{
		std::fprintf(stderr, "need at least two cars and the derby mode\n");
		return 1;
	}
	std::map<std::string, std::pair<int, int>> record; // wins, games
	std::ofstream csv;
	if (!opt.csv.empty())
	{
		csv.open(opt.csv);
		csv << "a,b,winner,duration,impacts\n";
	}
	uint64_t seed = opt.seed;
	for (size_t i = 0; i < cars.size(); ++i)
	{
		for (size_t j = i + 1; j < cars.size(); ++j)
		{
			for (int k = 0; k < opt.duels; ++k)
			{
				VehicleBuild a;
				a.vehicleId = cars[i]->id;
				VehicleBuild b;
				b.vehicleId = cars[j]->id;
				const MatchStats s = RunMatch(db, *derby, {{a, 0}, {b, 1}}, "veteran", ++seed, opt.maxSeconds);
				record[a.vehicleId].second += 1;
				record[b.vehicleId].second += 1;
				if (s.winnerTeam == 0)
				{
					record[a.vehicleId].first += 1;
				}
				else if (s.winnerTeam == 1)
				{
					record[b.vehicleId].first += 1;
				}
				if (csv)
				{
					csv << a.vehicleId << "," << b.vehicleId << "," << s.winnerTeam << "," << s.duration << "," << s.impacts << "\n";
				}
			}
		}
	}
	std::vector<std::pair<std::string, float>> rates;
	for (const auto& [id, wg] : record)
	{
		rates.emplace_back(id, wg.second > 0 ? static_cast<float>(wg.first) / static_cast<float>(wg.second) : 0.0f);
	}
	std::sort(rates.begin(), rates.end(), [](const auto& x, const auto& y) { return x.second > y.second; });
	int failures = 0;
	for (const auto& [id, rate] : rates)
	{
		const bool dominant = opt.balance && rate > opt.maxWinRate;
		std::printf("%-28s win rate %5.1f%%%s\n", id.c_str(), rate * 100.0f, dominant ? "  <-- dominant" : "");
		failures += dominant ? 1 : 0;
	}
	return failures == 0 ? 0 : 1;
}

int Matches(const GameDatabase& db, const Options& opt)
{
	const ModeDef* mode = db.FindMode(opt.mode);
	if (!mode)
	{
		std::fprintf(stderr, "unknown mode %s\n", opt.mode.c_str());
		return 1;
	}
	Pcg32 rng(opt.seed);
	std::map<int, int> kinds;
	float totalDuration = 0.0f;
	int totalImpacts = 0;
	for (int m = 0; m < opt.matches; ++m)
	{
		std::vector<std::pair<VehicleBuild, int>> entrants;
		for (int i = 0; i < opt.cars; ++i)
		{
			VehicleBuild b;
			b.vehicleId = db.Vehicles()[rng.NextBounded(static_cast<uint32_t>(db.Vehicles().size()))].id;
			entrants.emplace_back(b, mode->teams >= 2 ? i % mode->teams : i);
		}
		const MatchStats s = RunMatch(db, *mode, entrants, "regular", opt.seed + static_cast<uint64_t>(m) * 31u, opt.maxSeconds);
		totalDuration += s.duration;
		totalImpacts += s.impacts;
		for (const auto& [k, c] : s.impactKinds)
		{
			kinds[k] += c;
		}
	}
	std::printf("%d matches of %s: avg duration %.1fs, avg impacts %.1f\n", opt.matches, mode->id.c_str(),
	            totalDuration / std::max(1, opt.matches), static_cast<float>(totalImpacts) / std::max(1, opt.matches));
	for (const auto& [k, c] : kinds)
	{
		std::printf("  %-12s %d\n", std::string(ToString(static_cast<ImpactKind>(k))).c_str(), c);
	}
	return 0;
}
} // namespace

int main(int argc, char** argv)
{
	Options opt;
	for (int i = 1; i < argc; ++i)
	{
		const std::string a = argv[i];
		auto next = [&](const char* name) -> std::string
		{
			if (i + 1 >= argc)
			{
				std::fprintf(stderr, "%s needs a value\n", name);
				std::exit(2);
			}
			return argv[++i];
		};
		if (a == "--data")
		{
			opt.dataDir = next("--data");
		}
		else if (a == "--smoke")
		{
			opt.smoke = true;
		}
		else if (a == "--balance")
		{
			opt.balance = true;
		}
		else if (a == "--duels")
		{
			opt.duels = std::atoi(next("--duels").c_str());
		}
		else if (a == "--tier")
		{
			opt.tier = std::atoi(next("--tier").c_str());
		}
		else if (a == "--mode")
		{
			opt.mode = next("--mode");
		}
		else if (a == "--cars")
		{
			opt.cars = std::atoi(next("--cars").c_str());
		}
		else if (a == "--matches")
		{
			opt.matches = std::atoi(next("--matches").c_str());
		}
		else if (a == "--seed")
		{
			opt.seed = static_cast<uint64_t>(std::strtoull(next("--seed").c_str(), nullptr, 10));
		}
		else if (a == "--max-winrate")
		{
			opt.maxWinRate = static_cast<float>(std::atof(next("--max-winrate").c_str()));
		}
		else if (a == "--max-seconds")
		{
			opt.maxSeconds = static_cast<float>(std::atof(next("--max-seconds").c_str()));
		}
		else if (a == "--csv")
		{
			opt.csv = next("--csv");
		}
		else
		{
			std::fprintf(stderr, "unknown argument %s\n", a.c_str());
			return 2;
		}
	}

	GameDatabase db;
	const bool loaded = db.LoadAll([&opt](const std::string& name, std::string& text) { return ReadText(opt.dataDir + "/" + name, text); });
	if (!loaded)
	{
		std::fprintf(stderr, "data errors:\n%s", db.FormatMessages().c_str());
		// The smoke test still requires valid data.
		return 1;
	}
	if (opt.smoke)
	{
		return Smoke(db, opt);
	}
	if (opt.duels > 0)
	{
		return Duels(db, opt);
	}
	if (opt.matches > 0)
	{
		return Matches(db, opt);
	}
	std::printf("nothing to do (use --smoke, --duels N or --matches N)\n");
	return 0;
}
