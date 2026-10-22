// Copyright BigCar Team. All Rights Reserved.
//
// Player profile (wallet, garage, inventory, progression, campaign) + offline garage/economy operations.
// The JSON layout is shared with the online backend (docs/BACKEND_API.md "Profile document"); offline-only data
// (campaign progress, equipped cosmetics) lives in extra fields the backend ignores.
#pragma once

#include "CrashCore/Database.h"
#include "CrashCore/Stats.h"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace crash
{
struct RankedEntry
{
	double rating = 1500.0;
	double rd = 350.0;
	double vol = 0.06;
	int wins = 0;
	int losses = 0;
	std::string tier = "bronze";
	int division = 3;
	int placementsLeft = 5;
};

struct SeasonPassState
{
	std::string season;
	int64_t xp = 0;
	bool premium = false;
	std::vector<int> claimedFree;
	std::vector<int> claimedPremium;
};

struct LevelProgress
{
	bool completed = false;
	std::array<bool, 3> stars{};
	int bestScore = 0;
	float bestTime = 0.0f;
	std::vector<bool> collectibles;
	int attempts = 0;

	int StarCount() const { return (stars[0] ? 1 : 0) + (stars[1] ? 1 : 0) + (stars[2] ? 1 : 0); }
	int CollectiblesFound() const;
};

struct HubProgress
{
	std::vector<std::string> secrets; // found secret ids
	std::vector<int> collectibles;    // found collectible indices
};

struct CampaignProgress
{
	std::map<std::string, LevelProgress> levels;
	std::map<std::string, HubProgress> hubs; // chapter id -> hub progress
	std::vector<std::string> storySeen;
};

struct PlayerStats
{
	int matches = 0;
	int wins = 0;
	int takedowns = 0;
	int wrecked = 0;
	double damageDealt = 0.0;
	std::map<std::string, int> impacts;
	double distanceKm = 0.0;
};

struct CosmeticLoadout
{
	std::string title;
	std::string banner;
	std::string horn;
	std::string sparks;
};

struct Profile
{
	int version = 1;
	std::string playerId = "local";
	std::string displayName = "Driver";
	int level = 1;
	int64_t xp = 0;
	int64_t credits = 0;
	int64_t gold = 0;
	std::string selected;
	std::vector<VehicleBuild> vehicles;
	std::vector<std::string> ownedEquipment;
	std::vector<std::string> ownedCosmetics;
	std::map<std::string, int> purchases;
	SeasonPassState seasonPass;
	std::map<std::string, RankedEntry> ranked;
	PlayerStats stats;
	CampaignProgress campaign;
	CosmeticLoadout loadout;

	JsonValue ToJson() const;
	static bool FromJson(const JsonValue& json, Profile& out);

	const VehicleBuild* FindVehicle(const std::string& vehicleId) const;
	VehicleBuild* FindVehicle(const std::string& vehicleId);
	const VehicleBuild* SelectedBuild() const;
	bool OwnsVehicle(const std::string& vehicleId) const { return FindVehicle(vehicleId) != nullptr; }
	bool OwnsEquipment(const GameDatabase& db, const std::string& itemId) const;
	bool OwnsCosmetic(const GameDatabase& db, const std::string& itemId) const;
	bool Owns(const GameDatabase& db, const ItemRef& item) const;
};

Profile MakeNewProfile(const GameDatabase& db, const std::string& playerId, const std::string& displayName);

// ---- Garage / economy operations. Error codes match the backend (docs/BACKEND_API.md).
struct OpResult
{
	bool ok = false;
	std::string error;
	std::vector<ItemRef> granted;
	int64_t creditsSpent = 0;

	static OpResult Fail(std::string code)
	{
		OpResult r;
		r.error = std::move(code);
		return r;
	}
	static OpResult Ok()
	{
		OpResult r;
		r.ok = true;
		return r;
	}
};

OpResult BuyVehicle(const GameDatabase& db, Profile& p, const std::string& vehicleId);
OpResult SelectVehicle(const GameDatabase& db, Profile& p, const std::string& vehicleId);
OpResult UpgradeVehicle(const GameDatabase& db, Profile& p, const std::string& vehicleId, UpgradeCategory category);
OpResult BuyEquipment(const GameDatabase& db, Profile& p, const std::string& itemId);
// itemId empty = unequip.
OpResult EquipItem(const GameDatabase& db, Profile& p, const std::string& vehicleId, EquipSlot slot, const std::string& itemId);
OpResult BuyCosmetic(const GameDatabase& db, Profile& p, const std::string& itemId);
// Empty strings leave the value unchanged.
OpResult CustomizeVehicle(const GameDatabase& db, Profile& p, const std::string& vehicleId, const std::string& paint,
                          const std::string& wheels);
// Grants items/currency. Already-owned items convert into 50% of their base credit price.
OpResult GrantItems(const GameDatabase& db, Profile& p, const std::vector<ItemRef>& items);

// ---- Progression.
int64_t XpToNextLevel(const ProgressionDef& prog, int level);
struct LevelUpResult
{
	int levelsGained = 0;
	std::vector<ItemRef> rewards;
};
LevelUpResult AddXp(const GameDatabase& db, Profile& p, int64_t xp);

struct MatchReward
{
	int64_t credits = 0;
	int64_t xp = 0;
};
MatchReward ComputeMatchReward(const ProgressionDef& prog, int score, bool won, bool ranked);

// Rank tier/division for a rating (docs/DATA_SCHEMA.md progression.ranks).
std::pair<const RankDef*, int> RankForRating(const ProgressionDef& prog, double rating);

} // namespace crash
