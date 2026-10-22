// Copyright BigCar Team. All Rights Reserved.
//
// Data-driven string tables (Content/BigCar/Data/Localization/<lang>/*.json). zh-Hans is the primary language and
// the fallback for every other language.
#pragma once

#include "CrashCore/Export.h"

#include <initializer_list>
#include <map>
#include <string>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace crash
{
class Localization
{
public:
	static constexpr const char* kPrimaryLanguage = "zh-Hans";

	CRASHCORE_EXPORT void Clear();
	// Merges one string-table file into `lang`. Returns false on parse errors or duplicate keys.
	CRASHCORE_EXPORT bool AddFile(const std::string& lang, std::string_view jsonText, std::string* error = nullptr);
	void SetLanguage(const std::string& lang) { language_ = lang; }
	const std::string& Language() const { return language_; }
	CRASHCORE_EXPORT std::vector<std::string> Languages() const;

	CRASHCORE_EXPORT bool Has(const std::string& key) const;
	CRASHCORE_EXPORT bool HasIn(const std::string& lang, const std::string& key) const;
	// Current language → zh-Hans → the key itself.
	CRASHCORE_EXPORT std::string Get(const std::string& key) const;
	// Replaces {0}, {1}, … with the given arguments.
	CRASHCORE_EXPORT std::string Format(const std::string& key, std::initializer_list<std::string> args) const;
	static CRASHCORE_EXPORT std::string FormatText(const std::string& pattern, std::initializer_list<std::string> args);

	CRASHCORE_EXPORT std::vector<std::string> MissingKeys(const std::string& lang, const std::vector<std::string>& keys) const;
	CRASHCORE_EXPORT size_t Count(const std::string& lang) const;

	// Dialogue lines are "Speaker|Line". Returns {speaker, line}; speaker may be a loc key.
	static CRASHCORE_EXPORT std::pair<std::string, std::string> SplitDialogue(const std::string& text);

private:
	std::map<std::string, std::unordered_map<std::string, std::string>> tables_;
	std::string language_ = kPrimaryLanguage;
};

} // namespace crash
