// Copyright BigCar Team. All Rights Reserved.

#include "Gameplay/BCFrontEndGameMode.h"

#include "Arena/BCArenaProps.h"
#include "Camera/CameraComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Core/BCProfileSubsystem.h"
#include "Engine/World.h"
#include "Gameplay/BCGameState.h"
#include "Gameplay/BCPlayerController.h"
#include "Gameplay/BCPlayerState.h"
#include "Vehicle/BCVehiclePawn.h"

ABCShowroom::ABCShowroom()
{
	PrimaryActorTick.bCanEverTick = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Root);
	Camera->SetRelativeLocationAndRotation(FVector(-720.0f, -330.0f, 210.0f), FRotator(-9.0f, 24.0f, 0.0f));
	Camera->SetFieldOfView(55.0f);
}

void ABCShowroom::BeginPlay()
{
	Super::BeginPlay();
	// Turntable floor, backdrop and studio lights.
	BCProps::AddMesh(this, RootComponent, BCProps::Cylinder(), FTransform(FRotator::ZeroRotator, FVector(0, 0, -5.0f), FVector(9.0f, 9.0f, 0.1f)),
					 FLinearColor(0.08f, 0.08f, 0.09f), false);
	BCProps::AddMesh(this, RootComponent, BCProps::Cube(), FTransform(FRotator::ZeroRotator, FVector(0, 0, -20.0f), FVector(60.0f, 60.0f, 0.2f)),
					 FLinearColor(0.02f, 0.02f, 0.025f), false);
	BCProps::AddMesh(this, RootComponent, BCProps::Cube(), FTransform(FRotator::ZeroRotator, FVector(1500.0f, 0, 600.0f), FVector(0.2f, 60.0f, 14.0f)),
					 FLinearColor(0.03f, 0.03f, 0.04f), false);
	for (int32 i = 0; i < 3; ++i)
	{
		USpotLightComponent* Spot = NewObject<USpotLightComponent>(this);
		Spot->SetupAttachment(RootComponent);
		Spot->SetMobility(EComponentMobility::Movable);
		const float A = UE_TWO_PI * i / 3.0f;
		const FVector Pos(FMath::Cos(A) * 700.0f, FMath::Sin(A) * 700.0f, 650.0f);
		Spot->SetRelativeLocation(Pos);
		Spot->SetWorldRotation((-Pos).Rotation());
		Spot->SetIntensity(i == 0 ? 120000.0f : 60000.0f);
		Spot->SetAttenuationRadius(2500.0f);
		Spot->SetOuterConeAngle(40.0f);
		Spot->SetLightColor(i == 1 ? FLinearColor(1.0f, 0.55f, 0.25f) : FLinearColor(0.85f, 0.9f, 1.0f));
		Spot->RegisterComponent();
	}
	USkyLightComponent* Sky = NewObject<USkyLightComponent>(this);
	Sky->SetupAttachment(RootComponent);
	Sky->SetMobility(EComponentMobility::Movable);
	Sky->bRealTimeCapture = true;
	Sky->SetIntensity(0.4f);
	Sky->RegisterComponent();
	USkyAtmosphereComponent* Atmosphere = NewObject<USkyAtmosphereComponent>(this);
	Atmosphere->SetupAttachment(RootComponent);
	Atmosphere->RegisterComponent();
	UDirectionalLightComponent* Sun = NewObject<UDirectionalLightComponent>(this);
	Sun->SetupAttachment(RootComponent);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetWorldRotation(FRotator(-6.0f, 150.0f, 0.0f));
	Sun->SetIntensity(2.0f);
	Sun->SetLightColor(FLinearColor(1.0f, 0.6f, 0.4f));
	Sun->SetAtmosphereSunLight(true);
	Sun->RegisterComponent();

	if (const UBCProfileSubsystem* Profile = UBCProfileSubsystem::Get(this))
	{
		ShowBuild(Profile->SelectedBuild());
	}
}

void ABCShowroom::ShowBuild(const FBCVehicleBuild& Build)
{
	if (!Build.IsValid())
	{
		return;
	}
	if (!Display)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Display = GetWorld()->SpawnActorDeferred<ABCVehiclePawn>(ABCVehiclePawn::StaticClass(), GetActorTransform(), this, nullptr,
																 ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Display)
		{
			return;
		}
		Display->InitVehicle(Build, 0, true);
		Display->FinishSpawning(GetActorTransform());
	}
	else
	{
		Display->SetDisplayBuild(Build);
	}
	// Sit the wheels on the turntable.
	if (const crash::VehicleDef* Def = Display->GetDef())
	{
		const float Height = Def->dimensions.groundClearance + (Def->dimensions.height - Def->dimensions.groundClearance) * 0.5f;
		Display->SetActorLocation(GetActorLocation() + FVector(0, 0, Height * 100.0f - Def->dimensions.wheelRadius * 0.0f));
	}
}

void ABCShowroom::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Spin += DeltaSeconds * 12.0f;
	if (Display)
	{
		Display->SetActorRotation(FRotator(0.0f, Spin, 0.0f));
	}
}

ABCFrontEndGameMode::ABCFrontEndGameMode()
{
	PlayerControllerClass = ABCPlayerController::StaticClass();
	PlayerStateClass = ABCPlayerState::StaticClass();
	GameStateClass = ABCGameState::StaticClass();
	DefaultPawnClass = nullptr;
}

void ABCFrontEndGameMode::BeginPlay()
{
	Super::BeginPlay();
	if (!Showroom)
	{
		Showroom = GetWorld()->SpawnActor<ABCShowroom>(FVector::ZeroVector, FRotator::ZeroRotator);
	}
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (APlayerController* PC = It->Get())
		{
			PC->SetViewTarget(Showroom);
		}
	}
}

void ABCFrontEndGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	// No pawn in the menus: view the showroom camera.
	if (!Showroom)
	{
		Showroom = GetWorld()->SpawnActor<ABCShowroom>(FVector::ZeroVector, FRotator::ZeroRotator);
	}
	NewPlayer->SetViewTarget(Showroom);
}
