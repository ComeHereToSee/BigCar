// Copyright BigCar Team. All Rights Reserved.
//
// Typed game content loaded from Content/BigCar/Data/*.json (see docs/DATA_SCHEMA.md).
#pragma once

#include "CrashCore/Export.h"
#include "CrashCore/Json.h"
#include "CrashCore/Math.h"
#include "CrashCore/Types.h"

#include <array>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace crash
{
struct Price
{
	Currency currency = Currency::Credits;
	int64_t amount = 0;

	bool IsFree() const { return amount <= 0; }
};

struct ItemRef
{
	ItemType type = ItemType::Currency;
	std::string id;
	int64_t amount = 0; // currency only
};

// Stat modifiers shared by upgrades and equipment (docs/DATA_SCHEMA.md "Stat modifiers").
struct StatMods
{
	// Multiplicative (default 1).
	float powerMul = 1.0f;
	float torqueMul = 1.0f;
	float gripMul = 1.0f;
	float brakeMul = 1.0f;
	float steerMul = 1.0f;
	float topSpeedMul = 1.0f;
	float massMul = 1.0f;
	float toughnessMul = 1.0f;
	float ramPowerMul = 1.0f;
	float boostAccelMul = 1.0f;
	float boostRegenMul = 1.0f;
	float downforceMul = 1.0f;
	float dragMul = 1.0f;
	float suspensionMul = 1.0f;
	float wheelHpMul = 1.0f;
	float engineHpMul = 1.0f;
	float selfDamageMul = 1.0f;
	float sideDamageMul = 1.0f;
	float rearDamageMul = 1.0f;
	// Additive (default 0).
	float massAddKg = 0.0f;
	float boostCapacityAdd = 0.0f;
	float cgHeightAdd = 0.0f;
	float launchPower = 0.0f;
	float tireDamage = 0.0f;
	float repairPerSec = 0.0f;
	float knockback = 0.0f;
	std::array<float, kZoneCount> armor{};

	CRASHCORE_EXPORT void Combine(const StatMods& other);
	// Applies a single "key": value pair. Returns false for unknown keys.
	CRASHCORE_EXPORT bool ApplyKey(std::string_view key, float value);
	static CRASHCORE_EXPORT bool IsKnownKey(std::string_view key);
};

struct BrandDef
{
	std::string id;
	std::string name; // loc key
	std::string country;
	Color color;
	bool licensed = false;
	std::string reference;
};

struct VehicleDimensions
{
	float length = 4.5f;
	float width = 1.8f;
	float height = 1.45f;
	float wheelbase = 2.7f;
	float trackFront = 1.55f;
	float trackRear = 1.55f;
	float groundClearance = 0.14f;
	float wheelRadius = 0.33f;
	float wheelWidth = 0.23f;
};

struct VehicleDef
{
	std::string id;
	std::string brand;
	std::string name; // loc key
	std::string reference;
	VehicleClass vehicleClass = VehicleClass::Sedan;
	VehicleClass body = VehicleClass::Sedan;
	int tier = 1;
	Price price;
	int unlockLevel = 1;
	bool starter = false;
	Drivetrain drivetrain = Drivetrain::FWD;
	EngineLayout engineLayout = EngineLayout::Front;
	float massKg = 1400.0f;
	float powerKw = 110.0f;
	float torqueNm = 200.0f;
	float idleRpm = 900.0f;
	float redlineRpm = 6500.0f;
	std::vector<float> gears;
	float reverseRatio = 3.2f;
	float finalDrive = 4.0f;
	float topSpeedKmh = 190.0f;
	VehicleDimensions dimensions;
	float cgHeight = 0.0f; // 0 => derived
	float frontWeight = 0.55f;
	float grip = 1.0f;
	float brakeG = 0.0f; // 0 => derived
	float steerLockDeg = 35.0f;
	float suspensionFrequency = 1.5f;
	float suspensionDamping = 0.4f;
	float suspensionTravel = 0.15f;
	float antiRoll = 0.6f;
	float dragCoefficient = 0.32f;
	float downforce = 0.0f;
	float toughness = 1.0f;
	std::array<float, kZoneCount> armor{};
	float ramPower = 1.0f;
	float boostCapacity = 100.0f;
	float boostAccel = 6.0f;
	float boostRegenPerSec = 6.0f;
	std::vector<Color> colors;
	std::vector<std::string> tags;

	CRASHCORE_EXPORT bool HasTag(std::string_view tag) const;
	bool IsElectric() const { return gears.size() <= 2 || HasTag("ev"); }
};

struct UpgradeLevelDef
{
	int64_t cost = 0; // credits before tier multiplier
	StatMods mods;
};

struct UpgradeCategoryDef
{
	UpgradeCategory category = UpgradeCategory::Engine;
	std::string id;
	std::string name;
	std::string description;
	std::vector<UpgradeLevelDef> levels;
};

struct UpgradesDef
{
	std::array<float, 5> tierCostMultiplier{1.0f, 1.6f, 2.5f, 4.0f, 6.0f};
	std::array<UpgradeCategoryDef, kUpgradeCategoryCount> categories;
};

struct EquipmentGeometry
{
	GeometryShape shape = GeometryShape::None;
	float length = 0.0f;
	float angleDeg = 0.0f;
	Color color{90, 95, 102};
	bool hasColor = false;
};

struct EquipmentDef
{
	std::string id;
	EquipSlot slot = EquipSlot::Front;
	std::string name;
	std::string description;
	Rarity rarity = Rarity::Common;
	EquipmentGeometry geometry;
	StatMods mods;
	AbilityType ability = AbilityType::None;
	int cr = 0;
	Price price;
	int unlockLevel = 1;
	std::vector<VehicleClass> allowedClasses;

	CRASHCORE_EXPORT bool AllowsClass(VehicleClass c) const;
};

enum class PaintFinish : uint8_t
{
	Gloss,
	Metallic,
	Matte,
	Chrome,
	Pearl,
	Carbon,
	Count
};

struct CosmeticDef
{
	std::string id;
	ItemType type = ItemType::Paint;
	std::string name;
	Rarity rarity = Rarity::Common;
	Price price;
	Color color;
	std::vector<Color> colors;
	PaintFinish finish = PaintFinish::Gloss;
	int spokes = 5;
	std::vector<float> frequencies;
};

struct PropDef
{
	PropType type = PropType::Block;
	Vec3 pos;
	float yaw = 0.0f;
	Vec3 size{1.0f, 1.0f, 1.0f};
	Color color{128, 128, 128};
	bool hasColor = false;
	bool soft = false;
	float radius = 1.0f;
	float height = 1.0f;
	float strength = 10.0f;
	float speed = 90.0f;
	float period = 4.0f;
	float travel = 3.0f;
	int count = 1;
	float intensity = 1.0f;
	VehicleClass vehicleClass = VehicleClass::Sedan;
	Vec3 to;
};

struct SpawnDef
{
	Vec3 pos; // z unused (floor)
	float yaw = 0.0f;
	int team = -1;
};

struct SecretDef
{
	std::string id;
	Vec3 pos;
	float radius = 5.0f;
};

struct ArenaDef
{
	std::string id;
	std::string name;
	std::string description;
	std::string theme;
	bool hub = false;
	ArenaShape shape = ArenaShape::Rect;
	float sizeX = 100.0f;
	float sizeY = 100.0f;
	float wallHeight = 3.0f;
	bool walls = true;
	float elevation = 0.0f;
	float killZ = -30.0f;
	std::string floorSurface = "asphalt";
	Color floorColor{80, 80, 80};
	float floorFriction = 1.0f;
	std::string lightingPreset = "day";
	float fog = 0.02f;
	Color tint{255, 255, 255};
	std::string level; // optional hand-made level asset replacing the procedural build
	std::vector<PropDef> props;
	std::vector<SpawnDef> spawns;
	std::vector<Vec3> collectibles;
	std::vector<SecretDef> secrets;

	// Half extent of the playable floor along X/Y (radius for circle/octagon).
	float HalfX() const { return sizeX * 0.5f; }
	float HalfY() const { return shape == ArenaShape::Rect ? sizeY * 0.5f : sizeX * 0.5f; }
	CRASHCORE_EXPORT bool IsInside(const Vec3& p, float margin = 0.0f) const;
	CRASHCORE_EXPORT const PropDef* FindHill() const;
};

struct ModeDef
{
	std::string id;
	std::string name;
	std::string description;
	int teams = 0;
	bool respawn = false;
	float respawnDelay = 3.0f;
	int lives = 1;
	float timeLimit = 300.0f;
	int scoreLimit = 0;
	int rounds = 1;
	int roundsToWin = 1;
	WinCondition win = WinCondition::LastStanding;
	bool ringOut = false;
	bool hill = false;
	bool bounty = false;
	int minPlayers = 2;
	int maxPlayers = 12;
	bool online = true;
	bool ranked = false;
};

struct AIProfileDef
{
	std::string id;
	std::string name;
	float skill = 0.5f;
	float aggression = 0.5f;
	float reaction = 0.4f;
	float aimLead = 0.5f;
	float flankBias = 0.5f;
	float abilityUse = 0.5f;
	float boostUse = 0.5f;
	float retreatHealth = 0.2f;
	float mistakeRate = 0.1f;
	float focusPlayer = 0.5f;
};

struct ObjectiveDef
{
	ObjectiveType type = ObjectiveType::Win;
	float value = 0.0f;
	ImpactKind kind = ImpactKind::None; // hits_of_kind
};

struct CampaignCarDef
{
	std::string vehicle;
	std::string ai = "regular";
	int team = 1;
	std::string name; // loc key or empty
	std::array<int, kUpgradeCategoryCount> upgrades{};
	std::array<std::string, kEquipSlotCount> equipment;
	bool boss = false;
};

struct RewardDef
{
	int64_t credits = 0;
	int64_t xp = 0;
	std::vector<ItemRef> items;
};

struct LevelDef
{
	std::string id;
	std::string chapter;
	int indexInChapter = 0;
	std::string name;
	std::string description;
	std::string arena;
	std::string mode;
	float timeLimit = 0.0f; // 0 => mode default
	int maxTier = 0;        // 0 => any
	std::string loaner;
	std::vector<CampaignCarDef> opponents;
	std::vector<CampaignCarDef> allies;
	ObjectiveDef objective;
	std::array<ObjectiveDef, 3> stars;
	int collectibles = 0;
	RewardDef rewards;
	std::vector<std::string> intro;
	std::vector<std::string> outro;
	std::string unlockLevel; // empty => previous in chapter
	int unlockStars = 0;
};

struct RivalDef
{
	std::string id;
	std::string name;
	std::string title;
	std::string vehicle;
	Color color;
};

struct ChapterDef
{
	std::string id;
	std::string name;
	std::string description;
	std::string hub;
	int unlockStars = 0;
	std::string boss;
	Color color;
	std::vector<LevelDef> levels;
};

struct CampaignDef
{
	std::vector<RivalDef> rivals;
	std::vector<ChapterDef> chapters;
};

struct RankDef
{
	std::string id;
	std::string name;
	int minRating = 0;
	int divisions = 1;
	Color color;
};

struct ScoreTable
{
	std::array<int, kImpactKindCount> impact{};
	int takedown = 1000;
	int assist = 350;
	int ringOut = 800;
	int revenge = 300;
	int multiTakedown = 500;
	int airtimePerSec = 40;
	int hillPerSec = 20;
	int survivalPerSec = 5;
	int propDestroyed = 50;
	float comboStep = 0.2f;
	float comboMax = 2.0f;
	float comboWindow = 4.0f;
	float minDamageForPoints = 25.0f;
};

struct MatchRewardDef
{
	int64_t participationCredits = 800;
	int64_t participationXp = 150;
	float creditsPerScore = 1.5f;
	float xpPerScore = 0.8f;
	float winMultiplier = 1.5f;
	float rankedMultiplier = 1.25f;
	int64_t maxCredits = 40000;
	int64_t maxXp = 20000;
};

struct ProgressionDef
{
	int maxLevel = 100;
	float xpBase = 1000.0f;
	float xpGrowth = 1.06f;
	std::vector<std::pair<int, std::vector<ItemRef>>> levelRewards;
	MatchRewardDef match;
	ScoreTable score;
	std::vector<RankDef> ranks;
};

// ---------------------------------------------------------------------------------------------
// Graphics settings description (graphics.json, owned by the engine team).

using CvarList = std::vector<std::pair<std::string, std::string>>;

struct GraphicsValueDef
{
	std::string id;
	std::string name;
	CvarList cvars;
	std::vector<std::pair<std::string, float>> post; // post-process overrides
	std::vector<std::pair<std::string, float>> game; // game-side tunables (debris count…)
	int scalability = -1;                            // level for the option's scalability group
};

struct GraphicsOptionDef
{
	std::string id;
	std::string name;
	std::string category;
	std::string scalabilityGroup; // e.g. "Shadow" (maps to Unreal's sg.ShadowQuality)
	std::string defaultValue;
	std::string requiresCvar; // hide when the cvar does not exist (plugin missing)
	bool restart = false;
	std::vector<GraphicsValueDef> values;

	CRASHCORE_EXPORT const GraphicsValueDef* FindValue(std::string_view valueId) const;
};

struct UpscalerModeDef
{
	std::string id;
	std::string name;
	CvarList cvars;
	float screenPercentage = -1.0f;
};

struct UpscalerDef
{
	std::string id;
	std::string name;
	std::string vendor;
	std::string detectCvar; // empty => always available
	CvarList enable;
	CvarList disable;
	std::vector<UpscalerModeDef> modes;
	std::string frameGenCvar;
	CvarList frameGenOn;
	CvarList frameGenOff;

	CRASHCORE_EXPORT const UpscalerModeDef* FindMode(std::string_view modeId) const;
};

struct GraphicsPresetDef
{
	std::string id;
	std::string name;
	std::vector<std::pair<std::string, std::string>> values; // option id -> value id
	std::string upscaler;
	std::string upscalerMode;
};

struct GraphicsDef
{
	std::vector<GraphicsOptionDef> options;
	std::vector<UpscalerDef> upscalers;
	std::vector<GraphicsPresetDef> presets;
	std::string lowLatencyCvar;
	CvarList lowLatencyOn;
	CvarList lowLatencyOff;

	CRASHCORE_EXPORT const GraphicsOptionDef* FindOption(std::string_view id) const;
	CRASHCORE_EXPORT const UpscalerDef* FindUpscaler(std::string_view id) const;
	CRASHCORE_EXPORT const GraphicsPresetDef* FindPreset(std::string_view id) const;
};

// ---------------------------------------------------------------------------------------------

struct LoadMessage
{
	enum class Severity : uint8_t
	{
		Warning,
		Error
	};
	Severity severity = Severity::Error;
	std::string file;
	std::string message;
};

class GameDatabase
{
public:
	// Reads a file into a string; returns false if missing. Lets Unreal plug in its own file system.
	using FileReader = std::function<bool(const std::string& relativePath, std::string& outText)>;

	// Loads every data file (except Localization). Returns false if any error was found.
	CRASHCORE_EXPORT bool LoadAll(const FileReader& reader);
	// Loads a single file from text (used by tests).
	CRASHCORE_EXPORT bool LoadFile(const std::string& fileName, std::string_view text);
	// Cross-file reference checks. Call after loading everything.
	CRASHCORE_EXPORT bool Validate();

	const std::vector<LoadMessage>& Messages() const { return messages_; }
	CRASHCORE_EXPORT bool HasErrors() const;
	CRASHCORE_EXPORT std::string FormatMessages() const;

	const std::vector<BrandDef>& Brands() const { return brands_; }
	const std::vector<VehicleDef>& Vehicles() const { return vehicles_; }
	const UpgradesDef& Upgrades() const { return upgrades_; }
	const std::vector<EquipmentDef>& Equipment() const { return equipment_; }
	const std::vector<CosmeticDef>& Cosmetics() const { return cosmetics_; }
	const std::vector<ArenaDef>& Arenas() const { return arenas_; }
	const std::vector<ModeDef>& Modes() const { return modes_; }
	const std::vector<AIProfileDef>& AIProfiles() const { return aiProfiles_; }
	const CampaignDef& Campaign() const { return campaign_; }
	const ProgressionDef& Progression() const { return progression_; }
	const GraphicsDef& Graphics() const { return graphics_; }

	CRASHCORE_EXPORT const BrandDef* FindBrand(std::string_view id) const;
	CRASHCORE_EXPORT const VehicleDef* FindVehicle(std::string_view id) const;
	CRASHCORE_EXPORT const EquipmentDef* FindEquipment(std::string_view id) const;
	CRASHCORE_EXPORT const CosmeticDef* FindCosmetic(std::string_view id) const;
	CRASHCORE_EXPORT const ArenaDef* FindArena(std::string_view id) const;
	CRASHCORE_EXPORT const ModeDef* FindMode(std::string_view id) const;
	CRASHCORE_EXPORT const AIProfileDef* FindAIProfile(std::string_view id) const;
	CRASHCORE_EXPORT const ChapterDef* FindChapter(std::string_view id) const;
	CRASHCORE_EXPORT const LevelDef* FindLevel(std::string_view id) const;
	CRASHCORE_EXPORT const RivalDef* FindRival(std::string_view id) const;
	const UpgradeCategoryDef& Upgrade(UpgradeCategory c) const { return upgrades_.categories[static_cast<size_t>(c)]; }

	// Price of buying upgrade `level` (1..5) of `category` for `vehicle`.
	CRASHCORE_EXPORT int64_t UpgradeCost(const VehicleDef& vehicle, UpgradeCategory category, int level) const;
	// Base price of an item reference (vehicle/equipment/cosmetic). Returns nullopt if unknown.
	CRASHCORE_EXPORT std::optional<Price> ItemPrice(const ItemRef& item) const;
	CRASHCORE_EXPORT bool ItemExists(const ItemRef& item) const;
	CRASHCORE_EXPORT std::vector<const VehicleDef*> StarterVehicles() const;

	// Every localization key referenced by content (for completeness checks).
	CRASHCORE_EXPORT std::vector<std::string> ReferencedLocKeys() const;

private:
	CRASHCORE_EXPORT void Error(const std::string& file, const std::string& message);
	CRASHCORE_EXPORT void Warn(const std::string& file, const std::string& message);
	CRASHCORE_EXPORT void RebuildIndices();

	CRASHCORE_EXPORT bool ParseBrands(const std::string& file, const JsonValue& root);
	CRASHCORE_EXPORT bool ParseVehicles(const std::string& file, const JsonValue& root);
	CRASHCORE_EXPORT bool ParseUpgrades(const std::string& file, const JsonValue& root);
	CRASHCORE_EXPORT bool ParseEquipment(const std::string& file, const JsonValue& root);
	CRASHCORE_EXPORT bool ParseCosmetics(const std::string& file, const JsonValue& root);
	CRASHCORE_EXPORT bool ParseArenas(const std::string& file, const JsonValue& root);
	CRASHCORE_EXPORT bool ParseModes(const std::string& file, const JsonValue& root);
	CRASHCORE_EXPORT bool ParseAI(const std::string& file, const JsonValue& root);
	CRASHCORE_EXPORT bool ParseCampaign(const std::string& file, const JsonValue& root);
	CRASHCORE_EXPORT bool ParseProgression(const std::string& file, const JsonValue& root);
	CRASHCORE_EXPORT bool ParseGraphics(const std::string& file, const JsonValue& root);

	std::vector<LoadMessage> messages_;
	std::vector<BrandDef> brands_;
	std::vector<VehicleDef> vehicles_;
	UpgradesDef upgrades_;
	std::vector<EquipmentDef> equipment_;
	std::vector<CosmeticDef> cosmetics_;
	std::vector<ArenaDef> arenas_;
	std::vector<ModeDef> modes_;
	std::vector<AIProfileDef> aiProfiles_;
	CampaignDef campaign_;
	ProgressionDef progression_;
	GraphicsDef graphics_;

	std::unordered_map<std::string, size_t> brandIndex_;
	std::unordered_map<std::string, size_t> vehicleIndex_;
	std::unordered_map<std::string, size_t> equipmentIndex_;
	std::unordered_map<std::string, size_t> cosmeticIndex_;
	std::unordered_map<std::string, size_t> arenaIndex_;
	std::unordered_map<std::string, size_t> modeIndex_;
	std::unordered_map<std::string, size_t> aiIndex_;
	std::unordered_map<std::string, std::pair<size_t, size_t>> levelIndex_;
};

// Shared helpers.
CRASHCORE_EXPORT bool ParseColor(std::string_view text, Color& out);
CRASHCORE_EXPORT std::string ColorToHex(const Color& color);
CRASHCORE_EXPORT bool ParsePrice(const JsonValue& value, Price& out);
CRASHCORE_EXPORT JsonValue PriceToJson(const Price& price);
CRASHCORE_EXPORT bool ParseItemRef(const JsonValue& value, ItemRef& out);
CRASHCORE_EXPORT JsonValue ItemRefToJson(const ItemRef& item);
CRASHCORE_EXPORT int64_t RoundTo100(double value);
CRASHCORE_EXPORT bool IsValidId(std::string_view id);

// The list of data files, in load order.
CRASHCORE_EXPORT const std::vector<std::string>& DataFileNames();

} // namespace crash
