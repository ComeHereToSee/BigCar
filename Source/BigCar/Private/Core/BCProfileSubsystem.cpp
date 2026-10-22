// Copyright BigCar Team. All Rights Reserved.

#include "Core/BCProfileSubsystem.h"

#include "Core/BCDataSubsystem.h"
#include "CrashCore/Json.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Online/BCOnlineSubsystem.h"

void UBCProfileSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Collection.InitializeDependency<UBCDataSubsystem>();
	Collection.InitializeDependency<UBCOnlineSubsystem>();
	Super::Initialize(Collection);

	const UBCDataSubsystem* Data = GetGameInstance()->GetSubsystem<UBCDataSubsystem>();
	FString Text;
	bool bLoaded = false;
	if (FFileHelper::LoadFileToString(Text, *SavePath()))
	{
		crash::JsonValue Json;
		crash::JsonError Err;
		if (crash::ParseJson(BC::ToStd(Text), Json, &Err) && crash::Profile::FromJson(Json, LocalProfile))
		{
			bLoaded = true;
		}
		else
		{
			UE_LOG(LogBigCar, Error, TEXT("Profile save is corrupt (%s); keeping a backup and starting fresh"),
				   *BC::ToFString(Err.ToString()));
			IFileManager::Get().Copy(*(SavePath() + TEXT(".corrupt")), *SavePath());
		}
	}
	if (!bLoaded && Data)
	{
		LocalProfile = crash::MakeNewProfile(Data->Db(), "local", "Driver");
		SaveLocal();
	}
	// Drop vehicles that no longer exist in the data (renamed content).
	if (Data)
	{
		LocalProfile.vehicles.erase(std::remove_if(LocalProfile.vehicles.begin(), LocalProfile.vehicles.end(),
												   [Data](const crash::VehicleBuild& B) { return Data->Db().FindVehicle(B.vehicleId) == nullptr; }),
									LocalProfile.vehicles.end());
		if (LocalProfile.vehicles.empty())
		{
			for (const crash::VehicleDef* Starter : Data->Db().StarterVehicles())
			{
				LocalProfile.vehicles.push_back(crash::MakeDefaultBuild(Data->Db(), Starter->id));
			}
		}
	}
	if (UBCOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UBCOnlineSubsystem>())
	{
		OnlineHandle = Online->OnProfileChanged.AddUObject(this, &UBCProfileSubsystem::HandleOnlineProfileChanged);
	}
}

void UBCProfileSubsystem::Deinitialize()
{
	if (UBCOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UBCOnlineSubsystem>())
	{
		Online->OnProfileChanged.Remove(OnlineHandle);
	}
	SaveLocal();
	Super::Deinitialize();
}

UBCProfileSubsystem* UBCProfileSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UBCProfileSubsystem>() : nullptr;
}

FString UBCProfileSubsystem::SavePath() const
{
	return FPaths::ProjectSavedDir() / TEXT("SaveGames/BigCarProfile.json");
}

void UBCProfileSubsystem::SaveLocal()
{
	const FString Text = BC::ToFString(LocalProfile.ToJson().Dump(true));
	const FString Tmp = SavePath() + TEXT(".tmp");
	// Write-then-move so a crash mid-save never corrupts the profile.
	if (FFileHelper::SaveStringToFile(Text, *Tmp, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
	{
		IFileManager::Get().Move(*SavePath(), *Tmp, true, true);
	}
}

void UBCProfileSubsystem::ResetLocal()
{
	if (const UBCDataSubsystem* Data = GetGameInstance()->GetSubsystem<UBCDataSubsystem>())
	{
		LocalProfile = crash::MakeNewProfile(Data->Db(), "local", LocalProfile.displayName);
		SaveLocal();
		OnChanged.Broadcast();
	}
}

void UBCProfileSubsystem::HandleOnlineProfileChanged()
{
	OnChanged.Broadcast();
}

bool UBCProfileSubsystem::IsOnline() const
{
	const UBCOnlineSubsystem* Online = GetGameInstance()->GetSubsystem<UBCOnlineSubsystem>();
	return bPreferOnline && Online && Online->IsLoggedIn() && Online->HasProfile();
}

void UBCProfileSubsystem::SetPreferOnline(bool bOnline)
{
	if (bPreferOnline != bOnline)
	{
		bPreferOnline = bOnline;
		OnChanged.Broadcast();
	}
}

const crash::Profile& UBCProfileSubsystem::Active() const
{
	if (IsOnline())
	{
		return GetGameInstance()->GetSubsystem<UBCOnlineSubsystem>()->Profile();
	}
	return LocalProfile;
}

FBCVehicleBuild UBCProfileSubsystem::SelectedBuild() const
{
	const crash::VehicleBuild* Build = Active().SelectedBuild();
	return Build ? FBCVehicleBuild::FromCrash(*Build) : FBCVehicleBuild();
}

const crash::VehicleBuild* UBCProfileSubsystem::FindBuild(const FString& VehicleId) const
{
	return Active().FindVehicle(BC::ToStd(VehicleId));
}

void UBCProfileSubsystem::LocalResult(const crash::OpResult& Result, FDone Done)
{
	if (Result.ok)
	{
		SaveLocal();
		OnChanged.Broadcast();
	}
	if (Done)
	{
		Done(Result.ok, BC::ToFString(Result.error));
	}
}

namespace BCProfileDetail
{
crash::JsonValue Body(std::initializer_list<std::pair<const char*, crash::JsonValue>> Fields)
{
	crash::JsonValue J = crash::JsonValue::MakeObject();
	for (const auto& [Key, Value] : Fields)
	{
		J.Set(Key, Value);
	}
	return J;
}
} // namespace BCProfileDetail

void UBCProfileSubsystem::BuyVehicle(const FString& VehicleId, FDone Done)
{
	if (IsOnline())
	{
		UBCOnlineSubsystem::Get(this)->GarageOp(TEXT("/v1/garage/buy-vehicle"),
												BCProfileDetail::Body({{"vehicleId", crash::JsonValue(BC::ToStd(VehicleId))}}), Done);
		return;
	}
	LocalResult(crash::BuyVehicle(UBCDataSubsystem::Get(this)->Db(), LocalProfile, BC::ToStd(VehicleId)), Done);
}

void UBCProfileSubsystem::SelectVehicle(const FString& VehicleId, FDone Done)
{
	if (IsOnline())
	{
		UBCOnlineSubsystem::Get(this)->GarageOp(TEXT("/v1/garage/select"),
												BCProfileDetail::Body({{"vehicleId", crash::JsonValue(BC::ToStd(VehicleId))}}), Done);
		return;
	}
	LocalResult(crash::SelectVehicle(UBCDataSubsystem::Get(this)->Db(), LocalProfile, BC::ToStd(VehicleId)), Done);
}

void UBCProfileSubsystem::Upgrade(const FString& VehicleId, crash::UpgradeCategory Category, FDone Done)
{
	if (IsOnline())
	{
		UBCOnlineSubsystem::Get(this)->GarageOp(TEXT("/v1/garage/upgrade"),
												BCProfileDetail::Body({{"vehicleId", crash::JsonValue(BC::ToStd(VehicleId))},
																	   {"category", crash::JsonValue(crash::ToString(Category))}}),
												Done);
		return;
	}
	LocalResult(crash::UpgradeVehicle(UBCDataSubsystem::Get(this)->Db(), LocalProfile, BC::ToStd(VehicleId), Category), Done);
}

void UBCProfileSubsystem::BuyEquipment(const FString& ItemId, FDone Done)
{
	if (IsOnline())
	{
		UBCOnlineSubsystem::Get(this)->GarageOp(TEXT("/v1/garage/buy-equipment"),
												BCProfileDetail::Body({{"itemId", crash::JsonValue(BC::ToStd(ItemId))}}), Done);
		return;
	}
	LocalResult(crash::BuyEquipment(UBCDataSubsystem::Get(this)->Db(), LocalProfile, BC::ToStd(ItemId)), Done);
}

void UBCProfileSubsystem::Equip(const FString& VehicleId, crash::EquipSlot Slot, const FString& ItemId, FDone Done)
{
	if (IsOnline())
	{
		UBCOnlineSubsystem::Get(this)->GarageOp(
			TEXT("/v1/garage/equip"),
			BCProfileDetail::Body({{"vehicleId", crash::JsonValue(BC::ToStd(VehicleId))},
								   {"slot", crash::JsonValue(crash::ToString(Slot))},
								   {"itemId", ItemId.IsEmpty() ? crash::JsonValue() : crash::JsonValue(BC::ToStd(ItemId))}}),
			Done);
		return;
	}
	LocalResult(crash::EquipItem(UBCDataSubsystem::Get(this)->Db(), LocalProfile, BC::ToStd(VehicleId), Slot, BC::ToStd(ItemId)), Done);
}

void UBCProfileSubsystem::BuyCosmetic(const FString& ItemId, FDone Done)
{
	if (IsOnline())
	{
		UBCOnlineSubsystem::Get(this)->GarageOp(TEXT("/v1/garage/buy-cosmetic"),
												BCProfileDetail::Body({{"itemId", crash::JsonValue(BC::ToStd(ItemId))}}), Done);
		return;
	}
	LocalResult(crash::BuyCosmetic(UBCDataSubsystem::Get(this)->Db(), LocalProfile, BC::ToStd(ItemId)), Done);
}

void UBCProfileSubsystem::Customize(const FString& VehicleId, const FString& Paint, const FString& Wheels, FDone Done)
{
	if (IsOnline())
	{
		UBCOnlineSubsystem::Get(this)->GarageOp(
			TEXT("/v1/garage/customize"),
			BCProfileDetail::Body({{"vehicleId", crash::JsonValue(BC::ToStd(VehicleId))},
								   {"paint", Paint.IsEmpty() ? crash::JsonValue() : crash::JsonValue(BC::ToStd(Paint))},
								   {"wheels", Wheels.IsEmpty() ? crash::JsonValue() : crash::JsonValue(BC::ToStd(Wheels))}}),
			Done);
		return;
	}
	LocalResult(crash::CustomizeVehicle(UBCDataSubsystem::Get(this)->Db(), LocalProfile, BC::ToStd(VehicleId), BC::ToStd(Paint),
										BC::ToStd(Wheels)),
				Done);
}

void UBCProfileSubsystem::SetLoadout(const FString& Title, const FString& Banner, const FString& Horn, const FString& Sparks)
{
	LocalProfile.loadout.title = BC::ToStd(Title);
	LocalProfile.loadout.banner = BC::ToStd(Banner);
	LocalProfile.loadout.horn = BC::ToStd(Horn);
	LocalProfile.loadout.sparks = BC::ToStd(Sparks);
	SaveLocal();
	OnChanged.Broadcast();
}

void UBCProfileSubsystem::SetDisplayName(const FString& Name)
{
	const FString Trimmed = Name.TrimStartAndEnd().Left(16);
	if (Trimmed.Len() < 2)
	{
		return;
	}
	LocalProfile.displayName = BC::ToStd(Trimmed);
	SaveLocal();
	if (UBCOnlineSubsystem* Online = UBCOnlineSubsystem::Get(this); Online && Online->IsLoggedIn())
	{
		Online->GarageOp(TEXT("/v1/profile/name"), BCProfileDetail::Body({{"displayName", crash::JsonValue(BC::ToStd(Trimmed))}}),
						 nullptr);
	}
	OnChanged.Broadcast();
}

crash::MatchReward UBCProfileSubsystem::GrantMatchRewards(int32 Score, bool bWon, crash::LevelUpResult& OutLevelUp)
{
	const crash::GameDatabase& Db = UBCDataSubsystem::Get(this)->Db();
	const crash::MatchReward Reward = crash::ComputeMatchReward(Db.Progression(), Score, bWon, false);
	LocalProfile.credits += Reward.credits;
	OutLevelUp = crash::AddXp(Db, LocalProfile, Reward.xp);
	SaveLocal();
	OnChanged.Broadcast();
	return Reward;
}

void UBCProfileSubsystem::RecordMatchStats(bool bWon, int32 Takedowns, int32 Wrecked, float DamageDealt,
										   const TArray<int32>& ImpactCounts, float DistanceKm)
{
	crash::PlayerStats& S = LocalProfile.stats;
	S.matches += 1;
	S.wins += bWon ? 1 : 0;
	S.takedowns += Takedowns;
	S.wrecked += Wrecked;
	S.damageDealt += DamageDealt;
	S.distanceKm += DistanceKm;
	for (int32 i = 1; i < ImpactCounts.Num() && i < crash::kImpactKindCount; ++i)
	{
		if (ImpactCounts[i] > 0)
		{
			S.impacts[std::string(crash::ToString(static_cast<crash::ImpactKind>(i)))] += ImpactCounts[i];
		}
	}
	SaveLocal();
}

crash::LevelCompletion UBCProfileSubsystem::ApplyCampaignResult(const crash::LevelDef& Level, const crash::LevelRunResult& Run)
{
	const crash::LevelCompletion C = crash::ApplyLevelResult(UBCDataSubsystem::Get(this)->Db(), LocalProfile, Level, Run);
	SaveLocal();
	OnChanged.Broadcast();
	return C;
}

bool UBCProfileSubsystem::MarkSecret(const FString& ChapterId, const FString& SecretId)
{
	const bool bNew = crash::MarkHubSecret(LocalProfile, BC::ToStd(ChapterId), BC::ToStd(SecretId));
	if (bNew)
	{
		SaveLocal();
	}
	return bNew;
}

bool UBCProfileSubsystem::MarkHubCollectible(const FString& ChapterId, int32 Index)
{
	const bool bNew = crash::MarkHubCollectible(LocalProfile, BC::ToStd(ChapterId), Index);
	if (bNew)
	{
		SaveLocal();
	}
	return bNew;
}

void UBCProfileSubsystem::MarkStorySeen(const FString& Key)
{
	const std::string K = BC::ToStd(Key);
	std::vector<std::string>& Seen = LocalProfile.campaign.storySeen;
	if (std::find(Seen.begin(), Seen.end(), K) == Seen.end())
	{
		Seen.push_back(K);
		SaveLocal();
	}
}

bool UBCProfileSubsystem::IsStorySeen(const FString& Key) const
{
	const std::string K = BC::ToStd(Key);
	const std::vector<std::string>& Seen = LocalProfile.campaign.storySeen;
	return std::find(Seen.begin(), Seen.end(), K) != Seen.end();
}
