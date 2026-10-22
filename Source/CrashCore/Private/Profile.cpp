// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Profile.h"

#include <algorithm>
#include <cmath>

namespace crash
{
namespace
{
JsonValue StringArray(const std::vector<std::string>& values)
{
	JsonValue arr = JsonValue::MakeArray();
	for (const std::string& v : values)
	{
		arr.Push(JsonValue(v));
	}
	return arr;
}

JsonValue IntArray(const std::vector<int>& values)
{
	JsonValue arr = JsonValue::MakeArray();
	for (const int v : values)
	{
		arr.Push(JsonValue(v));
	}
	return arr;
}

std::vector<std::string> ReadStrings(const JsonValue& arr)
{
	std::vector<std::string> out;
	for (const JsonValue& v : arr.Items())
	{
		if (v.IsString())
		{
			out.push_back(v.AsString());
		}
	}
	return out;
}

std::vector<int> ReadInts(const JsonValue& arr)
{
	std::vector<int> out;
	for (const JsonValue& v : arr.Items())
	{
		if (v.IsNumber())
		{
			out.push_back(v.AsInt());
		}
	}
	return out;
}

bool Contains(const std::vector<std::string>& values, const std::string& v)
{
	return std::find(values.begin(), values.end(), v) != values.end();
}

bool Spend(Profile& p, const Price& price)
{
	int64_t& balance = price.currency == Currency::Gold ? p.gold : p.credits;
	if (balance < price.amount)
	{
		return false;
	}
	balance -= price.amount;
	return true;
}
} // namespace

int LevelProgress::CollectiblesFound() const
{
	return static_cast<int>(std::count(collectibles.begin(), collectibles.end(), true));
}

// ---------------------------------------------------------------------------------------------
// Serialization

JsonValue Profile::ToJson() const
{
	JsonValue root = JsonValue::MakeObject();
	root.Set("version", JsonValue(version));
	root.Set("playerId", JsonValue(playerId));
	root.Set("displayName", JsonValue(displayName));
	root.Set("level", JsonValue(level));
	root.Set("xp", JsonValue(xp));
	JsonValue wallet = JsonValue::MakeObject();
	wallet.Set("credits", JsonValue(credits));
	wallet.Set("gold", JsonValue(gold));
	root.Set("wallet", std::move(wallet));

	JsonValue garage = JsonValue::MakeObject();
	garage.Set("selected", JsonValue(selected));
	JsonValue cars = JsonValue::MakeArray();
	for (const VehicleBuild& b : vehicles)
	{
		cars.Push(b.ToJson());
	}
	garage.Set("vehicles", std::move(cars));
	root.Set("garage", std::move(garage));

	JsonValue inventory = JsonValue::MakeObject();
	inventory.Set("equipment", StringArray(ownedEquipment));
	inventory.Set("cosmetics", StringArray(ownedCosmetics));
	root.Set("inventory", std::move(inventory));

	JsonValue purchasesJson = JsonValue::MakeObject();
	for (const auto& [offer, count] : purchases)
	{
		purchasesJson.Set(offer, JsonValue(count));
	}
	root.Set("purchases", std::move(purchasesJson));

	JsonValue pass = JsonValue::MakeObject();
	pass.Set("season", JsonValue(seasonPass.season));
	pass.Set("xp", JsonValue(seasonPass.xp));
	pass.Set("premium", JsonValue(seasonPass.premium));
	pass.Set("claimedFree", IntArray(seasonPass.claimedFree));
	pass.Set("claimedPremium", IntArray(seasonPass.claimedPremium));
	root.Set("seasonPass", std::move(pass));

	JsonValue rankedJson = JsonValue::MakeObject();
	for (const auto& [queue, entry] : ranked)
	{
		JsonValue e = JsonValue::MakeObject();
		e.Set("rating", JsonValue(entry.rating));
		e.Set("rd", JsonValue(entry.rd));
		e.Set("vol", JsonValue(entry.vol));
		e.Set("wins", JsonValue(entry.wins));
		e.Set("losses", JsonValue(entry.losses));
		e.Set("tier", JsonValue(entry.tier));
		e.Set("division", JsonValue(entry.division));
		e.Set("placementsLeft", JsonValue(entry.placementsLeft));
		rankedJson.Set(queue, std::move(e));
	}
	root.Set("ranked", std::move(rankedJson));

	JsonValue statsJson = JsonValue::MakeObject();
	statsJson.Set("matches", JsonValue(stats.matches));
	statsJson.Set("wins", JsonValue(stats.wins));
	statsJson.Set("takedowns", JsonValue(stats.takedowns));
	statsJson.Set("wrecked", JsonValue(stats.wrecked));
	statsJson.Set("damageDealt", JsonValue(stats.damageDealt));
	JsonValue impactsJson = JsonValue::MakeObject();
	for (const auto& [kind, count] : stats.impacts)
	{
		impactsJson.Set(kind, JsonValue(count));
	}
	statsJson.Set("impacts", std::move(impactsJson));
	statsJson.Set("distanceKm", JsonValue(stats.distanceKm));
	root.Set("stats", std::move(statsJson));

	// Offline-only sections.
	JsonValue camp = JsonValue::MakeObject();
	JsonValue levelsJson = JsonValue::MakeObject();
	for (const auto& [id, lp] : campaign.levels)
	{
		JsonValue l = JsonValue::MakeObject();
		l.Set("completed", JsonValue(lp.completed));
		JsonValue stars = JsonValue::MakeArray();
		for (const bool s : lp.stars)
		{
			stars.Push(JsonValue(s));
		}
		l.Set("stars", std::move(stars));
		l.Set("bestScore", JsonValue(lp.bestScore));
		l.Set("bestTime", JsonValue(lp.bestTime));
		JsonValue col = JsonValue::MakeArray();
		for (const bool c : lp.collectibles)
		{
			col.Push(JsonValue(static_cast<bool>(c)));
		}
		l.Set("collectibles", std::move(col));
		l.Set("attempts", JsonValue(lp.attempts));
		levelsJson.Set(id, std::move(l));
	}
	camp.Set("levels", std::move(levelsJson));
	JsonValue hubsJson = JsonValue::MakeObject();
	for (const auto& [chapter, hub] : campaign.hubs)
	{
		JsonValue h = JsonValue::MakeObject();
		h.Set("secrets", StringArray(hub.secrets));
		h.Set("collectibles", IntArray(hub.collectibles));
		hubsJson.Set(chapter, std::move(h));
	}
	camp.Set("hubs", std::move(hubsJson));
	camp.Set("storySeen", StringArray(campaign.storySeen));
	root.Set("campaign", std::move(camp));

	JsonValue loadoutJson = JsonValue::MakeObject();
	loadoutJson.Set("title", JsonValue(loadout.title));
	loadoutJson.Set("banner", JsonValue(loadout.banner));
	loadoutJson.Set("horn", JsonValue(loadout.horn));
	loadoutJson.Set("sparks", JsonValue(loadout.sparks));
	root.Set("loadout", std::move(loadoutJson));
	return root;
}

bool Profile::FromJson(const JsonValue& json, Profile& out)
{
	if (!json.IsObject())
	{
		return false;
	}
	Profile p;
	p.version = json["version"].AsInt(1);
	p.playerId = json["playerId"].AsString("local");
	p.displayName = json["displayName"].AsString("Driver");
	p.level = std::max(1, json["level"].AsInt(1));
	p.xp = std::max<int64_t>(0, json["xp"].AsInt64(0));
	p.credits = json["wallet"]["credits"].AsInt64(0);
	p.gold = json["wallet"]["gold"].AsInt64(0);
	p.selected = json["garage"]["selected"].AsString("");
	for (const JsonValue& v : json["garage"]["vehicles"].Items())
	{
		VehicleBuild b;
		if (VehicleBuild::FromJson(v, b))
		{
			p.vehicles.push_back(std::move(b));
		}
	}
	p.ownedEquipment = ReadStrings(json["inventory"]["equipment"]);
	p.ownedCosmetics = ReadStrings(json["inventory"]["cosmetics"]);
	for (const auto& [offer, count] : json["purchases"].Members())
	{
		p.purchases[offer] = count.AsInt(0);
	}
	const JsonValue& pass = json["seasonPass"];
	p.seasonPass.season = pass["season"].AsString("");
	p.seasonPass.xp = pass["xp"].AsInt64(0);
	p.seasonPass.premium = pass["premium"].AsBool(false);
	p.seasonPass.claimedFree = ReadInts(pass["claimedFree"]);
	p.seasonPass.claimedPremium = ReadInts(pass["claimedPremium"]);
	for (const auto& [queue, e] : json["ranked"].Members())
	{
		RankedEntry entry;
		entry.rating = e["rating"].AsDouble(1500.0);
		entry.rd = e["rd"].AsDouble(350.0);
		entry.vol = e["vol"].AsDouble(0.06);
		entry.wins = e["wins"].AsInt(0);
		entry.losses = e["losses"].AsInt(0);
		entry.tier = e["tier"].AsString("bronze");
		entry.division = e["division"].AsInt(3);
		entry.placementsLeft = e["placementsLeft"].AsInt(5);
		p.ranked[queue] = entry;
	}
	const JsonValue& s = json["stats"];
	p.stats.matches = s["matches"].AsInt(0);
	p.stats.wins = s["wins"].AsInt(0);
	p.stats.takedowns = s["takedowns"].AsInt(0);
	p.stats.wrecked = s["wrecked"].AsInt(0);
	p.stats.damageDealt = s["damageDealt"].AsDouble(0.0);
	for (const auto& [kind, count] : s["impacts"].Members())
	{
		p.stats.impacts[kind] = count.AsInt(0);
	}
	p.stats.distanceKm = s["distanceKm"].AsDouble(0.0);

	const JsonValue& camp = json["campaign"];
	for (const auto& [id, l] : camp["levels"].Members())
	{
		LevelProgress lp;
		lp.completed = l["completed"].AsBool(false);
		for (size_t i = 0; i < 3; ++i)
		{
			lp.stars[i] = l["stars"][i].AsBool(false);
		}
		lp.bestScore = l["bestScore"].AsInt(0);
		lp.bestTime = l["bestTime"].AsFloat(0.0f);
		for (const JsonValue& c : l["collectibles"].Items())
		{
			lp.collectibles.push_back(c.AsBool(false));
		}
		lp.attempts = l["attempts"].AsInt(0);
		p.campaign.levels[id] = std::move(lp);
	}
	for (const auto& [chapter, h] : camp["hubs"].Members())
	{
		HubProgress hub;
		hub.secrets = ReadStrings(h["secrets"]);
		hub.collectibles = ReadInts(h["collectibles"]);
		p.campaign.hubs[chapter] = std::move(hub);
	}
	p.campaign.storySeen = ReadStrings(camp["storySeen"]);
	const JsonValue& lo = json["loadout"];
	p.loadout.title = lo["title"].AsString("");
	p.loadout.banner = lo["banner"].AsString("");
	p.loadout.horn = lo["horn"].AsString("");
	p.loadout.sparks = lo["sparks"].AsString("");
	out = std::move(p);
	return true;
}

// ---------------------------------------------------------------------------------------------
// Queries

const VehicleBuild* Profile::FindVehicle(const std::string& vehicleId) const
{
	for (const VehicleBuild& b : vehicles)
	{
		if (b.vehicleId == vehicleId)
		{
			return &b;
		}
	}
	return nullptr;
}

VehicleBuild* Profile::FindVehicle(const std::string& vehicleId)
{
	for (VehicleBuild& b : vehicles)
	{
		if (b.vehicleId == vehicleId)
		{
			return &b;
		}
	}
	return nullptr;
}

const VehicleBuild* Profile::SelectedBuild() const
{
	const VehicleBuild* b = FindVehicle(selected);
	return b ? b : (vehicles.empty() ? nullptr : &vehicles.front());
}

bool Profile::OwnsEquipment(const GameDatabase& db, const std::string& itemId) const
{
	const EquipmentDef* e = db.FindEquipment(itemId);
	if (!e)
	{
		return false;
	}
	return e->price.IsFree() || Contains(ownedEquipment, itemId);
}

bool Profile::OwnsCosmetic(const GameDatabase& db, const std::string& itemId) const
{
	const CosmeticDef* c = db.FindCosmetic(itemId);
	if (!c)
	{
		return false;
	}
	return c->price.IsFree() || Contains(ownedCosmetics, itemId);
}

bool Profile::Owns(const GameDatabase& db, const ItemRef& item) const
{
	switch (item.type)
	{
	case ItemType::Vehicle:
		return OwnsVehicle(item.id);
	case ItemType::Equipment:
		return OwnsEquipment(db, item.id);
	case ItemType::Currency:
		return false;
	default:
		return OwnsCosmetic(db, item.id);
	}
}

Profile MakeNewProfile(const GameDatabase& db, const std::string& playerId, const std::string& displayName)
{
	Profile p;
	p.playerId = playerId;
	p.displayName = displayName;
	p.credits = 25000;
	for (const VehicleDef* v : db.StarterVehicles())
	{
		VehicleBuild b = MakeDefaultBuild(db, v->id);
		p.vehicles.push_back(b);
	}
	if (!p.vehicles.empty())
	{
		p.selected = p.vehicles.front().vehicleId;
	}
	return p;
}

// ---------------------------------------------------------------------------------------------
// Operations

OpResult BuyVehicle(const GameDatabase& db, Profile& p, const std::string& vehicleId)
{
	const VehicleDef* v = db.FindVehicle(vehicleId);
	if (!v)
	{
		return OpResult::Fail("not_found");
	}
	if (p.OwnsVehicle(vehicleId))
	{
		return OpResult::Fail("already_owned");
	}
	if (p.level < v->unlockLevel)
	{
		return OpResult::Fail("level_locked");
	}
	if (!Spend(p, v->price))
	{
		return OpResult::Fail("insufficient_funds");
	}
	p.vehicles.push_back(MakeDefaultBuild(db, vehicleId));
	OpResult r = OpResult::Ok();
	r.creditsSpent = v->price.currency == Currency::Credits ? v->price.amount : 0;
	r.granted.push_back({ItemType::Vehicle, vehicleId, 0});
	return r;
}

OpResult SelectVehicle(const GameDatabase& db, Profile& p, const std::string& vehicleId)
{
	(void)db;
	if (!p.OwnsVehicle(vehicleId))
	{
		return OpResult::Fail("not_owned");
	}
	p.selected = vehicleId;
	return OpResult::Ok();
}

OpResult UpgradeVehicle(const GameDatabase& db, Profile& p, const std::string& vehicleId, UpgradeCategory category)
{
	const VehicleDef* v = db.FindVehicle(vehicleId);
	VehicleBuild* b = p.FindVehicle(vehicleId);
	if (!v)
	{
		return OpResult::Fail("not_found");
	}
	if (!b)
	{
		return OpResult::Fail("not_owned");
	}
	int& level = b->upgrades[static_cast<size_t>(category)];
	if (level >= kMaxUpgradeLevel)
	{
		return OpResult::Fail("max_level");
	}
	const int64_t cost = db.UpgradeCost(*v, category, level + 1);
	if (cost < 0)
	{
		return OpResult::Fail("max_level");
	}
	if (p.credits < cost)
	{
		return OpResult::Fail("insufficient_funds");
	}
	p.credits -= cost;
	level += 1;
	OpResult r = OpResult::Ok();
	r.creditsSpent = cost;
	return r;
}

OpResult BuyEquipment(const GameDatabase& db, Profile& p, const std::string& itemId)
{
	const EquipmentDef* e = db.FindEquipment(itemId);
	if (!e)
	{
		return OpResult::Fail("not_found");
	}
	if (p.OwnsEquipment(db, itemId))
	{
		return OpResult::Fail("already_owned");
	}
	if (p.level < e->unlockLevel)
	{
		return OpResult::Fail("level_locked");
	}
	if (!Spend(p, e->price))
	{
		return OpResult::Fail("insufficient_funds");
	}
	p.ownedEquipment.push_back(itemId);
	OpResult r = OpResult::Ok();
	r.creditsSpent = e->price.currency == Currency::Credits ? e->price.amount : 0;
	r.granted.push_back({ItemType::Equipment, itemId, 0});
	return r;
}

OpResult EquipItem(const GameDatabase& db, Profile& p, const std::string& vehicleId, EquipSlot slot, const std::string& itemId)
{
	const VehicleDef* v = db.FindVehicle(vehicleId);
	VehicleBuild* b = p.FindVehicle(vehicleId);
	if (!v)
	{
		return OpResult::Fail("not_found");
	}
	if (!b)
	{
		return OpResult::Fail("not_owned");
	}
	if (itemId.empty())
	{
		b->equipment[static_cast<size_t>(slot)].clear();
		return OpResult::Ok();
	}
	const EquipmentDef* e = db.FindEquipment(itemId);
	if (!e)
	{
		return OpResult::Fail("not_found");
	}
	if (!p.OwnsEquipment(db, itemId))
	{
		return OpResult::Fail("not_owned");
	}
	if (e->slot != slot)
	{
		return OpResult::Fail("invalid_slot");
	}
	if (!e->AllowsClass(v->vehicleClass))
	{
		return OpResult::Fail("invalid_slot");
	}
	b->equipment[static_cast<size_t>(slot)] = itemId;
	return OpResult::Ok();
}

OpResult BuyCosmetic(const GameDatabase& db, Profile& p, const std::string& itemId)
{
	const CosmeticDef* c = db.FindCosmetic(itemId);
	if (!c)
	{
		return OpResult::Fail("not_found");
	}
	if (p.OwnsCosmetic(db, itemId))
	{
		return OpResult::Fail("already_owned");
	}
	if (!Spend(p, c->price))
	{
		return OpResult::Fail("insufficient_funds");
	}
	p.ownedCosmetics.push_back(itemId);
	OpResult r = OpResult::Ok();
	r.creditsSpent = c->price.currency == Currency::Credits ? c->price.amount : 0;
	r.granted.push_back({c->type, itemId, 0});
	return r;
}

OpResult CustomizeVehicle(const GameDatabase& db, Profile& p, const std::string& vehicleId, const std::string& paint,
                          const std::string& wheels)
{
	VehicleBuild* b = p.FindVehicle(vehicleId);
	if (!b)
	{
		return OpResult::Fail("not_owned");
	}
	if (!paint.empty())
	{
		const CosmeticDef* c = db.FindCosmetic(paint);
		if (!c || c->type != ItemType::Paint)
		{
			return OpResult::Fail("not_found");
		}
		if (!p.OwnsCosmetic(db, paint))
		{
			return OpResult::Fail("not_owned");
		}
		b->paint = paint;
	}
	if (!wheels.empty())
	{
		const CosmeticDef* c = db.FindCosmetic(wheels);
		if (!c || c->type != ItemType::Wheels)
		{
			return OpResult::Fail("not_found");
		}
		if (!p.OwnsCosmetic(db, wheels))
		{
			return OpResult::Fail("not_owned");
		}
		b->wheels = wheels;
	}
	return OpResult::Ok();
}

OpResult GrantItems(const GameDatabase& db, Profile& p, const std::vector<ItemRef>& items)
{
	OpResult r = OpResult::Ok();
	for (const ItemRef& item : items)
	{
		if (item.type == ItemType::Currency)
		{
			if (item.id == "gold")
			{
				p.gold += item.amount;
			}
			else
			{
				p.credits += item.amount;
			}
			r.granted.push_back(item);
			continue;
		}
		if (!db.ItemExists(item))
		{
			continue;
		}
		if (p.Owns(db, item))
		{
			// Same rule as the backend: 50% of the base price, rounded down, in the base price's currency.
			const std::optional<Price> price = db.ItemPrice(item);
			const int64_t refund = price ? price->amount / 2 : 0;
			if (refund > 0)
			{
				const bool gold = price->currency == Currency::Gold;
				(gold ? p.gold : p.credits) += refund;
				r.granted.push_back({ItemType::Currency, gold ? "gold" : "credits", refund});
			}
			continue;
		}
		switch (item.type)
		{
		case ItemType::Vehicle:
			p.vehicles.push_back(MakeDefaultBuild(db, item.id));
			break;
		case ItemType::Equipment:
			p.ownedEquipment.push_back(item.id);
			break;
		default:
			p.ownedCosmetics.push_back(item.id);
			break;
		}
		r.granted.push_back(item);
	}
	return r;
}

// ---------------------------------------------------------------------------------------------
// Progression

int64_t XpToNextLevel(const ProgressionDef& prog, int level)
{
	return static_cast<int64_t>(std::llround(prog.xpBase * std::pow(prog.xpGrowth, static_cast<double>(level - 1))));
}

LevelUpResult AddXp(const GameDatabase& db, Profile& p, int64_t xp)
{
	LevelUpResult result;
	const ProgressionDef& prog = db.Progression();
	p.xp += std::max<int64_t>(0, xp);
	while (p.level < prog.maxLevel)
	{
		const int64_t needed = XpToNextLevel(prog, p.level);
		if (p.xp < needed)
		{
			break;
		}
		p.xp -= needed;
		p.level += 1;
		result.levelsGained += 1;
		for (const auto& [level, items] : prog.levelRewards)
		{
			if (level == p.level)
			{
				const OpResult granted = GrantItems(db, p, items);
				result.rewards.insert(result.rewards.end(), granted.granted.begin(), granted.granted.end());
			}
		}
	}
	if (p.level >= prog.maxLevel)
	{
		p.xp = 0;
	}
	return result;
}

MatchReward ComputeMatchReward(const ProgressionDef& prog, int score, bool won, bool ranked)
{
	const MatchRewardDef& m = prog.match;
	double credits = static_cast<double>(m.participationCredits) + std::max(0, score) * static_cast<double>(m.creditsPerScore);
	double xp = static_cast<double>(m.participationXp) + std::max(0, score) * static_cast<double>(m.xpPerScore);
	if (won)
	{
		credits *= m.winMultiplier;
		xp *= m.winMultiplier;
	}
	if (ranked)
	{
		credits *= m.rankedMultiplier;
		xp *= m.rankedMultiplier;
	}
	MatchReward r;
	r.credits = std::min<int64_t>(std::llround(credits), m.maxCredits);
	r.xp = std::min<int64_t>(std::llround(xp), m.maxXp);
	return r;
}

std::pair<const RankDef*, int> RankForRating(const ProgressionDef& prog, double rating)
{
	if (prog.ranks.empty())
	{
		return {nullptr, 1};
	}
	size_t idx = 0;
	for (size_t i = 0; i < prog.ranks.size(); ++i)
	{
		if (rating >= prog.ranks[i].minRating)
		{
			idx = i;
		}
	}
	const RankDef& rank = prog.ranks[idx];
	if (rank.divisions <= 1)
	{
		return {&rank, 1};
	}
	const double lo = rank.minRating;
	const double hi = idx + 1 < prog.ranks.size() ? prog.ranks[idx + 1].minRating : lo + 400.0;
	const double t = std::clamp((rating - lo) / std::max(1.0, hi - lo), 0.0, 0.9999);
	// Division 1 is the highest.
	const int division = rank.divisions - static_cast<int>(t * rank.divisions);
	return {&rank, division};
}

} // namespace crash
