// Copyright BigCar Team. All Rights Reserved.

#include "UI/BCUIStyle.h"

#include "Brushes/SlateColorBrush.h"
#include "Brushes/SlateRoundedBoxBrush.h"
#include "Core/BCDataSubsystem.h"
#include "Core/BCProfileSubsystem.h"
#include "Core/BigCarTypes.h"
#include "CrashCore/Profile.h"
#include "Gameplay/BCPlayerController.h"
#include "Online/BCOnlineSubsystem.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSlider.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SSpacer.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SNullWidget.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Text/STextBlock.h"

namespace BCStyle
{
namespace StyleCache
{
TMap<FString, TSharedPtr<FSlateBrush>>& Brushes()
{
	static TMap<FString, TSharedPtr<FSlateBrush>> Cache;
	return Cache;
}

FString ColorKey(const FLinearColor& C)
{
	return FString::Printf(TEXT("%.4f,%.4f,%.4f,%.4f"), C.R, C.G, C.B, C.A);
}
} // namespace StyleCache

// ---------------------------------------------------------------------------------------------
// Palette

FLinearColor Hex(uint8 R, uint8 G, uint8 B, float A)
{
	FLinearColor C(FColor(R, G, B, 255));
	C.A = A;
	return C;
}

FLinearColor WithAlpha(const FLinearColor& Color, float Alpha)
{
	FLinearColor C = Color;
	C.A = Alpha;
	return C;
}

FLinearColor FromCrash(const crash::Color& Color, float Alpha)
{
	return Hex(Color.r, Color.g, Color.b, Alpha);
}

FLinearColor Accent() { return Hex(0xFF, 0x6A, 0x00); }
FLinearColor AccentHover() { return Hex(0xFF, 0x8A, 0x33); }
FLinearColor AccentDark() { return Hex(0xCC, 0x52, 0x00); }
FLinearColor Danger() { return Hex(0xFF, 0x3B, 0x30); }
FLinearColor Success() { return Hex(0x3D, 0xDC, 0x84); }
FLinearColor Warning() { return Hex(0xFF, 0xC2, 0x33); }
FLinearColor GoldColor() { return Hex(0xF5, 0xC5, 0x42); }
FLinearColor CreditsColor() { return Hex(0x7F, 0xD4, 0xFF); }
FLinearColor XpColor() { return Hex(0x9B, 0x7B, 0xFF); }
FLinearColor TextPrimary() { return Hex(0xF2, 0xF3, 0xF5); }
FLinearColor TextSecondary() { return Hex(0xA8, 0xAD, 0xB5); }
FLinearColor TextMuted() { return Hex(0x6B, 0x71, 0x7A); }
FLinearColor TextOnAccent() { return Hex(0x14, 0x10, 0x0C); }
FLinearColor PanelColor() { return Hex(0x12, 0x15, 0x1A, 0.90f); }
FLinearColor PanelLightColor() { return Hex(0x1E, 0x22, 0x29, 0.94f); }
FLinearColor LineColor() { return Hex(0x2E, 0x33, 0x3B); }

FLinearColor TeamColor(int32 Team)
{
	switch (Team)
	{
	case 0:
		return Hex(0x3D, 0x9B, 0xFF);
	case 1:
		return Hex(0xFF, 0x46, 0x46);
	case 2:
		return Hex(0x3D, 0xDC, 0x84);
	case 3:
		return Hex(0xFF, 0xD2, 0x3D);
	default:
		return Hex(0xD0, 0xD4, 0xDA);
	}
}

FLinearColor RarityColor(crash::Rarity Rarity)
{
	switch (Rarity)
	{
	case crash::Rarity::Rare:
		return Hex(0x3D, 0x9B, 0xFF);
	case crash::Rarity::Epic:
		return Hex(0xB2, 0x66, 0xFF);
	case crash::Rarity::Legendary:
		return Hex(0xFF, 0xB0, 0x20);
	default:
		return Hex(0xB8, 0xBE, 0xC6);
	}
}

FLinearColor HealthColor(float Fraction)
{
	const float F = FMath::Clamp(Fraction, 0.0f, 1.0f);
	const FLinearColor Red = Hex(0xFF, 0x3B, 0x30);
	const FLinearColor Yellow = Hex(0xFF, 0xC2, 0x33);
	const FLinearColor Green = Hex(0x3D, 0xDC, 0x84);
	return F >= 0.5f ? FLinearColor::LerpUsingHSV(Yellow, Green, (F - 0.5f) * 2.0f) : FLinearColor::LerpUsingHSV(Red, Yellow, F * 2.0f);
}

// ---------------------------------------------------------------------------------------------
// Fonts

FSlateFontInfo Font(int32 Size, bool bBold)
{
	return FCoreStyle::GetDefaultFontStyle(bBold ? "Bold" : "Regular", Size);
}

FSlateFontInfo FontOutlined(int32 Size, bool bBold, int32 OutlineSize)
{
	FSlateFontInfo Info = Font(Size, bBold);
	Info.OutlineSettings.OutlineSize = OutlineSize;
	Info.OutlineSettings.OutlineColor = FLinearColor(0.0f, 0.0f, 0.0f, 0.85f);
	return Info;
}

// ---------------------------------------------------------------------------------------------
// Brushes

const FSlateBrush* WhiteBrush()
{
	return SolidBrush(FLinearColor::White);
}

const FSlateBrush* SolidBrush(const FLinearColor& Color)
{
	const FString Key = TEXT("S|") + StyleCache::ColorKey(Color);
	TSharedPtr<FSlateBrush>& Brush = StyleCache::Brushes().FindOrAdd(Key);
	if (!Brush.IsValid())
	{
		Brush = MakeShared<FSlateColorBrush>(Color);
	}
	return Brush.Get();
}

const FSlateBrush* RoundedBrush(const FLinearColor& Color, float Radius, const FLinearColor& Outline, float OutlineWidth)
{
	const FString Key = FString::Printf(TEXT("R|%s|%.2f|%s|%.2f"), *StyleCache::ColorKey(Color), Radius, *StyleCache::ColorKey(Outline),
										OutlineWidth);
	TSharedPtr<FSlateBrush>& Brush = StyleCache::Brushes().FindOrAdd(Key);
	if (!Brush.IsValid())
	{
		if (OutlineWidth > 0.0f)
		{
			Brush = MakeShared<FSlateRoundedBoxBrush>(Color, Radius, Outline, OutlineWidth);
		}
		else
		{
			Brush = MakeShared<FSlateRoundedBoxBrush>(Color, Radius);
		}
	}
	return Brush.Get();
}

const FSlateBrush* PanelBrush()
{
	return RoundedBrush(PanelColor(), 10.0f, Hex(0x2A, 0x2F, 0x37, 0.9f), 1.0f);
}

const FSlateBrush* PanelLightBrush()
{
	return RoundedBrush(PanelLightColor(), 8.0f, Hex(0x33, 0x39, 0x42), 1.0f);
}

const FSlateBrush* CardBrush()
{
	return RoundedBrush(Hex(0x1A, 0x1E, 0x24, 0.92f), 8.0f, Hex(0x2C, 0x31, 0x39), 1.0f);
}

const FSlateBrush* SelectionBrush()
{
	return RoundedBrush(FLinearColor::Transparent, 8.0f, Accent(), 2.0f);
}

const FButtonStyle* PrimaryButtonStyle()
{
	static const FButtonStyle Style = FButtonStyle()
										  .SetNormal(*RoundedBrush(Accent(), 6.0f))
										  .SetHovered(*RoundedBrush(AccentHover(), 6.0f))
										  .SetPressed(*RoundedBrush(AccentDark(), 6.0f))
										  .SetDisabled(*RoundedBrush(Hex(0x3A, 0x3E, 0x45), 6.0f))
										  .SetNormalPadding(FMargin(0.0f))
										  .SetPressedPadding(FMargin(0.0f, 1.0f, 0.0f, 0.0f));
	return &Style;
}

const FButtonStyle* SecondaryButtonStyle()
{
	static const FButtonStyle Style = FButtonStyle()
										  .SetNormal(*RoundedBrush(Hex(0x26, 0x2A, 0x31, 0.95f), 6.0f, Hex(0x3A, 0x40, 0x49), 1.0f))
										  .SetHovered(*RoundedBrush(Hex(0x33, 0x38, 0x41, 0.98f), 6.0f, Accent(), 1.0f))
										  .SetPressed(*RoundedBrush(Hex(0x1C, 0x1F, 0x25), 6.0f, AccentDark(), 1.0f))
										  .SetDisabled(*RoundedBrush(Hex(0x1C, 0x1F, 0x24, 0.7f), 6.0f, Hex(0x2A, 0x2E, 0x34), 1.0f))
										  .SetNormalPadding(FMargin(0.0f))
										  .SetPressedPadding(FMargin(0.0f, 1.0f, 0.0f, 0.0f));
	return &Style;
}

const FButtonStyle* DangerButtonStyle()
{
	static const FButtonStyle Style = FButtonStyle()
										  .SetNormal(*RoundedBrush(Hex(0x8E, 0x1F, 0x1A, 0.95f), 6.0f, Danger(), 1.0f))
										  .SetHovered(*RoundedBrush(Hex(0xB3, 0x2A, 0x22), 6.0f, Hex(0xFF, 0x7A, 0x70), 1.0f))
										  .SetPressed(*RoundedBrush(Hex(0x6E, 0x16, 0x12), 6.0f, Danger(), 1.0f))
										  .SetDisabled(*RoundedBrush(Hex(0x3A, 0x22, 0x22, 0.7f), 6.0f))
										  .SetNormalPadding(FMargin(0.0f))
										  .SetPressedPadding(FMargin(0.0f, 1.0f, 0.0f, 0.0f));
	return &Style;
}

const FButtonStyle* GhostButtonStyle()
{
	static const FButtonStyle Style = FButtonStyle()
										  .SetNormal(*RoundedBrush(FLinearColor::Transparent, 6.0f))
										  .SetHovered(*RoundedBrush(Hex(0xFF, 0xFF, 0xFF, 0.08f), 6.0f))
										  .SetPressed(*RoundedBrush(Hex(0xFF, 0xFF, 0xFF, 0.14f), 6.0f))
										  .SetDisabled(*RoundedBrush(FLinearColor::Transparent, 6.0f))
										  .SetNormalPadding(FMargin(0.0f))
										  .SetPressedPadding(FMargin(0.0f));
	return &Style;
}

const FButtonStyle* MenuButtonStyle()
{
	static const FButtonStyle Style = FButtonStyle()
										  .SetNormal(*RoundedBrush(Hex(0x0C, 0x0E, 0x12, 0.55f), 4.0f, Hex(0xFF, 0xFF, 0xFF, 0.06f), 1.0f))
										  .SetHovered(*RoundedBrush(WithAlpha(Accent(), 0.92f), 4.0f))
										  .SetPressed(*RoundedBrush(AccentDark(), 4.0f))
										  .SetDisabled(*RoundedBrush(Hex(0x0C, 0x0E, 0x12, 0.35f), 4.0f))
										  .SetNormalPadding(FMargin(0.0f))
										  .SetPressedPadding(FMargin(2.0f, 1.0f, 0.0f, 0.0f));
	return &Style;
}

const FButtonStyle* CardButtonStyle()
{
	static const FButtonStyle Style = FButtonStyle()
										  .SetNormal(*CardBrush())
										  .SetHovered(*RoundedBrush(Hex(0x26, 0x2B, 0x33, 0.96f), 8.0f, Hex(0x4A, 0x51, 0x5C), 1.0f))
										  .SetPressed(*RoundedBrush(Hex(0x15, 0x18, 0x1D, 0.96f), 8.0f, AccentDark(), 1.0f))
										  .SetDisabled(*CardBrush())
										  .SetNormalPadding(FMargin(0.0f))
										  .SetPressedPadding(FMargin(0.0f));
	return &Style;
}

const FButtonStyle* TintButtonStyle()
{
	static const FButtonStyle Style = FButtonStyle()
										  .SetNormal(*RoundedBrush(FLinearColor::White, 14.0f))
										  .SetHovered(*RoundedBrush(FLinearColor(1.25f, 1.25f, 1.25f, 1.0f), 14.0f))
										  .SetPressed(*RoundedBrush(FLinearColor(0.8f, 0.8f, 0.8f, 1.0f), 14.0f))
										  .SetDisabled(*RoundedBrush(FLinearColor(0.5f, 0.5f, 0.5f, 0.5f), 14.0f))
										  .SetNormalPadding(FMargin(0.0f))
										  .SetPressedPadding(FMargin(0.0f));
	return &Style;
}

const FProgressBarStyle* BarStyle()
{
	static const FProgressBarStyle Style = FProgressBarStyle()
											   .SetBackgroundImage(*RoundedBrush(Hex(0x0A, 0x0C, 0x10, 0.85f), 3.0f))
											   .SetFillImage(*RoundedBrush(FLinearColor::White, 3.0f))
											   .SetMarqueeImage(*SolidBrush(FLinearColor::White));
	return &Style;
}

// ---------------------------------------------------------------------------------------------
// Text

FText Txt(const UObject* Ctx, const char* Key)
{
	return BC::Loc(Ctx, Key);
}

FText TxtStd(const UObject* Ctx, const std::string& Key)
{
	if (Key.empty())
	{
		return FText::GetEmpty();
	}
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(Ctx);
	return Data ? Data->Text(Key) : FText::FromString(BC::ToFString(Key));
}

FText TxtF(const UObject* Ctx, const FString& Key)
{
	if (Key.IsEmpty())
	{
		return FText::GetEmpty();
	}
	return BC::Loc(Ctx, Key);
}

FText Fmt(const UObject* Ctx, const char* Key, const TArray<FString>& Args)
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(Ctx);
	if (Data)
	{
		return Data->Format(FString(ANSI_TO_TCHAR(Key)), Args);
	}
	return FText::FromString(FString(ANSI_TO_TCHAR(Key)));
}

FString Num(int64 Value)
{
	const bool bNegative = Value < 0;
	uint64 Abs = bNegative ? static_cast<uint64>(-(Value + 1)) + 1 : static_cast<uint64>(Value);
	FString Digits = FString::Printf(TEXT("%llu"), static_cast<unsigned long long>(Abs));
	FString Out;
	const int32 Len = Digits.Len();
	for (int32 i = 0; i < Len; ++i)
	{
		Out.AppendChar(Digits[i]);
		const int32 Remaining = Len - 1 - i;
		if (Remaining > 0 && Remaining % 3 == 0)
		{
			Out.AppendChar(TEXT(','));
		}
	}
	return bNegative ? TEXT("-") + Out : Out;
}

FString Clock(float Seconds)
{
	const int32 Total = FMath::Max(0, FMath::CeilToInt(Seconds));
	const int32 H = Total / 3600;
	const int32 M = (Total / 60) % 60;
	const int32 S = Total % 60;
	if (H > 0)
	{
		return FString::Printf(TEXT("%d:%02d:%02d"), H, M, S);
	}
	return FString::Printf(TEXT("%d:%02d"), M, S);
}

FText ErrorText(const UObject* Ctx, const FString& Code)
{
	struct FErrorEntry
	{
		const TCHAR* Code;
		const char* Key;
	};
	static const FErrorEntry Table[] = {
		{TEXT("insufficient_funds"), "ui.error.insufficient_funds"},
		{TEXT("not_owned"), "ui.error.not_owned"},
		{TEXT("already_owned"), "ui.error.already_owned"},
		{TEXT("level_locked"), "ui.error.level_locked"},
		{TEXT("max_level"), "ui.error.max_level"},
		{TEXT("invalid_slot"), "ui.error.invalid_slot"},
		{TEXT("class_not_allowed"), "ui.error.class_not_allowed"},
		{TEXT("not_found"), "ui.error.not_found"},
		{TEXT("unauthorized"), "ui.error.unauthorized"},
		{TEXT("network"), "ui.error.network"},
		{TEXT("forbidden"), "ui.error.forbidden"},
		{TEXT("bad_request"), "ui.error.bad_request"},
		{TEXT("conflict"), "ui.error.conflict"},
		{TEXT("limit_reached"), "ui.error.limit_reached"},
		{TEXT("party_full"), "ui.error.party_full"},
		{TEXT("not_leader"), "ui.error.not_leader"},
		{TEXT("not_ready"), "ui.error.not_ready"},
		{TEXT("rate_limited"), "ui.error.rate_limited"},
		{TEXT("internal"), "ui.error.internal"},
		{TEXT("pending"), "ui.error.pending"},
	};
	if (Code.IsEmpty())
	{
		return Txt(Ctx, "ui.error.generic");
	}
	for (const FErrorEntry& E : Table)
	{
		if (Code == E.Code)
		{
			return Txt(Ctx, E.Key);
		}
	}
	return Fmt(Ctx, "ui.error.unknown", {Code});
}

FText CurrencyText(const UObject* Ctx, crash::Currency Currency, int64 Amount)
{
	return Fmt(Ctx, Currency == crash::Currency::Gold ? "ui.currency.gold_amount" : "ui.currency.credits_amount", {Num(Amount)});
}

FText PriceText(const UObject* Ctx, const crash::Price& Price)
{
	if (Price.IsFree())
	{
		return Txt(Ctx, "ui.common.free");
	}
	return CurrencyText(Ctx, Price.currency, Price.amount);
}

FLinearColor CurrencyColor(crash::Currency Currency)
{
	return Currency == crash::Currency::Gold ? GoldColor() : CreditsColor();
}

FText TierText(int32 Tier)
{
	return FText::FromString(FString::Printf(TEXT("T%d"), Tier));
}

FText RomanDivision(int32 Division)
{
	static const TCHAR* Numerals[] = {TEXT("I"), TEXT("II"), TEXT("III"), TEXT("IV"), TEXT("V")};
	if (Division >= 1 && Division <= 5)
	{
		return FText::FromString(Numerals[Division - 1]);
	}
	return FText::AsNumber(Division);
}

FText RankText(const UObject* Ctx, const crash::RankDef* Rank, int32 Division)
{
	if (!Rank)
	{
		return Txt(Ctx, "ui.rank.unranked");
	}
	const FString Name = TxtStd(Ctx, Rank->name).ToString();
	if (Rank->divisions > 1)
	{
		return FText::FromString(Name + TEXT(" ") + RomanDivision(Division).ToString());
	}
	return FText::FromString(Name);
}

FText TeamName(const UObject* Ctx, int32 Team)
{
	switch (Team)
	{
	case 0:
		return Txt(Ctx, "ui.team.blue");
	case 1:
		return Txt(Ctx, "ui.team.red");
	case 2:
		return Txt(Ctx, "ui.team.green");
	case 3:
		return Txt(Ctx, "ui.team.yellow");
	default:
		return Txt(Ctx, "ui.team.none");
	}
}

FText ObjectiveText(const UObject* Ctx, const crash::ObjectiveDef& Objective)
{
	const FString Value = FString::FromInt(FMath::RoundToInt(Objective.value));
	switch (Objective.type)
	{
	case crash::ObjectiveType::Win:
		return Txt(Ctx, "ui.objective.win");
	case crash::ObjectiveType::Takedowns:
		return Fmt(Ctx, "ui.objective.takedowns", {Value});
	case crash::ObjectiveType::TimeUnder:
		return Fmt(Ctx, "ui.objective.time_under", {Clock(Objective.value)});
	case crash::ObjectiveType::NoWreck:
		return Txt(Ctx, "ui.objective.no_wreck");
	case crash::ObjectiveType::DamageUnder:
		return Fmt(Ctx, "ui.objective.damage_under", {Value});
	case crash::ObjectiveType::Score:
		return Fmt(Ctx, "ui.objective.score", {Num(FMath::RoundToInt(Objective.value))});
	case crash::ObjectiveType::HitsOfKind:
		return Fmt(Ctx, "ui.objective.hits_of_kind", {Value, Txt(Ctx, ImpactKey(Objective.kind)).ToString()});
	case crash::ObjectiveType::Survive:
		return Fmt(Ctx, "ui.objective.survive", {Clock(Objective.value)});
	case crash::ObjectiveType::DestroyProps:
		return Fmt(Ctx, "ui.objective.destroy_props", {Value});
	case crash::ObjectiveType::HillTime:
		return Fmt(Ctx, "ui.objective.hill_time", {Value});
	case crash::ObjectiveType::Collect:
		return Fmt(Ctx, "ui.objective.collect", {Value});
	case crash::ObjectiveType::Boss:
		return Txt(Ctx, "ui.objective.boss");
	default:
		return Txt(Ctx, "ui.objective.win");
	}
}

const char* ClassKey(crash::VehicleClass Class)
{
	static const char* Keys[] = {"ui.class.compact", "ui.class.hatchback", "ui.class.sedan",	"ui.class.wagon",  "ui.class.coupe",
								 "ui.class.muscle",	 "ui.class.sports",	   "ui.class.supercar", "ui.class.hypercar", "ui.class.luxury",
								 "ui.class.suv",	 "ui.class.pickup",	   "ui.class.offroad",	"ui.class.van",	   "ui.class.truck"};
	static_assert(UE_ARRAY_COUNT(Keys) == static_cast<size_t>(crash::VehicleClass::Count), "class key table out of date");
	const int32 I = static_cast<int32>(Class);
	return I >= 0 && I < static_cast<int32>(UE_ARRAY_COUNT(Keys)) ? Keys[I] : "ui.class.sedan";
}

const char* DrivetrainKey(crash::Drivetrain Drivetrain)
{
	switch (Drivetrain)
	{
	case crash::Drivetrain::RWD:
		return "ui.drivetrain.rwd";
	case crash::Drivetrain::AWD:
		return "ui.drivetrain.awd";
	default:
		return "ui.drivetrain.fwd";
	}
}

const char* SlotKey(crash::EquipSlot Slot)
{
	static const char* Keys[] = {"ui.slot.front", "ui.slot.rear", "ui.slot.side",	 "ui.slot.roof",
								 "ui.slot.wheels", "ui.slot.core", "ui.slot.ability"};
	static_assert(UE_ARRAY_COUNT(Keys) == static_cast<size_t>(crash::EquipSlot::Count), "slot key table out of date");
	const int32 I = static_cast<int32>(Slot);
	return I >= 0 && I < static_cast<int32>(UE_ARRAY_COUNT(Keys)) ? Keys[I] : "ui.slot.front";
}

const char* RarityKey(crash::Rarity Rarity)
{
	switch (Rarity)
	{
	case crash::Rarity::Rare:
		return "ui.rarity.rare";
	case crash::Rarity::Epic:
		return "ui.rarity.epic";
	case crash::Rarity::Legendary:
		return "ui.rarity.legendary";
	default:
		return "ui.rarity.common";
	}
}

const char* AbilityKey(crash::AbilityType Ability)
{
	switch (Ability)
	{
	case crash::AbilityType::NitroBurst:
		return "ui.ability.nitro_burst";
	case crash::AbilityType::HydraulicJump:
		return "ui.ability.hydraulic_jump";
	case crash::AbilityType::RamCharge:
		return "ui.ability.ram_charge";
	case crash::AbilityType::Brace:
		return "ui.ability.brace";
	case crash::AbilityType::FieldRepair:
		return "ui.ability.field_repair";
	case crash::AbilityType::ShockBumper:
		return "ui.ability.shock_bumper";
	default:
		return "ui.ability.none";
	}
}

const char* AbilityDescKey(crash::AbilityType Ability)
{
	switch (Ability)
	{
	case crash::AbilityType::NitroBurst:
		return "ui.ability.nitro_burst.desc";
	case crash::AbilityType::HydraulicJump:
		return "ui.ability.hydraulic_jump.desc";
	case crash::AbilityType::RamCharge:
		return "ui.ability.ram_charge.desc";
	case crash::AbilityType::Brace:
		return "ui.ability.brace.desc";
	case crash::AbilityType::FieldRepair:
		return "ui.ability.field_repair.desc";
	case crash::AbilityType::ShockBumper:
		return "ui.ability.shock_bumper.desc";
	default:
		return "ui.ability.none.desc";
	}
}

const char* FinishKey(crash::PaintFinish Finish)
{
	switch (Finish)
	{
	case crash::PaintFinish::Metallic:
		return "ui.finish.metallic";
	case crash::PaintFinish::Matte:
		return "ui.finish.matte";
	case crash::PaintFinish::Chrome:
		return "ui.finish.chrome";
	case crash::PaintFinish::Pearl:
		return "ui.finish.pearl";
	case crash::PaintFinish::Carbon:
		return "ui.finish.carbon";
	default:
		return "ui.finish.gloss";
	}
}

const char* ImpactKey(crash::ImpactKind Kind)
{
	static const char* Keys[] = {"ui.impact.none",	   "ui.impact.head_on", "ui.impact.t_bone", "ui.impact.side_swipe", "ui.impact.rear_end",
								 "ui.impact.launch", "ui.impact.crush",	  "ui.impact.pit",	  "ui.impact.glance"};
	static_assert(UE_ARRAY_COUNT(Keys) == static_cast<size_t>(crash::ImpactKind::Count), "impact key table out of date");
	const int32 I = static_cast<int32>(Kind);
	return I >= 0 && I < static_cast<int32>(UE_ARRAY_COUNT(Keys)) ? Keys[I] : "ui.impact.none";
}

const char* ImpactHitKey(crash::ImpactKind Kind)
{
	static const char* Keys[] = {"ui.hit.none",	"ui.hit.head_on", "ui.hit.t_bone", "ui.hit.side_swipe", "ui.hit.rear_end",
								 "ui.hit.launch", "ui.hit.crush",	"ui.hit.pit",	 "ui.hit.glance"};
	static_assert(UE_ARRAY_COUNT(Keys) == static_cast<size_t>(crash::ImpactKind::Count), "hit key table out of date");
	const int32 I = static_cast<int32>(Kind);
	return I >= 0 && I < static_cast<int32>(UE_ARRAY_COUNT(Keys)) ? Keys[I] : "ui.hit.none";
}

const char* ItemTypeKey(crash::ItemType Type)
{
	static const char* Keys[] = {"ui.item.vehicle", "ui.item.equipment", "ui.item.paint",	"ui.item.wheels",  "ui.item.sparks",
								 "ui.item.horn",	"ui.item.title",	 "ui.item.banner", "ui.item.currency"};
	static_assert(UE_ARRAY_COUNT(Keys) == static_cast<size_t>(crash::ItemType::Count), "item type key table out of date");
	const int32 I = static_cast<int32>(Type);
	return I >= 0 && I < static_cast<int32>(UE_ARRAY_COUNT(Keys)) ? Keys[I] : "ui.item.currency";
}

const char* QueueKey(const FString& Queue)
{
	if (Queue == TEXT("casual_ffa"))
	{
		return "ui.queue.casual_ffa";
	}
	if (Queue == TEXT("casual_teams"))
	{
		return "ui.queue.casual_teams";
	}
	if (Queue == TEXT("ranked_1v1"))
	{
		return "ui.queue.ranked_1v1";
	}
	if (Queue == TEXT("ranked_2v2"))
	{
		return "ui.queue.ranked_2v2";
	}
	if (Queue == TEXT("ranked_3v3"))
	{
		return "ui.queue.ranked_3v3";
	}
	return "ui.queue.unknown";
}

const char* QueueDescKey(const FString& Queue)
{
	if (Queue == TEXT("casual_ffa"))
	{
		return "ui.queue.casual_ffa.desc";
	}
	if (Queue == TEXT("casual_teams"))
	{
		return "ui.queue.casual_teams.desc";
	}
	if (Queue == TEXT("ranked_1v1"))
	{
		return "ui.queue.ranked_1v1.desc";
	}
	if (Queue == TEXT("ranked_2v2"))
	{
		return "ui.queue.ranked_2v2.desc";
	}
	if (Queue == TEXT("ranked_3v3"))
	{
		return "ui.queue.ranked_3v3.desc";
	}
	return "ui.queue.unknown";
}

const char* LanguageKey(const FString& Lang)
{
	if (Lang == TEXT("zh-Hans"))
	{
		return "ui.lang.zh_hans";
	}
	if (Lang == TEXT("en"))
	{
		return "ui.lang.en";
	}
	return nullptr;
}

TFunction<void(bool, const FString&)> Feedback(ABCPlayerController* PC, const char* SuccessKey)
{
	TWeakObjectPtr<ABCPlayerController> WeakPC(PC);
	return [WeakPC, SuccessKey](bool bOk, const FString& Error) {
		ABCPlayerController* P = WeakPC.Get();
		if (!P)
		{
			return;
		}
		if (bOk)
		{
			if (SuccessKey)
			{
				P->ShowToast(Txt(P, SuccessKey));
			}
		}
		else
		{
			P->ShowToast(ErrorText(P, Error));
		}
	};
}

// ---------------------------------------------------------------------------------------------
// Builders

TSharedRef<SWidget> Label(const TAttribute<FText>& Text, int32 Size, const FLinearColor& Color, bool bBold, bool bWrap)
{
	return SNew(STextBlock).Text(Text).Font(Font(Size, bBold)).ColorAndOpacity(Color).AutoWrapText(bWrap);
}

TSharedRef<SWidget> LiveLabel(TFunction<FText()> Text, int32 Size, TFunction<FSlateColor()> Color, bool bBold)
{
	return SNew(STextBlock)
		.Text_Lambda([Text]() -> FText { return Text ? Text() : FText::GetEmpty(); })
		.Font(Font(Size, bBold))
		.ColorAndOpacity_Lambda([Color]() -> FSlateColor { return Color ? Color() : FSlateColor(TextPrimary()); });
}

TSharedRef<SWidget> BigTitle(const FText& Text, int32 Size)
{
	return SNew(STextBlock)
		.Text(Text)
		.Font(FontOutlined(Size, true, 3))
		.ColorAndOpacity(TextPrimary())
		.ShadowOffset(FVector2D(0.0f, 3.0f))
		.ShadowColorAndOpacity(FLinearColor(0.0f, 0.0f, 0.0f, 0.6f));
}

TSharedRef<SWidget> SectionHeader(const FText& Text)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 10.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(4.0f)
			.HeightOverride(20.0f)
			[
				SNew(SBorder).BorderImage(SolidBrush(Accent())).Padding(0.0f)
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Text).Font(Font(20, true)).ColorAndOpacity(TextPrimary())
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		.Padding(14.0f, 0.0f, 0.0f, 0.0f)
		[
			Divider()
		];
}

TSharedRef<SWidget> Divider(float Thickness)
{
	return SNew(SBox)
		.HeightOverride(Thickness)
		[
			SNew(SBorder).BorderImage(SolidBrush(LineColor())).Padding(0.0f)
		];
}

TSharedRef<SWidget> Panel(const TSharedRef<SWidget>& Content, const FMargin& Padding, bool bLight)
{
	return SNew(SBorder).BorderImage(bLight ? PanelLightBrush() : PanelBrush()).Padding(Padding)[Content];
}

TSharedRef<SWidget> Button(EButtonKind Kind, const TAttribute<FText>& Text, TFunction<void()> OnClick, const TAttribute<bool>& Enabled,
						   float MinWidth, int32 FontSize)
{
	const FButtonStyle* Style = SecondaryButtonStyle();
	FLinearColor TextColor = TextPrimary();
	switch (Kind)
	{
	case EButtonKind::Primary:
		Style = PrimaryButtonStyle();
		TextColor = TextOnAccent();
		break;
	case EButtonKind::Danger:
		Style = DangerButtonStyle();
		break;
	case EButtonKind::Ghost:
		Style = GhostButtonStyle();
		TextColor = TextSecondary();
		break;
	default:
		break;
	}
	const bool bPrimary = Kind == EButtonKind::Primary;
	TAttribute<bool> EnabledAttr = Enabled;
	return SNew(SBox)
		.MinDesiredWidth(MinWidth > 0.0f ? FOptionalSize(MinWidth) : FOptionalSize())
		[
			SNew(SButton)
			.ButtonStyle(Style)
			.IsEnabled(Enabled)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ContentPadding(FMargin(18.0f, bPrimary ? 10.0f : 8.0f))
			.OnClicked_Lambda([OnClick]() -> FReply {
				if (OnClick)
				{
					OnClick();
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Text(Text)
				.Font(Font(FontSize, true))
				.Justification(ETextJustify::Center)
				.ColorAndOpacity_Lambda([EnabledAttr, TextColor]() -> FSlateColor {
					return EnabledAttr.Get(true) ? FSlateColor(TextColor) : FSlateColor(TextMuted());
				})
			]
		];
}

TSharedRef<SWidget> PrimaryButton(const TAttribute<FText>& Text, TFunction<void()> OnClick, const TAttribute<bool>& Enabled, float MinWidth,
								  int32 FontSize)
{
	return Button(EButtonKind::Primary, Text, MoveTemp(OnClick), Enabled, MinWidth, FontSize);
}

TSharedRef<SWidget> SecondaryButton(const TAttribute<FText>& Text, TFunction<void()> OnClick, const TAttribute<bool>& Enabled,
									float MinWidth, int32 FontSize)
{
	return Button(EButtonKind::Secondary, Text, MoveTemp(OnClick), Enabled, MinWidth, FontSize);
}

TSharedRef<SWidget> ConfirmButton(const UObject* Ctx, EButtonKind Kind, const TAttribute<FText>& Text, TFunction<void()> OnConfirm,
								  const TAttribute<bool>& Enabled, float MinWidth, int32 FontSize)
{
	TSharedRef<double> ArmedAt = MakeShared<double>(-100.0);
	TWeakObjectPtr<const UObject> WeakCtx(Ctx);
	TAttribute<FText> BaseText = Text;
	TAttribute<FText> Shown = Bind<FText>([ArmedAt, WeakCtx, BaseText]() -> FText {
		if (FPlatformTime::Seconds() - *ArmedAt < 3.0)
		{
			return Txt(WeakCtx.Get(), "ui.common.confirm_purchase");
		}
		return BaseText.Get(FText::GetEmpty());
	});
	return Button(
		Kind, Shown,
		[ArmedAt, OnConfirm]() {
			const double Now = FPlatformTime::Seconds();
			if (Now - *ArmedAt < 3.0)
			{
				*ArmedAt = -100.0;
				if (OnConfirm)
				{
					OnConfirm();
				}
			}
			else
			{
				*ArmedAt = Now;
			}
		},
		Enabled, MinWidth, FontSize);
}

TSharedRef<SWidget> Bar(const TAttribute<TOptional<float>>& Fraction, const TAttribute<FSlateColor>& Color, float Height)
{
	return SNew(SBox)
		.HeightOverride(Height)
		[
			SNew(SProgressBar)
			.Style(BarStyle())
			.BarFillType(EProgressBarFillType::LeftToRight)
			.BorderPadding(FVector2D(0.0f, 0.0f))
			.Percent(Fraction)
			.FillColorAndOpacity(Color)
		];
}

TSharedRef<SWidget> StatBar(const FText& Name, TFunction<float()> Value0To10, const FLinearColor& Color)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(92.0f)
			[
				SNew(STextBlock).Text(Name).Font(Font(15)).ColorAndOpacity(TextSecondary())
			]
		]
		+ SHorizontalBox::Slot()
		.FillWidth(1.0f)
		.VAlign(VAlign_Center)
		[
			Bar(Bind<TOptional<float>>([Value0To10]() -> TOptional<float> {
					return FMath::Clamp((Value0To10 ? Value0To10() : 0.0f) / 10.0f, 0.0f, 1.0f);
				}),
				FSlateColor(Color), 10.0f)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(10.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(42.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([Value0To10]() -> FText {
					return FText::FromString(FString::Printf(TEXT("%.1f"), Value0To10 ? Value0To10() : 0.0f));
				})
				.Font(Font(15, true))
				.Justification(ETextJustify::Right)
				.ColorAndOpacity(TextPrimary())
			]
		];
}

TSharedRef<SWidget> PriceLabel(const UObject* Ctx, const crash::Price& Price, int32 Size, bool bStrike)
{
	const FLinearColor Color = Price.IsFree() ? Success() : CurrencyColor(Price.currency);
	TSharedRef<SWidget> Text = SNew(STextBlock)
								   .Text(PriceText(Ctx, Price))
								   .Font(Font(Size, !bStrike))
								   .ColorAndOpacity(bStrike ? TextMuted() : Color);
	if (!bStrike)
	{
		return Text;
	}
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			Text
		]
		+ SOverlay::Slot()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.HeightOverride(2.0f)
			[
				SNew(SBorder).BorderImage(SolidBrush(Danger())).Padding(0.0f)
			]
		];
}

TSharedRef<SWidget> TabRow(const TArray<FText>& Labels, TFunction<int32()> GetActive, TFunction<void(int32)> OnSelect, int32 FontSize)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 i = 0; i < Labels.Num(); ++i)
	{
		Row->AddSlot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 6.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(GhostButtonStyle())
				.ContentPadding(FMargin(16.0f, 8.0f, 16.0f, 0.0f))
				.OnClicked_Lambda([OnSelect, i]() -> FReply {
					if (OnSelect)
					{
						OnSelect(i);
					}
					return FReply::Handled();
				})
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot()
					.AutoHeight()
					.HAlign(HAlign_Center)
					[
						SNew(STextBlock)
						.Text(Labels[i])
						.Font(Font(FontSize, true))
						.ColorAndOpacity_Lambda([GetActive, i]() -> FSlateColor {
							return (GetActive && GetActive() == i) ? FSlateColor(TextPrimary()) : FSlateColor(TextMuted());
						})
					]
					+ SVerticalBox::Slot()
					.AutoHeight()
					.Padding(0.0f, 6.0f, 0.0f, 0.0f)
					[
						SNew(SBox)
						.HeightOverride(3.0f)
						[
							SNew(SBorder)
							.BorderImage(WhiteBrush())
							.Padding(0.0f)
							.BorderBackgroundColor_Lambda([GetActive, i]() -> FSlateColor {
								return (GetActive && GetActive() == i) ? FSlateColor(Accent()) : FSlateColor(FLinearColor::Transparent);
							})
						]
					]
				]
			];
	}
	return Row;
}

TSharedRef<SWidget> CycleSelector(const TAttribute<FText>& Value, TFunction<void()> OnPrev, TFunction<void()> OnNext,
								  const TAttribute<bool>& Enabled, float Width)
{
	auto Arrow = [](const TCHAR* Glyph, TFunction<void()> OnClick) -> TSharedRef<SWidget> {
		return SNew(SBox)
			.WidthOverride(40.0f)
			.HeightOverride(40.0f)
			[
				SNew(SButton)
				.ButtonStyle(SecondaryButtonStyle())
				.HAlign(HAlign_Center)
				.VAlign(VAlign_Center)
				.ContentPadding(FMargin(0.0f))
				.OnClicked_Lambda([OnClick]() -> FReply {
					if (OnClick)
					{
						OnClick();
					}
					return FReply::Handled();
				})
				[
					SNew(STextBlock).Text(FText::FromString(Glyph)).Font(Font(18, true)).ColorAndOpacity(Accent())
				]
			];
	};
	return SNew(SHorizontalBox)
		.IsEnabled(Enabled)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			Arrow(TEXT("<"), OnPrev)
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(6.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(FMath::Max(80.0f, Width - 92.0f))
			.HAlign(HAlign_Center)
			[
				SNew(STextBlock).Text(Value).Font(Font(17, true)).ColorAndOpacity(TextPrimary()).Justification(ETextJustify::Center)
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			Arrow(TEXT(">"), OnNext)
		];
}

TSharedRef<SWidget> SettingRow(const TAttribute<FText>& Name, const TSharedRef<SWidget>& Control, const TAttribute<FText>& Note)
{
	TAttribute<FText> NoteAttr = Note;
	return SNew(SBorder)
		.BorderImage(CardBrush())
		.Padding(FMargin(16.0f, 8.0f))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock).Text(Name).Font(Font(17)).ColorAndOpacity(TextPrimary())
				]
				+ SVerticalBox::Slot()
				.AutoHeight()
				[
					SNew(STextBlock)
					.Text(Note)
					.Font(Font(13))
					.ColorAndOpacity(TextMuted())
					.AutoWrapText(true)
					.Visibility_Lambda([NoteAttr]() -> EVisibility {
						return NoteAttr.Get(FText::GetEmpty()).IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
					})
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(16.0f, 0.0f, 0.0f, 0.0f)
			[
				Control
			]
		];
}

TSharedRef<SWidget> Slider01(TFunction<float()> Get01, TFunction<void(float)> Set01, TFunction<void()> OnCommit, TFunction<FText()> ValueText,
							 float Width)
{
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SBox)
			.WidthOverride(Width - 80.0f)
			.VAlign(VAlign_Center)
			[
				SNew(SSlider)
				.SliderBarColor(FSlateColor(Hex(0x3A, 0x40, 0x49)))
				.SliderHandleColor(FSlateColor(Accent()))
				.Value_Lambda([Get01]() -> float { return Get01 ? FMath::Clamp(Get01(), 0.0f, 1.0f) : 0.0f; })
				.OnValueChanged_Lambda([Set01](float V) {
					if (Set01)
					{
						Set01(FMath::Clamp(V, 0.0f, 1.0f));
					}
				})
				.OnMouseCaptureEnd_Lambda([OnCommit]() {
					if (OnCommit)
					{
						OnCommit();
					}
				})
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(12.0f, 0.0f, 0.0f, 0.0f)
		[
			SNew(SBox)
			.WidthOverride(68.0f)
			[
				SNew(STextBlock)
				.Text_Lambda([ValueText]() -> FText { return ValueText ? ValueText() : FText::GetEmpty(); })
				.Font(Font(16, true))
				.Justification(ETextJustify::Right)
				.ColorAndOpacity(TextPrimary())
			]
		];
}

TSharedRef<SWidget> SliderRow(const TAttribute<FText>& Name, TFunction<float()> Get01, TFunction<void(float)> Set01, TFunction<void()> OnCommit,
							  TFunction<FText()> ValueText)
{
	return SettingRow(Name, Slider01(MoveTemp(Get01), MoveTemp(Set01), MoveTemp(OnCommit), MoveTemp(ValueText)));
}

TSharedRef<SWidget> Toggle(const UObject* Ctx, TFunction<bool()> Get, TFunction<void(bool)> Set, const TAttribute<bool>& Enabled)
{
	TWeakObjectPtr<const UObject> WeakCtx(Ctx);
	return SNew(SBox)
		.WidthOverride(120.0f)
		.HeightOverride(36.0f)
		[
			SNew(SButton)
			.ButtonStyle(TintButtonStyle())
			.IsEnabled(Enabled)
			.HAlign(HAlign_Center)
			.VAlign(VAlign_Center)
			.ContentPadding(FMargin(0.0f))
			.ButtonColorAndOpacity_Lambda([Get]() -> FSlateColor {
				return (Get && Get()) ? FSlateColor(Accent()) : FSlateColor(Hex(0x30, 0x35, 0x3D));
			})
			.OnClicked_Lambda([Get, Set]() -> FReply {
				if (Set)
				{
					Set(!(Get && Get()));
				}
				return FReply::Handled();
			})
			[
				SNew(STextBlock)
				.Font(Font(15, true))
				.Text_Lambda([Get, WeakCtx]() -> FText {
					return (Get && Get()) ? Txt(WeakCtx.Get(), "ui.common.on") : Txt(WeakCtx.Get(), "ui.common.off");
				})
				.ColorAndOpacity_Lambda([Get]() -> FSlateColor {
					return (Get && Get()) ? FSlateColor(TextOnAccent()) : FSlateColor(TextSecondary());
				})
			]
		];
}

TSharedRef<SWidget> ToggleRow(const UObject* Ctx, const TAttribute<FText>& Name, TFunction<bool()> Get, TFunction<void(bool)> Set,
							  const TAttribute<bool>& Enabled, const TAttribute<FText>& Note)
{
	return SettingRow(Name, Toggle(Ctx, MoveTemp(Get), MoveTemp(Set), Enabled), Note);
}

TSharedRef<SWidget> SelectableCard(const TSharedRef<SWidget>& Content, TFunction<bool()> IsSelected, TFunction<void()> OnClick,
								   const FLinearColor& Stripe, const FMargin& Padding)
{
	return SNew(SButton)
		.ButtonStyle(CardButtonStyle())
		.ContentPadding(FMargin(0.0f))
		.OnClicked_Lambda([OnClick]() -> FReply {
			if (OnClick)
			{
				OnClick();
			}
			return FReply::Handled();
		})
		[
			SNew(SOverlay)
			+ SOverlay::Slot()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.Padding(2.0f, 6.0f, 0.0f, 6.0f)
				[
					SNew(SBox)
					.WidthOverride(4.0f)
					[
						SNew(SBorder).BorderImage(RoundedBrush(Stripe, 2.0f)).Padding(0.0f)
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.Padding(Padding)
				[
					Content
				]
			]
			+ SOverlay::Slot()
			[
				SNew(SBorder)
				.BorderImage(SelectionBrush())
				.Padding(0.0f)
				.Visibility_Lambda([IsSelected]() -> EVisibility {
					return (IsSelected && IsSelected()) ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
				})
			]
		];
}

TSharedRef<SWidget> ItemCard(const TAttribute<FText>& Title, const TAttribute<FText>& Subtitle, const FLinearColor& Stripe,
							 const TSharedRef<SWidget>& Body, const TSharedRef<SWidget>& Footer, TFunction<bool()> IsSelected,
							 TFunction<void()> OnClick)
{
	TAttribute<FText> SubAttr = Subtitle;
	TAttribute<FText> TitleAttr = Title;
	TSharedRef<SWidget> Content = SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		[
			SNew(STextBlock)
			.Text(Title)
			.Font(Font(17, true))
			.ColorAndOpacity(TextPrimary())
			.Visibility_Lambda([TitleAttr]() -> EVisibility {
				return TitleAttr.Get(FText::GetEmpty()).IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 2.0f, 0.0f, 0.0f)
		[
			SNew(STextBlock)
			.Text(Subtitle)
			.Font(Font(13))
			.ColorAndOpacity(Stripe.A > 0.01f ? Stripe : TextSecondary())
			.Visibility_Lambda([SubAttr]() -> EVisibility {
				return SubAttr.Get(FText::GetEmpty()).IsEmpty() ? EVisibility::Collapsed : EVisibility::Visible;
			})
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 4.0f, 0.0f, 0.0f)
		[
			Body
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			Footer
		];
	if (OnClick || IsSelected)
	{
		return SelectableCard(Content, MoveTemp(IsSelected), MoveTemp(OnClick), Stripe);
	}
	return SNew(SBorder)
		.BorderImage(CardBrush())
		.Padding(0.0f)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(2.0f, 6.0f, 0.0f, 6.0f)
			[
				SNew(SBox)
				.WidthOverride(4.0f)
				[
					SNew(SBorder).BorderImage(RoundedBrush(Stripe, 2.0f)).Padding(0.0f)
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(FMargin(12.0f, 10.0f))
			[
				Content
			]
		];
}

TSharedRef<SWidget> Badge(const TAttribute<FText>& Text, const FLinearColor& Color, int32 Size, bool bFilled)
{
	return SNew(SBorder)
		.BorderImage(bFilled ? RoundedBrush(Color, 4.0f) : RoundedBrush(FLinearColor::Transparent, 4.0f, Color, 1.0f))
		.Padding(FMargin(7.0f, 2.0f))
		.VAlign(VAlign_Center)
		[
			SNew(STextBlock).Text(Text).Font(Font(Size, true)).ColorAndOpacity(bFilled ? TextOnAccent() : Color)
		];
}

TSharedRef<SWidget> Stars(TFunction<bool(int32)> IsFilled, int32 Count, int32 Size)
{
	TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox);
	for (int32 i = 0; i < Count; ++i)
	{
		Row->AddSlot()
			.AutoWidth()
			.Padding(0.0f, 0.0f, 2.0f, 0.0f)
			[
				SNew(STextBlock)
				.Text(FText::FromString(TEXT("\u2605")))
				.Font(Font(Size, true))
				.ColorAndOpacity_Lambda([IsFilled, i]() -> FSlateColor {
					return (IsFilled && IsFilled(i)) ? FSlateColor(GoldColor()) : FSlateColor(Hex(0x3A, 0x3F, 0x47));
				})
			];
	}
	return Row;
}

TSharedRef<SWidget> Swatch(const FLinearColor& Color, float Size, const FLinearColor& Second)
{
	if (Second.A > 0.01f)
	{
		return SNew(SBox)
			.WidthOverride(Size)
			.HeightOverride(Size)
			[
				SNew(SBorder)
				.BorderImage(RoundedBrush(Hex(0x0A, 0x0C, 0x10), Size * 0.2f, Hex(0xFF, 0xFF, 0xFF, 0.25f), 1.0f))
				.Padding(3.0f)
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SBorder).BorderImage(SolidBrush(Color)).Padding(0.0f)]
					+ SHorizontalBox::Slot().FillWidth(1.0f)[SNew(SBorder).BorderImage(SolidBrush(Second)).Padding(0.0f)]
				]
			];
	}
	return SNew(SBox)
		.WidthOverride(Size)
		.HeightOverride(Size)
		[
			SNew(SBorder).BorderImage(RoundedBrush(Color, Size * 0.2f, Hex(0xFF, 0xFF, 0xFF, 0.3f), 1.0f)).Padding(0.0f)
		];
}

TSharedRef<SWidget> WalletBar(ABCPlayerController* PC)
{
	TWeakObjectPtr<ABCPlayerController> WeakPC(PC);
	auto Chip = [](const TSharedRef<SWidget>& Content) -> TSharedRef<SWidget> {
		return SNew(SBorder).BorderImage(RoundedBrush(Hex(0x0A, 0x0C, 0x10, 0.75f), 14.0f, Hex(0xFF, 0xFF, 0xFF, 0.08f), 1.0f)).Padding(FMargin(12.0f, 5.0f))
			[Content];
	};
	return SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			SNew(SBox)
			.Visibility_Lambda([WeakPC]() -> EVisibility {
				const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(WeakPC.Get());
				return Profile && Profile->IsOnline() ? EVisibility::Visible : EVisibility::Collapsed;
			})
			[
				Badge(Bind<FText>([WeakPC]() -> FText { return Txt(WeakPC.Get(), "ui.common.online_save"); }), Success(), 12)
			]
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			Chip(SNew(STextBlock)
					 .Font(Font(16, true))
					 .ColorAndOpacity(XpColor())
					 .Text_Lambda([WeakPC]() -> FText {
						 const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(WeakPC.Get());
						 return Fmt(WeakPC.Get(), "ui.common.level_short", {FString::FromInt(Profile ? Profile->Active().level : 1)});
					 }))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		.Padding(0.0f, 0.0f, 8.0f, 0.0f)
		[
			Chip(SNew(STextBlock)
					 .Font(Font(16, true))
					 .ColorAndOpacity(CreditsColor())
					 .Text_Lambda([WeakPC]() -> FText {
						 const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(WeakPC.Get());
						 return CurrencyText(WeakPC.Get(), crash::Currency::Credits, Profile ? Profile->Active().credits : 0);
					 }))
		]
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			Chip(SNew(STextBlock)
					 .Font(Font(16, true))
					 .ColorAndOpacity(GoldColor())
					 .Text_Lambda([WeakPC]() -> FText {
						 const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(WeakPC.Get());
						 return CurrencyText(WeakPC.Get(), crash::Currency::Gold, Profile ? Profile->Active().gold : 0);
					 }))
		];
}

TSharedRef<SWidget> ToastLayer(ABCPlayerController* PC)
{
	TWeakObjectPtr<ABCPlayerController> WeakPC(PC);
	static constexpr int32 MaxShown = 4;
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox).Visibility(EVisibility::HitTestInvisible);
	for (int32 SlotIndex = 0; SlotIndex < MaxShown; ++SlotIndex)
	{
		// Index into the newest MaxShown toasts, oldest on top.
		auto Index = [WeakPC, SlotIndex]() -> int32 {
			const ABCPlayerController* P = WeakPC.Get();
			if (!P)
			{
				return INDEX_NONE;
			}
			const int32 Count = P->Toasts().Num();
			const int32 First = FMath::Max(0, Count - 4);
			const int32 I = First + SlotIndex;
			return I < Count ? I : INDEX_NONE;
		};
		Box->AddSlot()
			.AutoHeight()
			.HAlign(HAlign_Center)
			.Padding(0.0f, 0.0f, 0.0f, 6.0f)
			[
				SNew(SBorder)
				.BorderImage(RoundedBrush(Hex(0x10, 0x12, 0x16, 0.94f), 8.0f, Accent(), 1.0f))
				.Padding(FMargin(20.0f, 10.0f))
				.Visibility_Lambda([Index]() -> EVisibility { return Index() == INDEX_NONE ? EVisibility::Collapsed : EVisibility::HitTestInvisible; })
				.ColorAndOpacity_Lambda([WeakPC, Index]() -> FLinearColor {
					const ABCPlayerController* P = WeakPC.Get();
					const int32 I = Index();
					if (!P || I == INDEX_NONE)
					{
						return FLinearColor::White;
					}
					const double Age = FPlatformTime::Seconds() - P->Toasts()[I].ShownAt;
					const float Alpha = Age > 3.4 ? FMath::Clamp(static_cast<float>((4.0 - Age) / 0.6), 0.0f, 1.0f) : 1.0f;
					return FLinearColor(1.0f, 1.0f, 1.0f, Alpha);
				})
				[
					SNew(STextBlock)
					.Font(Font(17, true))
					.ColorAndOpacity(TextPrimary())
					.Text_Lambda([WeakPC, Index]() -> FText {
						const ABCPlayerController* P = WeakPC.Get();
						const int32 I = Index();
						return (P && I != INDEX_NONE) ? P->Toasts()[I].Text : FText::GetEmpty();
					})
				]
			];
	}
	return Box;
}

TSharedRef<SWidget> ScreenFrame(ABCPlayerController* PC, const FText& Title, const TSharedRef<SWidget>& Body, float BackgroundAlpha,
								const TSharedPtr<SWidget>& HeaderExtra, bool bShowBack)
{
	TWeakObjectPtr<ABCPlayerController> WeakPC(PC);
	return SNew(SOverlay)
		+ SOverlay::Slot()
		[
			SNew(SBorder).BorderImage(SolidBrush(Hex(0x05, 0x06, 0x08, BackgroundAlpha))).Padding(0.0f)
		]
		+ SOverlay::Slot()
		[
			SNew(SVerticalBox)
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(SolidBrush(Hex(0x0B, 0x0D, 0x11, 0.92f)))
				.Padding(FMargin(24.0f, 12.0f))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 20.0f, 0.0f)
					[
						SNew(SBox)
						.Visibility(bShowBack ? EVisibility::Visible : EVisibility::Collapsed)
						[
							Button(EButtonKind::Secondary, Txt(PC, "ui.common.back"),
								   [WeakPC]() {
									   if (ABCPlayerController* P = WeakPC.Get())
									   {
										   P->Back();
									   }
								   },
								   true, 110.0f, 16)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(0.0f, 0.0f, 12.0f, 0.0f)
					[
						SNew(SBox)
						.WidthOverride(6.0f)
						.HeightOverride(34.0f)
						[
							SNew(SBorder).BorderImage(SolidBrush(Accent())).Padding(0.0f)
						]
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						SNew(STextBlock).Text(Title).Font(FontOutlined(30, true, 1)).ColorAndOpacity(TextPrimary())
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(24.0f, 0.0f)
					[
						HeaderExtra.IsValid() ? HeaderExtra.ToSharedRef() : SNullWidget::NullWidget
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					[
						WalletBar(PC)
					]
				]
			]
			+ SVerticalBox::Slot()
			.AutoHeight()
			[
				SNew(SBox)
				.HeightOverride(2.0f)
				[
					SNew(SBorder).BorderImage(SolidBrush(WithAlpha(Accent(), 0.85f))).Padding(0.0f)
				]
			]
			+ SVerticalBox::Slot()
			.FillHeight(1.0f)
			.Padding(FMargin(32.0f, 20.0f, 32.0f, 24.0f))
			[
				Body
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Top)
		.Padding(0.0f, 96.0f, 0.0f, 0.0f)
		[
			ToastLayer(PC)
		];
}
} // namespace BCStyle
