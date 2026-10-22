// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Settings.h"

#include <algorithm>
#include <cstdio>

namespace crash
{
namespace
{
std::string FormatFloat(float v)
{
	char buffer[32];
	std::snprintf(buffer, sizeof(buffer), "%.2f", static_cast<double>(v));
	for (char* p = buffer; *p; ++p)
	{
		if (*p == ',')
		{
			*p = '.';
		}
	}
	return buffer;
}

void Append(CvarList& out, const CvarList& in)
{
	out.insert(out.end(), in.begin(), in.end());
}
} // namespace

JsonValue GraphicsSettings::ToJson() const
{
	JsonValue v = JsonValue::MakeObject();
	v.Set("preset", JsonValue(preset));
	JsonValue o = JsonValue::MakeObject();
	for (const auto& [key, value] : overrides)
	{
		o.Set(key, JsonValue(value));
	}
	v.Set("overrides", std::move(o));
	v.Set("upscaler", JsonValue(upscaler));
	v.Set("upscalerMode", JsonValue(upscalerMode));
	v.Set("frameGeneration", JsonValue(frameGeneration));
	v.Set("lowLatency", JsonValue(lowLatency));
	v.Set("resolutionScale", JsonValue(resolutionScale));
	v.Set("resolutionX", JsonValue(resolutionX));
	v.Set("resolutionY", JsonValue(resolutionY));
	v.Set("windowMode", JsonValue(windowMode));
	v.Set("vsync", JsonValue(vsync));
	v.Set("frameLimit", JsonValue(frameLimit));
	v.Set("hdr", JsonValue(hdr));
	v.Set("brightness", JsonValue(brightness));
	return v;
}

GraphicsSettings GraphicsSettings::FromJson(const JsonValue& json)
{
	GraphicsSettings s;
	s.preset = json["preset"].AsString(s.preset);
	for (const auto& [key, value] : json["overrides"].Members())
	{
		if (value.IsString())
		{
			s.overrides[key] = value.AsString();
		}
	}
	s.upscaler = json["upscaler"].AsString(s.upscaler);
	s.upscalerMode = json["upscalerMode"].AsString(s.upscalerMode);
	s.frameGeneration = json["frameGeneration"].AsBool(s.frameGeneration);
	s.lowLatency = json["lowLatency"].AsBool(s.lowLatency);
	s.resolutionScale = Clamp(json["resolutionScale"].AsFloat(s.resolutionScale), 30.0f, 200.0f);
	s.resolutionX = std::max(0, json["resolutionX"].AsInt(0));
	s.resolutionY = std::max(0, json["resolutionY"].AsInt(0));
	s.windowMode = json["windowMode"].AsString(s.windowMode);
	s.vsync = json["vsync"].AsBool(s.vsync);
	s.frameLimit = std::clamp(json["frameLimit"].AsInt(0), 0, 1000);
	s.hdr = json["hdr"].AsBool(s.hdr);
	s.brightness = Clamp(json["brightness"].AsFloat(s.brightness), 0.5f, 1.5f);
	return s;
}

JsonValue GameplaySettings::ToJson() const
{
	JsonValue v = JsonValue::MakeObject();
	v.Set("fov", JsonValue(fov));
	v.Set("cameraShake", JsonValue(cameraShake));
	v.Set("crashCam", JsonValue(crashCam));
	v.Set("slowMotion", JsonValue(slowMotion));
	v.Set("speedUnit", JsonValue(speedUnit));
	v.Set("language", JsonValue(language));
	v.Set("steeringAssist", JsonValue(steeringAssist));
	v.Set("invertAirPitch", JsonValue(invertAirPitch));
	v.Set("damageNumbers", JsonValue(damageNumbers));
	v.Set("hudScale", JsonValue(hudScale));
	v.Set("vibration", JsonValue(vibration));
	return v;
}

GameplaySettings GameplaySettings::FromJson(const JsonValue& json)
{
	GameplaySettings s;
	s.fov = Clamp(json["fov"].AsFloat(s.fov), 60.0f, 120.0f);
	s.cameraShake = Clamp(json["cameraShake"].AsFloat(s.cameraShake), 0.0f, 2.0f);
	s.crashCam = json["crashCam"].AsBool(s.crashCam);
	s.slowMotion = json["slowMotion"].AsBool(s.slowMotion);
	s.speedUnit = json["speedUnit"].AsString(s.speedUnit) == "mph" ? "mph" : "kmh";
	s.language = json["language"].AsString(s.language);
	s.steeringAssist = Clamp(json["steeringAssist"].AsFloat(s.steeringAssist), 0.0f, 1.0f);
	s.invertAirPitch = json["invertAirPitch"].AsBool(s.invertAirPitch);
	s.damageNumbers = json["damageNumbers"].AsBool(s.damageNumbers);
	s.hudScale = Clamp(json["hudScale"].AsFloat(s.hudScale), 0.6f, 1.5f);
	s.vibration = json["vibration"].AsBool(s.vibration);
	return s;
}

JsonValue AudioSettings::ToJson() const
{
	JsonValue v = JsonValue::MakeObject();
	v.Set("master", JsonValue(master));
	v.Set("music", JsonValue(music));
	v.Set("effects", JsonValue(effects));
	v.Set("engine", JsonValue(engine));
	v.Set("ui", JsonValue(ui));
	return v;
}

AudioSettings AudioSettings::FromJson(const JsonValue& json)
{
	AudioSettings s;
	s.master = Clamp01(json["master"].AsFloat(s.master));
	s.music = Clamp01(json["music"].AsFloat(s.music));
	s.effects = Clamp01(json["effects"].AsFloat(s.effects));
	s.engine = Clamp01(json["engine"].AsFloat(s.engine));
	s.ui = Clamp01(json["ui"].AsFloat(s.ui));
	return s;
}

JsonValue UserSettings::ToJson() const
{
	JsonValue v = JsonValue::MakeObject();
	v.Set("version", JsonValue(1));
	v.Set("graphics", graphics.ToJson());
	v.Set("gameplay", gameplay.ToJson());
	v.Set("audio", audio.ToJson());
	return v;
}

UserSettings UserSettings::FromJson(const JsonValue& json)
{
	UserSettings s;
	s.graphics = GraphicsSettings::FromJson(json["graphics"]);
	s.gameplay = GameplaySettings::FromJson(json["gameplay"]);
	s.audio = AudioSettings::FromJson(json["audio"]);
	return s;
}

std::string EffectiveGraphicsValue(const GraphicsDef& def, const GraphicsSettings& settings, const std::string& optionId)
{
	const GraphicsOptionDef* option = def.FindOption(optionId);
	if (!option)
	{
		return std::string();
	}
	const auto it = settings.overrides.find(optionId);
	if (it != settings.overrides.end() && option->FindValue(it->second))
	{
		return it->second;
	}
	if (const GraphicsPresetDef* preset = def.FindPreset(settings.preset))
	{
		for (const auto& [id, value] : preset->values)
		{
			if (id == optionId && option->FindValue(value))
			{
				return value;
			}
		}
	}
	return option->defaultValue;
}

void ApplyGraphicsPreset(const GraphicsDef& def, GraphicsSettings& settings, const std::string& presetId)
{
	const GraphicsPresetDef* preset = def.FindPreset(presetId);
	if (!preset)
	{
		return;
	}
	settings.preset = presetId;
	settings.overrides.clear();
	settings.upscaler = preset->upscaler;
	settings.upscalerMode = preset->upscalerMode;
}

std::vector<const UpscalerDef*> AvailableUpscalers(const GraphicsDef& def, const CvarExists& hasCvar)
{
	std::vector<const UpscalerDef*> out;
	for (const UpscalerDef& u : def.upscalers)
	{
		if (u.detectCvar.empty() || (hasCvar && hasCvar(u.detectCvar)))
		{
			out.push_back(&u);
		}
	}
	return out;
}

ResolvedGraphics ResolveGraphics(const GraphicsDef& def, const GraphicsSettings& settings, const CvarExists& hasCvar)
{
	ResolvedGraphics r;
	auto exists = [&hasCvar](const std::string& name) { return name.empty() || (hasCvar && hasCvar(name)); };

	// ---- Options.
	for (const GraphicsOptionDef& option : def.options)
	{
		if (!option.requiresCvar.empty() && !exists(option.requiresCvar))
		{
			continue;
		}
		const GraphicsValueDef* value = option.FindValue(EffectiveGraphicsValue(def, settings, option.id));
		if (!value)
		{
			continue;
		}
		if (!option.scalabilityGroup.empty() && value->scalability >= 0)
		{
			r.scalability.emplace_back(option.scalabilityGroup, value->scalability);
		}
		Append(r.cvars, value->cvars);
		r.post.insert(r.post.end(), value->post.begin(), value->post.end());
		r.game.insert(r.game.end(), value->game.begin(), value->game.end());
		if (option.restart)
		{
			r.restartOptions.push_back(option.id);
		}
	}

	// ---- Upscaler: disable every other available upscaler first, then enable the chosen one.
	const std::vector<const UpscalerDef*> available = AvailableUpscalers(def, hasCvar);
	const UpscalerDef* chosen = nullptr;
	for (const UpscalerDef* u : available)
	{
		if (u->id == settings.upscaler)
		{
			chosen = u;
		}
	}
	if (!chosen)
	{
		r.notes.push_back("upscaler '" + settings.upscaler + "' unavailable, falling back");
		for (const char* fallback : {"tsr", "off"})
		{
			for (const UpscalerDef* u : available)
			{
				if (!chosen && u->id == fallback)
				{
					chosen = u;
				}
			}
		}
		if (!chosen && !available.empty())
		{
			chosen = available.front();
		}
	}
	for (const UpscalerDef* u : available)
	{
		if (u == chosen)
		{
			continue;
		}
		Append(r.cvars, u->disable);
		if (!u->frameGenCvar.empty() && exists(u->frameGenCvar))
		{
			Append(r.cvars, u->frameGenOff);
		}
	}
	if (chosen)
	{
		r.activeUpscaler = chosen->id;
		Append(r.cvars, chosen->enable);
		const UpscalerModeDef* mode = chosen->FindMode(settings.upscalerMode);
		if (!mode && !chosen->modes.empty())
		{
			mode = &chosen->modes.front();
		}
		float screenPercentage = settings.resolutionScale;
		if (mode)
		{
			r.activeUpscalerMode = mode->id;
			Append(r.cvars, mode->cvars);
			if (mode->screenPercentage > 0.0f)
			{
				screenPercentage = mode->screenPercentage;
			}
		}
		r.cvars.emplace_back("r.ScreenPercentage", FormatFloat(screenPercentage));
		if (!chosen->frameGenCvar.empty() && exists(chosen->frameGenCvar))
		{
			Append(r.cvars, settings.frameGeneration ? chosen->frameGenOn : chosen->frameGenOff);
		}
	}

	// ---- Low latency (NVIDIA Reflex / AMD Anti-Lag via plugin cvars).
	if (!def.lowLatencyCvar.empty() && exists(def.lowLatencyCvar))
	{
		Append(r.cvars, settings.lowLatency ? def.lowLatencyOn : def.lowLatencyOff);
	}

	// ---- Brightness via post exposure bias.
	r.post.emplace_back("exposureBias", (settings.brightness - 1.0f) * 2.0f);
	return r;
}

} // namespace crash
