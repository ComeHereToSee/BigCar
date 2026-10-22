// Copyright BigCar Team. All Rights Reserved.
//
// Procedural audio: engine, tire skid and crash sounds generated in real time by CrashCore DSP (no audio assets).
#pragma once

#include "Components/SynthComponent.h"
#include "CoreMinimal.h"
#include "CrashCore/AudioSynth.h"

#include "BCSynthComponents.generated.h"

UCLASS(ClassGroup = (BigCar))
class BIGCAR_API UBCEngineSynthComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UBCEngineSynthComponent();
	void Configure(int32 Cylinders, float IdleRpm, float RedlineRpm, bool bElectric, uint32 Seed);
	void SetEngineState(float Rpm, float Throttle, float Volume);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	crash::EngineSynth Synth; // audio render thread only
	int32 Rate = 48000;
};

UCLASS(ClassGroup = (BigCar))
class BIGCAR_API UBCSkidSynthComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UBCSkidSynthComponent();
	void SetSlip(float Slip, float Volume);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	crash::SkidSynth Synth;
	int32 Rate = 48000;
};

UCLASS(ClassGroup = (BigCar))
class BIGCAR_API UBCImpactSynthComponent : public USynthComponent
{
	GENERATED_BODY()

public:
	UBCImpactSynthComponent();
	void Trigger(float Intensity, float Metallic, float Volume);
	void SetHorn(bool bOn, float FreqA, float FreqB, float Volume);

protected:
	virtual bool Init(int32& SampleRate) override;
	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override;

private:
	crash::ImpactSynth Synth;
	int32 Rate = 48000;
	uint32 Seed = 1;
	bool bHorn = false;
	float HornA = 415.0f;
	float HornB = 520.0f;
	float HornVolume = 0.0f;
	float HornLevel = 0.0f;
	double HornPhaseA = 0.0;
	double HornPhaseB = 0.0;
};
