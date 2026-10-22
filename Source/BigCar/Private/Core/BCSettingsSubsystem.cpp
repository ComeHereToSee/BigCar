// Copyright BigCar Team. All Rights Reserved.

#include "Core/BCSettingsSubsystem.h"

#include "Core/BCDataSubsystem.h"
#include "Core/BigCarTypes.h"
#include "CrashCore/Json.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/GameUserSettings.h"
#include "HAL/IConsoleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void UBCSettingsSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UBCDataSubsystem>();
	Super::Initialize(Collection);
	FString Text;
	bool bLoaded = false;
	if (FFileHelper::LoadFileToString(Text, *SavePath()))
	{
		crash::JsonValue Json;
		if (crash::ParseJson(BC::ToStd(Text), Json))
		{
			Current = crash::UserSettings::FromJson(Json);
			bLoaded = true;
		}
	}
	if (!bLoaded)
	{
		PickFirstRunDefaults();
	}
	if (!IsRunningDedicatedServer())
	{
		Apply(Current);
	}
	else if (UBCDataSubsystem* Data = GetGameInstance()->GetSubsystem<UBCDataSubsystem>())
	{
		Data->SetLanguage(BC::ToFString(Current.gameplay.language));
	}
}

UBCSettingsSubsystem* UBCSettingsSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UBCSettingsSubsystem>() : nullptr;
}

FString UBCSettingsSubsystem::SavePath() const
{
	return FPaths::ProjectSavedDir() / TEXT("Config/BigCarSettings.json");
}

bool UBCSettingsSubsystem::CvarExists(const FString& Name)
{
	return IConsoleManager::Get().FindConsoleVariable(*Name) != nullptr;
}

const crash::GraphicsDef& UBCSettingsSubsystem::GraphicsDef() const
{
	return GetGameInstance()->GetSubsystem<UBCDataSubsystem>()->Db().Graphics();
}

TArray<const crash::UpscalerDef*> UBCSettingsSubsystem::AvailableUpscalers() const
{
	TArray<const crash::UpscalerDef*> Out;
	for (const crash::UpscalerDef* U : crash::AvailableUpscalers(GraphicsDef(), [](const std::string& N) { return CvarExists(BC::ToFString(N)); }))
	{
		Out.Add(U);
	}
	return Out;
}

bool UBCSettingsSubsystem::IsOptionAvailable(const crash::GraphicsOptionDef& Option) const
{
	return Option.requiresCvar.empty() || CvarExists(BC::ToFString(Option.requiresCvar));
}

void UBCSettingsSubsystem::PickFirstRunDefaults()
{
	Current = crash::UserSettings();
	crash::ApplyGraphicsPreset(GraphicsDef(), Current.graphics, "high");
	// Prefer vendor upscalers when their plugins are installed and supported.
	for (const char* Preferred : {"dlss", "fsr4", "xess", "fsr", "fsr3"})
	{
		for (const crash::UpscalerDef* U : AvailableUpscalers())
		{
			if (U->id == Preferred)
			{
				Current.graphics.upscaler = Preferred;
				Current.graphics.upscalerMode = "quality";
				return;
			}
		}
	}
}

void UBCSettingsSubsystem::Apply(const crash::UserSettings& NewSettings)
{
	Current = NewSettings;
	if (UBCDataSubsystem* Data = GetGameInstance()->GetSubsystem<UBCDataSubsystem>())
	{
		Data->SetLanguage(BC::ToFString(Current.gameplay.language));
	}
	ApplyGraphics();
	Save();
	OnApplied.Broadcast();
}

void UBCSettingsSubsystem::ApplyGraphics()
{
	const crash::GraphicsSettings& G = Current.graphics;
	LastResolved = crash::ResolveGraphics(GraphicsDef(), G, [](const std::string& N) { return CvarExists(BC::ToFString(N)); });
	for (const std::string& Note : LastResolved.notes)
	{
		UE_LOG(LogBigCar, Log, TEXT("Graphics: %s"), *BC::ToFString(Note));
	}

	UGameUserSettings* GUS = GEngine ? GEngine->GetGameUserSettings() : nullptr;
	if (GUS)
	{
		for (const auto& [Group, Level] : LastResolved.scalability)
		{
			if (Group == "ViewDistance")
			{
				GUS->SetViewDistanceQuality(Level);
			}
			else if (Group == "AntiAliasing")
			{
				GUS->SetAntiAliasingQuality(Level);
			}
			else if (Group == "Shadow")
			{
				GUS->SetShadowQuality(Level);
			}
			else if (Group == "GlobalIllumination")
			{
				GUS->SetGlobalIlluminationQuality(Level);
			}
			else if (Group == "Reflection")
			{
				GUS->SetReflectionQuality(Level);
			}
			else if (Group == "PostProcess")
			{
				GUS->SetPostProcessingQuality(Level);
			}
			else if (Group == "Texture")
			{
				GUS->SetTextureQuality(Level);
			}
			else if (Group == "Effects")
			{
				GUS->SetVisualEffectQuality(Level);
			}
			else if (Group == "Foliage")
			{
				GUS->SetFoliageQuality(Level);
			}
			else if (Group == "Shading")
			{
				GUS->SetShadingQuality(Level);
			}
		}
		EWindowMode::Type Mode = EWindowMode::Fullscreen;
		if (G.windowMode == "windowed")
		{
			Mode = EWindowMode::Windowed;
		}
		else if (G.windowMode == "windowed_fullscreen")
		{
			Mode = EWindowMode::WindowedFullscreen;
		}
		GUS->SetFullscreenMode(Mode);
		GUS->SetScreenResolution(G.resolutionX > 0 && G.resolutionY > 0 ? FIntPoint(G.resolutionX, G.resolutionY)
																		: GUS->GetDesktopResolution());
		GUS->SetVSyncEnabled(G.vsync);
		GUS->SetFrameRateLimit(static_cast<float>(G.frameLimit));
		GUS->SetDynamicResolutionEnabled(false);
		if (GUS->SupportsHDRDisplayOutput())
		{
			GUS->EnableHDRDisplayOutput(G.hdr);
		}
		GUS->ApplySettings(false);
		GUS->SaveSettings();
	}

	// Console variables last so they win over scalability presets.
	for (const auto& [Name, Value] : LastResolved.cvars)
	{
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(*BC::ToFString(Name)))
		{
			Var->Set(*BC::ToFString(Value), ECVF_SetByGameSetting);
		}
	}

	PostValues.Reset();
	for (const auto& [Key, Value] : LastResolved.post)
	{
		PostValues.Add(BC::ToFString(Key), Value);
	}
	GameValues.Reset();
	for (const auto& [Key, Value] : LastResolved.game)
	{
		GameValues.Add(BC::ToFString(Key), Value);
	}

	// Options flagged "restart" only take effect after a restart; remember what was active at boot.
	bNeedsRestart = false;
	for (const std::string& OptionId : LastResolved.restartOptions)
	{
		const FString Key = BC::ToFString(OptionId);
		const FString Value = BC::ToFString(crash::EffectiveGraphicsValue(GraphicsDef(), G, OptionId));
		if (const FString* Booted = AppliedRestartValues.Find(Key))
		{
			bNeedsRestart |= *Booted != Value;
		}
		else
		{
			AppliedRestartValues.Add(Key, Value);
		}
	}
}

void UBCSettingsSubsystem::Save() const
{
	FFileHelper::SaveStringToFile(BC::ToFString(Current.ToJson().Dump(true)), *SavePath(),
								  FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
}

float UBCSettingsSubsystem::PostValue(const FString& Key, float Default) const
{
	const float* V = PostValues.Find(Key);
	return V ? *V : Default;
}

float UBCSettingsSubsystem::GameValue(const FString& Key, float Default) const
{
	const float* V = GameValues.Find(Key);
	return V ? *V : Default;
}

float UBCSettingsSubsystem::AudioVolume(EBCAudioChannel Channel) const
{
	const crash::AudioSettings& A = Current.audio;
	switch (Channel)
	{
	case EBCAudioChannel::Music:
		return A.master * A.music;
	case EBCAudioChannel::Effects:
		return A.master * A.effects;
	case EBCAudioChannel::Engine:
		return A.master * A.engine;
	case EBCAudioChannel::UI:
		return A.master * A.ui;
	default:
		return A.master;
	}
}
