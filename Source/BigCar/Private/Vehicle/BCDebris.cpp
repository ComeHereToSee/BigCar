// Copyright BigCar Team. All Rights Reserved.

#include "Vehicle/BCDebris.h"

#include "Components/StaticMeshComponent.h"
#include "Core/BCSettingsSubsystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

int32 ABCDebris::LiveCount = 0;

ABCDebris::ABCDebris()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Mesh->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Mesh->SetCastShadow(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (Cube.Succeeded())
	{
		Mesh->SetStaticMesh(Cube.Object);
	}
}

void ABCDebris::EndPlay(const EEndPlayReason::Type Reason)
{
	LiveCount = FMath::Max(0, LiveCount - 1);
	Super::EndPlay(Reason);
}

void ABCDebris::Burst(UWorld* World, const FVector& Location, const FVector& Direction, const FLinearColor& Color, int32 Count,
					  float SizeCm, float Speed, float LifeSeconds, int32 Shape)
{
	if (!World || World->GetNetMode() == NM_DedicatedServer || Count <= 0)
	{
		return;
	}
	const UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(World);
	const int32 MaxDebris = Settings ? FMath::RoundToInt(Settings->GameValue(TEXT("maxDebris"), 64.0f)) : 64;
	static UStaticMesh* Cylinder = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static UMaterialInterface* BaseMat = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	const FVector Dir = Direction.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	for (int32 i = 0; i < Count && LiveCount < MaxDebris; ++i)
	{
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		const FRotator Rot(FMath::FRandRange(0.0, 360.0), FMath::FRandRange(0.0, 360.0), FMath::FRandRange(0.0, 360.0));
		ABCDebris* D = World->SpawnActor<ABCDebris>(Location + FMath::VRand() * SizeCm, Rot, Params);
		if (!D)
		{
			continue;
		}
		++LiveCount;
		if (Shape == 1 && Cylinder)
		{
			D->Mesh->SetStaticMesh(Cylinder);
		}
		const float Jitter = FMath::FRandRange(0.6f, 1.4f);
		D->Mesh->SetWorldScale3D(FVector(SizeCm / 100.0f * Jitter, SizeCm / 100.0f * FMath::FRandRange(0.4f, 1.2f), SizeCm / 100.0f * 0.3f * Jitter));
		if (BaseMat)
		{
			UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMat, D);
			Mid->SetVectorParameterValue(TEXT("Color"), Color);
			D->Mesh->SetMaterial(0, Mid);
		}
		D->Mesh->SetSimulatePhysics(true);
		D->Mesh->SetMassOverrideInKg(NAME_None, FMath::Max(0.5f, SizeCm * 0.2f), true);
		const FVector V = FMath::VRandCone(Dir, FMath::DegreesToRadians(55.0f)) * Speed * FMath::FRandRange(0.5f, 1.2f) + FVector(0, 0, Speed * 0.3f);
		D->Mesh->SetPhysicsLinearVelocity(V);
		D->Mesh->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * 720.0f);
		D->SetLifeSpan(LifeSeconds * FMath::FRandRange(0.7f, 1.3f));
	}
}
