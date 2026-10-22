// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Settings.h"
#include "TestData.h"
#include "TestFramework.h"

#include <set>

using namespace crash;
using namespace bigcar_test;

namespace
{
std::string CvarValue(const ResolvedGraphics& r, const std::string& name)
{
	std::string value;
	for (const auto& [k, v] : r.cvars)
	{
		if (k == name)
		{
			value = v; // last write wins
		}
	}
	return value;
}

CvarExists Has(std::set<std::string> cvars)
{
	return [cvars](const std::string& name) { return cvars.count(name) > 0; };
}
} // namespace

TEST_CASE("graphics: every preset resolves every option and a scalability level per group")
{
	const GraphicsDef& g = FixtureDb().Graphics();
	CHECK(g.presets.size() == 5u);
	for (const GraphicsPresetDef& p : g.presets)
	{
		GraphicsSettings s;
		ApplyGraphicsPreset(g, s, p.id);
		const ResolvedGraphics r = ResolveGraphics(g, s, Has({}));
		CHECK_EQ(r.scalability.size(), 10u);
		CHECK_EQ(r.activeUpscaler, std::string("tsr"));
		CHECK(!CvarValue(r, "r.ScreenPercentage").empty());
		for (const GraphicsOptionDef& o : g.options)
		{
			CHECK_MSG(!EffectiveGraphicsValue(g, s, o.id).empty(), o.id);
		}
	}
	GraphicsSettings low;
	ApplyGraphicsPreset(g, low, "low");
	const ResolvedGraphics r = ResolveGraphics(g, low, Has({}));
	CHECK_EQ(CvarValue(r, "r.DynamicGlobalIlluminationMethod"), std::string("0"));
	CHECK_EQ(CvarValue(r, "r.ScreenPercentage"), std::string("50.00"));
	GraphicsSettings cine;
	ApplyGraphicsPreset(g, cine, "cinematic");
	const ResolvedGraphics rc = ResolveGraphics(g, cine, Has({}));
	CHECK_EQ(CvarValue(rc, "r.Lumen.HardwareRayTracing"), std::string("1"));
	CHECK_EQ(CvarValue(rc, "r.ScreenPercentage"), std::string("100.00"));
}

TEST_CASE("graphics: overrides beat the preset")
{
	const GraphicsDef& g = FixtureDb().Graphics();
	GraphicsSettings s;
	ApplyGraphicsPreset(g, s, "epic");
	s.overrides["motion_blur"] = "off";
	s.overrides["shadows"] = "low";
	const ResolvedGraphics r = ResolveGraphics(g, s, Has({}));
	CHECK_EQ(CvarValue(r, "r.MotionBlurQuality"), std::string("0"));
	bool foundShadow = false;
	for (const auto& [group, level] : r.scalability)
	{
		if (group == "Shadow")
		{
			foundShadow = true;
			CHECK_EQ(level, 0);
		}
	}
	CHECK(foundShadow);
	bool blurPost = false;
	for (const auto& [key, value] : r.post)
	{
		if (key == "motionBlur")
		{
			blurPost = true;
			CHECK_NEAR(value, 0.0f, 1e-6f);
		}
	}
	CHECK(blurPost);
}

TEST_CASE("graphics: DLSS when the plugin is present, fallback to TSR when not")
{
	const GraphicsDef& g = FixtureDb().Graphics();
	GraphicsSettings s;
	ApplyGraphicsPreset(g, s, "high");
	s.upscaler = "dlss";
	s.upscalerMode = "performance";
	s.frameGeneration = true;

	const ResolvedGraphics missing = ResolveGraphics(g, s, Has({}));
	CHECK_EQ(missing.activeUpscaler, std::string("tsr"));
	CHECK(!missing.notes.empty());
	CHECK(CvarValue(missing, "r.NGX.DLSS.Enable").empty());

	const ResolvedGraphics dlss = ResolveGraphics(
	    g, s, Has({"r.NGX.DLSS.Enable", "r.Streamline.DLSSG.Enable", "r.FidelityFX.FSR3.Enabled", "r.FidelityFX.FI.Enabled"}));
	CHECK_EQ(dlss.activeUpscaler, std::string("dlss"));
	CHECK_EQ(CvarValue(dlss, "r.NGX.DLSS.Enable"), std::string("1"));
	CHECK_EQ(CvarValue(dlss, "r.Streamline.DLSSG.Enable"), std::string("1"));
	CHECK_EQ(CvarValue(dlss, "r.ScreenPercentage"), std::string("50.00"));
	CHECK_EQ(CvarValue(dlss, "r.FidelityFX.FSR3.Enabled"), std::string("0")); // other upscalers are switched off
	CHECK_EQ(CvarValue(dlss, "r.FidelityFX.FI.Enabled"), std::string("0"));
}

TEST_CASE("graphics: FSR 4 / FSR 3 quality modes use their own cvars")
{
	const GraphicsDef& g = FixtureDb().Graphics();
	GraphicsSettings s;
	ApplyGraphicsPreset(g, s, "high");
	s.upscaler = "fsr4";
	s.upscalerMode = "balanced";
	const ResolvedGraphics r = ResolveGraphics(g, s, Has({"r.FidelityFX.FSR4.Enabled", "r.NGX.DLSS.Enable"}));
	CHECK_EQ(r.activeUpscaler, std::string("fsr4"));
	CHECK_EQ(CvarValue(r, "r.FidelityFX.FSR4.Enabled"), std::string("1"));
	CHECK_EQ(CvarValue(r, "r.FidelityFX.FSR4.QualityMode"), std::string("2"));
	CHECK_EQ(CvarValue(r, "r.NGX.DLSS.Enable"), std::string("0"));
	s.upscaler = "fsr3";
	s.upscalerMode = "quality";
	s.frameGeneration = true;
	const ResolvedGraphics r3 = ResolveGraphics(g, s, Has({"r.FidelityFX.FSR3.Enabled", "r.FidelityFX.FI.Enabled"}));
	CHECK_EQ(CvarValue(r3, "r.FidelityFX.FSR3.QualityMode"), std::string("1"));
	CHECK_EQ(CvarValue(r3, "r.FidelityFX.FI.Enabled"), std::string("1"));
	const std::vector<const UpscalerDef*> avail = AvailableUpscalers(g, Has({"r.XeSS.Enabled"}));
	bool xess = false;
	bool dlss = false;
	for (const UpscalerDef* u : avail)
	{
		xess = xess || u->id == "xess";
		dlss = dlss || u->id == "dlss";
	}
	CHECK(xess);
	CHECK(!dlss);
}

TEST_CASE("graphics: low latency and brightness")
{
	const GraphicsDef& g = FixtureDb().Graphics();
	GraphicsSettings s;
	s.lowLatency = true;
	s.brightness = 1.25f;
	const ResolvedGraphics r = ResolveGraphics(g, s, Has({"t.Streamline.Reflex.Enable"}));
	CHECK_EQ(CvarValue(r, "t.Streamline.Reflex.Enable"), std::string("1"));
	float bias = 0.0f;
	for (const auto& [k, v] : r.post)
	{
		if (k == "exposureBias")
		{
			bias = v;
		}
	}
	CHECK_NEAR(bias, 0.5f, 1e-5f);
}

TEST_CASE("settings: json round trip with clamping")
{
	UserSettings s;
	s.graphics.preset = "custom";
	s.graphics.overrides["bloom"] = "off";
	s.graphics.upscaler = "dlss";
	s.gameplay.fov = 105.0f;
	s.gameplay.language = "en";
	s.audio.music = 0.3f;
	const UserSettings back = UserSettings::FromJson(s.ToJson());
	CHECK_EQ(back.ToJson().Dump(), s.ToJson().Dump());
	JsonValue bad;
	REQUIRE(ParseJson(R"({"gameplay": {"fov": 400, "speedUnit": "furlongs"}, "audio": {"master": 7}})", bad));
	const UserSettings clamped = UserSettings::FromJson(bad);
	CHECK_NEAR(clamped.gameplay.fov, 120.0f, 1e-5f);
	CHECK_EQ(clamped.gameplay.speedUnit, std::string("kmh"));
	CHECK_NEAR(clamped.audio.master, 1.0f, 1e-5f);
}
