// Copyright BigCar Team. All Rights Reserved.

#include "Core/BCDataSubsystem.h"

#include "Core/BigCarTypes.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void UBCDataSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	Database = MakeUnique<crash::GameDatabase>();
	Loc = MakeUnique<crash::Localization>();

	const double Start = FPlatformTime::Seconds();
	bLoaded = Database->LoadAll([this](const std::string& Name, std::string& Text) {
		return ReadDataFile(BC::ToFString(Name), Text);
	});
	for (const crash::LoadMessage& M : Database->Messages())
	{
		if (M.severity == crash::LoadMessage::Severity::Error)
		{
			UE_LOG(LogBigCar, Error, TEXT("Data %s: %s"), *BC::ToFString(M.file), *BC::ToFString(M.message));
		}
		else
		{
			UE_LOG(LogBigCar, Warning, TEXT("Data %s: %s"), *BC::ToFString(M.file), *BC::ToFString(M.message));
		}
	}
	LoadLocalization();
	UE_LOG(LogBigCar, Log, TEXT("Game data loaded from %s in %.1f ms: %d vehicles, %d equipment, %d arenas, %d chapters (%s)"),
		   *DataDir(), (FPlatformTime::Seconds() - Start) * 1000.0, static_cast<int32>(Database->Vehicles().size()),
		   static_cast<int32>(Database->Equipment().size()), static_cast<int32>(Database->Arenas().size()),
		   static_cast<int32>(Database->Campaign().chapters.size()), bLoaded ? TEXT("ok") : TEXT("WITH ERRORS"));
}

void UBCDataSubsystem::Deinitialize()
{
	Database.Reset();
	Loc.Reset();
	Super::Deinitialize();
}

UBCDataSubsystem* UBCDataSubsystem::Get(const UObject* WorldContext)
{
	const UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	const UGameInstance* GI = World ? World->GetGameInstance() : nullptr;
	return GI ? GI->GetSubsystem<UBCDataSubsystem>() : nullptr;
}

FString UBCDataSubsystem::DataDir() const
{
	return FPaths::ProjectContentDir() / TEXT("BigCar/Data");
}

bool UBCDataSubsystem::ReadDataFile(const FString& RelativePath, std::string& OutText) const
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *(DataDir() / RelativePath), FILEREAD_Silent))
	{
		return false;
	}
	OutText.assign(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<size_t>(Bytes.Num()));
	return true;
}

void UBCDataSubsystem::LoadLocalization()
{
	Loc->Clear();
	const FString Root = DataDir() / TEXT("Localization");
	TArray<FString> LangDirs;
	IFileManager::Get().FindFiles(LangDirs, *(Root / TEXT("*")), false, true);
	for (const TCHAR* Known : {TEXT("zh-Hans"), TEXT("en")})
	{
		LangDirs.AddUnique(Known);
	}
	for (const FString& Lang : LangDirs)
	{
		TArray<FString> Files;
		IFileManager::Get().FindFiles(Files, *(Root / Lang / TEXT("*.json")), true, false);
		for (const TCHAR* Known : {TEXT("ui.json"), TEXT("content.json"), TEXT("story.json")})
		{
			Files.AddUnique(Known);
		}
		for (const FString& File : Files)
		{
			std::string Text;
			if (!ReadDataFile(TEXT("Localization") / Lang / File, Text))
			{
				continue;
			}
			std::string Error;
			if (!Loc->AddFile(BC::ToStd(Lang), Text, &Error))
			{
				UE_LOG(LogBigCar, Error, TEXT("Localization %s/%s: %s"), *Lang, *File, *BC::ToFString(Error));
			}
		}
	}
}

FText UBCDataSubsystem::Text(const std::string& Key) const
{
	return FText::FromString(BC::ToFString(Loc->Get(Key)));
}

FText UBCDataSubsystem::Text(const FString& Key) const
{
	return Text(BC::ToStd(Key));
}

FString UBCDataSubsystem::Str(const FString& Key) const
{
	return BC::ToFString(Loc->Get(BC::ToStd(Key)));
}

FText UBCDataSubsystem::Format(const FString& Key, const TArray<FString>& Args) const
{
	FString Pattern = Str(Key);
	for (int32 i = 0; i < Args.Num(); ++i)
	{
		Pattern = Pattern.Replace(*FString::Printf(TEXT("{%d}"), i), *Args[i]);
	}
	return FText::FromString(Pattern);
}

FString UBCDataSubsystem::Language() const
{
	return BC::ToFString(Loc->Language());
}

void UBCDataSubsystem::SetLanguage(const FString& Lang)
{
	Loc->SetLanguage(BC::ToStd(Lang));
}

TArray<FString> UBCDataSubsystem::Languages() const
{
	TArray<FString> Out;
	for (const std::string& L : Loc->Languages())
	{
		Out.Add(BC::ToFString(L));
	}
	return Out;
}

FText UBCDataSubsystem::BrandName(const crash::BrandDef& Brand) const
{
	if (Brand.licensed && !Brand.reference.empty())
	{
		return FText::FromString(BC::ToFString(Brand.reference));
	}
	return Text(Brand.name);
}

FText UBCDataSubsystem::VehicleName(const crash::VehicleDef& Vehicle) const
{
	const crash::BrandDef* Brand = Database->FindBrand(Vehicle.brand);
	if (Brand && Brand->licensed && !Vehicle.reference.empty())
	{
		return FText::FromString(BC::ToFString(Vehicle.reference));
	}
	const FString Model = BC::ToFString(Loc->Get(Vehicle.name));
	if (Brand)
	{
		const FString BrandText = BC::ToFString(Loc->Get(Brand->name));
		if (!Model.StartsWith(BrandText))
		{
			return FText::FromString(BrandText + TEXT(" ") + Model);
		}
	}
	return FText::FromString(Model);
}

FText UBCDataSubsystem::ItemName(const crash::ItemRef& Item) const
{
	switch (Item.type)
	{
	case crash::ItemType::Vehicle:
		if (const crash::VehicleDef* V = Database->FindVehicle(Item.id))
		{
			return VehicleName(*V);
		}
		break;
	case crash::ItemType::Equipment:
		if (const crash::EquipmentDef* E = Database->FindEquipment(Item.id))
		{
			return Text(E->name);
		}
		break;
	case crash::ItemType::Currency:
		return Format(Item.id == "gold" ? TEXT("ui.currency.gold_amount") : TEXT("ui.currency.credits_amount"),
					  {FString::Printf(TEXT("%lld"), static_cast<long long>(Item.amount))});
	default:
		if (const crash::CosmeticDef* C = Database->FindCosmetic(Item.id))
		{
			return Text(C->name);
		}
		break;
	}
	return FText::FromString(BC::ToFString(Item.id));
}

void UBCDataSubsystem::Dialogue(const std::string& Key, FText& OutSpeaker, FText& OutLine) const
{
	const auto [Speaker, Line] = crash::Localization::SplitDialogue(Loc->Get(Key));
	OutSpeaker = Speaker.empty() ? FText::GetEmpty() : FText::FromString(BC::ToFString(Loc->Get(Speaker)));
	OutLine = FText::FromString(BC::ToFString(Line));
}

FText BC::Loc(const UObject* WorldContext, const char* Key)
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(WorldContext);
	return Data ? Data->Text(std::string(Key)) : FText::FromString(UTF8_TO_TCHAR(Key));
}

FText BC::Loc(const UObject* WorldContext, const FString& Key)
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(WorldContext);
	return Data ? Data->Text(Key) : FText::FromString(Key);
}
