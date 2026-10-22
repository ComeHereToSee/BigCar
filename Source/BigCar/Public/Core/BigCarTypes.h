// Copyright BigCar Team. All Rights Reserved.
//
// Shared Unreal-side types and conversions between Unreal (cm, FVector, FString) and CrashCore (m, Vec3, UTF-8).
#pragma once

#include "CoreMinimal.h"
#include "CrashCore/Math.h"
#include "CrashCore/Stats.h"
#include "CrashCore/Types.h"
#include "CrashCore/VehicleSim.h"

#include <string>

#include "BigCarTypes.generated.h"

BIGCAR_API DECLARE_LOG_CATEGORY_EXTERN(LogBigCar, Log, All);

namespace BC
{
constexpr float CmPerM = 100.0f;

// Positions/lengths: meters <-> centimeters.
inline FVector ToUE(const crash::Vec3& v) { return FVector(v.x, v.y, v.z) * CmPerM; }
inline crash::Vec3 ToCrash(const FVector& v)
{
	return {static_cast<float>(v.X / CmPerM), static_cast<float>(v.Y / CmPerM), static_cast<float>(v.Z / CmPerM)};
}
// Directions / unitless vectors.
inline FVector DirToUE(const crash::Vec3& v) { return FVector(v.x, v.y, v.z); }
inline crash::Vec3 DirToCrash(const FVector& v) { return {static_cast<float>(v.X), static_cast<float>(v.Y), static_cast<float>(v.Z)}; }
// Newtons -> Unreal force units (kg*cm/s^2); N*s -> kg*cm/s.
inline FVector ForceToUE(const crash::Vec3& f) { return FVector(f.x, f.y, f.z) * CmPerM; }
inline FVector ImpulseToUE(const crash::Vec3& j) { return FVector(j.x, j.y, j.z) * CmPerM; }

inline crash::Frame FrameFromQuat(const FQuat& q)
{
	crash::Frame f;
	f.forward = DirToCrash(q.GetForwardVector());
	f.right = DirToCrash(q.GetRightVector());
	f.up = DirToCrash(q.GetUpVector());
	return f;
}

inline FLinearColor ToLinear(const crash::Color& c) { return FLinearColor(FColor(c.r, c.g, c.b)); }

inline FString ToFString(const std::string& s)
{
	const FUTF8ToTCHAR Conv(s.c_str(), static_cast<int32>(s.size()));
	return FString(Conv.Length(), Conv.Get());
}
inline std::string ToStd(const FString& s)
{
	const FTCHARToUTF8 Conv(*s);
	// Get() is ANSICHAR or UTF8CHAR depending on the engine version; both are byte-sized UTF-8 code units.
	return std::string(reinterpret_cast<const char*>(Conv.Get()), static_cast<size_t>(Conv.Length()));
}
inline FName ToFName(const std::string& s) { return FName(*ToFString(s)); }
} // namespace BC

// Network-friendly vehicle controls (quantized).
USTRUCT()
struct BIGCAR_API FBCControls
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 Throttle = 0;

	UPROPERTY()
	uint8 Brake = 0;

	UPROPERTY()
	int8 Steer = 0;

	// Bit 0 handbrake, 1 boost, 2 horn, 3 look back.
	UPROPERTY()
	uint8 Flags = 0;

	static FBCControls FromCrash(const crash::VehicleControls& In, bool bHorn = false, bool bLookBack = false);
	crash::VehicleControls ToCrash() const;
	bool Horn() const { return (Flags & 4) != 0; }
	bool LookBack() const { return (Flags & 8) != 0; }
	bool operator==(const FBCControls& O) const
	{
		return Throttle == O.Throttle && Brake == O.Brake && Steer == O.Steer && Flags == O.Flags;
	}
	bool operator!=(const FBCControls& O) const { return !(*this == O); }
};

// Replicated vehicle build (base car + upgrades + equipment + cosmetics).
USTRUCT(BlueprintType)
struct BIGCAR_API FBCVehicleBuild
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString VehicleId;

	UPROPERTY()
	TArray<uint8> Upgrades;

	UPROPERTY()
	TArray<FString> Equipment;

	UPROPERTY(BlueprintReadOnly)
	FString Paint;

	UPROPERTY(BlueprintReadOnly)
	FString Wheels;

	bool IsValid() const { return !VehicleId.IsEmpty(); }
	crash::VehicleBuild ToCrash() const;
	static FBCVehicleBuild FromCrash(const crash::VehicleBuild& Build);
};

// Replicated damage state, quantized to bytes (zones, engine, wheels; 255 = intact).
USTRUCT()
struct BIGCAR_API FBCDamageRep
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<uint8> Values;

	UPROPERTY()
	bool bWrecked = false;

	UPROPERTY()
	bool bOnFire = false;
};

UENUM(BlueprintType)
enum class EBCMatchPhase : uint8
{
	Waiting,
	Countdown,
	Playing,
	RoundEnd,
	Finished
};

// One line of the takedown / crash feed.
USTRUCT(BlueprintType)
struct BIGCAR_API FBCFeedEntry
{
	GENERATED_BODY()

	UPROPERTY()
	FString Attacker;

	UPROPERTY()
	FString Victim;

	UPROPERTY()
	uint8 Kind = 0; // crash::ImpactKind

	UPROPERTY()
	bool bTakedown = false;

	UPROPERTY()
	bool bRingOut = false;

	UPROPERTY()
	int32 AttackerTeam = -1;

	UPROPERTY()
	int32 VictimTeam = -1;
};

// Points popup for the local player.
USTRUCT(BlueprintType)
struct BIGCAR_API FBCScorePopup
{
	GENERATED_BODY()

	UPROPERTY()
	uint8 Type = 0; // crash::ScoreEventType

	UPROPERTY()
	uint8 Kind = 0; // crash::ImpactKind

	UPROPERTY()
	int32 Points = 0;

	UPROPERTY()
	float Multiplier = 1.0f;

	UPROPERTY()
	FString Other;
};

// Final results row sent to every client.
USTRUCT(BlueprintType)
struct BIGCAR_API FBCResultRow
{
	GENERATED_BODY()

	UPROPERTY()
	FString Name;

	UPROPERTY()
	int32 Team = -1;

	UPROPERTY()
	int32 Score = 0;

	UPROPERTY()
	int32 Takedowns = 0;

	UPROPERTY()
	int32 Assists = 0;

	UPROPERTY()
	int32 Wrecked = 0;

	UPROPERTY()
	int32 Damage = 0;

	UPROPERTY()
	bool bBot = false;
};

// Personal rewards / campaign outcome for the results screen.
USTRUCT(BlueprintType)
struct BIGCAR_API FBCPersonalResult
{
	GENERATED_BODY()

	UPROPERTY()
	bool bWon = false;

	UPROPERTY()
	int32 Placement = 0;

	UPROPERTY()
	int64 Credits = 0;

	UPROPERTY()
	int64 Xp = 0;

	UPROPERTY()
	int32 LevelsGained = 0;

	UPROPERTY()
	TArray<bool> Stars;

	UPROPERTY()
	TArray<bool> NewStars;

	UPROPERTY()
	TArray<FString> Items; // "type:id"

	UPROPERTY()
	float RatingBefore = 0.0f;

	UPROPERTY()
	float RatingAfter = 0.0f;

	UPROPERTY()
	FString LevelId;

	UPROPERTY()
	bool bCampaign = false;

	UPROPERTY()
	bool bOnline = false;
};
