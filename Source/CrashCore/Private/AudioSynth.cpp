// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/AudioSynth.h"

#include "CrashCore/Math.h"

#include <cmath>

namespace crash
{
namespace
{
float SoftClip(float x)
{
	return std::tanh(x);
}

double Wrap01(double p)
{
	return p - std::floor(p);
}
} // namespace

void OnePole::SetCutoff(float hz, float sampleRate)
{
	const float x = std::exp(-kTwoPi * std::min(hz, sampleRate * 0.45f) / sampleRate);
	a_ = 1.0f - x;
}

// ---------------------------------------------------------------------------------------------

void EngineSynth::Configure(int cylinders, float idleRpm, float redlineRpm, bool electric, uint64_t seed)
{
	cylinders_ = std::max(1, cylinders);
	idleRpm_ = idleRpm;
	redlineRpm_ = std::max(redlineRpm, idleRpm + 500.0f);
	electric_ = electric;
	rng_.Seed(seed);
	rpm_ = targetRpm_ = idleRpm_;
}

void EngineSynth::SetState(float rpm, float throttle)
{
	targetRpm_ = Clamp(rpm, idleRpm_ * 0.5f, redlineRpm_ * 1.05f);
	targetThrottle_ = Clamp01(throttle);
}

void EngineSynth::Render(float* out, int frames, int sampleRate)
{
	if (frames <= 0 || sampleRate <= 0)
	{
		return;
	}
	const float sr = static_cast<float>(sampleRate);
	if (lastSampleRate_ != sr)
	{
		noiseFilter_.SetCutoff(1800.0f, sr);
		toneFilter_.SetCutoff(electric_ ? 9000.0f : 5000.0f, sr);
		lastSampleRate_ = sr;
	}
	// Per-sample smoothing coefficients (~40 ms).
	const float smooth = 1.0f - std::exp(-1.0f / (0.04f * sr));
	for (int i = 0; i < frames; ++i)
	{
		rpm_ += (targetRpm_ - rpm_) * smooth;
		throttle_ += (targetThrottle_ - throttle_) * smooth;
		float sample = 0.0f;
		if (electric_)
		{
			// Motor whine: two inharmonic partials + inverter hiss.
			const double f0 = static_cast<double>(rpm_) / 60.0 * 6.0;
			phases_[0] = Wrap01(phases_[0] + f0 / sr);
			phases_[1] = Wrap01(phases_[1] + f0 * 2.76 / sr);
			sample = 0.35f * static_cast<float>(std::sin(kTwoPi * phases_[0])) +
			         0.15f * static_cast<float>(std::sin(kTwoPi * phases_[1])) * (0.4f + throttle_);
			sample += 0.03f * noiseFilter_.Process(rng_.Range(-1.0f, 1.0f));
			sample *= 0.5f + 0.5f * throttle_;
		}
		else
		{
			// Firing frequency of a four-stroke: rpm/60 * cylinders/2.
			const double firing = static_cast<double>(rpm_) / 60.0 * static_cast<double>(cylinders_) * 0.5;
			firingPhase_ = Wrap01(firingPhase_ + firing / sr);
			float harmonics = 0.0f;
			static const float kAmps[8] = {1.0f, 0.55f, 0.35f, 0.22f, 0.15f, 0.1f, 0.07f, 0.05f};
			const float brightness = 0.35f + 0.65f * throttle_;
			for (size_t h = 0; h < phases_.size(); ++h)
			{
				const double f = firing * static_cast<double>(h + 1);
				if (f > sr * 0.45)
				{
					break;
				}
				phases_[h] = Wrap01(phases_[h] + f / sr);
				const float amp = kAmps[h] * (h == 0 ? 1.0f : std::pow(brightness, static_cast<float>(h) * 0.5f));
				harmonics += amp * static_cast<float>(std::sin(kTwoPi * phases_[h]));
			}
			// Half-order rumble (cylinder-to-cylinder variation) and combustion noise pulsing with firing.
			const float rumble = 0.25f * static_cast<float>(std::sin(kPi * firingPhase_ * 2.0));
			const float pulse = std::pow(std::max(0.0f, static_cast<float>(std::sin(kTwoPi * firingPhase_))), 6.0f);
			const float noise = noiseFilter_.Process(rng_.Range(-1.0f, 1.0f)) * (0.3f + 0.9f * throttle_) * pulse;
			sample = 0.45f * harmonics + rumble * (0.6f - 0.3f * throttle_) + noise;
			sample = SoftClip(sample * (1.0f + 1.5f * throttle_)) * 0.6f;
		}
		sample = toneFilter_.Process(sample);
		out[i] = Clamp(sample * volume_, -1.0f, 1.0f);
	}
}

// ---------------------------------------------------------------------------------------------

void SkidSynth::Render(float* out, int frames, int sampleRate)
{
	if (frames <= 0 || sampleRate <= 0)
	{
		return;
	}
	const float sr = static_cast<float>(sampleRate);
	band_.SetCutoff(2600.0f, sr);
	const float smooth = 1.0f - std::exp(-1.0f / (0.05f * sr));
	for (int i = 0; i < frames; ++i)
	{
		slip_ += (targetSlip_ - slip_) * smooth;
		const float level = SmoothStep(2.0f, 9.0f, slip_);
		phase_ = Wrap01(phase_ + (900.0 + 25.0 * static_cast<double>(slip_)) / sr);
		const float squeal = static_cast<float>(std::sin(kTwoPi * phase_)) * 0.35f;
		const float hiss = band_.Process(rng_.Range(-1.0f, 1.0f));
		out[i] = Clamp((squeal + hiss) * level * volume_ * 0.6f, -1.0f, 1.0f);
	}
}

// ---------------------------------------------------------------------------------------------

void ImpactSynth::Trigger(float intensity, float metallic, uint32_t seed)
{
	if (voices_.size() >= 6)
	{
		voices_.erase(voices_.begin());
	}
	Voice v;
	v.rng.Seed(seed);
	v.amplitude = Clamp01(intensity);
	v.decay = Lerp(9.0f, 3.5f, Clamp01(intensity));
	v.noiseMix = Lerp(0.85f, 0.45f, Clamp01(metallic));
	// Sheet-metal modes: low thump + clangs.
	v.freqs = {Lerp(70.0f, 45.0f, intensity), v.rng.Range(180.0f, 320.0f), v.rng.Range(600.0f, 1100.0f), v.rng.Range(1800.0f, 3200.0f)};
	voices_.push_back(v);
}

void ImpactSynth::Render(float* out, int frames, int sampleRate)
{
	if (frames <= 0 || sampleRate <= 0 || voices_.empty())
	{
		return;
	}
	const float sr = static_cast<float>(sampleRate);
	const float dt = 1.0f / sr;
	for (Voice& v : voices_)
	{
		v.filter.SetCutoff(Lerp(1200.0f, 6000.0f, v.amplitude), sr);
		for (int i = 0; i < frames; ++i)
		{
			const float env = v.amplitude * std::exp(-v.decay * v.age);
			const float noiseEnv = std::exp(-v.decay * 2.5f * v.age);
			float tone = 0.0f;
			static const float kModeAmps[4] = {1.0f, 0.5f, 0.3f, 0.15f};
			for (size_t m = 0; m < v.freqs.size(); ++m)
			{
				v.phases[m] = Wrap01(v.phases[m] + static_cast<double>(v.freqs[m]) / sr);
				tone += kModeAmps[m] * static_cast<float>(std::sin(kTwoPi * v.phases[m])) * std::exp(-static_cast<float>(m) * 2.0f * v.age);
			}
			const float noise = v.filter.Process(v.rng.Range(-1.0f, 1.0f)) * noiseEnv;
			out[i] = Clamp(out[i] + SoftClip((tone * (1.0f - v.noiseMix) + noise * v.noiseMix * 2.0f) * env) * volume_, -1.0f, 1.0f);
			v.age += dt;
		}
	}
	voices_.erase(
	    std::remove_if(voices_.begin(), voices_.end(), [](const Voice& v) { return v.amplitude * std::exp(-v.decay * v.age) < 0.001f; }),
	    voices_.end());
}

} // namespace crash
