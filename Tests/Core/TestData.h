// Copyright BigCar Team. All Rights Reserved.
#pragma once

#include "CrashCore/Database.h"
#include "CrashCore/Localization.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

namespace bigcar_test
{
inline bool ReadFile(const std::string& path, std::string& out)
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

inline crash::GameDatabase::FileReader DirReader(const std::string& dir)
{
	return [dir](const std::string& name, std::string& text) { return ReadFile(dir + "/" + name, text); };
}

// Small, stable data set used by unit tests (Tests/Core/fixtures).
inline const crash::GameDatabase& FixtureDb()
{
	static crash::GameDatabase db;
	static bool loaded = false;
	if (!loaded)
	{
		loaded = true;
		if (!db.LoadAll(DirReader(BIGCAR_FIXTURE_DIR)))
		{
			std::fprintf(stderr, "Fixture data failed to load:\n%s", db.FormatMessages().c_str());
		}
	}
	return db;
}

inline bool RealDataAvailable()
{
	return std::filesystem::exists(std::string(BIGCAR_DATA_DIR) + "/vehicles.json");
}

// Loads every Localization/<lang>/*.json under a data dir.
inline bool LoadLocalization(const std::string& dataDir, crash::Localization& loc, std::string& error)
{
	const std::filesystem::path root = std::filesystem::path(dataDir) / "Localization";
	if (!std::filesystem::exists(root))
	{
		error = "no Localization directory";
		return false;
	}
	bool ok = true;
	for (const auto& langDir : std::filesystem::directory_iterator(root))
	{
		if (!langDir.is_directory())
		{
			continue;
		}
		const std::string lang = langDir.path().filename().string();
		for (const auto& file : std::filesystem::directory_iterator(langDir.path()))
		{
			if (file.path().extension() != ".json")
			{
				continue;
			}
			std::string text;
			std::string fileError;
			if (!ReadFile(file.path().string(), text) || !loc.AddFile(lang, text, &fileError))
			{
				error += file.path().string() + ": " + fileError + "\n";
				ok = false;
			}
		}
	}
	return ok;
}
} // namespace bigcar_test
