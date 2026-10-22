// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Database.h"

#include <algorithm>
#include <cmath>
#include <set>
#include <sstream>

namespace crash
{
// ---------------------------------------------------------------------------------------------
// StatMods

namespace
{
struct ModKey
{
	const char* key;
	float StatMods::*member;
	bool multiplicative;
};

const ModKey kModKeys[] = {
    {"powerMul", &StatMods::powerMul, true},
    {"torqueMul", &StatMods::torqueMul, true},
    {"gripMul", &StatMods::gripMul, true},
    {"brakeMul", &StatMods::brakeMul, true},
    {"steerMul", &StatMods::steerMul, true},
    {"topSpeedMul", &StatMods::topSpeedMul, true},
    {"massMul", &StatMods::massMul, true},
    {"toughnessMul", &StatMods::toughnessMul, true},
    {"ramPowerMul", &StatMods::ramPowerMul, true},
    {"boostAccelMul", &StatMods::boostAccelMul, true},
    {"boostRegenMul", &StatMods::boostRegenMul, true},
    {"downforceMul", &StatMods::downforceMul, true},
    {"dragMul", &StatMods::dragMul, true},
    {"suspensionMul", &StatMods::suspensionMul, true},
    {"wheelHpMul", &StatMods::wheelHpMul, true},
    {"engineHpMul", &StatMods::engineHpMul, true},
    {"selfDamageMul", &StatMods::selfDamageMul, true},
    {"sideDamageMul", &StatMods::sideDamageMul, true},
    {"rearDamageMul", &StatMods::rearDamageMul, true},
    {"massAddKg", &StatMods::massAddKg, false},
    {"boostCapacityAdd", &StatMods::boostCapacityAdd, false},
    {"cgHeightAdd", &StatMods::cgHeightAdd, false},
    {"launchPower", &StatMods::launchPower, false},
    {"tireDamage", &StatMods::tireDamage, false},
    {"repairPerSec", &StatMods::repairPerSec, false},
    {"knockback", &StatMods::knockback, false},
};

struct ArmorKey
{
	const char* key;
	int zone; // -1 = all
};

const ArmorKey kArmorKeys[] = {
    {"armorFront", 0}, {"armorRear", 1}, {"armorLeft", 2}, {"armorRight", 3}, {"armorTop", 4}, {"armorBottom", 5}, {"armorAll", -1},
};
} // namespace

void StatMods::Combine(const StatMods& other)
{
	for (const ModKey& k : kModKeys)
	{
		if (k.multiplicative)
		{
			this->*k.member *= other.*k.member;
		}
		else
		{
			this->*k.member += other.*k.member;
		}
	}
	for (int i = 0; i < kZoneCount; ++i)
	{
		armor[static_cast<size_t>(i)] += other.armor[static_cast<size_t>(i)];
	}
}

bool StatMods::ApplyKey(std::string_view key, float value)
{
	for (const ModKey& k : kModKeys)
	{
		if (key == k.key)
		{
			if (k.multiplicative)
			{
				this->*k.member *= value;
			}
			else
			{
				this->*k.member += value;
			}
			return true;
		}
	}
	for (const ArmorKey& a : kArmorKeys)
	{
		if (key == a.key)
		{
			if (a.zone < 0)
			{
				for (float& z : armor)
				{
					z += value;
				}
			}
			else
			{
				armor[static_cast<size_t>(a.zone)] += value;
			}
			return true;
		}
	}
	return false;
}

bool StatMods::IsKnownKey(std::string_view key)
{
	StatMods scratch;
	return scratch.ApplyKey(key, 1.0f);
}

// ---------------------------------------------------------------------------------------------
// Small helpers

EngineZoneWeights GetEngineZoneWeights(EngineLayout layout)
{
	EngineZoneWeights w;
	switch (layout)
	{
	case EngineLayout::Front:
		w.weights[ToIndex(Zone::Front)] = 1.0f;
		break;
	case EngineLayout::Mid:
		w.weights[ToIndex(Zone::Rear)] = 0.5f;
		w.weights[ToIndex(Zone::Left)] = 0.25f;
		w.weights[ToIndex(Zone::Right)] = 0.25f;
		break;
	case EngineLayout::Rear:
		w.weights[ToIndex(Zone::Rear)] = 1.0f;
		break;
	default:
		break;
	}
	return w;
}

bool ParseColor(std::string_view text, Color& out)
{
	if (text.size() != 7 || text[0] != '#')
	{
		return false;
	}
	unsigned values[3] = {0, 0, 0};
	for (int c = 0; c < 3; ++c)
	{
		unsigned v = 0;
		for (int i = 0; i < 2; ++i)
		{
			const char h = text[static_cast<size_t>(1 + c * 2 + i)];
			v <<= 4;
			if (h >= '0' && h <= '9')
			{
				v |= static_cast<unsigned>(h - '0');
			}
			else if (h >= 'a' && h <= 'f')
			{
				v |= static_cast<unsigned>(h - 'a' + 10);
			}
			else if (h >= 'A' && h <= 'F')
			{
				v |= static_cast<unsigned>(h - 'A' + 10);
			}
			else
			{
				return false;
			}
		}
		values[c] = v;
	}
	out = Color(static_cast<uint8_t>(values[0]), static_cast<uint8_t>(values[1]), static_cast<uint8_t>(values[2]));
	return true;
}

std::string ColorToHex(const Color& color)
{
	static const char* kHex = "0123456789ABCDEF";
	std::string s = "#";
	for (const uint8_t v : {color.r, color.g, color.b})
	{
		s.push_back(kHex[v >> 4]);
		s.push_back(kHex[v & 0xF]);
	}
	return s;
}

bool ParsePrice(const JsonValue& value, Price& out)
{
	if (!value.IsObject() || value.Size() != 1)
	{
		return false;
	}
	if (value.Has("credits"))
	{
		out.currency = Currency::Credits;
		out.amount = value["credits"].AsInt64(-1);
	}
	else if (value.Has("gold"))
	{
		out.currency = Currency::Gold;
		out.amount = value["gold"].AsInt64(-1);
	}
	else
	{
		return false;
	}
	return out.amount >= 0;
}

JsonValue PriceToJson(const Price& price)
{
	JsonValue v = JsonValue::MakeObject();
	v.Set(ToString(price.currency), JsonValue(price.amount));
	return v;
}

bool ParseItemRef(const JsonValue& value, ItemRef& out)
{
	if (!value.IsObject() || !FromString(value["type"].AsString(), out.type))
	{
		return false;
	}
	out.id = value["id"].AsString();
	out.amount = value["amount"].AsInt64(0);
	if (out.type == ItemType::Currency)
	{
		return (out.id == "credits" || out.id == "gold") && out.amount > 0;
	}
	return !out.id.empty();
}

JsonValue ItemRefToJson(const ItemRef& item)
{
	JsonValue v = JsonValue::MakeObject();
	v.Set("type", JsonValue(ToString(item.type)));
	v.Set("id", JsonValue(item.id));
	if (item.type == ItemType::Currency)
	{
		v.Set("amount", JsonValue(item.amount));
	}
	return v;
}

int64_t RoundTo100(double value)
{
	return static_cast<int64_t>(std::floor((value + 50.0) / 100.0)) * 100;
}

bool IsValidId(std::string_view id)
{
	if (id.empty() || id.size() > 64)
	{
		return false;
	}
	for (const char c : id)
	{
		if (!((c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'))
		{
			return false;
		}
	}
	return true;
}

const std::vector<std::string>& DataFileNames()
{
	static const std::vector<std::string> kFiles = {
	    "brands.json", "vehicles.json", "upgrades.json", "equipment.json",   "cosmetics.json", "arenas.json",
	    "modes.json",  "ai.json",       "campaign.json", "progression.json", "graphics.json",
	};
	return kFiles;
}

bool VehicleDef::HasTag(std::string_view tag) const
{
	return std::find(tags.begin(), tags.end(), tag) != tags.end();
}

bool EquipmentDef::AllowsClass(VehicleClass c) const
{
	return allowedClasses.empty() || std::find(allowedClasses.begin(), allowedClasses.end(), c) != allowedClasses.end();
}

bool ArenaDef::IsInside(const Vec3& p, float margin) const
{
	switch (shape)
	{
	case ArenaShape::Rect:
		return std::fabs(p.x) <= HalfX() - margin && std::fabs(p.y) <= HalfY() - margin;
	case ArenaShape::Circle:
		return p.Length2D() <= HalfX() - margin;
	case ArenaShape::Octagon:
	{
		// Regular octagon with circumradius R: inradius = R * cos(22.5 deg).
		const float r = (HalfX() - margin) * 0.92387953f;
		const float ax = std::fabs(p.x);
		const float ay = std::fabs(p.y);
		return ax <= r && ay <= r && (ax + ay) * 0.70710678f <= r;
	}
	default:
		return false;
	}
}

const PropDef* ArenaDef::FindHill() const
{
	for (const PropDef& p : props)
	{
		if (p.type == PropType::HillZone)
		{
			return &p;
		}
	}
	return nullptr;
}

const GraphicsValueDef* GraphicsOptionDef::FindValue(std::string_view valueId) const
{
	for (const GraphicsValueDef& v : values)
	{
		if (v.id == valueId)
		{
			return &v;
		}
	}
	return nullptr;
}

const UpscalerModeDef* UpscalerDef::FindMode(std::string_view modeId) const
{
	for (const UpscalerModeDef& m : modes)
	{
		if (m.id == modeId)
		{
			return &m;
		}
	}
	return nullptr;
}

const GraphicsOptionDef* GraphicsDef::FindOption(std::string_view id) const
{
	for (const GraphicsOptionDef& o : options)
	{
		if (o.id == id)
		{
			return &o;
		}
	}
	return nullptr;
}

const UpscalerDef* GraphicsDef::FindUpscaler(std::string_view id) const
{
	for (const UpscalerDef& u : upscalers)
	{
		if (u.id == id)
		{
			return &u;
		}
	}
	return nullptr;
}

const GraphicsPresetDef* GraphicsDef::FindPreset(std::string_view id) const
{
	for (const GraphicsPresetDef& p : presets)
	{
		if (p.id == id)
		{
			return &p;
		}
	}
	return nullptr;
}

// ---------------------------------------------------------------------------------------------
// Field reader with error context

namespace
{
class FieldReader
{
public:
	FieldReader(std::function<void(const std::string&)> onError, std::string context)
	    : onError_(std::move(onError)), context_(std::move(context))
	{
	}

	void Fail(const std::string& what) const { onError_(context_ + ": " + what); }

	std::string Id(const JsonValue& obj, const char* key = "id") const
	{
		const std::string& s = obj[key].AsString();
		if (!IsValidId(s))
		{
			Fail(std::string("invalid id '") + s + "' in field '" + key + "'");
		}
		return s;
	}

	std::string Str(const JsonValue& obj, const char* key, bool required = true) const
	{
		const JsonValue& v = obj[key];
		if (v.IsString())
		{
			return v.AsString();
		}
		if (required || !v.IsNull())
		{
			Fail(std::string("field '") + key + "' must be a string");
		}
		return std::string();
	}

	std::string OptStr(const JsonValue& obj, const char* key) const
	{
		const JsonValue& v = obj[key];
		if (v.IsNull())
		{
			return std::string();
		}
		if (!v.IsString())
		{
			Fail(std::string("field '") + key + "' must be a string or null");
			return std::string();
		}
		return v.AsString();
	}

	float Num(const JsonValue& obj, const char* key, float fallback, float lo, float hi, bool required = false) const
	{
		const JsonValue& v = obj[key];
		if (v.IsNull())
		{
			if (required)
			{
				Fail(std::string("missing number '") + key + "'");
			}
			return fallback;
		}
		if (!v.IsNumber())
		{
			Fail(std::string("field '") + key + "' must be a number");
			return fallback;
		}
		const float f = v.AsFloat();
		if (!(f >= lo && f <= hi))
		{
			std::ostringstream os;
			os << "field '" << key << "' = " << f << " out of range [" << lo << ", " << hi << "]";
			Fail(os.str());
			return Clamp(f, lo, hi);
		}
		return f;
	}

	int Int(const JsonValue& obj, const char* key, int fallback, int lo, int hi, bool required = false) const
	{
		const float f = Num(obj, key, static_cast<float>(fallback), static_cast<float>(lo), static_cast<float>(hi), required);
		return static_cast<int>(std::lround(f));
	}

	bool Bool(const JsonValue& obj, const char* key, bool fallback) const
	{
		const JsonValue& v = obj[key];
		if (v.IsNull())
		{
			return fallback;
		}
		if (!v.IsBool())
		{
			Fail(std::string("field '") + key + "' must be a bool");
			return fallback;
		}
		return v.AsBool();
	}

	Color Col(const JsonValue& obj, const char* key, Color fallback, bool* hasValue = nullptr) const
	{
		const JsonValue& v = obj[key];
		if (hasValue)
		{
			*hasValue = false;
		}
		if (v.IsNull())
		{
			return fallback;
		}
		Color c;
		if (!ParseColor(v.AsString(), c))
		{
			Fail(std::string("field '") + key + "' must be a #RRGGBB color");
			return fallback;
		}
		if (hasValue)
		{
			*hasValue = true;
		}
		return c;
	}

	template <typename E> E Enum(const JsonValue& obj, const char* key, E fallback, bool required = true) const
	{
		const JsonValue& v = obj[key];
		if (v.IsNull() && !required)
		{
			return fallback;
		}
		E out = fallback;
		if (!FromString(v.AsString(), out))
		{
			Fail(std::string("field '") + key + "' has invalid value '" + v.AsString() + "'");
		}
		return out;
	}

	Vec3 Vec(const JsonValue& obj, const char* key, Vec3 fallback, bool required = false) const
	{
		const JsonValue& v = obj[key];
		if (v.IsNull())
		{
			if (required)
			{
				Fail(std::string("missing vector '") + key + "'");
			}
			return fallback;
		}
		if (!v.IsArray() || v.Size() < 2 || v.Size() > 3)
		{
			Fail(std::string("field '") + key + "' must be [x, y] or [x, y, z]");
			return fallback;
		}
		for (const JsonValue& c : v.Items())
		{
			if (!c.IsNumber())
			{
				Fail(std::string("field '") + key + "' must contain numbers");
				return fallback;
			}
		}
		return {v[0].AsFloat(), v[1].AsFloat(), v.Size() > 2 ? v[2].AsFloat() : fallback.z};
	}

	StatMods Mods(const JsonValue& obj) const
	{
		StatMods mods;
		if (obj.IsNull())
		{
			return mods;
		}
		if (!obj.IsObject())
		{
			Fail("mods must be an object");
			return mods;
		}
		for (const auto& [key, value] : obj.Members())
		{
			if (!value.IsNumber())
			{
				Fail("mod '" + key + "' must be a number");
				continue;
			}
			if (!mods.ApplyKey(key, value.AsFloat()))
			{
				Fail("unknown mod key '" + key + "'");
			}
		}
		return mods;
	}

	std::vector<std::string> StrList(const JsonValue& obj, const char* key) const
	{
		std::vector<std::string> out;
		const JsonValue& v = obj[key];
		if (v.IsNull())
		{
			return out;
		}
		if (!v.IsArray())
		{
			Fail(std::string("field '") + key + "' must be an array of strings");
			return out;
		}
		for (const JsonValue& s : v.Items())
		{
			if (!s.IsString())
			{
				Fail(std::string("field '") + key + "' must contain strings");
				continue;
			}
			out.push_back(s.AsString());
		}
		return out;
	}

	CvarList Cvars(const JsonValue& obj) const
	{
		CvarList out;
		if (obj.IsNull())
		{
			return out;
		}
		if (!obj.IsObject())
		{
			Fail("cvars must be an object of name -> string value");
			return out;
		}
		for (const auto& [key, value] : obj.Members())
		{
			if (value.IsString())
			{
				out.emplace_back(key, value.AsString());
			}
			else if (value.IsNumber())
			{
				out.emplace_back(key, value.Dump());
			}
			else
			{
				Fail("cvar '" + key + "' must be a string or number");
			}
		}
		return out;
	}

	std::vector<std::pair<std::string, float>> FloatMap(const JsonValue& obj) const
	{
		std::vector<std::pair<std::string, float>> out;
		for (const auto& [key, value] : obj.Members())
		{
			if (!value.IsNumber())
			{
				Fail("'" + key + "' must be a number");
				continue;
			}
			out.emplace_back(key, value.AsFloat());
		}
		return out;
	}

	std::vector<ItemRef> Items(const JsonValue& arr) const
	{
		std::vector<ItemRef> out;
		for (const JsonValue& v : arr.Items())
		{
			ItemRef item;
			if (!ParseItemRef(v, item))
			{
				Fail("invalid item reference " + v.Dump());
				continue;
			}
			out.push_back(std::move(item));
		}
		return out;
	}

	Price PriceField(const JsonValue& obj, const char* key) const
	{
		Price p;
		if (!ParsePrice(obj[key], p))
		{
			Fail(std::string("field '") + key + "' must be {\"credits\": n} or {\"gold\": n}");
		}
		return p;
	}

private:
	std::function<void(const std::string&)> onError_;
	std::string context_;
};

template <typename T>
bool CheckUnique(const std::vector<T>& items, const std::function<void(const std::string&)>& onError, const char* what)
{
	std::set<std::string> seen;
	bool ok = true;
	for (const T& item : items)
	{
		if (!seen.insert(item.id).second)
		{
			onError(std::string("duplicate ") + what + " id '" + item.id + "'");
			ok = false;
		}
	}
	return ok;
}

bool CheckVersion(const JsonValue& root, const std::function<void(const std::string&)>& onError)
{
	if (root["version"].AsInt(0) != 1)
	{
		onError("missing or unsupported \"version\" (expected 1)");
		return false;
	}
	return true;
}
} // namespace

// ---------------------------------------------------------------------------------------------
// GameDatabase

void GameDatabase::Error(const std::string& file, const std::string& message)
{
	messages_.push_back({LoadMessage::Severity::Error, file, message});
}

void GameDatabase::Warn(const std::string& file, const std::string& message)
{
	messages_.push_back({LoadMessage::Severity::Warning, file, message});
}

bool GameDatabase::HasErrors() const
{
	for (const LoadMessage& m : messages_)
	{
		if (m.severity == LoadMessage::Severity::Error)
		{
			return true;
		}
	}
	return false;
}

std::string GameDatabase::FormatMessages() const
{
	std::string out;
	for (const LoadMessage& m : messages_)
	{
		out += m.severity == LoadMessage::Severity::Error ? "ERROR " : "WARN  ";
		out += m.file + ": " + m.message + "\n";
	}
	return out;
}

bool GameDatabase::LoadAll(const FileReader& reader)
{
	messages_.clear();
	for (const std::string& file : DataFileNames())
	{
		std::string text;
		if (!reader(file, text))
		{
			Error(file, "file not found");
			continue;
		}
		LoadFile(file, text);
	}
	Validate();
	return !HasErrors();
}

bool GameDatabase::LoadFile(const std::string& fileName, std::string_view text)
{
	JsonValue root;
	JsonError parseError;
	if (!ParseJson(text, root, &parseError))
	{
		Error(fileName, "JSON parse error at " + parseError.ToString());
		return false;
	}
	const size_t errorsBefore = messages_.size();
	auto onError = [this, &fileName](const std::string& m) { Error(fileName, m); };
	if (!CheckVersion(root, onError))
	{
		return false;
	}
	bool ok = false;
	if (fileName == "brands.json")
	{
		ok = ParseBrands(fileName, root);
	}
	else if (fileName == "vehicles.json")
	{
		ok = ParseVehicles(fileName, root);
	}
	else if (fileName == "upgrades.json")
	{
		ok = ParseUpgrades(fileName, root);
	}
	else if (fileName == "equipment.json")
	{
		ok = ParseEquipment(fileName, root);
	}
	else if (fileName == "cosmetics.json")
	{
		ok = ParseCosmetics(fileName, root);
	}
	else if (fileName == "arenas.json")
	{
		ok = ParseArenas(fileName, root);
	}
	else if (fileName == "modes.json")
	{
		ok = ParseModes(fileName, root);
	}
	else if (fileName == "ai.json")
	{
		ok = ParseAI(fileName, root);
	}
	else if (fileName == "campaign.json")
	{
		ok = ParseCampaign(fileName, root);
	}
	else if (fileName == "progression.json")
	{
		ok = ParseProgression(fileName, root);
	}
	else if (fileName == "graphics.json")
	{
		ok = ParseGraphics(fileName, root);
	}
	else
	{
		Warn(fileName, "unknown data file ignored");
		return true;
	}
	RebuildIndices();
	return ok && messages_.size() == errorsBefore;
}

void GameDatabase::RebuildIndices()
{
	brandIndex_.clear();
	for (size_t i = 0; i < brands_.size(); ++i)
	{
		brandIndex_.emplace(brands_[i].id, i);
	}
	vehicleIndex_.clear();
	for (size_t i = 0; i < vehicles_.size(); ++i)
	{
		vehicleIndex_.emplace(vehicles_[i].id, i);
	}
	equipmentIndex_.clear();
	for (size_t i = 0; i < equipment_.size(); ++i)
	{
		equipmentIndex_.emplace(equipment_[i].id, i);
	}
	cosmeticIndex_.clear();
	for (size_t i = 0; i < cosmetics_.size(); ++i)
	{
		cosmeticIndex_.emplace(cosmetics_[i].id, i);
	}
	arenaIndex_.clear();
	for (size_t i = 0; i < arenas_.size(); ++i)
	{
		arenaIndex_.emplace(arenas_[i].id, i);
	}
	modeIndex_.clear();
	for (size_t i = 0; i < modes_.size(); ++i)
	{
		modeIndex_.emplace(modes_[i].id, i);
	}
	aiIndex_.clear();
	for (size_t i = 0; i < aiProfiles_.size(); ++i)
	{
		aiIndex_.emplace(aiProfiles_[i].id, i);
	}
	levelIndex_.clear();
	for (size_t c = 0; c < campaign_.chapters.size(); ++c)
	{
		for (size_t l = 0; l < campaign_.chapters[c].levels.size(); ++l)
		{
			levelIndex_.emplace(campaign_.chapters[c].levels[l].id, std::make_pair(c, l));
		}
	}
}

bool GameDatabase::ParseBrands(const std::string& file, const JsonValue& root)
{
	brands_.clear();
	auto onError = [this, &file](const std::string& m) { Error(file, m); };
	for (const JsonValue& b : root["brands"].Items())
	{
		FieldReader r(onError, "brand '" + b["id"].AsString() + "'");
		BrandDef def;
		def.id = r.Id(b);
		def.name = r.Str(b, "name");
		def.country = r.Str(b, "country", false);
		def.color = r.Col(b, "color", Color(200, 200, 200));
		def.licensed = r.Bool(b, "licensed", false);
		def.reference = r.Str(b, "reference", false);
		brands_.push_back(std::move(def));
	}
	if (brands_.empty())
	{
		onError("no brands defined");
	}
	return CheckUnique(brands_, onError, "brand");
}

bool GameDatabase::ParseVehicles(const std::string& file, const JsonValue& root)
{
	vehicles_.clear();
	auto onError = [this, &file](const std::string& m) { Error(file, m); };
	for (const JsonValue& v : root["vehicles"].Items())
	{
		FieldReader r(onError, "vehicle '" + v["id"].AsString() + "'");
		VehicleDef def;
		def.id = r.Id(v);
		def.brand = r.Str(v, "brand");
		def.name = r.Str(v, "name");
		def.reference = r.Str(v, "reference", false);
		def.vehicleClass = r.Enum(v, "class", VehicleClass::Sedan);
		def.body = r.Enum(v, "body", def.vehicleClass, false);
		def.tier = r.Int(v, "tier", 1, 1, 5, true);
		def.price = r.PriceField(v, "price");
		def.unlockLevel = r.Int(v, "unlockLevel", 1, 1, 100);
		def.starter = r.Bool(v, "starter", false);
		def.drivetrain = r.Enum(v, "drivetrain", Drivetrain::FWD);
		def.engineLayout = r.Enum(v, "engineLayout", EngineLayout::Front);
		def.massKg = r.Num(v, "massKg", 1400.0f, 500.0f, 40000.0f, true);
		def.powerKw = r.Num(v, "powerKw", 100.0f, 20.0f, 2000.0f, true);
		def.torqueNm = r.Num(v, "torqueNm", 200.0f, 50.0f, 20000.0f, true);
		def.idleRpm = r.Num(v, "idleRpm", 900.0f, 0.0f, 3000.0f);
		def.redlineRpm = r.Num(v, "redlineRpm", 6500.0f, 1500.0f, 25000.0f, true);
		for (const JsonValue& g : v["gears"].Items())
		{
			def.gears.push_back(g.AsFloat());
		}
		if (def.gears.empty() || def.gears.size() > 10)
		{
			r.Fail("gears must have 1-10 entries");
		}
		for (size_t i = 0; i < def.gears.size(); ++i)
		{
			if (def.gears[i] <= 0.0f || (i > 0 && def.gears[i] >= def.gears[i - 1]))
			{
				r.Fail("gears must be positive and strictly decreasing");
				break;
			}
		}
		def.reverseRatio = r.Num(v, "reverseRatio", 3.2f, 0.5f, 20.0f);
		def.finalDrive = r.Num(v, "finalDrive", 4.0f, 0.5f, 15.0f);
		def.topSpeedKmh = r.Num(v, "topSpeedKmh", 190.0f, 40.0f, 520.0f, true);
		const JsonValue& d = v["dimensions"];
		if (!d.IsObject())
		{
			r.Fail("missing dimensions");
		}
		def.dimensions.length = r.Num(d, "length", 4.5f, 2.0f, 20.0f, true);
		def.dimensions.width = r.Num(d, "width", 1.8f, 1.2f, 3.0f, true);
		def.dimensions.height = r.Num(d, "height", 1.45f, 0.9f, 4.5f, true);
		def.dimensions.wheelbase = r.Num(d, "wheelbase", 2.7f, 1.5f, 12.0f, true);
		def.dimensions.trackFront = r.Num(d, "trackFront", 1.55f, 1.0f, 2.8f, true);
		def.dimensions.trackRear = r.Num(d, "trackRear", 1.55f, 1.0f, 2.8f, true);
		def.dimensions.groundClearance = r.Num(d, "groundClearance", 0.14f, 0.05f, 0.8f, true);
		def.dimensions.wheelRadius = r.Num(d, "wheelRadius", 0.33f, 0.2f, 0.9f, true);
		def.dimensions.wheelWidth = r.Num(d, "wheelWidth", 0.23f, 0.1f, 0.7f, true);
		if (def.dimensions.wheelbase >= def.dimensions.length)
		{
			r.Fail("wheelbase must be shorter than length");
		}
		if (def.dimensions.trackFront >= def.dimensions.width || def.dimensions.trackRear >= def.dimensions.width)
		{
			r.Fail("track must be narrower than width");
		}
		def.cgHeight = r.Num(v, "cgHeight", 0.0f, 0.0f, 2.5f);
		if (def.cgHeight <= 0.0f)
		{
			def.cgHeight = 0.35f * def.dimensions.height + def.dimensions.groundClearance;
		}
		def.frontWeight = r.Num(v, "frontWeight", 0.55f, 0.3f, 0.7f);
		def.grip = r.Num(v, "grip", 1.0f, 0.6f, 1.6f);
		def.brakeG = r.Num(v, "brakeG", 0.0f, 0.0f, 1.6f);
		if (def.brakeG <= 0.0f)
		{
			def.brakeG = 0.9f * def.grip;
		}
		def.steerLockDeg = r.Num(v, "steerLockDeg", 35.0f, 15.0f, 55.0f);
		const JsonValue& s = v["suspension"];
		def.suspensionFrequency = r.Num(s, "frequency", 1.5f, 0.8f, 3.0f);
		def.suspensionDamping = r.Num(s, "damping", 0.4f, 0.2f, 0.9f);
		def.suspensionTravel = r.Num(s, "travel", 0.15f, 0.05f, 0.6f);
		def.antiRoll = r.Num(s, "antiRoll", 0.6f, 0.0f, 1.5f);
		def.dragCoefficient = r.Num(v, "dragCoefficient", 0.32f, 0.15f, 1.0f);
		def.downforce = r.Num(v, "downforce", 0.0f, 0.0f, 5.0f);
		def.toughness = r.Num(v, "toughness", 1.0f, 0.5f, 2.5f);
		def.armor.fill(0.05f);
		const JsonValue& a = v["armor"];
		for (int z = 0; z < kZoneCount; ++z)
		{
			const std::string key(ToString(static_cast<Zone>(z)));
			def.armor[static_cast<size_t>(z)] = r.Num(a, key.c_str(), 0.05f, 0.0f, 0.6f);
		}
		def.ramPower = r.Num(v, "ramPower", 1.0f, 0.5f, 2.0f);
		const JsonValue& boost = v["boost"];
		def.boostCapacity = r.Num(boost, "capacity", 100.0f, 0.0f, 300.0f);
		def.boostAccel = r.Num(boost, "accel", 6.0f, 0.0f, 20.0f);
		def.boostRegenPerSec = r.Num(boost, "regenPerSec", 6.0f, 0.0f, 50.0f);
		for (const JsonValue& c : v["colors"].Items())
		{
			Color col;
			if (ParseColor(c.AsString(), col))
			{
				def.colors.push_back(col);
			}
			else
			{
				r.Fail("invalid color " + c.Dump());
			}
		}
		if (def.colors.empty())
		{
			def.colors.push_back(Color(200, 200, 200));
		}
		def.tags = r.StrList(v, "tags");
		vehicles_.push_back(std::move(def));
	}
	if (vehicles_.empty())
	{
		onError("no vehicles defined");
	}
	return CheckUnique(vehicles_, onError, "vehicle");
}

bool GameDatabase::ParseUpgrades(const std::string& file, const JsonValue& root)
{
	upgrades_ = UpgradesDef();
	auto onError = [this, &file](const std::string& m) { Error(file, m); };
	FieldReader top(onError, "upgrades");
	const JsonValue& mult = root["tierCostMultiplier"];
	if (mult.Size() != 5)
	{
		top.Fail("tierCostMultiplier must have 5 entries");
	}
	for (size_t i = 0; i < 5 && i < mult.Size(); ++i)
	{
		upgrades_.tierCostMultiplier[i] = mult[i].AsFloat(1.0f);
	}
	std::array<bool, kUpgradeCategoryCount> seen{};
	for (const JsonValue& c : root["categories"].Items())
	{
		FieldReader r(onError, "upgrade '" + c["id"].AsString() + "'");
		UpgradeCategory cat = UpgradeCategory::Engine;
		if (!FromString(c["id"].AsString(), cat))
		{
			r.Fail("unknown upgrade category");
			continue;
		}
		const size_t idx = static_cast<size_t>(cat);
		if (seen[idx])
		{
			r.Fail("duplicate category");
			continue;
		}
		seen[idx] = true;
		UpgradeCategoryDef& def = upgrades_.categories[idx];
		def.category = cat;
		def.id = c["id"].AsString();
		def.name = r.Str(c, "name");
		def.description = r.Str(c, "description", false);
		for (const JsonValue& l : c["levels"].Items())
		{
			UpgradeLevelDef level;
			level.cost = static_cast<int64_t>(r.Num(l, "cost", 0.0f, 1.0f, 1.0e8f, true));
			level.mods = r.Mods(l["mods"]);
			def.levels.push_back(level);
		}
		if (def.levels.size() != static_cast<size_t>(kMaxUpgradeLevel))
		{
			r.Fail("must have exactly 5 levels");
		}
	}
	for (int i = 0; i < kUpgradeCategoryCount; ++i)
	{
		if (!seen[static_cast<size_t>(i)])
		{
			top.Fail(std::string("missing category '") + std::string(ToString(static_cast<UpgradeCategory>(i))) + "'");
		}
	}
	return true;
}

bool GameDatabase::ParseEquipment(const std::string& file, const JsonValue& root)
{
	equipment_.clear();
	auto onError = [this, &file](const std::string& m) { Error(file, m); };
	for (const JsonValue& e : root["equipment"].Items())
	{
		FieldReader r(onError, "equipment '" + e["id"].AsString() + "'");
		EquipmentDef def;
		def.id = r.Id(e);
		def.slot = r.Enum(e, "slot", EquipSlot::Front);
		def.name = r.Str(e, "name");
		def.description = r.Str(e, "description", false);
		def.rarity = r.Enum(e, "rarity", Rarity::Common, false);
		// Design rule: melee only. Anything that could damage at a distance is rejected.
		if (!e["contactOnly"].AsBool(false))
		{
			r.Fail("equipment must be contactOnly (no ranged equipment allowed)");
		}
		for (const char* forbidden : {"range", "projectile", "ammo", "projectiles", "fireRate"})
		{
			if (e.Has(forbidden))
			{
				r.Fail(std::string("ranged field '") + forbidden + "' is not allowed");
			}
		}
		const JsonValue& g = e["geometry"];
		def.geometry.shape = r.Enum(g, "shape", GeometryShape::None, false);
		def.geometry.length = r.Num(g, "length", 0.0f, 0.0f, 2.0f);
		def.geometry.angleDeg = r.Num(g, "angleDeg", 0.0f, 0.0f, 60.0f);
		def.geometry.color = r.Col(g, "color", Color(90, 95, 102), &def.geometry.hasColor);
		def.mods = r.Mods(e["mods"]);
		const JsonValue& ab = e["ability"];
		if (!ab.IsNull())
		{
			def.ability = r.Enum(e, "ability", AbilityType::None);
			if (def.slot != EquipSlot::Ability)
			{
				r.Fail("only items in the 'ability' slot may grant an ability");
			}
		}
		if (def.slot == EquipSlot::Ability && def.ability == AbilityType::None)
		{
			r.Fail("ability-slot item must declare an ability");
		}
		def.cr = r.Int(e, "cr", 0, 0, 500);
		def.price = r.PriceField(e, "price");
		def.unlockLevel = r.Int(e, "unlockLevel", 1, 1, 100);
		for (const std::string& c : r.StrList(e, "allowedClasses"))
		{
			VehicleClass vc;
			if (FromString(c, vc))
			{
				def.allowedClasses.push_back(vc);
			}
			else
			{
				r.Fail("unknown class '" + c + "' in allowedClasses");
			}
		}
		equipment_.push_back(std::move(def));
	}
	return CheckUnique(equipment_, onError, "equipment");
}

bool GameDatabase::ParseCosmetics(const std::string& file, const JsonValue& root)
{
	cosmetics_.clear();
	auto onError = [this, &file](const std::string& m) { Error(file, m); };
	struct Section
	{
		const char* key;
		ItemType type;
	};
	const Section sections[] = {
	    {"paints", ItemType::Paint}, {"wheels", ItemType::Wheels}, {"sparks", ItemType::Sparks},
	    {"horns", ItemType::Horn},   {"titles", ItemType::Title},  {"banners", ItemType::Banner},
	};
	for (const Section& section : sections)
	{
		for (const JsonValue& c : root[section.key].Items())
		{
			FieldReader r(onError, std::string(section.key) + " '" + c["id"].AsString() + "'");
			CosmeticDef def;
			def.id = r.Id(c);
			def.type = section.type;
			def.name = r.Str(c, "name");
			def.rarity = r.Enum(c, "rarity", Rarity::Common, false);
			def.price = r.PriceField(c, "price");
			def.color = r.Col(c, "color", Color(200, 200, 200));
			for (const JsonValue& col : c["colors"].Items())
			{
				Color parsed;
				if (ParseColor(col.AsString(), parsed))
				{
					def.colors.push_back(parsed);
				}
			}
			if (section.type == ItemType::Paint)
			{
				const std::string finish = c["finish"].AsString("gloss");
				static const char* kFinishes[] = {"gloss", "metallic", "matte", "chrome", "pearl", "carbon"};
				bool found = false;
				for (int i = 0; i < 6; ++i)
				{
					if (finish == kFinishes[i])
					{
						def.finish = static_cast<PaintFinish>(i);
						found = true;
					}
				}
				if (!found)
				{
					r.Fail("unknown finish '" + finish + "'");
				}
			}
			def.spokes = r.Int(c, "spokes", 5, 0, 20);
			for (const JsonValue& f : c["frequencies"].Items())
			{
				def.frequencies.push_back(f.AsFloat());
			}
			cosmetics_.push_back(std::move(def));
		}
	}
	return CheckUnique(cosmetics_, onError, "cosmetic");
}

bool GameDatabase::ParseArenas(const std::string& file, const JsonValue& root)
{
	arenas_.clear();
	auto onError = [this, &file](const std::string& m) { Error(file, m); };
	for (const JsonValue& a : root["arenas"].Items())
	{
		FieldReader r(onError, "arena '" + a["id"].AsString() + "'");
		ArenaDef def;
		def.id = r.Id(a);
		def.name = r.Str(a, "name");
		def.description = r.Str(a, "description", false);
		def.theme = r.Str(a, "theme", false);
		def.hub = r.Bool(a, "hub", false);
		def.shape = r.Enum(a, "shape", ArenaShape::Rect);
		const JsonValue& size = a["size"];
		def.sizeX = size[0].AsFloat(100.0f);
		def.sizeY = size.Size() > 1 ? size[1].AsFloat(def.sizeX) : def.sizeX;
		if (def.sizeX < 20.0f || def.sizeY < 20.0f || def.sizeX > 2000.0f || def.sizeY > 2000.0f)
		{
			r.Fail("size out of range [20, 2000]");
		}
		def.wallHeight = r.Num(a, "wallHeight", 3.0f, 0.0f, 30.0f);
		def.walls = r.Bool(a, "walls", true);
		def.elevation = r.Num(a, "elevation", 0.0f, 0.0f, 200.0f);
		def.killZ = r.Num(a, "killZ", def.elevation - 30.0f, -500.0f, 200.0f);
		const JsonValue& floorJson = a["floor"];
		def.floorSurface = floorJson["surface"].AsString("asphalt");
		def.floorColor = r.Col(floorJson, "color", Color(80, 80, 80));
		def.floorFriction = r.Num(floorJson, "friction", 1.0f, 0.1f, 1.5f);
		const JsonValue& lightJson = a["lighting"];
		def.lightingPreset = lightJson["preset"].AsString("day");
		def.fog = r.Num(lightJson, "fog", 0.02f, 0.0f, 1.0f);
		def.tint = r.Col(lightJson, "tint", Color(255, 255, 255));
		def.level = a["level"].AsString("");
		for (const JsonValue& p : a["props"].Items())
		{
			PropDef prop;
			prop.type = r.Enum(p, "type", PropType::Block);
			prop.pos = r.Vec(p, "pos", Vec3{}, true);
			prop.yaw = r.Num(p, "yaw", 0.0f, -720.0f, 720.0f);
			if (p["size"].IsNumber())
			{
				const float edge = p["size"].AsFloat(1.0f); // crate_stack: crate edge length
				prop.size = Vec3{edge, edge, edge};
			}
			else
			{
				prop.size = r.Vec(p, "size", Vec3{4.0f, 4.0f, 2.0f});
			}
			prop.color = r.Col(p, "color", Color(128, 128, 128), &prop.hasColor);
			prop.soft = r.Bool(p, "soft", false);
			prop.radius = r.Num(p, "radius", 3.0f, 0.1f, 200.0f);
			prop.height = r.Num(p, "height", 2.0f, 0.1f, 200.0f);
			prop.strength = r.Num(p, "strength", 10.0f, 0.0f, 100.0f);
			prop.speed = r.Num(p, "speed", 90.0f, -1000.0f, 1000.0f);
			prop.period = r.Num(p, "period", 4.0f, 0.2f, 120.0f);
			prop.travel = r.Num(p, "travel", 3.0f, 0.0f, 50.0f);
			prop.count = r.Int(p, "count", 1, 1, 50);
			prop.intensity = r.Num(p, "intensity", 1.0f, 0.0f, 10.0f);
			prop.vehicleClass = r.Enum(p, "class", VehicleClass::Sedan, false);
			prop.to = r.Vec(p, "to", prop.pos);
			def.props.push_back(prop);
		}
		for (const JsonValue& s : a["spawns"].Items())
		{
			SpawnDef spawn;
			spawn.pos = r.Vec(s, "pos", Vec3{}, true);
			spawn.yaw = r.Num(s, "yaw", 0.0f, -720.0f, 720.0f);
			spawn.team = r.Int(s, "team", -1, -1, 2);
			def.spawns.push_back(spawn);
		}
		for (const JsonValue& c : a["collectibles"].Items())
		{
			if (!c.IsArray() || c.Size() < 2)
			{
				r.Fail("collectible must be [x, y, z]");
				continue;
			}
			def.collectibles.push_back({c[0].AsFloat(), c[1].AsFloat(), c[2].AsFloat(0.5f)});
		}
		for (const JsonValue& s : a["secrets"].Items())
		{
			SecretDef secret;
			secret.id = r.Id(s);
			secret.pos = r.Vec(s, "pos", Vec3{}, true);
			secret.radius = r.Num(s, "radius", 5.0f, 0.5f, 100.0f);
			def.secrets.push_back(std::move(secret));
		}
		if (def.spawns.size() < 12)
		{
			r.Fail("needs at least 12 spawns");
		}
		for (const SpawnDef& s : def.spawns)
		{
			if (!def.IsInside(s.pos, 1.0f))
			{
				r.Fail("spawn outside arena bounds");
				break;
			}
		}
		arenas_.push_back(std::move(def));
	}
	return CheckUnique(arenas_, onError, "arena");
}

bool GameDatabase::ParseModes(const std::string& file, const JsonValue& root)
{
	modes_.clear();
	auto onError = [this, &file](const std::string& m) { Error(file, m); };
	for (const JsonValue& m : root["modes"].Items())
	{
		FieldReader r(onError, "mode '" + m["id"].AsString() + "'");
		ModeDef def;
		def.id = r.Id(m);
		def.name = r.Str(m, "name");
		def.description = r.Str(m, "description", false);
		def.teams = r.Int(m, "teams", 0, 0, 3);
		if (def.teams == 1)
		{
			r.Fail("teams must be 0, 2 or 3");
		}
		def.respawn = r.Bool(m, "respawn", false);
		def.respawnDelay = r.Num(m, "respawnDelay", 3.0f, 0.0f, 60.0f);
		def.lives = r.Int(m, "lives", 1, 0, 99);
		def.timeLimit = r.Num(m, "timeLimit", 300.0f, 0.0f, 7200.0f);
		def.scoreLimit = r.Int(m, "scoreLimit", 0, 0, 10000000);
		def.rounds = r.Int(m, "rounds", 1, 1, 15);
		def.roundsToWin = r.Int(m, "roundsToWin", 1, 1, 15);
		def.win = r.Enum(m, "win", WinCondition::LastStanding);
		def.ringOut = r.Bool(m, "ringOut", false);
		def.hill = r.Bool(m, "hill", false);
		def.bounty = r.Bool(m, "bounty", false);
		def.minPlayers = r.Int(m, "minPlayers", 2, 1, 24);
		def.maxPlayers = r.Int(m, "maxPlayers", 12, 1, 24);
		def.online = r.Bool(m, "online", true);
		def.ranked = r.Bool(m, "ranked", false);
		modes_.push_back(std::move(def));
	}
	for (const char* required : {"derby", "takedown", "team_takedown", "team_elimination", "ring_out", "king_of_hill", "bounty", "survival",
	                             "destruction", "boss", "explore"})
	{
		bool found = false;
		for (const ModeDef& m : modes_)
		{
			found = found || m.id == required;
		}
		if (!found)
		{
			onError(std::string("missing required mode '") + required + "'");
		}
	}
	return CheckUnique(modes_, onError, "mode");
}

bool GameDatabase::ParseAI(const std::string& file, const JsonValue& root)
{
	aiProfiles_.clear();
	auto onError = [this, &file](const std::string& m) { Error(file, m); };
	for (const JsonValue& p : root["profiles"].Items())
	{
		FieldReader r(onError, "ai '" + p["id"].AsString() + "'");
		AIProfileDef def;
		def.id = r.Id(p);
		def.name = r.Str(p, "name", false);
		def.skill = r.Num(p, "skill", 0.5f, 0.0f, 1.0f);
		def.aggression = r.Num(p, "aggression", 0.5f, 0.0f, 1.0f);
		def.reaction = r.Num(p, "reaction", 0.4f, 0.05f, 2.0f);
		def.aimLead = r.Num(p, "aimLead", 0.5f, 0.0f, 1.0f);
		def.flankBias = r.Num(p, "flankBias", 0.5f, 0.0f, 1.0f);
		def.abilityUse = r.Num(p, "abilityUse", 0.5f, 0.0f, 1.0f);
		def.boostUse = r.Num(p, "boostUse", 0.5f, 0.0f, 1.0f);
		def.retreatHealth = r.Num(p, "retreatHealth", 0.2f, 0.0f, 1.0f);
		def.mistakeRate = r.Num(p, "mistakeRate", 0.1f, 0.0f, 1.0f);
		def.focusPlayer = r.Num(p, "focusPlayer", 0.5f, 0.0f, 1.0f);
		aiProfiles_.push_back(std::move(def));
	}
	for (const char* required : {"rookie", "regular", "veteran", "elite", "boss"})
	{
		bool found = false;
		for (const AIProfileDef& p : aiProfiles_)
		{
			found = found || p.id == required;
		}
		if (!found)
		{
			onError(std::string("missing required ai profile '") + required + "'");
		}
	}
	return CheckUnique(aiProfiles_, onError, "ai profile");
}

namespace
{
ObjectiveDef ParseObjective(const FieldReader& r, const JsonValue& o)
{
	ObjectiveDef def;
	def.type = r.Enum(o, "type", ObjectiveType::Win);
	def.value = r.Num(o, "value", 0.0f, 0.0f, 1.0e7f);
	if (def.type == ObjectiveType::HitsOfKind)
	{
		def.kind = r.Enum(o, "kind", ImpactKind::TBone);
	}
	return def;
}

CampaignCarDef ParseCampaignCar(const FieldReader& r, const JsonValue& c)
{
	CampaignCarDef def;
	def.vehicle = r.Str(c, "vehicle");
	def.ai = c["ai"].AsString("regular");
	def.team = r.Int(c, "team", 1, 0, 16);
	def.name = r.OptStr(c, "name");
	def.boss = r.Bool(c, "boss", false);
	for (const auto& [key, value] : c["upgrades"].Members())
	{
		UpgradeCategory cat;
		if (!FromString(key, cat))
		{
			r.Fail("unknown upgrade category '" + key + "'");
			continue;
		}
		def.upgrades[static_cast<size_t>(cat)] = std::clamp(value.AsInt(0), 0, kMaxUpgradeLevel);
	}
	for (const auto& [key, value] : c["equipment"].Members())
	{
		EquipSlot slot;
		if (!FromString(key, slot))
		{
			r.Fail("unknown equipment slot '" + key + "'");
			continue;
		}
		def.equipment[static_cast<size_t>(slot)] = value.AsString();
	}
	return def;
}
} // namespace

bool GameDatabase::ParseCampaign(const std::string& file, const JsonValue& root)
{
	campaign_ = CampaignDef();
	auto onError = [this, &file](const std::string& m) { Error(file, m); };
	for (const JsonValue& rv : root["rivals"].Items())
	{
		FieldReader r(onError, "rival '" + rv["id"].AsString() + "'");
		RivalDef def;
		def.id = r.Id(rv);
		def.name = r.Str(rv, "name");
		def.title = r.Str(rv, "title", false);
		def.vehicle = r.Str(rv, "vehicle");
		def.color = r.Col(rv, "color", Color(255, 100, 0));
		campaign_.rivals.push_back(std::move(def));
	}
	for (const JsonValue& ch : root["chapters"].Items())
	{
		FieldReader r(onError, "chapter '" + ch["id"].AsString() + "'");
		ChapterDef chapter;
		chapter.id = r.Id(ch);
		chapter.name = r.Str(ch, "name");
		chapter.description = r.Str(ch, "description", false);
		chapter.hub = r.OptStr(ch, "hub");
		chapter.unlockStars = r.Int(ch, "unlockStars", 0, 0, 1000);
		chapter.boss = r.OptStr(ch, "boss");
		chapter.color = r.Col(ch, "color", Color(255, 120, 0));
		int index = 0;
		for (const JsonValue& lv : ch["levels"].Items())
		{
			FieldReader lr(onError, "level '" + lv["id"].AsString() + "'");
			LevelDef level;
			level.id = lr.Id(lv);
			level.chapter = chapter.id;
			level.indexInChapter = index++;
			level.name = lr.Str(lv, "name");
			level.description = lr.Str(lv, "description", false);
			level.arena = lr.Str(lv, "arena");
			level.mode = lr.Str(lv, "mode");
			level.timeLimit = lr.Num(lv, "timeLimit", 0.0f, 0.0f, 3600.0f);
			level.maxTier = lv["maxTier"].IsNull() ? 0 : lr.Int(lv, "maxTier", 0, 1, 5);
			level.loaner = lr.OptStr(lv, "loaner");
			for (const JsonValue& o : lv["opponents"].Items())
			{
				level.opponents.push_back(ParseCampaignCar(lr, o));
			}
			for (const JsonValue& a : lv["allies"].Items())
			{
				CampaignCarDef ally = ParseCampaignCar(lr, a);
				ally.team = 0;
				level.allies.push_back(std::move(ally));
			}
			level.objective = ParseObjective(lr, lv["objective"]);
			const JsonValue& stars = lv["stars"];
			if (stars.Size() != 3)
			{
				lr.Fail("must have exactly 3 stars");
			}
			for (size_t i = 0; i < 3 && i < stars.Size(); ++i)
			{
				level.stars[i] = ParseObjective(lr, stars[i]);
			}
			level.collectibles = lr.Int(lv, "collectibles", 0, 0, 10);
			const JsonValue& rewards = lv["rewards"];
			level.rewards.credits = static_cast<int64_t>(lr.Num(rewards, "credits", 0.0f, 0.0f, 1.0e8f));
			level.rewards.xp = static_cast<int64_t>(lr.Num(rewards, "xp", 0.0f, 0.0f, 1.0e7f));
			level.rewards.items = lr.Items(rewards["items"]);
			level.intro = lr.StrList(lv["story"], "intro");
			level.outro = lr.StrList(lv["story"], "outro");
			const JsonValue& unlock = lv["unlock"];
			level.unlockLevel = lr.OptStr(unlock, "level");
			level.unlockStars = lr.Int(unlock, "stars", 0, 0, 1000);
			chapter.levels.push_back(std::move(level));
		}
		if (chapter.levels.empty())
		{
			r.Fail("chapter has no levels");
		}
		campaign_.chapters.push_back(std::move(chapter));
	}
	CheckUnique(campaign_.rivals, onError, "rival");
	CheckUnique(campaign_.chapters, onError, "chapter");
	std::set<std::string> levelIds;
	for (const ChapterDef& ch : campaign_.chapters)
	{
		for (const LevelDef& l : ch.levels)
		{
			if (!levelIds.insert(l.id).second)
			{
				onError("duplicate level id '" + l.id + "'");
			}
		}
	}
	return true;
}

bool GameDatabase::ParseProgression(const std::string& file, const JsonValue& root)
{
	progression_ = ProgressionDef();
	auto onError = [this, &file](const std::string& m) { Error(file, m); };
	FieldReader r(onError, "progression");
	const JsonValue& player = root["player"];
	progression_.maxLevel = r.Int(player, "maxLevel", 100, 2, 1000, true);
	progression_.xpBase = r.Num(player, "xpBase", 1000.0f, 10.0f, 1.0e7f, true);
	progression_.xpGrowth = r.Num(player, "xpGrowth", 1.06f, 1.0f, 2.0f, true);
	for (const JsonValue& lr : root["levelRewards"].Items())
	{
		progression_.levelRewards.emplace_back(lr["level"].AsInt(0), r.Items(lr["items"]));
	}
	const JsonValue& m = root["match"];
	progression_.match.participationCredits = static_cast<int64_t>(r.Num(m, "participationCredits", 800.0f, 0.0f, 1.0e7f));
	progression_.match.participationXp = static_cast<int64_t>(r.Num(m, "participationXp", 150.0f, 0.0f, 1.0e7f));
	progression_.match.creditsPerScore = r.Num(m, "creditsPerScore", 1.5f, 0.0f, 100.0f);
	progression_.match.xpPerScore = r.Num(m, "xpPerScore", 0.8f, 0.0f, 100.0f);
	progression_.match.winMultiplier = r.Num(m, "winMultiplier", 1.5f, 1.0f, 10.0f);
	progression_.match.rankedMultiplier = r.Num(m, "rankedMultiplier", 1.25f, 1.0f, 10.0f);
	progression_.match.maxCredits = static_cast<int64_t>(r.Num(m, "maxCredits", 40000.0f, 0.0f, 1.0e8f));
	progression_.match.maxXp = static_cast<int64_t>(r.Num(m, "maxXp", 20000.0f, 0.0f, 1.0e8f));
	const JsonValue& s = root["score"];
	ScoreTable& t = progression_.score;
	for (int k = 0; k < kImpactKindCount; ++k)
	{
		const ImpactKind kind = static_cast<ImpactKind>(k);
		if (kind == ImpactKind::None)
		{
			continue;
		}
		const std::string key(ToString(kind));
		t.impact[static_cast<size_t>(k)] = r.Int(s, key.c_str(), 0, 0, 100000, true);
	}
	t.takedown = r.Int(s, "takedown", 1000, 0, 100000, true);
	t.assist = r.Int(s, "assist", 350, 0, 100000);
	t.ringOut = r.Int(s, "ring_out", 800, 0, 100000);
	t.revenge = r.Int(s, "revenge", 300, 0, 100000);
	t.multiTakedown = r.Int(s, "multi_takedown", 500, 0, 100000);
	t.airtimePerSec = r.Int(s, "airtime_per_sec", 40, 0, 10000);
	t.hillPerSec = r.Int(s, "hill_per_sec", 20, 0, 10000);
	t.survivalPerSec = r.Int(s, "survival_per_sec", 5, 0, 10000);
	t.propDestroyed = r.Int(s, "prop_destroyed", 50, 0, 10000);
	t.comboStep = r.Num(s, "combo_step", 0.2f, 0.0f, 2.0f);
	t.comboMax = r.Num(s, "combo_max", 2.0f, 1.0f, 10.0f);
	t.comboWindow = r.Num(s, "combo_window", 4.0f, 0.5f, 30.0f);
	t.minDamageForPoints = r.Num(s, "min_damage_for_points", 25.0f, 0.0f, 10000.0f);
	int lastRating = -1;
	for (const JsonValue& rk : root["ranks"].Items())
	{
		RankDef def;
		def.id = r.Id(rk);
		def.name = r.Str(rk, "name");
		def.minRating = r.Int(rk, "minRating", 0, 0, 10000);
		def.divisions = r.Int(rk, "divisions", 1, 1, 5);
		def.color = r.Col(rk, "color", Color(200, 200, 200));
		if (def.minRating <= lastRating)
		{
			r.Fail("ranks must be sorted by minRating ascending");
		}
		lastRating = def.minRating;
		progression_.ranks.push_back(std::move(def));
	}
	if (progression_.ranks.empty())
	{
		r.Fail("no ranks defined");
	}
	return true;
}

bool GameDatabase::ParseGraphics(const std::string& file, const JsonValue& root)
{
	graphics_ = GraphicsDef();
	auto onError = [this, &file](const std::string& m) { Error(file, m); };
	for (const JsonValue& o : root["options"].Items())
	{
		FieldReader r(onError, "graphics option '" + o["id"].AsString() + "'");
		GraphicsOptionDef def;
		def.id = r.Id(o);
		def.name = r.Str(o, "name");
		def.category = o["category"].AsString("general");
		def.scalabilityGroup = o["scalability"].AsString("");
		def.defaultValue = r.Str(o, "default");
		def.requiresCvar = o["requiresCvar"].AsString("");
		def.restart = r.Bool(o, "restart", false);
		for (const JsonValue& v : o["values"].Items())
		{
			GraphicsValueDef value;
			value.id = r.Id(v);
			value.name = r.Str(v, "name");
			value.cvars = r.Cvars(v["cvars"]);
			value.post = r.FloatMap(v["post"]);
			value.game = r.FloatMap(v["game"]);
			value.scalability = r.Int(v, "level", -1, -1, 4);
			if (!def.scalabilityGroup.empty() && value.scalability < 0)
			{
				r.Fail("value '" + value.id + "' needs a scalability level");
			}
			def.values.push_back(std::move(value));
		}
		if (def.values.empty() || !def.FindValue(def.defaultValue))
		{
			r.Fail("needs values and a valid default");
		}
		graphics_.options.push_back(std::move(def));
	}
	for (const JsonValue& u : root["upscalers"].Items())
	{
		FieldReader r(onError, "upscaler '" + u["id"].AsString() + "'");
		UpscalerDef def;
		def.id = r.Id(u);
		def.name = r.Str(u, "name");
		def.vendor = u["vendor"].AsString("");
		def.detectCvar = u["detectCvar"].AsString("");
		def.enable = r.Cvars(u["enable"]);
		def.disable = r.Cvars(u["disable"]);
		for (const JsonValue& m : u["modes"].Items())
		{
			UpscalerModeDef mode;
			mode.id = r.Id(m);
			mode.name = r.Str(m, "name");
			mode.cvars = r.Cvars(m["cvars"]);
			mode.screenPercentage = r.Num(m, "screenPercentage", -1.0f, -1.0f, 200.0f);
			def.modes.push_back(std::move(mode));
		}
		const JsonValue& fg = u["frameGen"];
		if (fg.IsObject())
		{
			def.frameGenCvar = fg["detectCvar"].AsString("");
			def.frameGenOn = r.Cvars(fg["on"]);
			def.frameGenOff = r.Cvars(fg["off"]);
		}
		graphics_.upscalers.push_back(std::move(def));
	}
	for (const JsonValue& p : root["presets"].Items())
	{
		FieldReader r(onError, "graphics preset '" + p["id"].AsString() + "'");
		GraphicsPresetDef def;
		def.id = r.Id(p);
		def.name = r.Str(p, "name");
		def.upscaler = p["upscaler"].AsString("tsr");
		def.upscalerMode = p["upscalerMode"].AsString("quality");
		for (const auto& [key, value] : p["values"].Members())
		{
			def.values.emplace_back(key, value.AsString());
		}
		graphics_.presets.push_back(std::move(def));
	}
	const JsonValue& ll = root["lowLatency"];
	if (ll.IsObject())
	{
		FieldReader r(onError, "lowLatency");
		graphics_.lowLatencyCvar = ll["detectCvar"].AsString("");
		graphics_.lowLatencyOn = r.Cvars(ll["on"]);
		graphics_.lowLatencyOff = r.Cvars(ll["off"]);
	}
	// Presets must reference real options/values.
	for (const GraphicsPresetDef& p : graphics_.presets)
	{
		for (const auto& [optionId, valueId] : p.values)
		{
			const GraphicsOptionDef* opt = graphics_.FindOption(optionId);
			if (!opt || !opt->FindValue(valueId))
			{
				onError("preset '" + p.id + "' references unknown option/value " + optionId + "=" + valueId);
			}
		}
		if (!graphics_.FindUpscaler(p.upscaler))
		{
			onError("preset '" + p.id + "' references unknown upscaler '" + p.upscaler + "'");
		}
	}
	return true;
}

// ---------------------------------------------------------------------------------------------
// Cross references

bool GameDatabase::Validate()
{
	RebuildIndices();
	const size_t before = messages_.size();
	auto err = [this](const std::string& file, const std::string& m) { Error(file, m); };

	for (const VehicleDef& v : vehicles_)
	{
		if (!FindBrand(v.brand))
		{
			err("vehicles.json", "vehicle '" + v.id + "' references unknown brand '" + v.brand + "'");
		}
	}
	if (!vehicles_.empty() && StarterVehicles().empty())
	{
		err("vehicles.json", "at least one vehicle must be a starter");
	}

	auto checkItem = [&](const std::string& file, const std::string& where, const ItemRef& item)
	{
		if (!ItemExists(item))
		{
			err(file, where + " references unknown " + std::string(ToString(item.type)) + " '" + item.id + "'");
		}
	};

	for (const auto& [level, items] : progression_.levelRewards)
	{
		for (const ItemRef& item : items)
		{
			checkItem("progression.json", "levelRewards[" + std::to_string(level) + "]", item);
		}
	}

	auto checkCar = [&](const LevelDef& level, const CampaignCarDef& car)
	{
		const VehicleDef* v = FindVehicle(car.vehicle);
		if (!v)
		{
			err("campaign.json", "level '" + level.id + "' uses unknown vehicle '" + car.vehicle + "'");
			return;
		}
		if (!FindAIProfile(car.ai))
		{
			err("campaign.json", "level '" + level.id + "' uses unknown ai '" + car.ai + "'");
		}
		for (int s = 0; s < kEquipSlotCount; ++s)
		{
			const std::string& itemId = car.equipment[static_cast<size_t>(s)];
			if (itemId.empty())
			{
				continue;
			}
			const EquipmentDef* e = FindEquipment(itemId);
			if (!e)
			{
				err("campaign.json", "level '" + level.id + "' uses unknown equipment '" + itemId + "'");
			}
			else if (static_cast<int>(e->slot) != s)
			{
				err("campaign.json", "level '" + level.id + "' puts '" + itemId + "' in the wrong slot");
			}
		}
	};

	for (const RivalDef& rival : campaign_.rivals)
	{
		if (!FindVehicle(rival.vehicle))
		{
			err("campaign.json", "rival '" + rival.id + "' uses unknown vehicle '" + rival.vehicle + "'");
		}
	}
	for (const ChapterDef& ch : campaign_.chapters)
	{
		if (!ch.hub.empty())
		{
			const ArenaDef* hub = FindArena(ch.hub);
			if (!hub)
			{
				err("campaign.json", "chapter '" + ch.id + "' hub '" + ch.hub + "' is not an arena");
			}
		}
		if (!ch.boss.empty() && !FindRival(ch.boss))
		{
			err("campaign.json", "chapter '" + ch.id + "' boss '" + ch.boss + "' is not a rival");
		}
		for (const LevelDef& l : ch.levels)
		{
			const ArenaDef* arena = FindArena(l.arena);
			if (!arena)
			{
				err("campaign.json", "level '" + l.id + "' uses unknown arena '" + l.arena + "'");
			}
			else if (l.collectibles > static_cast<int>(arena->collectibles.size()))
			{
				err("campaign.json", "level '" + l.id + "' wants more collectibles than arena '" + l.arena + "' has");
			}
			if (!FindMode(l.mode))
			{
				err("campaign.json", "level '" + l.id + "' uses unknown mode '" + l.mode + "'");
			}
			if (!l.loaner.empty() && !FindVehicle(l.loaner))
			{
				err("campaign.json", "level '" + l.id + "' loaner '" + l.loaner + "' is not a vehicle");
			}
			if (l.opponents.empty() && l.mode != "explore" && l.mode != "destruction")
			{
				Warn("campaign.json", "level '" + l.id + "' has no opponents");
			}
			for (const CampaignCarDef& c : l.opponents)
			{
				checkCar(l, c);
			}
			for (const CampaignCarDef& c : l.allies)
			{
				checkCar(l, c);
			}
			for (const ItemRef& item : l.rewards.items)
			{
				checkItem("campaign.json", "level '" + l.id + "' reward", item);
			}
			if (!l.unlockLevel.empty() && !FindLevel(l.unlockLevel))
			{
				err("campaign.json", "level '" + l.id + "' unlock level '" + l.unlockLevel + "' does not exist");
			}
		}
	}
	return messages_.size() == before;
}

// ---------------------------------------------------------------------------------------------
// Lookups

namespace
{
template <typename T>
const T* FindIn(const std::vector<T>& items, const std::unordered_map<std::string, size_t>& index, std::string_view id)
{
	const auto it = index.find(std::string(id));
	return it == index.end() ? nullptr : &items[it->second];
}
} // namespace

const BrandDef* GameDatabase::FindBrand(std::string_view id) const
{
	return FindIn(brands_, brandIndex_, id);
}
const VehicleDef* GameDatabase::FindVehicle(std::string_view id) const
{
	return FindIn(vehicles_, vehicleIndex_, id);
}
const EquipmentDef* GameDatabase::FindEquipment(std::string_view id) const
{
	return FindIn(equipment_, equipmentIndex_, id);
}
const CosmeticDef* GameDatabase::FindCosmetic(std::string_view id) const
{
	return FindIn(cosmetics_, cosmeticIndex_, id);
}
const ArenaDef* GameDatabase::FindArena(std::string_view id) const
{
	return FindIn(arenas_, arenaIndex_, id);
}
const ModeDef* GameDatabase::FindMode(std::string_view id) const
{
	return FindIn(modes_, modeIndex_, id);
}
const AIProfileDef* GameDatabase::FindAIProfile(std::string_view id) const
{
	return FindIn(aiProfiles_, aiIndex_, id);
}

const ChapterDef* GameDatabase::FindChapter(std::string_view id) const
{
	for (const ChapterDef& ch : campaign_.chapters)
	{
		if (ch.id == id)
		{
			return &ch;
		}
	}
	return nullptr;
}

const LevelDef* GameDatabase::FindLevel(std::string_view id) const
{
	const auto it = levelIndex_.find(std::string(id));
	if (it == levelIndex_.end())
	{
		return nullptr;
	}
	return &campaign_.chapters[it->second.first].levels[it->second.second];
}

const RivalDef* GameDatabase::FindRival(std::string_view id) const
{
	for (const RivalDef& r : campaign_.rivals)
	{
		if (r.id == id)
		{
			return &r;
		}
	}
	return nullptr;
}

int64_t GameDatabase::UpgradeCost(const VehicleDef& vehicle, UpgradeCategory category, int level) const
{
	const UpgradeCategoryDef& def = Upgrade(category);
	if (level < 1 || level > static_cast<int>(def.levels.size()))
	{
		return -1;
	}
	const int tierIndex = std::clamp(vehicle.tier, 1, 5) - 1;
	return RoundTo100(static_cast<double>(def.levels[static_cast<size_t>(level - 1)].cost) *
	                  static_cast<double>(upgrades_.tierCostMultiplier[static_cast<size_t>(tierIndex)]));
}

std::optional<Price> GameDatabase::ItemPrice(const ItemRef& item) const
{
	switch (item.type)
	{
	case ItemType::Vehicle:
		if (const VehicleDef* v = FindVehicle(item.id))
		{
			return v->price;
		}
		break;
	case ItemType::Equipment:
		if (const EquipmentDef* e = FindEquipment(item.id))
		{
			return e->price;
		}
		break;
	case ItemType::Currency:
		return std::nullopt;
	default:
		if (const CosmeticDef* c = FindCosmetic(item.id))
		{
			if (c->type == item.type)
			{
				return c->price;
			}
		}
		break;
	}
	return std::nullopt;
}

bool GameDatabase::ItemExists(const ItemRef& item) const
{
	if (item.type == ItemType::Currency)
	{
		return (item.id == "credits" || item.id == "gold") && item.amount > 0;
	}
	return ItemPrice(item).has_value();
}

std::vector<const VehicleDef*> GameDatabase::StarterVehicles() const
{
	std::vector<const VehicleDef*> out;
	for (const VehicleDef& v : vehicles_)
	{
		if (v.starter)
		{
			out.push_back(&v);
		}
	}
	return out;
}

std::vector<std::string> GameDatabase::ReferencedLocKeys() const
{
	std::vector<std::string> keys;
	auto add = [&keys](const std::string& k)
	{
		if (!k.empty())
		{
			keys.push_back(k);
		}
	};
	for (const BrandDef& b : brands_)
	{
		add(b.name);
	}
	for (const VehicleDef& v : vehicles_)
	{
		add(v.name);
	}
	for (const UpgradeCategoryDef& u : upgrades_.categories)
	{
		add(u.name);
		add(u.description);
	}
	for (const EquipmentDef& e : equipment_)
	{
		add(e.name);
		add(e.description);
	}
	for (const CosmeticDef& c : cosmetics_)
	{
		add(c.name);
	}
	for (const ArenaDef& a : arenas_)
	{
		add(a.name);
		add(a.description);
	}
	for (const ModeDef& m : modes_)
	{
		add(m.name);
		add(m.description);
	}
	for (const AIProfileDef& p : aiProfiles_)
	{
		add(p.name);
	}
	for (const RivalDef& r : campaign_.rivals)
	{
		add(r.name);
		add(r.title);
	}
	for (const ChapterDef& ch : campaign_.chapters)
	{
		add(ch.name);
		add(ch.description);
		for (const LevelDef& l : ch.levels)
		{
			add(l.name);
			add(l.description);
			for (const std::string& k : l.intro)
			{
				add(k);
			}
			for (const std::string& k : l.outro)
			{
				add(k);
			}
			for (const CampaignCarDef& c : l.opponents)
			{
				add(c.name);
			}
		}
	}
	for (const RankDef& r : progression_.ranks)
	{
		add(r.name);
	}
	for (const GraphicsOptionDef& o : graphics_.options)
	{
		add(o.name);
		for (const GraphicsValueDef& v : o.values)
		{
			add(v.name);
		}
	}
	for (const UpscalerDef& u : graphics_.upscalers)
	{
		add(u.name);
		for (const UpscalerModeDef& m : u.modes)
		{
			add(m.name);
		}
	}
	for (const GraphicsPresetDef& p : graphics_.presets)
	{
		add(p.name);
	}
	std::sort(keys.begin(), keys.end());
	keys.erase(std::unique(keys.begin(), keys.end()), keys.end());
	return keys;
}

} // namespace crash
