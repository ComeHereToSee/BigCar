// Copyright BigCar Team. All Rights Reserved.
//
// User settings (graphics / gameplay / audio) and the graphics resolver that turns them into console variables,
// scalability levels and post-process overrides using the data in graphics.json. Upscalers (TSR, DLSS, FSR, XeSS)
// are described in data so plugin cvar names can change without code changes.
#pragma once

#include "CrashCore/Database.h"

#include <functional>
#include <map>
#include <string>
#include <vector>

namespace crash
{
struct GraphicsSettings
{
	std::string preset = "high";                  // a preset id, or "custom"
	std::map<std::string, std::string> overrides; // option id -> value id (applied on top of the preset)
	std::string upscaler = "tsr";
	std::string upscalerMode = "quality";
	bool frameGeneration = false;
	bool lowLatency = true;
	float resolutionScale = 100.0f; // used when the upscaler mode does not set its own screen percentage
	int resolutionX = 0;            // 0 = desktop resolution
	int resolutionY = 0;
	std::string windowMode = "fullscreen"; // fullscreen, windowed_fullscreen, windowed
	bool vsync = false;
	int frameLimit = 0; // 0 = unlimited
	bool hdr = false;
	float brightness = 1.0f;

	JsonValue ToJson() const;
	static GraphicsSettings FromJson(const JsonValue& json);
};

struct GameplaySettings
{
	float fov = 90.0f;
	float cameraShake = 1.0f;
	bool crashCam = true;   // cinematic camera on big hits / takedowns
	bool slowMotion = true; // single player only
	std::string speedUnit = "kmh";
	std::string language = "zh-Hans";
	float steeringAssist = 0.6f;
	bool invertAirPitch = false;
	bool damageNumbers = true;
	float hudScale = 1.0f;
	bool vibration = true;

	JsonValue ToJson() const;
	static GameplaySettings FromJson(const JsonValue& json);
};

struct AudioSettings
{
	float master = 1.0f;
	float music = 0.7f;
	float effects = 1.0f;
	float engine = 0.9f;
	float ui = 0.8f;

	JsonValue ToJson() const;
	static AudioSettings FromJson(const JsonValue& json);
};

struct UserSettings
{
	GraphicsSettings graphics;
	GameplaySettings gameplay;
	AudioSettings audio;

	JsonValue ToJson() const;
	static UserSettings FromJson(const JsonValue& json);
};

using CvarExists = std::function<bool(const std::string& name)>;

struct ResolvedGraphics
{
	CvarList cvars;                                       // in application order
	std::vector<std::pair<std::string, int>> scalability; // group -> level (0..4)
	std::vector<std::pair<std::string, float>> post;
	std::vector<std::pair<std::string, float>> game;
	std::vector<std::string> restartOptions; // options whose change needs a restart
	std::string activeUpscaler;
	std::string activeUpscalerMode;
	std::vector<std::string> notes;
};

// Value of an option after preset + overrides (falls back to the option default).
std::string EffectiveGraphicsValue(const GraphicsDef& def, const GraphicsSettings& settings, const std::string& optionId);
// Switches to a preset and clears overrides.
void ApplyGraphicsPreset(const GraphicsDef& def, GraphicsSettings& settings, const std::string& presetId);
std::vector<const UpscalerDef*> AvailableUpscalers(const GraphicsDef& def, const CvarExists& hasCvar);
ResolvedGraphics ResolveGraphics(const GraphicsDef& def, const GraphicsSettings& settings, const CvarExists& hasCvar);

} // namespace crash
