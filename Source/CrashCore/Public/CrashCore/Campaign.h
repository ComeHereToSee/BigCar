// Copyright BigCar Team. All Rights Reserved.
//
// Campaign rules: objectives and stars, unlock chain, rewards, collectibles, hub secrets and exploration %.
#pragma once

#include "CrashCore/Export.h"
#include "CrashCore/Profile.h"

#include <array>
#include <string>
#include <vector>

namespace crash
{
// What happened in one attempt of a campaign level (filled by the game).
struct LevelRunResult
{
	bool won = false;
	bool playerWrecked = false;
	int takedowns = 0;
	int score = 0;
	float time = 0.0f;                // seconds played
	float damageTakenFraction = 0.0f; // 0..1 of total hp
	float survived = 0.0f;            // seconds alive
	int propsDestroyed = 0;
	float hillTime = 0.0f;
	bool bossDefeated = false;
	std::array<int, kImpactKindCount> hits{};
	std::vector<int> collectiblesFound; // indices picked up during this run
};

CRASHCORE_EXPORT bool EvaluateObjective(const ObjectiveDef& objective, const LevelRunResult& run);

struct LevelCompletion
{
	bool success = false;
	bool firstClear = false;
	std::array<bool, 3> starsThisRun{};
	std::array<bool, 3> newStars{};
	int64_t credits = 0;
	int64_t xp = 0;
	std::vector<ItemRef> items;
	LevelUpResult levelUp;
};

// Updates progress (stars, best score/time, collectibles), grants rewards. Collectibles are kept even on failure.
CRASHCORE_EXPORT LevelCompletion ApplyLevelResult(const GameDatabase& db, Profile& profile, const LevelDef& level,
                                                  const LevelRunResult& run);

CRASHCORE_EXPORT bool IsChapterUnlocked(const GameDatabase& db, const Profile& profile, const ChapterDef& chapter);
CRASHCORE_EXPORT bool IsLevelUnlocked(const GameDatabase& db, const Profile& profile, const LevelDef& level);
CRASHCORE_EXPORT int TotalStars(const Profile& profile);
CRASHCORE_EXPORT int MaxStars(const GameDatabase& db);

// Hub exploration.
CRASHCORE_EXPORT bool MarkHubSecret(Profile& profile, const std::string& chapterId, const std::string& secretId);
CRASHCORE_EXPORT bool MarkHubCollectible(Profile& profile, const std::string& chapterId, int index);

struct ExplorationStats
{
	int collectiblesFound = 0;
	int collectiblesTotal = 0;
	int secretsFound = 0;
	int secretsTotal = 0;
	int starsEarned = 0;
	int starsTotal = 0;
	int levelsCompleted = 0;
	int levelsTotal = 0;
	float percent = 0.0f; // 0..100
};
// Weighted: 40% collectibles, 30% stars, 20% completion, 10% secrets. chapterId empty = whole campaign.
CRASHCORE_EXPORT ExplorationStats ComputeExploration(const GameDatabase& db, const Profile& profile,
                                                     const std::string& chapterId = std::string());

// Builds the effective build for an AI car of a campaign level.
CRASHCORE_EXPORT VehicleBuild MakeCampaignBuild(const CampaignCarDef& car);

} // namespace crash
