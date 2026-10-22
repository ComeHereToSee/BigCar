// Copyright BigCar Team. All Rights Reserved.

#include "Vehicle/BCVehiclePawn.h"

#include "Audio/BCSynthComponents.h"
#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/BCDataSubsystem.h"
#include "Core/BCSettingsSubsystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "GameFramework/SpringArmComponent.h"
#include "Gameplay/BCGameState.h"
#include "Gameplay/BCMatchGameMode.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Net/UnrealNetwork.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ProceduralMeshComponent.h"
#include "UObject/ConstructorHelpers.h"
#include "Vehicle/BCDebris.h"
#include "Vehicle/BCVehicleDamage.h"
#include "Vehicle/BCVehicleMovement.h"

namespace BCPawnDetail
{
const FLinearColor TireColor(0.02f, 0.02f, 0.022f);
const FLinearColor GlassColor(0.02f, 0.03f, 0.045f);
const FLinearColor WreckColor(0.05f, 0.04f, 0.035f);

int32 GuessCylinders(const crash::VehicleDef& Def)
{
	if (Def.IsElectric())
	{
		return 1;
	}
	if (Def.powerKw < 110.0f)
	{
		return 4;
	}
	if (Def.powerKw < 230.0f)
	{
		return 6;
	}
	if (Def.powerKw < 450.0f)
	{
		return 8;
	}
	return Def.powerKw < 600.0f ? 10 : 12;
}
} // namespace BCPawnDetail

ABCVehiclePawn::ABCVehiclePawn()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PrePhysics;
	bReplicates = true;
	SetReplicateMovement(true);
	AutoPossessAI = EAutoPossessAI::Disabled;

	Body = CreateDefaultSubobject<UBoxComponent>(TEXT("Body"));
	RootComponent = Body;
	Body->SetBoxExtent(FVector(220.0f, 90.0f, 60.0f), false);
	Body->SetCollisionProfileName(TEXT("Vehicle"));
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	Body->SetNotifyRigidBodyCollision(true);
	Body->SetUseCCD(true);
	Body->SetLinearDamping(0.02f);
	Body->SetAngularDamping(0.25f);
	Body->SetHiddenInGame(true);
	Body->SetCanEverAffectNavigation(false);

	BodyMesh = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("BodyMesh"));
	BodyMesh->SetupAttachment(Body);
	BodyMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	BodyMesh->SetCastShadow(true);

	VehicleMovement = CreateDefaultSubobject<UBCVehicleMovement>(TEXT("VehicleMovement"));
	Damage = CreateDefaultSubobject<UBCVehicleDamage>(TEXT("Damage"));

	CameraArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraArm"));
	CameraArm->SetupAttachment(Body);
	CameraArm->SetRelativeRotation(FRotator(-11.0f, 0.0f, 0.0f));
	CameraArm->TargetArmLength = 650.0f;
	CameraArm->SocketOffset = FVector(0.0f, 0.0f, 170.0f);
	CameraArm->bUsePawnControlRotation = false;
	CameraArm->bInheritPitch = false;
	CameraArm->bInheritRoll = false;
	CameraArm->bInheritYaw = true;
	CameraArm->bDoCollisionTest = true;
	CameraArm->ProbeSize = 14.0f;
	CameraArm->bEnableCameraLag = true;
	CameraArm->CameraLagSpeed = 9.0f;
	CameraArm->CameraLagMaxDistance = 400.0f;
	CameraArm->bEnableCameraRotationLag = true;
	CameraArm->CameraRotationLagSpeed = 6.0f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(CameraArm, USpringArmComponent::SocketName);
	Camera->SetFieldOfView(90.0f);

	EngineAudio = CreateDefaultSubobject<UBCEngineSynthComponent>(TEXT("EngineAudio"));
	EngineAudio->SetupAttachment(Body);
	SkidAudio = CreateDefaultSubobject<UBCSkidSynthComponent>(TEXT("SkidAudio"));
	SkidAudio->SetupAttachment(Body);
	ImpactAudio = CreateDefaultSubobject<UBCImpactSynthComponent>(TEXT("ImpactAudio"));
	ImpactAudio->SetupAttachment(Body);

	FireLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FireLight"));
	FireLight->SetupAttachment(Body);
	FireLight->SetLightColor(FLinearColor(1.0f, 0.38f, 0.08f));
	FireLight->SetIntensity(0.0f);
	FireLight->SetAttenuationRadius(700.0f);
	FireLight->SetCastShadows(false);
	FireLight->SetVisibility(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeFinder(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> CylinderFinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> ConeFinder(TEXT("/Engine/BasicShapes/Cone.Cone"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> MaterialFinder(TEXT("/Engine/BasicShapes/BasicShapeMaterial.BasicShapeMaterial"));
	CubeMesh = CubeFinder.Object;
	CylinderMesh = CylinderFinder.Object;
	ConeMesh = ConeFinder.Object;
	BaseMaterial = MaterialFinder.Object;
}

void ABCVehiclePawn::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ABCVehiclePawn, Build);
	DOREPLIFETIME(ABCVehiclePawn, Team);
	DOREPLIFETIME(ABCVehiclePawn, ParticipantId);
	DOREPLIFETIME_CONDITION(ABCVehiclePawn, RepControls, COND_SkipOwner);
	DOREPLIFETIME(ABCVehiclePawn, bControlsEnabled);
	DOREPLIFETIME(ABCVehiclePawn, bBoss);
}

void ABCVehiclePawn::InitVehicle(const FBCVehicleBuild& InBuild, int32 InTeam, bool bInDisplayOnly)
{
	Build = InBuild;
	Team = InTeam;
	bDisplayOnly = bInDisplayOnly;
	if (HasActorBegunPlay())
	{
		BuildVehicle();
	}
}

void ABCVehiclePawn::SetDisplayBuild(const FBCVehicleBuild& InBuild)
{
	Build = InBuild;
	bDisplayOnly = true;
	BuildVehicle();
}

void ABCVehiclePawn::BeginPlay()
{
	Super::BeginPlay();
	if (Build.IsValid())
	{
		BuildVehicle();
	}
	if (HasAuthority() && !bDisplayOnly)
	{
		Body->OnComponentHit.AddDynamic(this, &ABCVehiclePawn::OnBodyHit);
	}
	VehicleMovement->OnLanded = [this](float Seconds) { HandleLanded(Seconds); };
	Damage->OnChanged.AddUObject(this, &ABCVehiclePawn::UpdateDamageVisuals);
	if (GetNetMode() != NM_DedicatedServer && !bDisplayOnly)
	{
		EngineAudio->Start();
		SkidAudio->Start();
		ImpactAudio->Start();
	}
}

void ABCVehiclePawn::EndPlay(const EEndPlayReason::Type Reason)
{
	VehicleMovement->OnLanded = nullptr;
	Super::EndPlay(Reason);
}

void ABCVehiclePawn::OnRep_Build()
{
	BuildVehicle();
}

void ABCVehiclePawn::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);
	SetOwner(NewController);
}

FString ABCVehiclePawn::GetDriverName() const
{
	if (const APlayerState* PS = GetPlayerState())
	{
		return PS->GetPlayerName();
	}
	if (const crash::VehicleDef* Def = GetDef())
	{
		if (const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this))
		{
			return Data->VehicleName(*Def).ToString();
		}
	}
	return GetName();
}

bool ABCVehiclePawn::IsWrecked() const
{
	return Damage && Damage->IsWrecked();
}

FVector ABCVehiclePawn::GetHalfExtentsCm() const
{
	return Body->GetUnscaledBoxExtent();
}

// ---------------------------------------------------------------------------------------------
// Building the car

UMaterialInstanceDynamic* ABCVehiclePawn::MakeMaterial(const FLinearColor& Color)
{
	if (!BaseMaterial)
	{
		return nullptr;
	}
	UMaterialInstanceDynamic* Mid = UMaterialInstanceDynamic::Create(BaseMaterial, this);
	Mid->SetVectorParameterValue(TEXT("Color"), Color);
	return Mid;
}

UStaticMeshComponent* ABCVehiclePawn::AddPart(UStaticMesh* Mesh, const FTransform& Relative, const FLinearColor& Color, bool bCollision,
											 USceneComponent* Parent)
{
	UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(this);
	Part->SetStaticMesh(Mesh);
	Part->SetMobility(EComponentMobility::Movable);
	Part->SetupAttachment(Parent ? Parent : Body.Get());
	Part->SetRelativeTransform(Relative);
	Part->SetMaterial(0, MakeMaterial(Color));
	if (bCollision && !bDisplayOnly)
	{
		Part->SetCollisionProfileName(TEXT("Vehicle"));
		Part->SetCollisionResponseToAllChannels(ECR_Block);
		Part->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
		Part->SetMassOverrideInKg(NAME_None, 1.0f, true);
		Part->SetNotifyRigidBodyCollision(true);
	}
	else
	{
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	Part->RegisterComponent();
	Parts.Add(Part);
	return Part;
}

void ABCVehiclePawn::BuildVehicle()
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	if (!Data || (!Data->IsLoaded() && Data->Db().Vehicles().empty()))
	{
		return;
	}
	const crash::GameDatabase& Db = Data->Db();
	crash::VehicleBuild CrashBuild = Build.ToCrash();
	if (!Db.FindVehicle(CrashBuild.vehicleId))
	{
		UE_LOG(LogBigCar, Warning, TEXT("Unknown vehicle '%s', using a starter"), *Build.VehicleId);
		const std::vector<const crash::VehicleDef*> Starters = Db.StarterVehicles();
		if (Starters.empty())
		{
			return;
		}
		CrashBuild = crash::MakeDefaultBuild(Db, Starters.front()->id);
	}
	Stats = crash::ComputeEffectiveStats(Db, CrashBuild);
	const crash::VehicleDef& Def = *Stats.def;
	const crash::VehicleSimSetup Setup = crash::MakeSimSetup(Stats);
	BodyCenterHeight = Setup.bodyCenterHeight;

	// Clear previous parts (garage rebuilds).
	for (UPrimitiveComponent* Part : Parts)
	{
		if (Part)
		{
			Part->DestroyComponent();
		}
	}
	Parts.Reset();
	WheelMeshes.Reset();
	for (USpotLightComponent* Light : Headlights)
	{
		if (Light)
		{
			Light->DestroyComponent();
		}
	}
	Headlights.Reset();

	// Physics body.
	Body->SetBoxExtent(BC::ToUE(Setup.bodyHalfExtents), false);
	if (!bDisplayOnly)
	{
		UPhysicalMaterial* PhysMat = NewObject<UPhysicalMaterial>(this);
		PhysMat->Friction = 0.35f;
		PhysMat->Restitution = 0.12f;
		Body->SetPhysMaterialOverride(PhysMat);
	}

	// Visual body.
	const UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(this);
	const int32 Segments = Settings ? FMath::RoundToInt(Settings->GameValue(TEXT("deformSegments"), 14.0f)) : 14;
	BodyShape.Build(Def, Setup.bodyCenterHeight, Segments);
	BodyShape.CreateSections(BodyMesh);
	PaintMaterial = MakeMaterial(FLinearColor::White);
	GlassMaterial = MakeMaterial(BCPawnDetail::GlassColor);
	BodyMesh->SetMaterial(FBCBodyMesh::BodySection, PaintMaterial);
	BodyMesh->SetMaterial(FBCBodyMesh::GlassSection, GlassMaterial);
	ApplyPaint();

	// Wheels: tire + rim + spoke bar so spinning is visible.
	FLinearColor RimColor(0.35f, 0.36f, 0.38f);
	if (const crash::CosmeticDef* W = Db.FindCosmetic(CrashBuild.wheels))
	{
		RimColor = BC::ToLinear(W->color);
	}
	const float Radius = Def.dimensions.wheelRadius * 100.0f;
	const float Width = Def.dimensions.wheelWidth * 100.0f;
	for (int32 i = 0; i < crash::kWheelCount; ++i)
	{
		UStaticMeshComponent* Tire = AddPart(CylinderMesh, FTransform::Identity, BCPawnDetail::TireColor, false);
		Tire->SetRelativeScale3D(FVector(Radius * 2.0f / 100.0f, Radius * 2.0f / 100.0f, Width / 100.0f));
		Tire->SetCastShadow(true);
		WheelMeshes.Add(Tire);
		// Children inherit the tire scale; express them in the tire's unit space.
		AddPart(CylinderMesh, FTransform(FRotator::ZeroRotator, FVector(0, 0, 0), FVector(0.62f, 0.62f, 1.04f)), RimColor, false, Tire);
		AddPart(CubeMesh, FTransform(FRotator::ZeroRotator, FVector(0, 0, 0), FVector(0.55f, 0.08f, 1.08f)), RimColor * 0.6f, false, Tire);
	}

	// Lights (front white, rear red) and a dark grille.
	const float L = Def.dimensions.length * 100.0f;
	const float WidthCm = Def.dimensions.width * 100.0f;
	const float H = Def.dimensions.height * 100.0f;
	const FBCBodyStyle Style = GetBodyStyle(Def.body);
	const float Ground = -Setup.bodyCenterHeight * 100.0f;
	const float FrontZ = Ground + H * Style.NoseHeight - 12.0f;
	const float RearZ = Ground + H * Style.TailHeight - 14.0f;
	for (const float Side : {-1.0f, 1.0f})
	{
		AddPart(CubeMesh, FTransform(FRotator::ZeroRotator, FVector(L * 0.5f - 3.0f, Side * WidthCm * 0.33f, FrontZ),
									 FVector(0.06f, WidthCm * 0.0022f, 0.09f)),
				FLinearColor(1.0f, 0.97f, 0.85f), false);
		AddPart(CubeMesh, FTransform(FRotator::ZeroRotator, FVector(-L * 0.5f + 3.0f, Side * WidthCm * 0.36f, RearZ),
									 FVector(0.06f, WidthCm * 0.002f, 0.07f)),
				FLinearColor(0.8f, 0.02f, 0.02f), false);
	}
	AddPart(CubeMesh, FTransform(FRotator::ZeroRotator, FVector(L * 0.5f - 2.0f, 0.0f, FrontZ - 6.0f), FVector(0.05f, WidthCm * 0.004f, 0.12f)),
			FLinearColor(0.03f, 0.03f, 0.03f), false);

	BuildEquipment();
	BuildLights();

	// Simulation.
	VehicleMovement->Setup(Stats, Body);
	Damage->Setup(Stats);
	WheelLost.Init(false, crash::kWheelCount);
	ZoneBrokenShown.Init(0, crash::kZoneCount);
	bWreckedVisual = false;

	if (!bDisplayOnly)
	{
		Body->SetSimulatePhysics(true);
		Body->SetMassOverrideInKg(NAME_None, Stats.massKg, true);
		Body->SetCenterOfMass(BC::ToUE(Setup.comLocal));
		// Weld collision equipment into the body so it pushes/scoops physically.
		for (UPrimitiveComponent* Part : Parts)
		{
			if (Part && Part->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
			{
				Part->WeldTo(Body);
			}
		}
		Body->SetMassOverrideInKg(NAME_None, Stats.massKg, true);
	}
	else
	{
		Body->SetSimulatePhysics(false);
		Body->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

	// Camera distance scales with the car.
	CameraArm->TargetArmLength = 380.0f + L * 0.7f;
	CameraArm->SocketOffset = FVector(0.0f, 0.0f, H * 0.6f + 90.0f);

	if (GetNetMode() != NM_DedicatedServer)
	{
		EngineAudio->Configure(BCPawnDetail::GuessCylinders(Def), Def.idleRpm, Def.redlineRpm, Def.IsElectric(),
							   static_cast<uint32>(GetTypeHash(Build.VehicleId)));
	}
	bBuilt = true;
	UpdateWheels(0.0f);
}

void ABCVehiclePawn::ApplyPaint()
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	if (!PaintMaterial || !Data || !Stats.def)
	{
		return;
	}
	FLinearColor Color = Stats.def->colors.empty() ? FLinearColor(0.6f, 0.6f, 0.6f) : BC::ToLinear(Stats.def->colors.front());
	if (const crash::CosmeticDef* Paint = Data->Db().FindCosmetic(BC::ToStd(Build.Paint)))
	{
		Color = BC::ToLinear(Paint->color);
	}
	if (IsWrecked() || bWreckedVisual)
	{
		Color = FMath::Lerp(Color, BCPawnDetail::WreckColor, 0.85f);
	}
	CurrentPaintColor = Color;
	PaintMaterial->SetVectorParameterValue(TEXT("Color"), Color);
}

void ABCVehiclePawn::BuildEquipment()
{
	const crash::VehicleDef& Def = *Stats.def;
	const float L = Def.dimensions.length * 100.0f;
	const float W = Def.dimensions.width * 100.0f;
	const float H = Def.dimensions.height * 100.0f;
	const float Clear = Def.dimensions.groundClearance * 100.0f;
	const float Ground = -BodyCenterHeight * 100.0f;
	const float BumperZ = Ground + Clear + 28.0f;
	const float RoofZ = Ground + H;
	auto Box = [this](const FVector& Loc, const FRotator& Rot, const FVector& SizeCm, const FLinearColor& Color, bool bCollision) {
		return AddPart(CubeMesh, FTransform(Rot, Loc, SizeCm / 100.0f), Color, bCollision);
	};
	for (const crash::Attachment& A : Stats.attachments)
	{
		const FLinearColor C = BC::ToLinear(A.color);
		const float Len = FMath::Max(A.length * 100.0f, 15.0f);
		switch (A.shape)
		{
		case crash::GeometryShape::RamBar:
		case crash::GeometryShape::RearBumper:
		{
			const float Dir = A.shape == crash::GeometryShape::RamBar ? 1.0f : -1.0f;
			const float X = Dir * (L * 0.5f + Len * 0.5f);
			Box(FVector(X, 0.0f, BumperZ), FRotator::ZeroRotator, FVector(Len * 0.5f, W * 0.96f, 26.0f), C, true);
			Box(FVector(X, 0.0f, BumperZ + 30.0f), FRotator::ZeroRotator, FVector(Len * 0.35f, W * 0.7f, 8.0f), C * 0.8f, false);
			for (const float Side : {-0.32f, 0.32f})
			{
				Box(FVector(X - Dir * Len * 0.25f, Side * W, BumperZ + 18.0f), FRotator::ZeroRotator, FVector(8.0f, 8.0f, 50.0f), C * 0.7f, false);
			}
			break;
		}
		case crash::GeometryShape::Wedge:
		case crash::GeometryShape::Plow:
		{
			const bool bPlow = A.shape == crash::GeometryShape::Plow;
			const float Angle = A.angleDeg > 0.0f ? A.angleDeg : (bPlow ? 35.0f : 16.0f);
			const float PlateLen = Len / FMath::Cos(FMath::DegreesToRadians(Angle)) + 40.0f;
			// Leading edge almost on the ground, rising toward the car.
			const float FrontX = L * 0.5f + Len;
			const float FrontZ = Ground + 5.0f;
			const FVector Center(FrontX - PlateLen * 0.5f * FMath::Cos(FMath::DegreesToRadians(Angle)), 0.0f,
								 FrontZ + PlateLen * 0.5f * FMath::Sin(FMath::DegreesToRadians(Angle)));
			Box(Center, FRotator(-Angle, 0.0f, 0.0f), FVector(PlateLen, W * (bPlow ? 1.08f : 0.98f), 6.0f), C, true);
			if (bPlow)
			{
				Box(Center + FVector(-10.0f, 0.0f, 20.0f), FRotator(-Angle, 0.0f, 0.0f), FVector(PlateLen * 0.6f, W * 1.08f, 4.0f), C * 0.8f, false);
			}
			break;
		}
		case crash::GeometryShape::Spikes:
		case crash::GeometryShape::RearSpikes:
		{
			const float Dir = A.shape == crash::GeometryShape::Spikes ? 1.0f : -1.0f;
			Box(FVector(Dir * (L * 0.5f + 6.0f), 0.0f, BumperZ), FRotator::ZeroRotator, FVector(12.0f, W * 0.9f, 22.0f), C * 0.7f, true);
			for (int32 k = 0; k < 5; ++k)
			{
				const float Y = (static_cast<float>(k) - 2.0f) * W * 0.2f;
				AddPart(ConeMesh, FTransform(FRotator(Dir > 0.0f ? -90.0f : 90.0f, 0.0f, 0.0f), FVector(Dir * (L * 0.5f + 12.0f + Len * 0.5f), Y, BumperZ),
											 FVector(0.18f, 0.18f, Len / 100.0f)),
						C, false);
			}
			break;
		}
		case crash::GeometryShape::Cage:
		{
			const float Hx = L * 0.18f;
			const float Hy = W * 0.38f;
			for (const float Sx : {-1.0f, 1.0f})
			{
				for (const float Sy : {-1.0f, 1.0f})
				{
					AddPart(CylinderMesh, FTransform(FRotator::ZeroRotator, FVector(Sx * Hx, Sy * Hy, RoofZ + 6.0f), FVector(0.07f, 0.07f, 0.16f)), C, false);
				}
				Box(FVector(Sx * Hx, 0.0f, RoofZ + 14.0f), FRotator::ZeroRotator, FVector(7.0f, Hy * 2.0f, 7.0f), C, false);
			}
			for (const float Sy : {-1.0f, 1.0f})
			{
				Box(FVector(0.0f, Sy * Hy, RoofZ + 14.0f), FRotator::ZeroRotator, FVector(Hx * 2.0f, 7.0f, 7.0f), C, false);
			}
			break;
		}
		case crash::GeometryShape::RoofArmor:
			Box(FVector(0.0f, 0.0f, RoofZ + 4.0f), FRotator::ZeroRotator, FVector(L * 0.38f, W * 0.7f, 8.0f), C, true);
			break;
		case crash::GeometryShape::SidePlates:
			for (const float Side : {-1.0f, 1.0f})
			{
				Box(FVector(0.0f, Side * (W * 0.5f + 4.0f), Ground + Clear + 45.0f), FRotator::ZeroRotator, FVector(L * 0.6f, 6.0f, 40.0f), C, true);
			}
			break;
		case crash::GeometryShape::SideBlades:
			for (const float Side : {-1.0f, 1.0f})
			{
				for (const float Ax : {0.5f, -0.5f})
				{
					Box(FVector(Ax * Def.dimensions.wheelbase * 100.0f, Side * (W * 0.5f + Len * 0.5f), Ground + Def.dimensions.wheelRadius * 100.0f),
						FRotator::ZeroRotator, FVector(46.0f, Len, 5.0f), C, true);
				}
			}
			break;
		case crash::GeometryShape::WheelSpikes:
			for (UStaticMeshComponent* Tire : WheelMeshes)
			{
				// Tire unit space: z is the axle; spikes stick out of both faces.
				AddPart(ConeMesh, FTransform(FRotator::ZeroRotator, FVector(0, 0, 0.75f), FVector(0.2f, 0.2f, 0.5f)), C, false, Tire);
				AddPart(ConeMesh, FTransform(FRotator(180.0f, 0.0f, 0.0f), FVector(0, 0, -0.75f), FVector(0.2f, 0.2f, 0.5f)), C, false, Tire);
			}
			break;
		default:
			break;
		}
	}
}

void ABCVehiclePawn::BuildLights()
{
	if (GetNetMode() == NM_DedicatedServer || bDisplayOnly || !Stats.def)
	{
		return;
	}
	const ABCGameState* GS = GetWorld() ? GetWorld()->GetGameState<ABCGameState>() : nullptr;
	if (!GS || !GS->IsNightArena())
	{
		return;
	}
	const float L = Stats.def->dimensions.length * 100.0f;
	const float W = Stats.def->dimensions.width * 100.0f;
	for (const float Side : {-1.0f, 1.0f})
	{
		USpotLightComponent* Spot = NewObject<USpotLightComponent>(this);
		Spot->SetupAttachment(Body);
		Spot->SetRelativeLocation(FVector(L * 0.5f, Side * W * 0.33f, 0.0f));
		Spot->SetRelativeRotation(FRotator(-6.0f, 0.0f, 0.0f));
		Spot->SetIntensity(30000.0f);
		Spot->SetAttenuationRadius(4500.0f);
		Spot->SetOuterConeAngle(32.0f);
		Spot->SetInnerConeAngle(18.0f);
		Spot->SetCastShadows(false);
		Spot->RegisterComponent();
		Headlights.Add(Spot);
	}
}

// ---------------------------------------------------------------------------------------------
// Per-frame

void ABCVehiclePawn::SetDriverInput(const crash::VehicleControls& Controls, bool bHorn, bool bLookBack)
{
	LocalControls = FBCControls::FromCrash(Controls, bHorn, bLookBack);
	bLocalHorn = bHorn;
	bLocalLookBack = bLookBack;
	if (HasAuthority())
	{
		RepControls = LocalControls;
	}
}

void ABCVehiclePawn::ServerSetControls_Implementation(FBCControls NewControls)
{
	RepControls = NewControls;
}

void ABCVehiclePawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!bBuilt)
	{
		return;
	}
	if (bDisplayOnly)
	{
		UpdateWheels(DeltaSeconds);
		return;
	}
	PreImpactVelocity = Body->GetPhysicsLinearVelocity();

	// Which controls drive the simulation on this machine.
	const bool bLocal = IsLocallyControlled();
	if (bLocal && !HasAuthority())
	{
		ControlSendTimer += DeltaSeconds;
		if (LocalControls != LastSentControls || ControlSendTimer > 0.1f)
		{
			ServerSetControls(LocalControls);
			LastSentControls = LocalControls;
			ControlSendTimer = 0.0f;
		}
	}
	FBCControls Active = bLocal ? LocalControls : RepControls;
	crash::VehicleControls Controls = Active.ToCrash();
	if (!bControlsEnabled || IsWrecked())
	{
		Controls = crash::VehicleControls();
		Controls.brake = IsWrecked() ? 0.2f : 1.0f;
		Controls.handbrake = true;
	}
	VehicleMovement->SetControls(Controls);
	VehicleMovement->SetDrivingDamage(Damage->State().DrivingEffects());
	crash::DrivingAssists Assists;
	Assists.suppressed = GetWorld()->GetTimeSeconds() - LastImpactTime < 1.5f;
	if (const UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(this); Settings && bLocal)
	{
		Assists.yawStability = Settings->Gameplay().steeringAssist;
	}
	VehicleMovement->SetAssists(Assists);
	VehicleMovement->StepVehicle(DeltaSeconds, true);

	if (HasAuthority())
	{
		const crash::AbilityEffects Fx = VehicleMovement->Ability().Effects();
		const crash::DamageEvent Burn = Damage->TickDamage(DeltaSeconds, Stats.repairPerSec + Fx.repairPerSec);
		if (Burn.becameWrecked)
		{
			if (ABCMatchGameMode* GM = GetWorld()->GetAuthGameMode<ABCMatchGameMode>())
			{
				GM->NotifyVehicleWrecked(this, false);
			}
		}
		// Players stuck on their roof get flipped back automatically.
		if (VehicleMovement->IsUpsideDown() && Body->GetPhysicsLinearVelocity().Size() < 300.0f && !IsWrecked())
		{
			UpsideDownTime += DeltaSeconds;
			if (UpsideDownTime > 4.0f)
			{
				RequestReset();
			}
		}
		else
		{
			UpsideDownTime = 0.0f;
		}
	}

	if (GetNetMode() != NM_DedicatedServer)
	{
		UpdateWheels(DeltaSeconds);
		UpdateAudio();
		if (bLocal)
		{
			UpdateCamera(DeltaSeconds);
		}
		if (Damage->IsOnFire())
		{
			FireFlicker += DeltaSeconds * 18.0f;
			FireLight->SetIntensity(6000.0f + 3000.0f * FMath::Sin(FireFlicker) * FMath::Sin(FireFlicker * 0.37f));
		}
		DentCooldown = FMath::Max(0.0f, DentCooldown - DeltaSeconds);
	}
}

void ABCVehiclePawn::UpdateWheels(float DeltaSeconds)
{
	if (!Stats.def || WheelMeshes.Num() != crash::kWheelCount)
	{
		return;
	}
	const float HalfW = Stats.def->dimensions.width * 50.0f;
	const float WheelW = Stats.def->dimensions.wheelWidth * 100.0f;
	for (int32 i = 0; i < crash::kWheelCount; ++i)
	{
		UStaticMeshComponent* Tire = WheelMeshes[i];
		if (!Tire)
		{
			continue;
		}
		FVector Mount = VehicleMovement->WheelMountLocal(i);
		const float Drop = bDisplayOnly ? VehicleMovement->GetSim().GetSetup().travel * 100.0f : VehicleMovement->WheelDropCm(i);
		// Visual wheel sits flush with the body side, inside the wheel well.
		Mount.Y = FMath::Sign(Mount.Y) * FMath::Max(FMath::Abs(Mount.Y), HalfW - WheelW * 0.5f - 1.0f);
		const FVector Center = Mount - FVector(0.0f, 0.0f, Drop);
		const FQuat Base(FVector::ForwardVector, FMath::DegreesToRadians(90.0f));
		const FQuat Spin(FVector::RightVector, VehicleMovement->WheelSpin(i));
		const FQuat Steer(FVector::UpVector, VehicleMovement->WheelSteer(i));
		Tire->SetRelativeLocationAndRotation(Center, Steer * Spin * Base);
	}
}

void ABCVehiclePawn::UpdateCamera(float DeltaSeconds)
{
	const UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(this);
	const float BaseFov = Settings ? Settings->Gameplay().fov : 90.0f;
	const float ShakeScale = Settings ? Settings->Gameplay().cameraShake : 1.0f;
	const float Speed = VehicleMovement->SpeedKmh();
	const float TargetFov = BaseFov + FMath::Clamp((Speed - 80.0f) / 25.0f, 0.0f, 8.0f) + (VehicleMovement->IsBoosting() ? 7.0f : 0.0f);
	Camera->SetFieldOfView(FMath::FInterpTo(Camera->FieldOfView, TargetFov, DeltaSeconds, 4.0f));

	const bool bLookBack = bLocalLookBack;
	const float TargetYaw = bLookBack ? 180.0f : 0.0f;
	FRotator ArmRot = CameraArm->GetRelativeRotation();
	ArmRot.Yaw = FMath::FInterpTo(ArmRot.Yaw, TargetYaw, DeltaSeconds, 12.0f);
	CameraArm->SetRelativeRotation(ArmRot);

	ShakeAmount = FMath::Max(0.0f, ShakeAmount - DeltaSeconds * 2.5f);
	const float S = ShakeAmount * ShakeAmount * 22.0f * ShakeScale;
	Camera->SetRelativeLocation(FVector(0.0f, FMath::FRandRange(-S, S), FMath::FRandRange(-S, S)));

	if (CrashCamTime > 0.0f)
	{
		CrashCamTime -= DeltaSeconds;
		if (AActor* Focus = CrashCamFocus.Get())
		{
			const FVector From = Camera->GetComponentLocation();
			Camera->SetWorldRotation((Focus->GetActorLocation() - From).Rotation());
		}
		if (CrashCamTime <= 0.0f)
		{
			Camera->SetRelativeRotation(FRotator::ZeroRotator);
		}
	}
}

void ABCVehiclePawn::UpdateAudio()
{
	const UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(this);
	const float EngineVol = Settings ? Settings->AudioVolume(EBCAudioChannel::Engine) : 1.0f;
	const float FxVol = Settings ? Settings->AudioVolume(EBCAudioChannel::Effects) : 1.0f;
	const bool bWrecked = IsWrecked();
	EngineAudio->SetEngineState(VehicleMovement->Rpm(), VehicleMovement->LastOutput().throttleAudio, bWrecked ? 0.0f : EngineVol * 0.7f);
	SkidAudio->SetSlip(VehicleMovement->GroundedWheels() > 0 ? VehicleMovement->MaxSlip() : 0.0f, FxVol * 0.8f);
	const FBCControls Active = IsLocallyControlled() ? LocalControls : RepControls;
	ImpactAudio->SetHorn(Active.Horn() && !bWrecked, 415.0f, 520.0f, FxVol);
}

void ABCVehiclePawn::AddCameraShake(float Strength)
{
	ShakeAmount = FMath::Clamp(ShakeAmount + Strength, 0.0f, 1.5f);
}

void ABCVehiclePawn::SetCrashCam(float Duration, AActor* Focus)
{
	const UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(this);
	if (Settings && !Settings->Gameplay().crashCam)
	{
		return;
	}
	CrashCamTime = Duration;
	CrashCamFocus = Focus;
}

void ABCVehiclePawn::HandleLanded(float Seconds)
{
	if (!HasAuthority())
	{
		return;
	}
	if (ABCMatchGameMode* GM = GetWorld()->GetAuthGameMode<ABCMatchGameMode>())
	{
		GM->NotifyAirtime(this, Seconds);
	}
}

// ---------------------------------------------------------------------------------------------
// Abilities / reset

void ABCVehiclePawn::RequestAbility()
{
	if (!HasAuthority())
	{
		ServerUseAbility();
		return;
	}
	if (!bControlsEnabled || IsWrecked())
	{
		return;
	}
	const crash::AbilityTrigger T = VehicleMovement->Ability().TryActivate();
	if (!T.activated)
	{
		return;
	}
	if (T.jumpVelocity > 0.0f)
	{
		Body->AddImpulse(FVector(0.0f, 0.0f, T.jumpVelocity * 100.0f), NAME_None, true);
	}
	if (T.addBoost > 0.0f)
	{
		VehicleMovement->GetSimMutable().AddBoost(T.addBoost);
	}
	MulticastAbility(static_cast<uint8>(VehicleMovement->Ability().Type()));
}

void ABCVehiclePawn::ServerUseAbility_Implementation()
{
	RequestAbility();
}

void ABCVehiclePawn::MulticastAbility_Implementation(uint8 Ability)
{
	if (!HasAuthority())
	{
		VehicleMovement->Ability().TryActivate(); // mirror cooldown/active timers for HUD and local sim
	}
	if (GetNetMode() != NM_DedicatedServer)
	{
		const FLinearColor Color = Ability == static_cast<uint8>(crash::AbilityType::Brace)		  ? FLinearColor(0.3f, 0.6f, 1.0f)
								   : Ability == static_cast<uint8>(crash::AbilityType::FieldRepair) ? FLinearColor(0.2f, 1.0f, 0.4f)
																									 : FLinearColor(1.0f, 0.6f, 0.1f);
		ABCDebris::Burst(GetWorld(), GetActorLocation(), FVector::UpVector, Color, 8, 6.0f, 600.0f, 0.6f);
		ImpactAudio->Trigger(0.25f, 0.2f, UBCSettingsSubsystem::Get(this) ? UBCSettingsSubsystem::Get(this)->AudioVolume(EBCAudioChannel::Effects) : 1.0f);
	}
}

void ABCVehiclePawn::RequestReset()
{
	if (!HasAuthority())
	{
		ServerRequestReset();
		return;
	}
	if (IsWrecked())
	{
		return;
	}
	// Only when actually stuck: upside down, or nearly stopped.
	if (!VehicleMovement->IsUpsideDown() && Body->GetPhysicsLinearVelocity().Size() > 500.0f)
	{
		return;
	}
	const FRotator Yaw(0.0f, GetActorRotation().Yaw, 0.0f);
	ServerResetTo(FTransform(Yaw, GetActorLocation() + FVector(0.0f, 0.0f, 150.0f)), false);
	UpsideDownTime = 0.0f;
}

void ABCVehiclePawn::ServerRequestReset_Implementation()
{
	RequestReset();
}

// ---------------------------------------------------------------------------------------------
// Server gameplay

void ABCVehiclePawn::OnBodyHit(UPrimitiveComponent* HitComp, AActor* OtherActor, UPrimitiveComponent* OtherComp, FVector NormalImpulse,
							   const FHitResult& Hit)
{
	if (!HasAuthority() || !OtherActor || OtherActor == this)
	{
		return;
	}
	if (ABCMatchGameMode* GM = GetWorld()->GetAuthGameMode<ABCMatchGameMode>())
	{
		GM->HandleVehicleHit(this, OtherActor, OtherComp, Hit, NormalImpulse);
	}
}

crash::DamageEvent ABCVehiclePawn::ServerApplyImpactResult(const FBCImpactApply& Apply)
{
	LastImpactTime = GetWorld()->GetTimeSeconds();
	const crash::DamageEvent Event = Damage->ApplyDamage(Apply.Damage);
	if (!Apply.ImpulseUE.IsNearlyZero())
	{
		Body->AddImpulseAtLocation(Apply.ImpulseUE, Apply.WorldPoint);
	}
	const FTransform Xf = Body->GetComponentTransform();
	const FVector LocalPoint = Xf.InverseTransformPosition(Apply.WorldPoint);
	const FVector LocalDir = Xf.InverseTransformVectorNoScale(Apply.DentDirWorld.GetSafeNormal());
	const float Total = Apply.Damage.BodyTotal();
	const float Strength = FMath::Clamp(Total / FMath::Max(1.0f, Stats.zoneHp[0]), 0.0f, 1.0f);
	MulticastImpactFX(Apply.WorldPoint, -Apply.DentDirWorld.GetSafeNormal(), Apply.Severity, Apply.Kind, LocalPoint, LocalDir, Strength,
					  Apply.bVehicle);
	for (int32 z = 0; z < crash::kZoneCount; ++z)
	{
		if (Event.zoneBroken && Damage->State().ZoneFraction(static_cast<crash::Zone>(z)) <= 0.0f && ZoneBrokenShown.IsValidIndex(z) &&
			!ZoneBrokenShown[z])
		{
			ZoneBrokenShown[z] = 1;
			MulticastPartLost(static_cast<uint8>(z), -1);
		}
	}
	for (int32 w = 0; w < crash::kWheelCount; ++w)
	{
		if (Event.wheelLost[static_cast<size_t>(w)])
		{
			MulticastPartLost(255, static_cast<int8>(w));
		}
	}
	return Event;
}

void ABCVehiclePawn::ServerAddImpulse(const FVector& ImpulseUE, const FVector& WorldPoint)
{
	Body->AddImpulseAtLocation(ImpulseUE, WorldPoint);
}

void ABCVehiclePawn::ServerRepair(float Fraction)
{
	Damage->Repair(Fraction);
	if (Fraction >= 1.0f)
	{
		MulticastRepaired();
	}
}

void ABCVehiclePawn::ServerWreck(bool bRingOut)
{
	bControlsEnabled = false;
	if (!bRingOut)
	{
		// Wreck explosion: pop the car up and spin it.
		Body->AddImpulse(FVector(FMath::FRandRange(-150.0f, 150.0f), FMath::FRandRange(-150.0f, 150.0f), 520.0f), NAME_None, true);
		Body->AddAngularImpulseInDegrees(FVector(FMath::FRandRange(-90.0f, 90.0f), FMath::FRandRange(-120.0f, 120.0f), 0.0f), NAME_None, true);
	}
	MulticastWrecked(bRingOut);
}

void ABCVehiclePawn::ServerResetTo(const FTransform& Transform, bool bRepair)
{
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	SetActorTransform(Transform, false, nullptr, ETeleportType::TeleportPhysics);
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Body->SetPhysicsAngularVelocityInRadians(FVector::ZeroVector);
	VehicleMovement->ResetState();
	if (bRepair)
	{
		Damage->RepairFull();
		bControlsEnabled = true;
		MulticastRepaired();
	}
}

// ---------------------------------------------------------------------------------------------
// Cosmetics

void ABCVehiclePawn::MulticastImpactFX_Implementation(FVector_NetQuantize WorldPoint, FVector_NetQuantizeNormal Normal, float Severity,
													   uint8 Kind, FVector_NetQuantize LocalDent, FVector_NetQuantizeNormal LocalDentDir,
													   float DentStrength, bool bVehicle)
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	// Body deformation.
	if (DentStrength > 0.02f)
	{
		const float Radius = FMath::Lerp(45.0f, 110.0f, FMath::Clamp(DentStrength, 0.0f, 1.0f));
		const float Depth = FMath::Lerp(4.0f, 30.0f, FMath::Clamp(DentStrength, 0.0f, 1.0f));
		if (BodyShape.ApplyDent(LocalDent, LocalDentDir, Radius, Depth))
		{
			BodyShape.UpdateSections(BodyMesh);
		}
	}
	const UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(this);
	const int32 Sparks = Settings ? FMath::RoundToInt(Settings->GameValue(TEXT("sparkCount"), 14.0f)) : 14;
	const FLinearColor SparkColor(1.0f, 0.72f, 0.3f);
	ABCDebris::Burst(GetWorld(), WorldPoint, Normal, SparkColor, FMath::RoundToInt(Sparks * (0.3f + Severity)), 3.0f, 700.0f + 900.0f * Severity, 0.5f);
	if (Severity > 0.25f)
	{
		ABCDebris::Burst(GetWorld(), WorldPoint, Normal, CurrentPaintColor, FMath::RoundToInt(3 + Severity * 6), 12.0f, 400.0f + 600.0f * Severity, 6.0f);
	}
	const float FxVol = Settings ? Settings->AudioVolume(EBCAudioChannel::Effects) : 1.0f;
	ImpactAudio->Trigger(FMath::Clamp(0.15f + Severity, 0.0f, 1.0f), bVehicle ? 1.0f : 0.4f, FxVol);
	if (IsLocallyControlled())
	{
		AddCameraShake(0.3f + Severity * 1.2f);
	}
	(void)Kind;
}

void ABCVehiclePawn::MulticastPartLost_Implementation(uint8 Zone, int8 Wheel)
{
	if (GetNetMode() == NM_DedicatedServer || !Stats.def)
	{
		return;
	}
	const FTransform Xf = Body->GetComponentTransform();
	if (Wheel >= 0 && WheelMeshes.IsValidIndex(Wheel) && WheelMeshes[Wheel])
	{
		WheelMeshes[Wheel]->SetVisibility(false, true);
		ABCDebris::Burst(GetWorld(), WheelMeshes[Wheel]->GetComponentLocation(), Xf.GetUnitAxis(EAxis::Y) * (Wheel % 2 == 0 ? -1.0f : 1.0f),
						 BCPawnDetail::TireColor, 1, Stats.def->dimensions.wheelRadius * 160.0f, 500.0f, 12.0f, 1);
		return;
	}
	// A broken body panel falls off.
	const FVector H = GetHalfExtentsCm();
	FVector Local = FVector::ZeroVector;
	switch (static_cast<crash::Zone>(Zone))
	{
	case crash::Zone::Front:
		Local = FVector(H.X, 0.0f, 0.0f);
		break;
	case crash::Zone::Rear:
		Local = FVector(-H.X, 0.0f, 0.0f);
		break;
	case crash::Zone::Left:
		Local = FVector(0.0f, -H.Y, 0.0f);
		break;
	case crash::Zone::Right:
		Local = FVector(0.0f, H.Y, 0.0f);
		break;
	default:
		Local = FVector(0.0f, 0.0f, H.Z);
		break;
	}
	ABCDebris::Burst(GetWorld(), Xf.TransformPosition(Local), Xf.TransformVectorNoScale(Local.GetSafeNormal()), CurrentPaintColor, 3, 40.0f,
					 450.0f, 10.0f);
}

void ABCVehiclePawn::MulticastWrecked_Implementation(bool bRingOut)
{
	bWreckedVisual = true;
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	ApplyPaint();
	if (!bRingOut)
	{
		ABCDebris::Burst(GetWorld(), GetActorLocation(), FVector::UpVector, FLinearColor(1.0f, 0.45f, 0.1f), 18, 10.0f, 1200.0f, 1.2f);
		ABCDebris::Burst(GetWorld(), GetActorLocation(), FVector::UpVector, BCPawnDetail::WreckColor, 10, 25.0f, 900.0f, 8.0f);
		FireLight->SetVisibility(true);
		FireLight->SetIntensity(20000.0f);
	}
	const UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(this);
	ImpactAudio->Trigger(1.0f, 0.7f, Settings ? Settings->AudioVolume(EBCAudioChannel::Effects) : 1.0f);
	if (IsLocallyControlled())
	{
		AddCameraShake(1.5f);
	}
}

void ABCVehiclePawn::MulticastRepaired_Implementation()
{
	BodyShape.Restore();
	BodyShape.UpdateSections(BodyMesh);
	for (UStaticMeshComponent* Tire : WheelMeshes)
	{
		if (Tire)
		{
			Tire->SetVisibility(true, true);
		}
	}
	ZoneBrokenShown.Init(0, crash::kZoneCount);
	bWreckedVisual = false;
	ApplyPaint();
	UpdateDamageVisuals();
}

void ABCVehiclePawn::UpdateDamageVisuals()
{
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}
	const bool bFire = Damage->IsOnFire();
	FireLight->SetVisibility(bFire || bWreckedVisual);
	if (!bFire && !bWreckedVisual)
	{
		FireLight->SetIntensity(0.0f);
	}
	for (int32 i = 0; i < crash::kWheelCount && i < WheelMeshes.Num(); ++i)
	{
		if (WheelMeshes[i] && Damage->State().IsWheelDetached(i))
		{
			WheelMeshes[i]->SetVisibility(false, true);
		}
	}
}

void ABCVehiclePawn::SpawnDebris(const FVector& WorldPoint, const FVector& Direction, const FLinearColor& Color, int32 Count, float Size)
{
	ABCDebris::Burst(GetWorld(), WorldPoint, Direction, Color, Count, Size, 600.0f, 6.0f);
}
