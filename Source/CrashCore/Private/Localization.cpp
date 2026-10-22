// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Localization.h"

#include "CrashCore/Json.h"

namespace crash
{
void Localization::Clear()
{
	tables_.clear();
}

bool Localization::AddFile(const std::string& lang, std::string_view jsonText, std::string* error)
{
	JsonValue root;
	JsonError parseError;
	if (!ParseJson(jsonText, root, &parseError))
	{
		if (error)
		{
			*error = parseError.ToString();
		}
		return false;
	}
	const JsonValue& strings = root["strings"];
	if (!strings.IsObject())
	{
		if (error)
		{
			*error = "missing \"strings\" object";
		}
		return false;
	}
	auto& table = tables_[lang];
	bool ok = true;
	for (const auto& [key, value] : strings.Members())
	{
		if (!value.IsString())
		{
			ok = false;
			if (error)
			{
				*error = "value of '" + key + "' is not a string";
			}
			continue;
		}
		if (!table.emplace(key, value.AsString()).second)
		{
			ok = false;
			if (error)
			{
				*error = "duplicate key '" + key + "'";
			}
		}
	}
	return ok;
}

std::vector<std::string> Localization::Languages() const
{
	std::vector<std::string> out;
	for (const auto& [lang, table] : tables_)
	{
		out.push_back(lang);
	}
	return out;
}

bool Localization::HasIn(const std::string& lang, const std::string& key) const
{
	const auto t = tables_.find(lang);
	return t != tables_.end() && t->second.count(key) > 0;
}

bool Localization::Has(const std::string& key) const
{
	return HasIn(language_, key) || HasIn(kPrimaryLanguage, key);
}

std::string Localization::Get(const std::string& key) const
{
	for (const std::string& lang : {language_, std::string(kPrimaryLanguage)})
	{
		const auto t = tables_.find(lang);
		if (t == tables_.end())
		{
			continue;
		}
		const auto it = t->second.find(key);
		if (it != t->second.end())
		{
			return it->second;
		}
	}
	return key;
}

std::string Localization::FormatText(const std::string& pattern, std::initializer_list<std::string> args)
{
	std::string out;
	out.reserve(pattern.size() + 16);
	for (size_t i = 0; i < pattern.size(); ++i)
	{
		if (pattern[i] == '{' && i + 2 < pattern.size() && pattern[i + 2] == '}' && pattern[i + 1] >= '0' && pattern[i + 1] <= '9')
		{
			const size_t index = static_cast<size_t>(pattern[i + 1] - '0');
			if (index < args.size())
			{
				out += *(args.begin() + index);
				i += 2;
				continue;
			}
		}
		out.push_back(pattern[i]);
	}
	return out;
}

std::string Localization::Format(const std::string& key, std::initializer_list<std::string> args) const
{
	return FormatText(Get(key), args);
}

std::vector<std::string> Localization::MissingKeys(const std::string& lang, const std::vector<std::string>& keys) const
{
	std::vector<std::string> missing;
	for (const std::string& k : keys)
	{
		if (!HasIn(lang, k))
		{
			missing.push_back(k);
		}
	}
	return missing;
}

size_t Localization::Count(const std::string& lang) const
{
	const auto t = tables_.find(lang);
	return t == tables_.end() ? 0 : t->second.size();
}

std::pair<std::string, std::string> Localization::SplitDialogue(const std::string& text)
{
	const size_t bar = text.find('|');
	if (bar == std::string::npos)
	{
		return {std::string(), text};
	}
	return {text.substr(0, bar), text.substr(bar + 1)};
}

} // namespace crash
