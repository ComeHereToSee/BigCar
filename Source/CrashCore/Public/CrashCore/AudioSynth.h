// Copyright BigCar Team. All Rights Reserved.
//
// Procedural audio DSP (engine-agnostic): engine/motor sound driven by rpm + throttle, tire skid, and metal crash
// one-shots. Unreal wraps these in USynthComponents so the game has convincing sound with zero audio assets; recorded
// samples can replace them later per vehicle.
#pragma once

#include "CrashCore/Export.h"
#include "CrashCore/Random.h"

#include <array>
#include <cstdint>
#include <vector>

namespace crash
{
class OnePole
{
public:
	CRASHCORE_EXPORT void SetCutoff(float hz, float sampleRate);
	float Process(float x)
	{
		z_ += a_ * (x - z_);
		return z_;
	}

private:
	float a_ = 1.0f;
	float z_ = 0.0f;
};

class EngineSynth
{
public:
	CRASHCORE_EXPORT void Configure(int cylinders, float idleRpm, float redlineRpm, bool electric, uint64_t seed);
	CRASHCORE_EXPORT void SetState(float rpm, float throttle);
	void SetVolume(float volume) { volume_ = volume; }
	// Renders mono samples in [-1, 1] (overwrites `out`).
	CRASHCORE_EXPORT void Render(float* out, int frames, int sampleRate);

private:
	int cylinders_ = 4;
	float idleRpm_ = 900.0f;
	float redlineRpm_ = 6500.0f;
	bool electric_ = false;
	float targetRpm_ = 900.0f;
	float targetThrottle_ = 0.0f;
	float rpm_ = 900.0f;
	float throttle_ = 0.0f;
	float volume_ = 1.0f;
	std::array<double, 8> phases_{};
	double firingPhase_ = 0.0;
	Pcg32 rng_{7u};
	OnePole noiseFilter_;
	OnePole toneFilter_;
	float lastSampleRate_ = 0.0f;
};

class SkidSynth
{
public:
	void SetSlip(float slip) { targetSlip_ = slip; }
	void SetVolume(float volume) { volume_ = volume; }
	CRASHCORE_EXPORT void Render(float* out, int frames, int sampleRate);

private:
	float targetSlip_ = 0.0f;
	float slip_ = 0.0f;
	float volume_ = 1.0f;
	double phase_ = 0.0;
	Pcg32 rng_{11u};
	OnePole band_;
};

class ImpactSynth
{
public:
	// intensity 0..1, metallic 0..1 (car vs car = 1, car vs wall = 0.4).
	CRASHCORE_EXPORT void Trigger(float intensity, float metallic, uint32_t seed);
	void SetVolume(float volume) { volume_ = volume; }
	// Adds into `out`.
	CRASHCORE_EXPORT void Render(float* out, int frames, int sampleRate);
	bool IsActive() const { return !voices_.empty(); }

private:
	struct Voice
	{
		float amplitude = 1.0f;
		float decay = 6.0f; // 1/s
		float noiseMix = 0.5f;
		std::array<float, 4> freqs{};
		std::array<double, 4> phases{};
		float age = 0.0f;
		Pcg32 rng{3u};
		OnePole filter;
	};
	std::vector<Voice> voices_;
	float volume_ = 1.0f;
};

} // namespace crash
