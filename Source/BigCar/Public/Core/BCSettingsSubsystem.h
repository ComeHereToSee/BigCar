// Copyright BigCar Team. All Rights Reserved.
//
// Graphics / gameplay / audio settings. Graphics options, presets and upscalers (TSR, DLSS, FSR, XeSS) are described
// in graphics.json and resolved by CrashCore into console variables + scalability levels + post-process overrides.
#pragma once

#include "CoreMinimal.h"
#include "CrashCore/Settings.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "BCSettingsSubsystem.generated.h"

UENUM()
enum class EBCAudioChannel : uint8
{
	Music,
	Effects,
	Engine,
	UI
};

UCLASS()
class BIGCAR_API UBCSettingsSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	static UBCSettingsSubsystem* Get(const UObject* WorldContext);

	const crash::UserSettings& Settings() const { return Current; }
	// Edit a copy, then Apply() it.
	void Apply(const crash::UserSettings& NewSettings);
	void Save() const;

	const crash::GraphicsDef& GraphicsDef() const;
	TArray<const crash::UpscalerDef*> AvailableUpscalers() const;
	bool IsOptionAvailable(const crash::GraphicsOptionDef& Option) const;
	static bool CvarExists(const FString& Name);
	FString ActiveUpscaler() const { return LastResolved.activeUpscaler.empty() ? FString() : FString(LastResolved.activeUpscaler.c_str()); }
	bool NeedsRestart() const { return bNeedsRestart; }

	// Values used by gameplay code.
	float PostValue(const FString& Key, float Default) const;
	float GameValue(const FString& Key, float Default) const;
	float AudioVolume(EBCAudioChannel Channel) const;
	const crash::GameplaySettings& Gameplay() const { return Current.gameplay; }

	FSimpleMulticastDelegate OnApplied;

private:
	void ApplyGraphics();
	void PickFirstRunDefaults();
	FString SavePath() const;

	crash::UserSettings Current;
	crash::ResolvedGraphics LastResolved;
	TMap<FString, float> PostValues;
	TMap<FString, float> GameValues;
	TMap<FString, FString> AppliedRestartValues;
	bool bNeedsRestart = false;
};
