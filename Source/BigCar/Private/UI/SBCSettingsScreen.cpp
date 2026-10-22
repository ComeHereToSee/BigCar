// Copyright BigCar Team. All Rights Reserved.
//
// Settings: graphics (display, preset, upscaler/frame generation/low latency, per-option values grouped by category),
// gameplay, audio and a read-only controls reference. Changes apply immediately (sliders apply when released or after
// a short pause while dragging).

#include "Core/BCDataSubsystem.h"
#include "Core/BCSettingsSubsystem.h"
#include "Core/BigCarTypes.h"
#include "CrashCore/Settings.h"
#include "Gameplay/BCPlayerController.h"
#include "UI/BCUI.h"
#include "UI/BCUIStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SCompoundWidget.h"
#include "Widgets/Text/STextBlock.h"

#include <algorithm>
#include <string>
#include <vector>

namespace BCSettingsScreenState
{
int32 Tab = 0;

struct FResolution
{
	int32 X;
	int32 Y;
};
const FResolution Resolutions[] = {{0, 0}, {1280, 720}, {1600, 900}, {1920, 1080}, {2560, 1440}, {3200, 1800}, {3840, 2160}};
const int32 FrameLimits[] = {0, 30, 60, 90, 120, 144, 165, 240};
const char* const WindowModes[] = {"fullscreen", "windowed_fullscreen", "windowed"};
} // namespace BCSettingsScreenState

class SBCSettingsScreen : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SBCSettingsScreen) : _Owner(nullptr) {}
		SLATE_ARGUMENT(ABCPlayerController*, Owner)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	virtual ~SBCSettingsScreen() override;
	virtual void Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime) override;

private:
	using FFloatField = TFunction<float*(crash::UserSettings&)>;
	using FBoolField = TFunction<bool*(crash::UserSettings&)>;

	UBCSettingsSubsystem* GetSettings() const { return SettingsSub.Get(); }
	const crash::GraphicsDef* GetGraphicsDef() const;
	void Commit();
	void Changed(bool bImmediate);
	void RebuildAll();
	TSharedRef<SWidget> BuildBody();

	TSharedRef<SWidget> MakeGraphicsTab();
	TSharedRef<SWidget> MakeGameplayTab();
	TSharedRef<SWidget> MakeAudioTab();
	TSharedRef<SWidget> MakeControlsTab();

	TSharedRef<SWidget> Visible(const TSharedRef<SWidget>& Widget, TFunction<bool()> IsVisible);
	TSharedRef<SWidget> CycleRow(const FText& Name, TFunction<FText()> Value, TFunction<void(int32)> Step, const FText& Note = FText::GetEmpty());
	TSharedRef<SWidget> SliderRow(const FText& Name, float Min, float Max, FFloatField Field, TFunction<FText(float)> Format);
	TSharedRef<SWidget> ToggleRow(const FText& Name, FBoolField Field, const FText& Note = FText::GetEmpty());
	TSharedRef<SWidget> OptionRow(const crash::GraphicsOptionDef& Option);

	void SetOption(const std::string& OptionId, const std::string& ValueId);
	void StepPreset(int32 Dir);
	void StepUpscaler(int32 Dir);
	void StepUpscalerMode(int32 Dir);
	const crash::UpscalerDef* PendingUpscaler() const;
	const crash::UpscalerModeDef* PendingMode() const;
	void ResetTab();

	TWeakObjectPtr<ABCPlayerController> PC;
	TWeakObjectPtr<UBCSettingsSubsystem> SettingsSub;
	crash::UserSettings Pending;
	bool bDirty = false;
	double LastChange = 0.0;
	bool bRebuildRequested = false;
};

void SBCSettingsScreen::Construct(const FArguments& InArgs)
{
	PC = InArgs._Owner;
	SettingsSub = UBCSettingsSubsystem::Get(PC.Get());
	if (const UBCSettingsSubsystem* Settings = GetSettings())
	{
		Pending = Settings->Settings();
	}
	ChildSlot
	[
		BuildBody()
	];
}

SBCSettingsScreen::~SBCSettingsScreen()
{
	if (bDirty)
	{
		Commit();
	}
}

void SBCSettingsScreen::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	if (bDirty && FPlatformTime::Seconds() - LastChange > 0.35)
	{
		Commit();
	}
	if (bRebuildRequested)
	{
		bRebuildRequested = false;
		RebuildAll();
	}
}

const crash::GraphicsDef* SBCSettingsScreen::GetGraphicsDef() const
{
	const UBCSettingsSubsystem* Settings = GetSettings();
	return Settings ? &Settings->GraphicsDef() : nullptr;
}

void SBCSettingsScreen::Commit()
{
	bDirty = false;
	if (UBCSettingsSubsystem* Settings = GetSettings())
	{
		Settings->Apply(Pending);
	}
}

void SBCSettingsScreen::Changed(bool bImmediate)
{
	if (bImmediate)
	{
		Commit();
	}
	else
	{
		bDirty = true;
		LastChange = FPlatformTime::Seconds();
	}
}

void SBCSettingsScreen::RebuildAll()
{
	ChildSlot
	[
		BuildBody()
	];
}

TSharedRef<SWidget> SBCSettingsScreen::BuildBody()
{
	ABCPlayerController* P = PC.Get();
	TArray<FText> Tabs = {BCStyle::Txt(P, "ui.settings.tab.graphics"), BCStyle::Txt(P, "ui.settings.tab.gameplay"),
						  BCStyle::Txt(P, "ui.settings.tab.audio"), BCStyle::Txt(P, "ui.settings.tab.controls")};

	TSharedRef<SWidget> RestartBadge =
		SNew(SBox)
		.HAlign(HAlign_Left)
		.Visibility_Lambda([this]() -> EVisibility {
			const UBCSettingsSubsystem* Settings = GetSettings();
			return (Settings && Settings->NeedsRestart()) ? EVisibility::Visible : EVisibility::Collapsed;
		})
		[
			BCStyle::Badge(BCStyle::Txt(P, "ui.settings.restart_required"), BCStyle::Warning(), 14)
		];

	TSharedRef<SWidget> Body =
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			[
				BCStyle::TabRow(Tabs, []() { return BCSettingsScreenState::Tab; }, [](int32 Index) { BCSettingsScreenState::Tab = Index; }, 20)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				BCStyle::Button(BCStyle::EButtonKind::Ghost, BCStyle::Txt(P, "ui.settings.reset_tab"), [this]() { ResetTab(); }, true, 0.0f, 15)
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		.Padding(0.0f, 12.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.HAlign(HAlign_Center)
			[
				SNew(SBox)
				.WidthOverride(980.0f)
				[
					SNew(SWidgetSwitcher)
					.WidgetIndex_Lambda([]() { return BCSettingsScreenState::Tab; })
					+ SWidgetSwitcher::Slot()[MakeGraphicsTab()]
					+ SWidgetSwitcher::Slot()[MakeGameplayTab()]
					+ SWidgetSwitcher::Slot()[MakeAudioTab()]
					+ SWidgetSwitcher::Slot()[MakeControlsTab()]
				]
			]
		];

	return BCStyle::ScreenFrame(P, BCStyle::Txt(P, "ui.settings.title"), Body, 0.86f, RestartBadge);
}

// ---------------------------------------------------------------------------------------------
// Row builders

TSharedRef<SWidget> SBCSettingsScreen::Visible(const TSharedRef<SWidget>& Widget, TFunction<bool()> IsVisible)
{
	return SNew(SBox)
		.Padding(FMargin(0.0f, 0.0f, 0.0f, 6.0f))
		.Visibility_Lambda([IsVisible]() -> EVisibility { return (!IsVisible || IsVisible()) ? EVisibility::Visible : EVisibility::Collapsed; })
		[
			Widget
		];
}

TSharedRef<SWidget> SBCSettingsScreen::CycleRow(const FText& Name, TFunction<FText()> Value, TFunction<void(int32)> Step, const FText& Note)
{
	return BCStyle::SettingRow(Name,
							   BCStyle::CycleSelector(BCStyle::Bind<FText>(Value),
													  [Step]() {
														  if (Step)
														  {
															  Step(-1);
														  }
													  },
													  [Step]() {
														  if (Step)
														  {
															  Step(1);
														  }
													  }),
							   Note);
}

TSharedRef<SWidget> SBCSettingsScreen::SliderRow(const FText& Name, float Min, float Max, FFloatField Field, TFunction<FText(float)> Format)
{
	const float Range = FMath::Max(0.0001f, Max - Min);
	return BCStyle::SliderRow(
		Name, [this, Field, Min, Range]() { return (*Field(Pending) - Min) / Range; },
		[this, Field, Min, Range](float V) {
			*Field(Pending) = Min + V * Range;
			Changed(false);
		},
		[this]() { Commit(); }, [this, Field, Format]() { return Format(*Field(Pending)); });
}

TSharedRef<SWidget> SBCSettingsScreen::ToggleRow(const FText& Name, FBoolField Field, const FText& Note)
{
	return BCStyle::ToggleRow(
		PC.Get(), Name, [this, Field]() { return *Field(Pending); },
		[this, Field](bool bOn) {
			*Field(Pending) = bOn;
			Changed(true);
		},
		true, Note);
}

// ---------------------------------------------------------------------------------------------
// Graphics

const crash::UpscalerDef* SBCSettingsScreen::PendingUpscaler() const
{
	const crash::GraphicsDef* Def = GetGraphicsDef();
	return Def ? Def->FindUpscaler(Pending.graphics.upscaler) : nullptr;
}

const crash::UpscalerModeDef* SBCSettingsScreen::PendingMode() const
{
	const crash::UpscalerDef* Upscaler = PendingUpscaler();
	if (!Upscaler)
	{
		return nullptr;
	}
	const crash::UpscalerModeDef* Mode = Upscaler->FindMode(Pending.graphics.upscalerMode);
	return Mode ? Mode : (Upscaler->modes.empty() ? nullptr : &Upscaler->modes.front());
}

void SBCSettingsScreen::SetOption(const std::string& OptionId, const std::string& ValueId)
{
	const crash::GraphicsDef* Def = GetGraphicsDef();
	if (!Def)
	{
		return;
	}
	if (Pending.graphics.preset != "custom")
	{
		// Freeze the preset's values so leaving the preset keeps every other option as it was.
		for (const crash::GraphicsOptionDef& O : Def->options)
		{
			Pending.graphics.overrides[O.id] = crash::EffectiveGraphicsValue(*Def, Pending.graphics, O.id);
		}
		Pending.graphics.preset = "custom";
	}
	Pending.graphics.overrides[OptionId] = ValueId;
	Changed(true);
}

void SBCSettingsScreen::StepPreset(int32 Dir)
{
	const crash::GraphicsDef* Def = GetGraphicsDef();
	if (!Def || Def->presets.empty())
	{
		return;
	}
	const int32 Num = static_cast<int32>(Def->presets.size());
	int32 Index = INDEX_NONE;
	for (int32 i = 0; i < Num; ++i)
	{
		if (Def->presets[static_cast<size_t>(i)].id == Pending.graphics.preset)
		{
			Index = i;
		}
	}
	if (Index == INDEX_NONE)
	{
		Index = Dir > 0 ? 0 : Num - 1;
	}
	else
	{
		Index = (Index + Dir + Num) % Num;
	}
	crash::ApplyGraphicsPreset(*Def, Pending.graphics, Def->presets[static_cast<size_t>(Index)].id);
	// Keep the player's upscaler if the preset's one is not available on this machine.
	bool bAvailable = false;
	if (const UBCSettingsSubsystem* Settings = GetSettings())
	{
		for (const crash::UpscalerDef* U : Settings->AvailableUpscalers())
		{
			bAvailable |= U->id == Pending.graphics.upscaler;
		}
	}
	if (!bAvailable)
	{
		if (const UBCSettingsSubsystem* Settings = GetSettings())
		{
			Pending.graphics.upscaler = BC::ToStd(Settings->ActiveUpscaler());
		}
	}
	Changed(true);
}

void SBCSettingsScreen::StepUpscaler(int32 Dir)
{
	const UBCSettingsSubsystem* Settings = GetSettings();
	if (!Settings)
	{
		return;
	}
	const TArray<const crash::UpscalerDef*> Upscalers = Settings->AvailableUpscalers();
	if (Upscalers.Num() == 0)
	{
		return;
	}
	int32 Index = 0;
	for (int32 i = 0; i < Upscalers.Num(); ++i)
	{
		if (Upscalers[i]->id == Pending.graphics.upscaler)
		{
			Index = i;
		}
	}
	Index = (Index + Dir + Upscalers.Num()) % Upscalers.Num();
	const crash::UpscalerDef* Next = Upscalers[Index];
	Pending.graphics.upscaler = Next->id;
	if (!Next->FindMode(Pending.graphics.upscalerMode) && !Next->modes.empty())
	{
		const crash::UpscalerModeDef* Quality = Next->FindMode("quality");
		Pending.graphics.upscalerMode = Quality ? Quality->id : Next->modes.front().id;
	}
	Changed(true);
}

void SBCSettingsScreen::StepUpscalerMode(int32 Dir)
{
	const crash::UpscalerDef* Upscaler = PendingUpscaler();
	if (!Upscaler || Upscaler->modes.empty())
	{
		return;
	}
	const int32 Num = static_cast<int32>(Upscaler->modes.size());
	int32 Index = 0;
	for (int32 i = 0; i < Num; ++i)
	{
		if (Upscaler->modes[static_cast<size_t>(i)].id == Pending.graphics.upscalerMode)
		{
			Index = i;
		}
	}
	Index = (Index + Dir + Num) % Num;
	Pending.graphics.upscalerMode = Upscaler->modes[static_cast<size_t>(Index)].id;
	Changed(true);
}

TSharedRef<SWidget> SBCSettingsScreen::OptionRow(const crash::GraphicsOptionDef& Option)
{
	ABCPlayerController* P = PC.Get();
	const std::string OptionId = Option.id;
	return CycleRow(
		BCStyle::TxtStd(P, Option.name),
		[this, OptionId]() -> FText {
			const crash::GraphicsDef* Def = GetGraphicsDef();
			const crash::GraphicsOptionDef* Opt = Def ? Def->FindOption(OptionId) : nullptr;
			if (!Opt)
			{
				return FText::GetEmpty();
			}
			const crash::GraphicsValueDef* Value = Opt->FindValue(crash::EffectiveGraphicsValue(*Def, Pending.graphics, OptionId));
			return Value ? BCStyle::TxtStd(PC.Get(), Value->name) : FText::GetEmpty();
		},
		[this, OptionId](int32 Dir) {
			const crash::GraphicsDef* Def = GetGraphicsDef();
			const crash::GraphicsOptionDef* Opt = Def ? Def->FindOption(OptionId) : nullptr;
			if (!Opt || Opt->values.empty())
			{
				return;
			}
			const std::string Current = crash::EffectiveGraphicsValue(*Def, Pending.graphics, OptionId);
			const int32 Num = static_cast<int32>(Opt->values.size());
			int32 Index = 0;
			for (int32 i = 0; i < Num; ++i)
			{
				if (Opt->values[static_cast<size_t>(i)].id == Current)
				{
					Index = i;
				}
			}
			Index = (Index + Dir + Num) % Num;
			SetOption(OptionId, Opt->values[static_cast<size_t>(Index)].id);
		},
		Option.restart ? BCStyle::Txt(P, "ui.settings.needs_restart_note") : FText::GetEmpty());
}

TSharedRef<SWidget> SBCSettingsScreen::MakeGraphicsTab()
{
	ABCPlayerController* P = PC.Get();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto Header = [&Box](const FText& Text) {
		Box->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 8.0f)[BCStyle::SectionHeader(Text)];
	};
	auto Add = [&Box](const TSharedRef<SWidget>& Widget) { Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[Widget]; };

	// ---- Display.
	Header(BCStyle::Txt(P, "ui.settings.display"));
	Add(CycleRow(
		BCStyle::Txt(P, "ui.settings.window_mode"),
		[this]() -> FText {
			const std::string& Mode = Pending.graphics.windowMode;
			if (Mode == "windowed")
			{
				return BCStyle::Txt(PC.Get(), "ui.settings.window.windowed");
			}
			if (Mode == "windowed_fullscreen")
			{
				return BCStyle::Txt(PC.Get(), "ui.settings.window.windowed_fullscreen");
			}
			return BCStyle::Txt(PC.Get(), "ui.settings.window.fullscreen");
		},
		[this](int32 Dir) {
			int32 Index = 0;
			for (int32 i = 0; i < 3; ++i)
			{
				if (Pending.graphics.windowMode == BCSettingsScreenState::WindowModes[i])
				{
					Index = i;
				}
			}
			Index = (Index + Dir + 3) % 3;
			Pending.graphics.windowMode = BCSettingsScreenState::WindowModes[Index];
			Changed(true);
		}));
	Add(CycleRow(
		BCStyle::Txt(P, "ui.settings.resolution"),
		[this]() -> FText {
			if (Pending.graphics.resolutionX <= 0 || Pending.graphics.resolutionY <= 0)
			{
				return BCStyle::Txt(PC.Get(), "ui.settings.resolution.desktop");
			}
			return FText::FromString(FString::Printf(TEXT("%d \u00D7 %d"), Pending.graphics.resolutionX, Pending.graphics.resolutionY));
		},
		[this](int32 Dir) {
			constexpr int32 Count = static_cast<int32>(UE_ARRAY_COUNT(BCSettingsScreenState::Resolutions));
			int32 Index = 0;
			for (int32 i = 0; i < Count; ++i)
			{
				const BCSettingsScreenState::FResolution& R = BCSettingsScreenState::Resolutions[i];
				if (R.X == Pending.graphics.resolutionX && R.Y == Pending.graphics.resolutionY)
				{
					Index = i;
				}
			}
			Index = (Index + Dir + Count) % Count;
			Pending.graphics.resolutionX = BCSettingsScreenState::Resolutions[Index].X;
			Pending.graphics.resolutionY = BCSettingsScreenState::Resolutions[Index].Y;
			Changed(true);
		}));
	Add(ToggleRow(BCStyle::Txt(P, "ui.settings.vsync"), [](crash::UserSettings& S) -> bool* { return &S.graphics.vsync; }));
	Add(CycleRow(
		BCStyle::Txt(P, "ui.settings.frame_limit"),
		[this]() -> FText {
			return Pending.graphics.frameLimit <= 0 ? BCStyle::Txt(PC.Get(), "ui.settings.frame_limit.unlimited")
													: BCStyle::Fmt(PC.Get(), "ui.settings.fps", {FString::FromInt(Pending.graphics.frameLimit)});
		},
		[this](int32 Dir) {
			constexpr int32 Count = static_cast<int32>(UE_ARRAY_COUNT(BCSettingsScreenState::FrameLimits));
			int32 Index = 0;
			for (int32 i = 0; i < Count; ++i)
			{
				if (BCSettingsScreenState::FrameLimits[i] == Pending.graphics.frameLimit)
				{
					Index = i;
				}
			}
			Index = (Index + Dir + Count) % Count;
			Pending.graphics.frameLimit = BCSettingsScreenState::FrameLimits[Index];
			Changed(true);
		}));
	Add(ToggleRow(BCStyle::Txt(P, "ui.settings.hdr"), [](crash::UserSettings& S) -> bool* { return &S.graphics.hdr; },
				  BCStyle::Txt(P, "ui.settings.hdr.desc")));
	Add(SliderRow(BCStyle::Txt(P, "ui.settings.brightness"), 0.5f, 1.5f, [](crash::UserSettings& S) -> float* { return &S.graphics.brightness; },
				  [](float V) { return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.0f))); }));

	// ---- Preset.
	Header(BCStyle::Txt(P, "ui.settings.quality"));
	Add(CycleRow(
		BCStyle::Txt(P, "ui.settings.preset"),
		[this]() -> FText {
			const crash::GraphicsDef* Def = GetGraphicsDef();
			const crash::GraphicsPresetDef* Preset = Def ? Def->FindPreset(Pending.graphics.preset) : nullptr;
			return Preset ? BCStyle::TxtStd(PC.Get(), Preset->name) : BCStyle::Txt(PC.Get(), "ui.gfx.preset.custom");
		},
		[this](int32 Dir) { StepPreset(Dir); }, BCStyle::Txt(P, "ui.settings.preset.desc")));

	// ---- Upscaling.
	Header(BCStyle::Txt(P, "ui.settings.upscaling"));
	Add(CycleRow(
		BCStyle::Txt(P, "ui.settings.upscaler"),
		[this]() -> FText {
			const crash::UpscalerDef* Upscaler = PendingUpscaler();
			return Upscaler ? BCStyle::TxtStd(PC.Get(), Upscaler->name) : FText::FromString(BC::ToFString(Pending.graphics.upscaler));
		},
		[this](int32 Dir) { StepUpscaler(Dir); }, BCStyle::Txt(P, "ui.settings.upscaler.desc")));
	Box->AddSlot()
		.AutoHeight()
		[
			Visible(CycleRow(
						BCStyle::Txt(P, "ui.settings.upscaler_mode"),
						[this]() -> FText {
							const crash::UpscalerModeDef* Mode = PendingMode();
							return Mode ? BCStyle::TxtStd(PC.Get(), Mode->name) : FText::GetEmpty();
						},
						[this](int32 Dir) { StepUpscalerMode(Dir); }),
					[this]() {
						const crash::UpscalerDef* Upscaler = PendingUpscaler();
						return Upscaler && Upscaler->modes.size() > 1;
					})
		];
	Box->AddSlot()
		.AutoHeight()
		[
			Visible(SliderRow(BCStyle::Txt(P, "ui.settings.render_scale"), 30.0f, 100.0f,
							  [](crash::UserSettings& S) -> float* { return &S.graphics.resolutionScale; },
							  [](float V) { return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V))); }),
					[this]() {
						const crash::UpscalerModeDef* Mode = PendingMode();
						return !Mode || Mode->screenPercentage <= 0.0f;
					})
		];
	Box->AddSlot()
		.AutoHeight()
		[
			Visible(ToggleRow(BCStyle::Txt(P, "ui.settings.frame_generation"), [](crash::UserSettings& S) -> bool* { return &S.graphics.frameGeneration; },
							  BCStyle::Txt(P, "ui.settings.frame_generation.desc")),
					[this]() {
						const crash::UpscalerDef* Upscaler = PendingUpscaler();
						return Upscaler && !Upscaler->frameGenCvar.empty() && UBCSettingsSubsystem::CvarExists(BC::ToFString(Upscaler->frameGenCvar));
					})
		];
	Box->AddSlot()
		.AutoHeight()
		[
			Visible(ToggleRow(BCStyle::Txt(P, "ui.settings.low_latency"), [](crash::UserSettings& S) -> bool* { return &S.graphics.lowLatency; },
							  BCStyle::Txt(P, "ui.settings.low_latency.desc")),
					[this]() {
						const crash::GraphicsDef* Def = GetGraphicsDef();
						return Def && !Def->lowLatencyCvar.empty() && UBCSettingsSubsystem::CvarExists(BC::ToFString(Def->lowLatencyCvar));
					})
		];
	Box->AddSlot()
		.AutoHeight()
		.Padding(0.0f, 0.0f, 0.0f, 6.0f)
		[
			SNew(STextBlock)
			.Font(BCStyle::Font(13))
			.ColorAndOpacity(BCStyle::Warning())
			.Visibility_Lambda([this]() -> EVisibility {
				const UBCSettingsSubsystem* Settings = GetSettings();
				return (Settings && !bDirty && !Settings->ActiveUpscaler().IsEmpty() &&
						Settings->ActiveUpscaler() != BC::ToFString(Pending.graphics.upscaler))
						   ? EVisibility::Visible
						   : EVisibility::Collapsed;
			})
			.Text_Lambda([this]() -> FText {
				const UBCSettingsSubsystem* Settings = GetSettings();
				const crash::GraphicsDef* Def = GetGraphicsDef();
				const crash::UpscalerDef* Active = (Settings && Def) ? Def->FindUpscaler(BC::ToStd(Settings->ActiveUpscaler())) : nullptr;
				return BCStyle::Fmt(PC.Get(), "ui.settings.upscaler_fallback",
									{Active ? BCStyle::TxtStd(PC.Get(), Active->name).ToString() : FString()});
			})
		];

	// ---- Per-option values, grouped by category.
	if (const crash::GraphicsDef* Def = GetGraphicsDef())
	{
		const UBCSettingsSubsystem* Settings = GetSettings();
		std::vector<std::string> Categories = {"quality", "lighting", "post", "game"};
		for (const crash::GraphicsOptionDef& O : Def->options)
		{
			if (std::find(Categories.begin(), Categories.end(), O.category) == Categories.end())
			{
				Categories.push_back(O.category);
			}
		}
		for (const std::string& Category : Categories)
		{
			bool bAny = false;
			for (const crash::GraphicsOptionDef& O : Def->options)
			{
				bAny |= O.category == Category && (!Settings || Settings->IsOptionAvailable(O));
			}
			if (!bAny)
			{
				continue;
			}
			const char* Key = "ui.settings.cat.other";
			if (Category == "quality")
			{
				Key = "ui.settings.cat.quality";
			}
			else if (Category == "lighting")
			{
				Key = "ui.settings.cat.lighting";
			}
			else if (Category == "post")
			{
				Key = "ui.settings.cat.post";
			}
			else if (Category == "game")
			{
				Key = "ui.settings.cat.game";
			}
			Header(BCStyle::Txt(P, Key));
			for (const crash::GraphicsOptionDef& O : Def->options)
			{
				if (O.category == Category && (!Settings || Settings->IsOptionAvailable(O)))
				{
					Add(OptionRow(O));
				}
			}
		}
	}

	return SNew(SScrollBox) + SScrollBox::Slot()[Box];
}

// ---------------------------------------------------------------------------------------------
// Gameplay / audio / controls

TSharedRef<SWidget> SBCSettingsScreen::MakeGameplayTab()
{
	ABCPlayerController* P = PC.Get();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto Header = [&Box](const FText& Text) {
		Box->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 8.0f)[BCStyle::SectionHeader(Text)];
	};
	auto Add = [&Box](const TSharedRef<SWidget>& Widget) { Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[Widget]; };
	auto Percent = [](float V) { return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.0f))); };

	Header(BCStyle::Txt(P, "ui.settings.general"));
	Add(CycleRow(
		BCStyle::Txt(P, "ui.settings.language"),
		[this]() -> FText {
			const FString Lang = BC::ToFString(Pending.gameplay.language);
			const char* Key = BCStyle::LanguageKey(Lang);
			return Key ? BCStyle::Txt(PC.Get(), Key) : FText::FromString(Lang);
		},
		[this](int32 Dir) {
			TArray<FString> Langs;
			if (const UBCDataSubsystem* Data = UBCDataSubsystem::Get(PC.Get()))
			{
				Langs = Data->Languages();
			}
			Langs.AddUnique(TEXT("zh-Hans"));
			Langs.AddUnique(TEXT("en"));
			Langs.Sort([](const FString& A, const FString& B) {
				// Primary language first.
				if (A == TEXT("zh-Hans") || B == TEXT("zh-Hans"))
				{
					return A == TEXT("zh-Hans");
				}
				return A < B;
			});
			int32 Index = Langs.IndexOfByKey(BC::ToFString(Pending.gameplay.language));
			Index = Index == INDEX_NONE ? 0 : (Index + Dir + Langs.Num()) % Langs.Num();
			Pending.gameplay.language = BC::ToStd(Langs[Index]);
			Changed(true);
			bRebuildRequested = true;
		}));
	Add(CycleRow(
		BCStyle::Txt(P, "ui.settings.speed_unit"),
		[this]() -> FText {
			return BCStyle::Txt(PC.Get(), Pending.gameplay.speedUnit == "mph" ? "ui.settings.unit.mph" : "ui.settings.unit.kmh");
		},
		[this](int32) {
			Pending.gameplay.speedUnit = Pending.gameplay.speedUnit == "mph" ? "kmh" : "mph";
			Changed(true);
		}));
	Add(SliderRow(BCStyle::Txt(P, "ui.settings.hud_scale"), 0.6f, 1.5f, [](crash::UserSettings& S) -> float* { return &S.gameplay.hudScale; }, Percent));
	Add(ToggleRow(BCStyle::Txt(P, "ui.settings.damage_numbers"), [](crash::UserSettings& S) -> bool* { return &S.gameplay.damageNumbers; }));

	Header(BCStyle::Txt(P, "ui.settings.camera"));
	Add(SliderRow(BCStyle::Txt(P, "ui.settings.fov"), 60.0f, 120.0f, [](crash::UserSettings& S) -> float* { return &S.gameplay.fov; },
				  [](float V) { return FText::FromString(FString::Printf(TEXT("%d\u00B0"), FMath::RoundToInt(V))); }));
	Add(SliderRow(BCStyle::Txt(P, "ui.settings.camera_shake"), 0.0f, 2.0f, [](crash::UserSettings& S) -> float* { return &S.gameplay.cameraShake; },
				  Percent));
	Add(ToggleRow(BCStyle::Txt(P, "ui.settings.crash_cam"), [](crash::UserSettings& S) -> bool* { return &S.gameplay.crashCam; },
				  BCStyle::Txt(P, "ui.settings.crash_cam.desc")));
	Add(ToggleRow(BCStyle::Txt(P, "ui.settings.slow_motion"), [](crash::UserSettings& S) -> bool* { return &S.gameplay.slowMotion; },
				  BCStyle::Txt(P, "ui.settings.slow_motion.desc")));

	Header(BCStyle::Txt(P, "ui.settings.driving"));
	Add(SliderRow(BCStyle::Txt(P, "ui.settings.steering_assist"), 0.0f, 1.0f,
				  [](crash::UserSettings& S) -> float* { return &S.gameplay.steeringAssist; }, Percent));
	Add(ToggleRow(BCStyle::Txt(P, "ui.settings.invert_air_pitch"), [](crash::UserSettings& S) -> bool* { return &S.gameplay.invertAirPitch; }));
	Add(ToggleRow(BCStyle::Txt(P, "ui.settings.vibration"), [](crash::UserSettings& S) -> bool* { return &S.gameplay.vibration; }));

	return SNew(SScrollBox) + SScrollBox::Slot()[Box];
}

TSharedRef<SWidget> SBCSettingsScreen::MakeAudioTab()
{
	ABCPlayerController* P = PC.Get();
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	auto Add = [&Box](const TSharedRef<SWidget>& Widget) { Box->AddSlot().AutoHeight().Padding(0.0f, 0.0f, 0.0f, 6.0f)[Widget]; };
	auto Percent = [](float V) { return FText::FromString(FString::Printf(TEXT("%d%%"), FMath::RoundToInt(V * 100.0f))); };
	Box->AddSlot().AutoHeight().Padding(0.0f, 14.0f, 0.0f, 8.0f)[BCStyle::SectionHeader(BCStyle::Txt(P, "ui.settings.volume"))];
	Add(SliderRow(BCStyle::Txt(P, "ui.settings.master"), 0.0f, 1.0f, [](crash::UserSettings& S) -> float* { return &S.audio.master; }, Percent));
	Add(SliderRow(BCStyle::Txt(P, "ui.settings.music"), 0.0f, 1.0f, [](crash::UserSettings& S) -> float* { return &S.audio.music; }, Percent));
	Add(SliderRow(BCStyle::Txt(P, "ui.settings.effects"), 0.0f, 1.0f, [](crash::UserSettings& S) -> float* { return &S.audio.effects; }, Percent));
	Add(SliderRow(BCStyle::Txt(P, "ui.settings.engine"), 0.0f, 1.0f, [](crash::UserSettings& S) -> float* { return &S.audio.engine; }, Percent));
	Add(SliderRow(BCStyle::Txt(P, "ui.settings.ui_volume"), 0.0f, 1.0f, [](crash::UserSettings& S) -> float* { return &S.audio.ui; }, Percent));
	return SNew(SScrollBox) + SScrollBox::Slot()[Box];
}

TSharedRef<SWidget> SBCSettingsScreen::MakeControlsTab()
{
	ABCPlayerController* P = PC.Get();
	struct FBinding
	{
		const char* Action;
		const char* Keyboard;
		const char* Gamepad;
	};
	static const FBinding Bindings[] = {
		{"ui.controls.throttle", "ui.controls.kb.throttle", "ui.controls.pad.throttle"},
		{"ui.controls.brake", "ui.controls.kb.brake", "ui.controls.pad.brake"},
		{"ui.controls.steer", "ui.controls.kb.steer", "ui.controls.pad.steer"},
		{"ui.controls.handbrake", "ui.controls.kb.handbrake", "ui.controls.pad.handbrake"},
		{"ui.controls.boost", "ui.controls.kb.boost", "ui.controls.pad.boost"},
		{"ui.controls.ability", "ui.controls.kb.ability", "ui.controls.pad.ability"},
		{"ui.controls.reset", "ui.controls.kb.reset", "ui.controls.pad.reset"},
		{"ui.controls.look_back", "ui.controls.kb.look_back", "ui.controls.pad.look_back"},
		{"ui.controls.horn", "ui.controls.kb.horn", "ui.controls.pad.horn"},
		{"ui.controls.pause", "ui.controls.kb.pause", "ui.controls.pad.pause"},
		{"ui.controls.scoreboard", "ui.controls.kb.scoreboard", "ui.controls.pad.scoreboard"},
	};
	auto Cell = [](const FText& Text, int32 Size, const FLinearColor& Color, bool bBold) -> TSharedRef<SWidget> {
		return SNew(STextBlock).Text(Text).Font(BCStyle::Font(Size, bBold)).ColorAndOpacity(Color);
	};

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	Box->AddSlot()
		.AutoHeight()
		.Padding(16.0f, 14.0f, 16.0f, 8.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().FillWidth(1.0f)[Cell(BCStyle::Txt(P, "ui.controls.col.action"), 14, BCStyle::TextMuted(), true)]
			+ SHorizontalBox::Slot().FillWidth(1.2f)[Cell(BCStyle::Txt(P, "ui.controls.col.keyboard"), 14, BCStyle::TextMuted(), true)]
			+ SHorizontalBox::Slot().FillWidth(1.0f)[Cell(BCStyle::Txt(P, "ui.controls.col.gamepad"), 14, BCStyle::TextMuted(), true)]
		];
	for (const FBinding& B : Bindings)
	{
		Box->AddSlot()
			.AutoHeight()
			.Padding(0.0f, 0.0f, 0.0f, 4.0f)
			[
				SNew(SBorder)
				.BorderImage(BCStyle::CardBrush())
				.Padding(FMargin(16.0f, 10.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[Cell(BCStyle::Txt(P, B.Action), 17, BCStyle::TextPrimary(), true)]
					+ SHorizontalBox::Slot().FillWidth(1.2f).VAlign(VAlign_Center)[Cell(BCStyle::Txt(P, B.Keyboard), 16, BCStyle::Accent(), true)]
					+ SHorizontalBox::Slot().FillWidth(1.0f).VAlign(VAlign_Center)[Cell(BCStyle::Txt(P, B.Gamepad), 16, BCStyle::CreditsColor(), true)]
				]
			];
	}
	Box->AddSlot().AutoHeight().Padding(4.0f, 10.0f)[BCStyle::Label(BCStyle::Txt(P, "ui.controls.note"), 13, BCStyle::TextMuted(), false, true)];
	return SNew(SScrollBox) + SScrollBox::Slot()[Box];
}

void SBCSettingsScreen::ResetTab()
{
	const crash::UserSettings Defaults{};
	switch (BCSettingsScreenState::Tab)
	{
	case 0:
	{
		const crash::GraphicsSettings Old = Pending.graphics;
		Pending.graphics = Defaults.graphics;
		if (const crash::GraphicsDef* Def = GetGraphicsDef())
		{
			crash::ApplyGraphicsPreset(*Def, Pending.graphics, "high");
		}
		// Keep the display mode the player picked; reset only quality settings.
		Pending.graphics.windowMode = Old.windowMode;
		Pending.graphics.resolutionX = Old.resolutionX;
		Pending.graphics.resolutionY = Old.resolutionY;
		Pending.graphics.upscaler = Old.upscaler;
		Pending.graphics.upscalerMode = Old.upscalerMode;
		break;
	}
	case 1:
	{
		const std::string Language = Pending.gameplay.language;
		Pending.gameplay = Defaults.gameplay;
		Pending.gameplay.language = Language;
		break;
	}
	case 2:
		Pending.audio = Defaults.audio;
		break;
	default:
		return;
	}
	Changed(true);
	if (ABCPlayerController* P = PC.Get())
	{
		P->ShowToast(BCStyle::Txt(P, "ui.settings.reset_done"));
	}
}

TSharedRef<SWidget> BCUI::MakeSettings(ABCPlayerController* PC)
{
	return SNew(SBCSettingsScreen).Owner(PC);
}
