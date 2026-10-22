// Copyright BigCar Team. All Rights Reserved.
//
// Unreal automation tests (run headless in CI: UnrealEditor-Cmd ... -ExecCmds="Automation RunTests BigCar" -nullrhi).
// The engine-agnostic rules are unit-tested by CMake; these cover the Unreal integration: data loading through the
// engine file system, arena building, vehicle spawning, Chaos physics driving and AI-vs-AI crashes in a real world.

#include "Arena/BCArenaBuilder.h"
#include "Core/BCDataSubsystem.h"
#include "Core/BCGameInstance.h"
#include "Core/BigCarTypes.h"
#include "CrashCore/Database.h"
#include "CrashCore/Localization.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Gameplay/BCGameState.h"
#include "Gameplay/BCMatchGameMode.h"
#include "HAL/FileManager.h"
#include "Misc/AutomationTest.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Vehicle/BCVehicleDamage.h"
#include "Vehicle/BCVehicleMovement.h"
#include "Vehicle/BCVehiclePawn.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace BCTestDetail
{
bool ReadData(const FString& Name, std::string& Out)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *(FPaths::ProjectContentDir() / TEXT("BigCar/Data") / Name), FILEREAD_Silent))
	{
		return false;
	}
	Out.assign(reinterpret_cast<const char*>(Bytes.GetData()), static_cast<size_t>(Bytes.Num()));
	return true;
}

// Creates a standalone game instance + world with the given URL options and begins play.
struct FTestWorld
{
	UBCGameInstance* GameInstance = nullptr;
	UWorld* World = nullptr;

	bool Create(const FString& Options)
	{
		GameInstance = NewObject<UBCGameInstance>(GEngine);
		GameInstance->AddToRoot();
		GameInstance->InitializeStandalone();
		World = GameInstance->GetWorld();
		if (!World)
		{
			return false;
		}
		const FURL Url(nullptr, *(FString(TEXT("/Engine/Maps/Entry?")) + Options), TRAVEL_Absolute);
		World->SetGameMode(Url);
		World->InitializeActorsForPlay(Url);
		World->BeginPlay();
		return true;
	}

	void Step(float Seconds, float Dt = 1.0f / 60.0f)
	{
		const int32 Steps = FMath::CeilToInt(Seconds / Dt);
		for (int32 i = 0; i < Steps; ++i)
		{
			World->Tick(LEVELTICK_All, Dt);
		}
	}

	void Destroy()
	{
		if (World)
		{
			World->EndPlay(EEndPlayReason::Quit);
		}
		if (GameInstance)
		{
			GameInstance->Shutdown();
			GameInstance->RemoveFromRoot();
		}
		if (World && GEngine)
		{
			GEngine->DestroyWorldContext(World);
			World->DestroyWorld(false);
		}
		World = nullptr;
		GameInstance = nullptr;
	}
};
} // namespace BCTestDetail

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBCDataLoadTest, "BigCar.Data.LoadsWithoutErrors",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FBCDataLoadTest::RunTest(const FString& Parameters)
{
	crash::GameDatabase Db;
	const bool bOk = Db.LoadAll([](const std::string& Name, std::string& Text) { return BCTestDetail::ReadData(BC::ToFString(Name), Text); });
	for (const crash::LoadMessage& M : Db.Messages())
	{
		AddError(FString::Printf(TEXT("%s: %s"), *BC::ToFString(M.file), *BC::ToFString(M.message)));
	}
	TestTrue(TEXT("data loads"), bOk);
	TestTrue(TEXT("has vehicles"), Db.Vehicles().size() > 10);
	TestTrue(TEXT("has arenas"), !Db.Arenas().empty());
	TestTrue(TEXT("has campaign"), !Db.Campaign().chapters.empty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBCLocalizationTest, "BigCar.Data.LocalizationComplete",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FBCLocalizationTest::RunTest(const FString& Parameters)
{
	crash::GameDatabase Db;
	Db.LoadAll([](const std::string& Name, std::string& Text) { return BCTestDetail::ReadData(BC::ToFString(Name), Text); });
	crash::Localization Loc;
	for (const TCHAR* Lang : {TEXT("zh-Hans"), TEXT("en")})
	{
		for (const TCHAR* File : {TEXT("ui.json"), TEXT("content.json"), TEXT("story.json")})
		{
			std::string Text;
			if (BCTestDetail::ReadData(FString::Printf(TEXT("Localization/%s/%s"), Lang, File), Text))
			{
				std::string Error;
				TestTrue(FString::Printf(TEXT("%s/%s parses"), Lang, File), Loc.AddFile(BC::ToStd(Lang), Text, &Error));
			}
		}
	}
	for (const char* Lang : {"zh-Hans", "en"})
	{
		const std::vector<std::string> Missing = Loc.MissingKeys(Lang, Db.ReferencedLocKeys());
		for (size_t i = 0; i < Missing.size() && i < 20; ++i)
		{
			AddError(FString::Printf(TEXT("[%s] missing %s"), UTF8_TO_TCHAR(Lang), *BC::ToFString(Missing[i])));
		}
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBCArenaBuildTest, "BigCar.World.EveryArenaBuilds",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBCArenaBuildTest::RunTest(const FString& Parameters)
{
	BCTestDetail::FTestWorld W;
	if (!TestTrue(TEXT("world created"), W.Create(TEXT("game=/Script/BigCar.BCFrontEndGameMode"))))
	{
		W.Destroy();
		return false;
	}
	const UBCDataSubsystem* Data = W.GameInstance->GetSubsystem<UBCDataSubsystem>();
	TestNotNull(TEXT("data subsystem"), Data);
	if (Data)
	{
		for (const crash::ArenaDef& A : Data->Db().Arenas())
		{
			ABCArenaBuilder* Builder = W.World->SpawnActor<ABCArenaBuilder>();
			Builder->Build(BC::ToFString(A.id), true);
			TestTrue(FString::Printf(TEXT("arena %s has spawns"), *BC::ToFString(A.id)), Builder->SpawnTransforms(-1).Num() >= 12);
			// Spawn points must have floor under them.
			for (const FTransform& T : Builder->SpawnTransforms(-1))
			{
				FHitResult Hit;
				const FVector Start = T.GetLocation() + FVector(0, 0, 300.0f);
				const bool bHit = W.World->LineTraceSingleByChannel(Hit, Start, Start - FVector(0, 0, 1000.0f), ECC_Visibility);
				TestTrue(FString::Printf(TEXT("arena %s spawn has floor"), *BC::ToFString(A.id)), bHit);
			}
			Builder->Destroy();
		}
	}
	W.Destroy();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBCAiMatchTest, "BigCar.World.AiDerbyCrashes",
								 EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBCAiMatchTest::RunTest(const FString& Parameters)
{
	BCTestDetail::FTestWorld W;
	if (!TestTrue(TEXT("world created"), W.Create(TEXT("game=/Script/BigCar.BCMatchGameMode?Mode=derby?Bots=4?AI=elite?Time=90"))))
	{
		W.Destroy();
		return false;
	}
	const ABCMatchGameMode* GM = W.World->GetAuthGameMode<ABCMatchGameMode>();
	TestNotNull(TEXT("match game mode"), GM);
	W.Step(2.0f);
	int32 Vehicles = 0;
	for (ABCVehiclePawn* V : TActorRange<ABCVehiclePawn>(W.World))
	{
		++Vehicles;
		// Settled on the floor with all four wheels down, not sunk, not flying.
		TestTrue(TEXT("vehicle grounded"), V->GetVehicleMovement()->GroundedWheels() >= 3);
	}
	TestTrue(TEXT("bots spawned"), Vehicles >= 4);
	// Let the AI fight.
	W.Step(45.0f);
	float MinIntegrity = 1.0f;
	float MaxSpeed = 0.0f;
	for (ABCVehiclePawn* V : TActorRange<ABCVehiclePawn>(W.World))
	{
		MinIntegrity = FMath::Min(MinIntegrity, V->GetDamage()->Integrity());
		MaxSpeed = FMath::Max(MaxSpeed, V->GetVehicleMovement()->SpeedKmh());
		TestTrue(TEXT("finite location"), !V->GetActorLocation().ContainsNaN());
	}
	const ABCGameState* GS = W.World->GetGameState<ABCGameState>();
	const bool bSomethingHappened = MinIntegrity < 0.99f || (GS && GS->RecentFeed.Num() > 0) || (GS && GS->Phase == EBCMatchPhase::Finished);
	TestTrue(TEXT("AI cars crashed into each other (damage, takedowns or a finished derby)"), bSomethingHappened);
	W.Destroy();
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
