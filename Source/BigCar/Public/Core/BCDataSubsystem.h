// Copyright BigCar Team. All Rights Reserved.
//
// Owns the game database (vehicles, equipment, arenas, campaign…) and the localization tables, both loaded from
// Content/BigCar/Data through the engine-agnostic CrashCore library.
#pragma once

#include "CoreMinimal.h"
#include "CrashCore/Database.h"
#include "CrashCore/Localization.h"
#include "Subsystems/GameInstanceSubsystem.h"

#include "BCDataSubsystem.generated.h"

UCLASS()
class BIGCAR_API UBCDataSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	static UBCDataSubsystem* Get(const UObject* WorldContext);

	bool IsLoaded() const { return bLoaded; }
	const crash::GameDatabase& Db() const { return *Database; }
	const crash::Localization& Strings() const { return *Loc; }
	FString DataDir() const;

	// Localization (current language → zh-Hans → key).
	FText Text(const FString& Key) const;
	FText Text(const std::string& Key) const;
	FString Str(const FString& Key) const;
	FText Format(const FString& Key, const TArray<FString>& Args) const;
	FString Language() const;
	void SetLanguage(const FString& Lang);
	TArray<FString> Languages() const;

	// Display names honoring brand licensing (fictional names until a license exists).
	FText VehicleName(const crash::VehicleDef& Vehicle) const;
	FText BrandName(const crash::BrandDef& Brand) const;
	FText ItemName(const crash::ItemRef& Item) const;
	// "Speaker|Line" dialogue entries resolved to display text.
	void Dialogue(const std::string& Key, FText& OutSpeaker, FText& OutLine) const;

	bool ReadDataFile(const FString& RelativePath, std::string& OutText) const;

private:
	void LoadLocalization();

	TUniquePtr<crash::GameDatabase> Database;
	TUniquePtr<crash::Localization> Loc;
	bool bLoaded = false;
};

// Shorthand for UI code: BC::Loc(this, key) with a ui.* key.
namespace BC
{
BIGCAR_API FText Loc(const UObject* WorldContext, const char* Key);
BIGCAR_API FText Loc(const UObject* WorldContext, const FString& Key);
} // namespace BC
