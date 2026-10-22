// Copyright BigCar Team. All Rights Reserved.
//
// Shared look & feel for every Slate screen: palette, fonts, brushes (static storage so they outlive widgets),
// button/bar styles and small widget builders (titles, buttons, stat bars, prices, tabs, cycle selectors, slider and
// toggle rows, cards, screen frame with header/wallet/toasts). Everything lives in namespace BCStyle.
#pragma once

#include "CoreMinimal.h"
#include "CrashCore/Database.h"
#include "CrashCore/Types.h"
#include "Fonts/SlateFontInfo.h"
#include "Layout/Margin.h"
#include "Misc/Attribute.h"
#include "Styling/SlateColor.h"
#include "Styling/SlateTypes.h"
#include "Templates/Function.h"

#include <string>

class SWidget;
class UObject;
class ABCPlayerController;
struct FSlateBrush;

namespace BCStyle
{
// ---------------------------------------------------------------------------------------------
// Palette (sRGB hex -> linear).

FLinearColor Hex(uint8 R, uint8 G, uint8 B, float A = 1.0f);
FLinearColor WithAlpha(const FLinearColor& Color, float Alpha);
FLinearColor FromCrash(const crash::Color& Color, float Alpha = 1.0f);

FLinearColor Accent();		// #FF6A00
FLinearColor AccentHover();
FLinearColor AccentDark();
FLinearColor Danger();		// #FF3B30
FLinearColor Success();		// #3DDC84
FLinearColor Warning();		// #FFC233
FLinearColor GoldColor();	// premium currency
FLinearColor CreditsColor();	// soft currency
FLinearColor XpColor();
FLinearColor TextPrimary();
FLinearColor TextSecondary();
FLinearColor TextMuted();
FLinearColor TextOnAccent();
FLinearColor PanelColor();
FLinearColor PanelLightColor();
FLinearColor LineColor();
FLinearColor TeamColor(int32 Team);
FLinearColor RarityColor(crash::Rarity Rarity);
// Green (1) -> yellow (0.5) -> red (0).
FLinearColor HealthColor(float Fraction);

// ---------------------------------------------------------------------------------------------
// Fonts (default font has CJK fallback).

FSlateFontInfo Font(int32 Size, bool bBold = false);
FSlateFontInfo FontOutlined(int32 Size, bool bBold = true, int32 OutlineSize = 2);

// ---------------------------------------------------------------------------------------------
// Brushes and widget styles (cached, never freed while the game runs).

const FSlateBrush* WhiteBrush();
const FSlateBrush* SolidBrush(const FLinearColor& Color);
const FSlateBrush* RoundedBrush(const FLinearColor& Color, float Radius, const FLinearColor& Outline = FLinearColor::Transparent,
								float OutlineWidth = 0.0f);
const FSlateBrush* PanelBrush();
const FSlateBrush* PanelLightBrush();
const FSlateBrush* CardBrush();
const FSlateBrush* SelectionBrush();

const FButtonStyle* PrimaryButtonStyle();
const FButtonStyle* SecondaryButtonStyle();
const FButtonStyle* DangerButtonStyle();
const FButtonStyle* GhostButtonStyle();
const FButtonStyle* MenuButtonStyle();
const FButtonStyle* CardButtonStyle();
// White brushes, meant to be tinted with SButton::ButtonColorAndOpacity.
const FButtonStyle* TintButtonStyle();
const FProgressBarStyle* BarStyle();

// ---------------------------------------------------------------------------------------------
// Text helpers.

// UI string (ui.* key literal).
FText Txt(const UObject* Ctx, const char* Key);
// Content string (veh./mode./arena./camp./equip./cos./rank./store./ai. keys from data).
FText TxtStd(const UObject* Ctx, const std::string& Key);
FText TxtF(const UObject* Ctx, const FString& Key);
// "{0} ... {1}" formatting of a ui.* key.
FText Fmt(const UObject* Ctx, const char* Key, const TArray<FString>& Args);
// 1234567 -> "1,234,567".
FString Num(int64 Value);
// 185 -> "3:05".
FString Clock(float Seconds);
FText ErrorText(const UObject* Ctx, const FString& Code);
FText CurrencyText(const UObject* Ctx, crash::Currency Currency, int64 Amount);
FText PriceText(const UObject* Ctx, const crash::Price& Price);
FLinearColor CurrencyColor(crash::Currency Currency);
FText TierText(int32 Tier);
FText RomanDivision(int32 Division);
FText RankText(const UObject* Ctx, const crash::RankDef* Rank, int32 Division);
FText TeamName(const UObject* Ctx, int32 Team);
FText ObjectiveText(const UObject* Ctx, const crash::ObjectiveDef& Objective);

// Literal key tables (keeps every key visible to the localization CI scan).
const char* ClassKey(crash::VehicleClass Class);
const char* DrivetrainKey(crash::Drivetrain Drivetrain);
const char* SlotKey(crash::EquipSlot Slot);
const char* RarityKey(crash::Rarity Rarity);
const char* AbilityKey(crash::AbilityType Ability);
const char* AbilityDescKey(crash::AbilityType Ability);
const char* FinishKey(crash::PaintFinish Finish);
const char* ImpactKey(crash::ImpactKind Kind);	  // ui.impact.* (names)
const char* ImpactHitKey(crash::ImpactKind Kind); // ui.hit.* (score popup titles)
const char* ItemTypeKey(crash::ItemType Type);
const char* QueueKey(const FString& Queue);
const char* QueueDescKey(const FString& Queue);
const char* LanguageKey(const FString& Lang);

// Done callback for garage/store/online operations: toasts ui.error.<code> on failure, SuccessKey (optional) on success.
TFunction<void(bool, const FString&)> Feedback(ABCPlayerController* PC, const char* SuccessKey = nullptr);

// Dynamic attribute from a getter.
template <typename T>
TAttribute<T> Bind(TFunction<T()> Getter)
{
	return TAttribute<T>::Create(TAttribute<T>::FGetter::CreateLambda([Getter]() -> T { return Getter(); }));
}

// ---------------------------------------------------------------------------------------------
// Builders.

enum class EButtonKind : uint8
{
	Primary,
	Secondary,
	Danger,
	Ghost
};

TSharedRef<SWidget> Label(const TAttribute<FText>& Text, int32 Size = 16, const FLinearColor& Color = FLinearColor(0.88f, 0.89f, 0.9f),
						  bool bBold = false, bool bWrap = false);
TSharedRef<SWidget> LiveLabel(TFunction<FText()> Text, int32 Size, TFunction<FSlateColor()> Color, bool bBold = false);
TSharedRef<SWidget> BigTitle(const FText& Text, int32 Size = 64);
TSharedRef<SWidget> SectionHeader(const FText& Text);
TSharedRef<SWidget> Divider(float Thickness = 1.0f);
TSharedRef<SWidget> Panel(const TSharedRef<SWidget>& Content, const FMargin& Padding = FMargin(18.0f), bool bLight = false);
TSharedRef<SWidget> Button(EButtonKind Kind, const TAttribute<FText>& Text, TFunction<void()> OnClick,
						   const TAttribute<bool>& Enabled = true, float MinWidth = 0.0f, int32 FontSize = 17);
TSharedRef<SWidget> PrimaryButton(const TAttribute<FText>& Text, TFunction<void()> OnClick, const TAttribute<bool>& Enabled = true,
								  float MinWidth = 0.0f, int32 FontSize = 18);
TSharedRef<SWidget> SecondaryButton(const TAttribute<FText>& Text, TFunction<void()> OnClick, const TAttribute<bool>& Enabled = true,
									float MinWidth = 0.0f, int32 FontSize = 16);
// Two-step button: the first click arms it (shows a confirm label), the second within 3 s runs OnConfirm. For spending money.
TSharedRef<SWidget> ConfirmButton(const UObject* Ctx, EButtonKind Kind, const TAttribute<FText>& Text, TFunction<void()> OnConfirm,
								  const TAttribute<bool>& Enabled = true, float MinWidth = 0.0f, int32 FontSize = 16);
// Horizontal bar 0..1.
TSharedRef<SWidget> Bar(const TAttribute<TOptional<float>>& Fraction, const TAttribute<FSlateColor>& Color, float Height = 8.0f);
// Label, horizontal bar and the number (value 0..10).
TSharedRef<SWidget> StatBar(const FText& Name, TFunction<float()> Value0To10, const FLinearColor& Color = FLinearColor(1.0f, 0.15f, 0.0f));
TSharedRef<SWidget> PriceLabel(const UObject* Ctx, const crash::Price& Price, int32 Size = 16, bool bStrike = false);
TSharedRef<SWidget> TabRow(const TArray<FText>& Labels, TFunction<int32()> GetActive, TFunction<void(int32)> OnSelect,
						   int32 FontSize = 18);
// "<  value  >".
TSharedRef<SWidget> CycleSelector(const TAttribute<FText>& Value, TFunction<void()> OnPrev, TFunction<void()> OnNext,
								  const TAttribute<bool>& Enabled = true, float Width = 300.0f);
// Label on the left, control on the right, optional note below the label.
TSharedRef<SWidget> SettingRow(const TAttribute<FText>& Name, const TSharedRef<SWidget>& Control,
							   const TAttribute<FText>& Note = TAttribute<FText>());
// Slider 0..1. OnCommit runs when the drag ends (in addition to OnChanged on every move).
TSharedRef<SWidget> Slider01(TFunction<float()> Get01, TFunction<void(float)> Set01, TFunction<void()> OnCommit,
							 TFunction<FText()> ValueText, float Width = 300.0f);
TSharedRef<SWidget> SliderRow(const TAttribute<FText>& Name, TFunction<float()> Get01, TFunction<void(float)> Set01,
							  TFunction<void()> OnCommit, TFunction<FText()> ValueText);
TSharedRef<SWidget> Toggle(const UObject* Ctx, TFunction<bool()> Get, TFunction<void(bool)> Set, const TAttribute<bool>& Enabled = true);
TSharedRef<SWidget> ToggleRow(const UObject* Ctx, const TAttribute<FText>& Name, TFunction<bool()> Get, TFunction<void(bool)> Set,
							  const TAttribute<bool>& Enabled = true, const TAttribute<FText>& Note = TAttribute<FText>());
// Clickable card with accent stripe and selection outline.
TSharedRef<SWidget> SelectableCard(const TSharedRef<SWidget>& Content, TFunction<bool()> IsSelected, TFunction<void()> OnClick,
								   const FLinearColor& Stripe = FLinearColor::Transparent, const FMargin& Padding = FMargin(12.0f, 10.0f));
// Item card: title (rarity colored), subtitle, body, footer (price / buttons).
TSharedRef<SWidget> ItemCard(const TAttribute<FText>& Title, const TAttribute<FText>& Subtitle, const FLinearColor& Stripe,
							 const TSharedRef<SWidget>& Body, const TSharedRef<SWidget>& Footer, TFunction<bool()> IsSelected = nullptr,
							 TFunction<void()> OnClick = nullptr);
TSharedRef<SWidget> Badge(const TAttribute<FText>& Text, const FLinearColor& Color, int32 Size = 12, bool bFilled = true);
// Three (or N) stars, gold when filled.
TSharedRef<SWidget> Stars(TFunction<bool(int32)> IsFilled, int32 Count = 3, int32 Size = 20);
TSharedRef<SWidget> Swatch(const FLinearColor& Color, float Size = 28.0f, const FLinearColor& Second = FLinearColor::Transparent);
// Credits / gold / level of the active profile (live).
TSharedRef<SWidget> WalletBar(ABCPlayerController* PC);
// Up to four recent toasts of the player controller (live).
TSharedRef<SWidget> ToastLayer(ABCPlayerController* PC);
// Full screen menu layout: dimmed background, header (back, title, extra, wallet), body, toasts.
TSharedRef<SWidget> ScreenFrame(ABCPlayerController* PC, const FText& Title, const TSharedRef<SWidget>& Body, float BackgroundAlpha = 0.78f,
								const TSharedPtr<SWidget>& HeaderExtra = nullptr, bool bShowBack = true);
} // namespace BCStyle
