// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/AudioSynth.h"
#include "TestFramework.h"

#include <vector>

using namespace crash;

namespace
{
int ZeroCrossings(const std::vector<float>& s)
{
	int n = 0;
	for (size_t i = 1; i < s.size(); ++i)
	{
		n += (s[i - 1] < 0.0f) != (s[i] < 0.0f) ? 1 : 0;
	}
	return n;
}

float Rms(const std::vector<float>& s)
{
	double sum = 0.0;
	for (const float v : s)
	{
		sum += static_cast<double>(v) * v;
	}
	return static_cast<float>(std::sqrt(sum / static_cast<double>(s.size())));
}
} // namespace

TEST_CASE("audio: engine synth is bounded and pitch follows rpm")
{
	const int sr = 48000;
	auto render = [&](float rpm)
	{
		EngineSynth e;
		e.Configure(8, 900.0f, 7000.0f, false, 1);
		e.SetState(rpm, 0.5f);
		std::vector<float> warm(static_cast<size_t>(sr / 2));
		e.Render(warm.data(), static_cast<int>(warm.size()), sr); // let smoothing settle
		std::vector<float> buf(static_cast<size_t>(sr));
		e.Render(buf.data(), static_cast<int>(buf.size()), sr);
		return buf;
	};
	const std::vector<float> idle = render(1000.0f);
	const std::vector<float> high = render(6000.0f);
	for (const float v : high)
	{
		REQUIRE(v >= -1.0f && v <= 1.0f && std::isfinite(v));
	}
	CHECK(Rms(idle) > 0.01f);
	CHECK(Rms(high) > 0.01f);
	CHECK_MSG(ZeroCrossings(high) > ZeroCrossings(idle) * 2, ZeroCrossings(high) << " vs " << ZeroCrossings(idle));
}

TEST_CASE("audio: EV whine, skid and impact one-shots")
{
	const int sr = 44100;
	EngineSynth ev;
	ev.Configure(1, 0.0f, 16000.0f, true, 2);
	ev.SetState(8000.0f, 1.0f);
	std::vector<float> buf(4410);
	ev.Render(buf.data(), 4410, sr);
	CHECK(Rms(buf) > 0.01f);

	SkidSynth skid;
	skid.SetSlip(0.0f);
	std::vector<float> quiet(4410);
	skid.Render(quiet.data(), 4410, sr);
	CHECK(Rms(quiet) < 0.001f);
	skid.SetSlip(12.0f);
	std::vector<float> loud(sr);
	skid.Render(loud.data(), sr, sr);
	CHECK(Rms(loud) > 0.02f);

	ImpactSynth hit;
	hit.Trigger(1.0f, 1.0f, 99);
	CHECK(hit.IsActive());
	std::vector<float> crash(static_cast<size_t>(sr) * 3, 0.0f);
	hit.Render(crash.data(), static_cast<int>(crash.size()), sr);
	std::vector<float> head(crash.begin(), crash.begin() + 4410);
	CHECK(Rms(head) > 0.05f);
	for (const float v : crash)
	{
		REQUIRE(v >= -1.0f && v <= 1.0f);
	}
	CHECK(!hit.IsActive()); // decayed
}
