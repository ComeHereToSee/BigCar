// Copyright BigCar Team. All Rights Reserved.

#include "Arena/BCArenaProps.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/BCDataSubsystem.h"
#include "Core/BigCarTypes.h"
#include "CrashCore/VehicleSim.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Gameplay/BCMatchGameMode.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "ProceduralMeshComponent.h"
#include "Vehicle/BCBodyMesh.h"
#include "Vehicle/BCDebris.h"
#include "Vehicle/BCVehiclePawn.h"

// ---------------------------------------------------------------------------------------------
// Helpers

UStaticMesh* BCProps::Cube()
{
	static UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	return Mesh;
}

UStaticMesh* BCProps::Cylinder()
{
	static UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	return Mesh;
}

UStaticMesh* BCProps::Cone()
{
	static UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cone.Cone"));
	return Mesh;
}

UStaticMesh* BCProps::Sphere()
{
	static UStaticMesh* Mesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	return Mesh;
}

UMaterialInstanceDynamic* BCProps::Material(UObject* Outer, const FLinearColor& Color)
{
	static UMaterialInterface* Base = LoadObject<UMaterialInterface>(nullptr, TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	if (!Base)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(Base, Outer);
	Mid->SetVectorParameterValue(TEXT("Color"), Color);
	return Mid;
}

UStaticMeshComponent* BCProps::AddMesh(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& Relative,
									   const FLinearColor& Color, bool bCollision)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(Owner);
	C->SetStaticMesh(Mesh);
	C->SetMobility(EComponentMobility::Movable);
	if (Parent)
	{
		C->SetupAttachment(Parent);
	}
	C->SetRelativeTransform(Relative);
	C->SetMaterial(0, Material(Owner, Color));
	if (bCollision)
	{
		C->SetCollisionProfileName(TEXT("BlockAll"));
	}
	else
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	C->RegisterComponent();
	if (!Parent)
	{
		Owner->SetRootComponent(C);
	}
	return C;
}

namespace BCPropsDetail
{
ABCVehiclePawn* AsVehicle(AActor* A)
{
	return Cast<ABCVehiclePawn>(A);
}

ABCMatchGameMode* GameMode(const AActor* A)
{
	UWorld* World = A ? A->GetWorld() : nullptr;
	return World ? World->GetAuthGameMode<ABCMatchGameMode>() : nullptr;
}
} // namespace BCPropsDetail

// ---------------------------------------------------------------------------------------------
// Pads

ABCPadActor::ABCPadActor()
{
	bReplicates = false;
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	RootComponent = Trigger;
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->SetGenerateOverlapEvents(true);
}

void ABCPadActor::Setup(EBCPadType InType, const FVector2D& SizeCm, float Strength)
{
	Type = InType;
	StrengthMs = Strength;
	Trigger->SetBoxExtent(FVector(SizeCm.X * 0.5f, SizeCm.Y * 0.5f, 120.0f));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ABCPadActor::OnOverlap);
	const FLinearColor Color = Type == EBCPadType::Jump ? FLinearColor(0.1f, 0.8f, 1.0f) : FLinearColor(1.0f, 0.55f, 0.0f);
	BCProps::AddMesh(this, Trigger, BCProps::Cube(), FTransform(FRotator::ZeroRotator, FVector(0, 0, -115.0f), FVector(SizeCm.X / 100.0f, SizeCm.Y / 100.0f, 0.06f)),
					 Color * 0.6f, false);
	// Chevron arrows.
	for (int32 i = -1; i <= 1; ++i)
	{
		const FRotator Rot = Type == EBCPadType::Jump ? FRotator::ZeroRotator : FRotator(-90.0f, 0.0f, 0.0f);
		BCProps::AddMesh(this, Trigger, BCProps::Cone(), FTransform(Rot, FVector(i * SizeCm.X * 0.25f, 0.0f, -100.0f), FVector(0.6f, 0.6f, 0.25f)),
						 Color, false);
	}
}

void ABCPadActor::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep,
							const FHitResult& Sweep)
{
	ABCVehiclePawn* Vehicle = BCPropsDetail::AsVehicle(Other);
	if (!HasAuthority() || !Vehicle || Vehicle->IsWrecked())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	const TWeakObjectPtr<AActor> Key(Vehicle);
	if (const double* Last = Cooldowns.Find(Key); Last && Now - *Last < 1.0)
	{
		return;
	}
	Cooldowns.Add(Key, Now);
	const FVector Impulse = Type == EBCPadType::Jump ? FVector(0.0f, 0.0f, StrengthMs * 100.0f) : GetActorForwardVector() * StrengthMs * 100.0f;
	Vehicle->GetBody()->AddImpulse(Impulse, NAME_None, true);
}

// ---------------------------------------------------------------------------------------------
// Hazards

ABCHazardActor::ABCHazardActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(false); // deterministic motion from world time on every machine
	Pivot = CreateDefaultSubobject<USceneComponent>(TEXT("Pivot"));
	RootComponent = Pivot;
	Pivot->SetMobility(EComponentMobility::Movable);
	Tags.Add(TEXT("Hazard"));
}

void ABCHazardActor::SetupSpinner(float RadiusCm, float HeightCm, float DegPerSec, const FLinearColor& Color)
{
	bCrusher = false;
	Speed = DegPerSec;
	BCProps::AddMesh(this, Pivot, BCProps::Cylinder(), FTransform(FRotator::ZeroRotator, FVector(0, 0, HeightCm * 0.5f), FVector(1.2f, 1.2f, HeightCm / 100.0f)),
					 Color * 0.5f, true);
	Moving = BCProps::AddMesh(this, Pivot, BCProps::Cube(), FTransform(FRotator::ZeroRotator, FVector(0, 0, HeightCm * 0.5f), FVector::OneVector), Color, true);
	// Arm: child of the rotating cube's space is avoided; scale the cube itself into a long bar.
	Moving->SetRelativeScale3D(FVector(RadiusCm * 2.0f / 100.0f, 0.6f, 0.6f));
}

void ABCHazardActor::SetupCrusher(const FVector& SizeCm, float PeriodSec, float TravelCm, const FLinearColor& Color)
{
	bCrusher = true;
	Period = FMath::Max(PeriodSec, 0.5f);
	Travel = TravelCm;
	// Frame posts.
	for (const float Side : {-1.0f, 1.0f})
	{
		BCProps::AddMesh(this, Pivot, BCProps::Cube(),
						 FTransform(FRotator::ZeroRotator, FVector(0.0f, Side * (SizeCm.Y * 0.5f + 40.0f), (TravelCm + SizeCm.Z) * 0.5f),
									FVector(0.6f, 0.6f, (TravelCm + SizeCm.Z) / 100.0f)),
						 FLinearColor(0.1f, 0.1f, 0.1f), true);
	}
	Moving = BCProps::AddMesh(this, Pivot, BCProps::Cube(), FTransform(FRotator::ZeroRotator, FVector(0, 0, TravelCm + SizeCm.Z * 0.5f), SizeCm / 100.0f),
							  Color, true);
	BaseLocation = Moving->GetRelativeLocation();
}

void ABCHazardActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!Moving)
	{
		return;
	}
	const float T = GetWorld()->GetTimeSeconds();
	if (bCrusher)
	{
		// Slow rise, fast slam.
		const float Cycle = FMath::Fmod(T, Period) / Period;
		const float Height = Cycle < 0.8f ? Cycle / 0.8f : 1.0f - (Cycle - 0.8f) / 0.2f;
		Moving->SetRelativeLocation(FVector(BaseLocation.X, BaseLocation.Y, BaseLocation.Z - Travel * (1.0f - Height)));
	}
	else
	{
		Moving->SetRelativeRotation(FRotator(0.0f, FMath::Fmod(T * Speed, 360.0f), 0.0f));
	}
}

// ---------------------------------------------------------------------------------------------
// Breakables

bool ABCBreakableProp::TakeHit(ABCVehiclePawn* By, float ClosingSpeed, const FVector& WorldPoint)
{
	if (bBroken || !HasAuthority())
	{
		return false;
	}
	Health -= ClosingSpeed / FMath::Max(BreakSpeed, 0.1f);
	if (Health > 0.0f)
	{
		return false;
	}
	bBroken = true;
	MulticastBreak(WorldPoint);
	if (ABCMatchGameMode* GM = BCPropsDetail::GameMode(this))
	{
		GM->NotifyPropDestroyed(this, By);
	}
	return true;
}

void ABCBreakableProp::MulticastBreak_Implementation(FVector_NetQuantize Point)
{
	OnBroken(Point);
}

void ABCBreakableProp::OnBroken(const FVector& Point)
{
	ABCDebris::Burst(GetWorld(), Point, FVector::UpVector, DebrisColor, 8, 25.0f, 600.0f, 6.0f);
	SetLifeSpan(0.1f);
}

ABCCrateActor::ABCCrateActor()
{
	bReplicates = true;
	SetReplicateMovement(true);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetStaticMesh(BCProps::Cube());
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetMobility(EComponentMobility::Movable);
	BreakSpeed = 7.0f;
}

void ABCCrateActor::Setup(float EdgeCm, const FLinearColor& Color)
{
	Mesh->SetWorldScale3D(FVector(EdgeCm / 100.0f));
	Mesh->SetMaterial(0, BCProps::Material(this, Color));
	DebrisColor = Color;
	Mesh->SetSimulatePhysics(true);
	Mesh->SetMassOverrideInKg(NAME_None, FMath::Clamp(EdgeCm * 0.6f, 15.0f, 200.0f), true);
}

void ABCCrateActor::OnBroken(const FVector& Point)
{
	ABCDebris::Burst(GetWorld(), GetActorLocation(), FVector::UpVector, DebrisColor, 10, Mesh->GetComponentScale().X * 30.0f, 700.0f, 8.0f);
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetLifeSpan(0.5f);
	(void)Point;
}

ABCBarrelActor::ABCBarrelActor()
{
	bReplicates = true;
	SetReplicateMovement(true);
	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	RootComponent = Mesh;
	Mesh->SetStaticMesh(BCProps::Cylinder());
	Mesh->SetCollisionProfileName(TEXT("PhysicsActor"));
	Mesh->SetMobility(EComponentMobility::Movable);
	Mesh->SetWorldScale3D(FVector(0.6f, 0.6f, 0.9f));
	BreakSpeed = 4.0f;
	DebrisColor = FLinearColor(0.9f, 0.1f, 0.05f);
	Tags.Add(TEXT("Explosive"));
}

void ABCBarrelActor::OnBroken(const FVector& Point)
{
	ABCDebris::Burst(GetWorld(), GetActorLocation(), FVector::UpVector, FLinearColor(1.0f, 0.5f, 0.05f), 24, 12.0f, 1600.0f, 1.0f);
	ABCDebris::Burst(GetWorld(), GetActorLocation(), FVector::UpVector, DebrisColor, 6, 25.0f, 1200.0f, 6.0f);
	if (HasAuthority())
	{
		if (ABCMatchGameMode* GM = BCPropsDetail::GameMode(this))
		{
			GM->Explosion(GetActorLocation(), 800.0f, 9.0f, 350.0f);
		}
	}
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);
	SetLifeSpan(0.5f);
	(void)Point;
}

ABCWreckProp::ABCWreckProp()
{
	bReplicates = true;
	SetReplicateMovement(true);
	Box = CreateDefaultSubobject<UBoxComponent>(TEXT("Box"));
	RootComponent = Box;
	Box->SetCollisionProfileName(TEXT("PhysicsActor"));
	Box->SetHiddenInGame(true);
	Shell = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("Shell"));
	Shell->SetupAttachment(Box);
	Shell->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BreakSpeed = 25.0f;
	Health = 3.0f;
}

void ABCWreckProp::Setup(const FString& VehicleClass, const FLinearColor& Color)
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	const crash::VehicleDef* Pick = nullptr;
	if (Data)
	{
		crash::VehicleClass Wanted = crash::VehicleClass::Sedan;
		crash::FromString(BC::ToStd(VehicleClass), Wanted);
		for (const crash::VehicleDef& V : Data->Db().Vehicles())
		{
			if (V.vehicleClass == Wanted)
			{
				Pick = &V;
				break;
			}
		}
		if (!Pick && !Data->Db().Vehicles().empty())
		{
			Pick = &Data->Db().Vehicles().front();
		}
	}
	if (!Pick)
	{
		return;
	}
	const float Height = Pick->dimensions.height - Pick->dimensions.groundClearance;
	const float Center = Pick->dimensions.groundClearance + Height * 0.5f;
	Box->SetBoxExtent(FVector(Pick->dimensions.length * 50.0f, Pick->dimensions.width * 50.0f, Height * 50.0f));
	FBCBodyMesh ShellMesh;
	ShellMesh.Build(*Pick, Center, 10);
	// Pre-dented: wrecks look wrecked.
	ShellMesh.ApplyDent(FVector(Pick->dimensions.length * 50.0f, 30.0f, 0.0f), FVector(-1, 0, 0), 120.0f, 30.0f);
	ShellMesh.ApplyDent(FVector(-30.0f, Pick->dimensions.width * 50.0f, 10.0f), FVector(0, -1, 0), 100.0f, 25.0f);
	ShellMesh.CreateSections(Shell);
	Shell->SetMaterial(0, BCProps::Material(this, Color));
	Shell->SetMaterial(1, BCProps::Material(this, FLinearColor(0.02f, 0.02f, 0.02f)));
	DebrisColor = Color;
	Box->SetSimulatePhysics(true);
	Box->SetMassOverrideInKg(NAME_None, Pick->massKg * 0.8f, true);
}

void ABCWreckProp::OnBroken(const FVector& Point)
{
	ABCDebris::Burst(GetWorld(), Point, FVector::UpVector, DebrisColor, 12, 30.0f, 800.0f, 8.0f);
	SetLifeSpan(6.0f); // flattened wreck stays a moment as an obstacle
}

ABCFenceActor::ABCFenceActor()
{
	bReplicates = true;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	BreakSpeed = 3.0f;
	Tags.Add(TEXT("Soft"));
}

void ABCFenceActor::Setup(const FVector& From, const FVector& To, float HeightCm, const FLinearColor& Color)
{
	SetActorLocation(From);
	const FVector Delta = To - From;
	const float Length = Delta.Size2D();
	const FRotator Yaw = Delta.Rotation();
	const int32 Count = FMath::Max(1, FMath::RoundToInt(Length / 300.0f));
	DebrisColor = Color;
	for (int32 i = 0; i < Count; ++i)
	{
		const float Along = (static_cast<float>(i) + 0.5f) * Length / Count;
		UStaticMeshComponent* Board = BCProps::AddMesh(this, RootComponent, BCProps::Cube(),
													   FTransform(Yaw, Yaw.RotateVector(FVector(Along, 0.0f, HeightCm * 0.5f)),
																  FVector(Length / Count / 100.0f * 0.95f, 0.08f, HeightCm / 100.0f)),
													   Color, true);
		Boards.Add(Board);
	}
}

void ABCFenceActor::OnBroken(const FVector& Point)
{
	for (UStaticMeshComponent* Board : Boards)
	{
		if (Board && FVector::Dist(Board->GetComponentLocation(), Point) < 600.0f)
		{
			Board->SetCollisionProfileName(TEXT("PhysicsActor"));
			Board->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Ignore);
			Board->SetSimulatePhysics(true);
			Board->AddImpulse(FVector(FMath::FRandRange(-300.0f, 300.0f), FMath::FRandRange(-300.0f, 300.0f), 400.0f), NAME_None, true);
		}
	}
	SetLifeSpan(8.0f);
}

// ---------------------------------------------------------------------------------------------
// Pit

ABCPitActor::ABCPitActor()
{
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	RootComponent = Root;
	Tags.Add(TEXT("Pit"));
}

void ABCPitActor::Setup(const FVector2D& SizeCm, bool bVisualHole)
{
	Extent = SizeCm * 0.5f;
	if (bVisualHole)
	{
		// Dark "hole" decal plane for floors that cannot be cut (round arenas).
		BCProps::AddMesh(this, RootComponent, BCProps::Cube(), FTransform(FRotator::ZeroRotator, FVector(0, 0, 1.0f), FVector(SizeCm.X / 100.0f, SizeCm.Y / 100.0f, 0.02f)),
						 FLinearColor(0.004f, 0.004f, 0.004f), false);
	}
	// Warning stripes around the edge.
	for (const float Side : {-1.0f, 1.0f})
	{
		BCProps::AddMesh(this, RootComponent, BCProps::Cube(), FTransform(FRotator::ZeroRotator, FVector(Side * (Extent.X + 20.0f), 0, 2.0f), FVector(0.3f, SizeCm.Y / 100.0f, 0.04f)),
						 FLinearColor(1.0f, 0.75f, 0.0f), false);
		BCProps::AddMesh(this, RootComponent, BCProps::Cube(), FTransform(FRotator::ZeroRotator, FVector(0, Side * (Extent.Y + 20.0f), 2.0f), FVector(SizeCm.X / 100.0f, 0.3f, 0.04f)),
						 FLinearColor(1.0f, 0.75f, 0.0f), false);
	}
}

bool ABCPitActor::Contains(const FVector& WorldPoint) const
{
	const FVector Local = GetActorTransform().InverseTransformPosition(WorldPoint);
	return FMath::Abs(Local.X) < Extent.X && FMath::Abs(Local.Y) < Extent.Y && Local.Z < 150.0f;
}

// ---------------------------------------------------------------------------------------------
// Collectibles / secrets

ABCCollectibleActor::ABCCollectibleActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	RootComponent = Trigger;
	Trigger->SetBoxExtent(FVector(180.0f));
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ABCCollectibleActor::OnOverlap);
	Visual = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Trigger);
	Visual->SetStaticMesh(BCProps::Cone());
	Visual->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Visual->SetRelativeScale3D(FVector(0.9f, 0.9f, 0.9f));
	Glow = CreateDefaultSubobject<UPointLightComponent>(TEXT("Glow"));
	Glow->SetupAttachment(Trigger);
	Glow->SetLightColor(FLinearColor(1.0f, 0.8f, 0.2f));
	Glow->SetIntensity(4000.0f);
	Glow->SetAttenuationRadius(600.0f);
	Glow->SetCastShadows(false);
}

void ABCCollectibleActor::BeginPlay()
{
	Super::BeginPlay();
	Visual->SetMaterial(0, BCProps::Material(this, FLinearColor(1.0f, 0.75f, 0.1f)));
}

void ABCCollectibleActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Time += DeltaSeconds;
	Visual->SetRelativeLocationAndRotation(FVector(0, 0, 20.0f * FMath::Sin(Time * 2.0f)), FRotator(0.0f, Time * 120.0f, 0.0f));
}

void ABCCollectibleActor::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep,
									const FHitResult& Sweep)
{
	ABCVehiclePawn* Vehicle = BCPropsDetail::AsVehicle(Other);
	if (bTaken || !HasAuthority() || !Vehicle || !Vehicle->IsPlayerControlled())
	{
		return;
	}
	bTaken = true;
	if (ABCMatchGameMode* GM = BCPropsDetail::GameMode(this))
	{
		GM->OnCollectiblePicked(this, Vehicle);
	}
	ABCDebris::Burst(GetWorld(), GetActorLocation(), FVector::UpVector, FLinearColor(1.0f, 0.8f, 0.2f), 14, 8.0f, 700.0f, 1.0f);
	Destroy();
}

ABCSecretZone::ABCSecretZone()
{
	bReplicates = false;
	Trigger = CreateDefaultSubobject<UBoxComponent>(TEXT("Trigger"));
	RootComponent = Trigger;
	Trigger->SetCollisionProfileName(TEXT("OverlapAllDynamic"));
}

void ABCSecretZone::Setup(const FString& InSecretId, float RadiusCm)
{
	SecretId = InSecretId;
	Trigger->SetBoxExtent(FVector(RadiusCm, RadiusCm, 400.0f));
	Trigger->OnComponentBeginOverlap.AddDynamic(this, &ABCSecretZone::OnOverlap);
}

void ABCSecretZone::OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep,
							  const FHitResult& Sweep)
{
	ABCVehiclePawn* Vehicle = BCPropsDetail::AsVehicle(Other);
	if (bFound || !HasAuthority() || !Vehicle || !Vehicle->IsPlayerControlled())
	{
		return;
	}
	bFound = true;
	if (ABCMatchGameMode* GM = BCPropsDetail::GameMode(this))
	{
		GM->OnSecretFound(this, Vehicle);
	}
}
