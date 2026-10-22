// Copyright BigCar Team. All Rights Reserved.

#include "Audio/BCSynthComponents.h"

#include <cmath>

namespace BCSynthDetail
{
void SetupAttenuation(USynthComponent* C, float FalloffCm)
{
	C->bAllowSpatialization = true;
	C->bOverrideAttenuation = true;
	C->AttenuationOverrides.bAttenuate = true;
	C->AttenuationOverrides.bSpatialize = true;
	C->AttenuationOverrides.AttenuationShapeExtents = FVector(400.0f, 0.0f, 0.0f);
	C->AttenuationOverrides.FalloffDistance = FalloffCm;
}
} // namespace BCSynthDetail

// ---------------------------------------------------------------------------------------------

UBCEngineSynthComponent::UBCEngineSynthComponent()
{
	BCSynthDetail::SetupAttenuation(this, 9000.0f);
}

bool UBCEngineSynthComponent::Init(int32& SampleRate)
{
	NumChannels = 1;
	Rate = SampleRate;
	return true;
}

void UBCEngineSynthComponent::Configure(int32 Cylinders, float IdleRpm, float RedlineRpm, bool bElectric, uint32 InSeed)
{
	SynthCommand([this, Cylinders, IdleRpm, RedlineRpm, bElectric, InSeed]() { Synth.Configure(Cylinders, IdleRpm, RedlineRpm, bElectric, InSeed); });
}

void UBCEngineSynthComponent::SetEngineState(float Rpm, float Throttle, float Volume)
{
	SynthCommand([this, Rpm, Throttle, Volume]() {
		Synth.SetState(Rpm, Throttle);
		Synth.SetVolume(Volume);
	});
}

int32 UBCEngineSynthComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	Synth.Render(OutAudio, NumSamples, Rate);
	return NumSamples;
}

// ---------------------------------------------------------------------------------------------

UBCSkidSynthComponent::UBCSkidSynthComponent()
{
	BCSynthDetail::SetupAttenuation(this, 6000.0f);
}

bool UBCSkidSynthComponent::Init(int32& SampleRate)
{
	NumChannels = 1;
	Rate = SampleRate;
	return true;
}

void UBCSkidSynthComponent::SetSlip(float Slip, float Volume)
{
	SynthCommand([this, Slip, Volume]() {
		Synth.SetSlip(Slip);
		Synth.SetVolume(Volume);
	});
}

int32 UBCSkidSynthComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	Synth.Render(OutAudio, NumSamples, Rate);
	return NumSamples;
}

// ---------------------------------------------------------------------------------------------

UBCImpactSynthComponent::UBCImpactSynthComponent()
{
	BCSynthDetail::SetupAttenuation(this, 15000.0f);
}

bool UBCImpactSynthComponent::Init(int32& SampleRate)
{
	NumChannels = 1;
	Rate = SampleRate;
	return true;
}

void UBCImpactSynthComponent::Trigger(float Intensity, float Metallic, float Volume)
{
	const uint32 NewSeed = ++Seed * 2654435761u;
	SynthCommand([this, Intensity, Metallic, Volume, NewSeed]() {
		Synth.SetVolume(Volume);
		Synth.Trigger(Intensity, Metallic, NewSeed);
	});
}

void UBCImpactSynthComponent::SetHorn(bool bOn, float FreqA, float FreqB, float Volume)
{
	SynthCommand([this, bOn, FreqA, FreqB, Volume]() {
		bHorn = bOn;
		HornA = FreqA;
		HornB = FreqB;
		HornVolume = Volume;
	});
}

int32 UBCImpactSynthComponent::OnGenerateAudio(float* OutAudio, int32 NumSamples)
{
	FMemory::Memzero(OutAudio, sizeof(float) * NumSamples);
	Synth.Render(OutAudio, NumSamples, Rate);
	// Two-tone horn (square-ish, band limited by soft clipping).
	const double Sr = static_cast<double>(Rate);
	for (int32 i = 0; i < NumSamples; ++i)
	{
		HornLevel += ((bHorn ? 1.0f : 0.0f) - HornLevel) * 0.002f;
		if (HornLevel < 0.0005f)
		{
			continue;
		}
		HornPhaseA = FMath::Fmod(HornPhaseA + HornA / Sr, 1.0);
		HornPhaseB = FMath::Fmod(HornPhaseB + HornB / Sr, 1.0);
		const float Tone = FMath::Sin(UE_TWO_PI * static_cast<float>(HornPhaseA)) + FMath::Sin(UE_TWO_PI * static_cast<float>(HornPhaseB));
		OutAudio[i] = FMath::Clamp(OutAudio[i] + std::tanh(Tone * 1.8f) * 0.35f * HornLevel * HornVolume, -1.0f, 1.0f);
	}
	return NumSamples;
}
