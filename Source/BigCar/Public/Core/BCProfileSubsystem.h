// Copyright BigCar Team. All Rights Reserved.
//
// The player's profile as seen by the UI: the local (offline) profile for career / quick play, or the online profile
// mirrored from the backend while logged in. Garage operations route to whichever one is active.
#pragma once

#include "Core/BigCarTypes.h"
#include "CoreMinimal.h"
#include "CrashCore/Campaign.h"
#include "CrashCore/Profile.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "BCProfileSubsystem.generated.h"

UCLASS()
class BIGCAR_API UBCProfileSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	using FDone = TFunction<void(bool bOk, const FString& Error)>;

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	static UBCProfileSubsystem* Get(const UObject* WorldContext);

	// True when the garage/store operate on the backend profile (online mode).
	bool IsOnline() const;
	void SetPreferOnline(bool bOnline);
	const crash::Profile& Active() const;
	const crash::Profile& Local() const { return LocalProfile; }
	crash::Profile& MutableLocal() { return LocalProfile; }
	void SaveLocal();
	void ResetLocal();

	FBCVehicleBuild SelectedBuild() const;
	const crash::VehicleBuild* FindBuild(const FString& VehicleId) const;

	// ---- Garage (validated locally offline, by the backend online).
	void BuyVehicle(const FString& VehicleId, FDone Done);
	void SelectVehicle(const FString& VehicleId, FDone Done);
	void Upgrade(const FString& VehicleId, crash::UpgradeCategory Category, FDone Done);
	void BuyEquipment(const FString& ItemId, FDone Done);
	void Equip(const FString& VehicleId, crash::EquipSlot Slot, const FString& ItemId, FDone Done);
	void BuyCosmetic(const FString& ItemId, FDone Done);
	void Customize(const FString& VehicleId, const FString& Paint, const FString& Wheels, FDone Done);
	void SetLoadout(const FString& Title, const FString& Banner, const FString& Horn, const FString& Sparks);
	void SetDisplayName(const FString& Name);

	// ---- Offline progression.
	crash::MatchReward GrantMatchRewards(int32 Score, bool bWon, crash::LevelUpResult& OutLevelUp);
	void RecordMatchStats(bool bWon, int32 Takedowns, int32 Wrecked, float DamageDealt, const TArray<int32>& ImpactCounts,
						  float DistanceKm);
	crash::LevelCompletion ApplyCampaignResult(const crash::LevelDef& Level, const crash::LevelRunResult& Run);
	bool MarkSecret(const FString& ChapterId, const FString& SecretId);
	bool MarkHubCollectible(const FString& ChapterId, int32 Index);
	void MarkStorySeen(const FString& Key);
	bool IsStorySeen(const FString& Key) const;

	FSimpleMulticastDelegate OnChanged;

private:
	void LocalResult(const crash::OpResult& Result, FDone Done);
	FString SavePath() const;
	void HandleOnlineProfileChanged();

	crash::Profile LocalProfile;
	bool bPreferOnline = false;
	FDelegateHandle OnlineHandle;
};
