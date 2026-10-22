// Copyright BigCar Team. All Rights Reserved.

#include "Arena/BCArenaBuilder.h"

#include "Arena/BCArenaProps.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/ExponentialHeightFogComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/PostProcessComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/BCDataSubsystem.h"
#include "Core/BCSettingsSubsystem.h"
#include "Core/BigCarTypes.h"
#include "Engine/World.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "ProceduralMeshComponent.h"

namespace BCArenaDetail
{
struct FRect2
{
	float MinX, MinY, MaxX, MaxY;
	bool Overlaps(const FRect2& O) const { return MinX < O.MaxX && MaxX > O.MinX && MinY < O.MaxY && MaxY > O.MinY; }
};

// Splits `A` minus `Hole` into up to four rectangles.
void Subtract(const FRect2& A, const FRect2& Hole, TArray<FRect2>& Out)
{
	if (!A.Overlaps(Hole))
	{
		Out.Add(A);
		return;
	}
	if (Hole.MinX > A.MinX)
	{
		Out.Add({A.MinX, A.MinY, Hole.MinX, A.MaxY});
	}
	if (Hole.MaxX < A.MaxX)
	{
		Out.Add({Hole.MaxX, A.MinY, A.MaxX, A.MaxY});
	}
	const float X0 = FMath::Max(A.MinX, Hole.MinX);
	const float X1 = FMath::Min(A.MaxX, Hole.MaxX);
	if (Hole.MinY > A.MinY)
	{
		Out.Add({X0, A.MinY, X1, Hole.MinY});
	}
	if (Hole.MaxY < A.MaxY)
	{
		Out.Add({X0, Hole.MaxY, X1, A.MaxY});
	}
}

void AddTwoSided(TArray<int32>& Tris, int32 A, int32 B, int32 C)
{
	Tris.Append({A, B, C, A, C, B});
}

FVector P(const crash::Vec3& V)
{
	return FVector(V.x, V.y, V.z) * 100.0f;
}
} // namespace BCArenaDetail

ABCArenaBuilder::ABCArenaBuilder()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false;
	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	Root->SetMobility(EComponentMobility::Static);
	RootComponent = Root;
}

void ABCArenaBuilder::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
}

FLinearColor ABCArenaBuilder::ThemeColor(int32 Slot) const
{
	static const TMap<FString, TArray<FLinearColor>> Themes = {
		{TEXT("junkyard"), {FLinearColor(0.32f, 0.18f, 0.09f), FLinearColor(0.45f, 0.38f, 0.3f), FLinearColor(0.6f, 0.3f, 0.08f), FLinearColor(0.2f, 0.2f, 0.22f)}},
		{TEXT("stadium"), {FLinearColor(0.55f, 0.55f, 0.58f), FLinearColor(0.85f, 0.1f, 0.08f), FLinearColor(0.1f, 0.3f, 0.85f), FLinearColor(0.95f, 0.95f, 0.95f)}},
		{TEXT("docks"), {FLinearColor(0.12f, 0.3f, 0.55f), FLinearColor(0.75f, 0.2f, 0.1f), FLinearColor(0.15f, 0.5f, 0.25f), FLinearColor(0.4f, 0.4f, 0.42f)}},
		{TEXT("desert"), {FLinearColor(0.75f, 0.58f, 0.36f), FLinearColor(0.55f, 0.38f, 0.22f), FLinearColor(0.85f, 0.7f, 0.45f), FLinearColor(0.35f, 0.25f, 0.15f)}},
		{TEXT("snow"), {FLinearColor(0.85f, 0.9f, 0.95f), FLinearColor(0.3f, 0.45f, 0.65f), FLinearColor(0.6f, 0.65f, 0.7f), FLinearColor(0.2f, 0.25f, 0.3f)}},
		{TEXT("city"), {FLinearColor(0.3f, 0.3f, 0.32f), FLinearColor(0.85f, 0.75f, 0.1f), FLinearColor(0.5f, 0.52f, 0.55f), FLinearColor(0.15f, 0.15f, 0.17f)}},
		{TEXT("industrial"), {FLinearColor(0.85f, 0.65f, 0.05f), FLinearColor(0.12f, 0.12f, 0.12f), FLinearColor(0.45f, 0.45f, 0.45f), FLinearColor(0.6f, 0.15f, 0.05f)}},
		{TEXT("highway"), {FLinearColor(0.5f, 0.5f, 0.5f), FLinearColor(0.9f, 0.9f, 0.9f), FLinearColor(0.95f, 0.75f, 0.05f), FLinearColor(0.2f, 0.4f, 0.2f)}},
		{TEXT("rooftop"), {FLinearColor(0.35f, 0.35f, 0.38f), FLinearColor(0.1f, 0.6f, 0.9f), FLinearColor(0.7f, 0.7f, 0.72f), FLinearColor(0.9f, 0.3f, 0.1f)}},
		{TEXT("quarry"), {FLinearColor(0.5f, 0.47f, 0.43f), FLinearColor(0.75f, 0.55f, 0.1f), FLinearColor(0.35f, 0.33f, 0.3f), FLinearColor(0.6f, 0.58f, 0.55f)}},
		{TEXT("airfield"), {FLinearColor(0.4f, 0.42f, 0.4f), FLinearColor(0.95f, 0.95f, 0.95f), FLinearColor(0.9f, 0.5f, 0.05f), FLinearColor(0.25f, 0.35f, 0.25f)}},
		{TEXT("neon"), {FLinearColor(0.03f, 0.03f, 0.06f), FLinearColor(1.0f, 0.05f, 0.6f), FLinearColor(0.05f, 0.9f, 1.0f), FLinearColor(0.6f, 0.1f, 1.0f)}},
	};
	const FString Theme = ArenaDef ? BC::ToFString(ArenaDef->theme) : FString(TEXT("city"));
	const TArray<FLinearColor>* Colors = Themes.Find(Theme);
	if (!Colors)
	{
		Colors = Themes.Find(TEXT("city"));
	}
	return (*Colors)[FMath::Abs(Slot) % Colors->Num()];
}

UStaticMeshComponent* ABCArenaBuilder::AddBox(const FVector& Center, const FVector& SizeCm, const FRotator& Rot, const FLinearColor& Color, bool bCollision)
{
	UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
	C->SetMobility(EComponentMobility::Static);
	C->SetStaticMesh(BCProps::Cube());
	C->SetupAttachment(RootComponent);
	C->SetWorldLocationAndRotation(Center, Rot);
	C->SetWorldScale3D(SizeCm / 100.0f);
	C->SetMaterial(0, BCProps::Material(this, Color));
	if (bCollision)
	{
		C->SetCollisionProfileName(TEXT("BlockAll"));
	}
	else
	{
		C->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}
	C->RegisterComponent();
	Geometry.Add(C);
	return C;
}

void ABCArenaBuilder::Build(const FString& ArenaId, bool bSpawnDynamicProps)
{
	const UBCDataSubsystem* Data = UBCDataSubsystem::Get(this);
	if (!Data)
	{
		return;
	}
	ArenaDef = Data->Db().FindArena(BC::ToStd(ArenaId));
	if (!ArenaDef && !Data->Db().Arenas().empty())
	{
		UE_LOG(LogBigCar, Warning, TEXT("Arena '%s' not found, using the first arena"), *ArenaId);
		ArenaDef = &Data->Db().Arenas().front();
	}
	if (!ArenaDef)
	{
		return;
	}
	FloorHeight = ArenaDef->elevation * 100.0f;
	FloorMaterial = NewObject<UPhysicalMaterial>(this);
	FloorMaterial->Friction = ArenaDef->floorFriction;
	FloorMaterial->Restitution = 0.1f;
	BuildFloor();
	BuildWalls();
	BuildProps(bSpawnDynamicProps);
	BuildLighting();
	if (UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(this))
	{
		SettingsHandle = Settings->OnApplied.AddUObject(this, &ABCArenaBuilder::ApplyPostProcessSettings);
	}
	UE_LOG(LogBigCar, Log, TEXT("Arena '%s' built: %d components, %d props"), *ArenaId, Geometry.Num(), SpawnedActors.Num());
}

void ABCArenaBuilder::AddPolygonFloor(int32 Sides, float RadiusCm, float Z, const FLinearColor& Color)
{
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->SetupAttachment(RootComponent);
	Mesh->bUseComplexAsSimpleCollision = true;
	TArray<FVector> V;
	TArray<int32> T;
	TArray<FVector> N;
	TArray<FVector2D> UV;
	const float Thickness = 200.0f;
	V.Add(FVector(0, 0, Z));
	N.Add(FVector::UpVector);
	UV.Add(FVector2D(0.5f, 0.5f));
	for (int32 i = 0; i < Sides; ++i)
	{
		const float A = UE_TWO_PI * static_cast<float>(i) / static_cast<float>(Sides) + (Sides == 8 ? UE_PI / 8.0f : 0.0f);
		V.Add(FVector(FMath::Cos(A) * RadiusCm, FMath::Sin(A) * RadiusCm, Z));
		N.Add(FVector::UpVector);
		UV.Add(FVector2D(FMath::Cos(A) * 0.5f + 0.5f, FMath::Sin(A) * 0.5f + 0.5f));
	}
	for (int32 i = 0; i < Sides; ++i)
	{
		BCArenaDetail::AddTwoSided(T, 0, 1 + i, 1 + (i + 1) % Sides);
	}
	// Side skirt.
	const int32 Base = V.Num();
	for (int32 i = 0; i < Sides; ++i)
	{
		const FVector Top = V[1 + i];
		V.Add(Top);
		V.Add(Top - FVector(0, 0, Thickness));
		const FVector Out = FVector(Top.X, Top.Y, 0.0f).GetSafeNormal();
		N.Add(Out);
		N.Add(Out);
		UV.Add(FVector2D(static_cast<float>(i), 0.0f));
		UV.Add(FVector2D(static_cast<float>(i), 1.0f));
	}
	for (int32 i = 0; i < Sides; ++i)
	{
		const int32 A0 = Base + i * 2;
		const int32 B0 = Base + ((i + 1) % Sides) * 2;
		BCArenaDetail::AddTwoSided(T, A0, B0, A0 + 1);
		BCArenaDetail::AddTwoSided(T, B0, B0 + 1, A0 + 1);
	}
	Mesh->CreateMeshSection(0, V, T, N, UV, TArray<FColor>(), TArray<FProcMeshTangent>(), true);
	Mesh->SetMaterial(0, BCProps::Material(this, Color));
	Mesh->SetPhysMaterialOverride(FloorMaterial);
	Mesh->RegisterComponent();
	Geometry.Add(Mesh);
}

void ABCArenaBuilder::BuildFloor()
{
	using BCArenaDetail::FRect2;
	const FLinearColor FloorColor = BC::ToLinear(ArenaDef->floorColor);
	const float HalfX = ArenaDef->HalfX() * 100.0f;
	const float HalfY = ArenaDef->HalfY() * 100.0f;
	const float Thick = 200.0f;
	if (ArenaDef->shape == crash::ArenaShape::Rect)
	{
		// Cut real holes for pits.
		TArray<FRect2> Rects = {{-HalfX, -HalfY, HalfX, HalfY}};
		for (const crash::PropDef& P : ArenaDef->props)
		{
			if (P.type != crash::PropType::Pit)
			{
				continue;
			}
			const FRect2 Hole{(P.pos.x - P.size.x * 0.5f) * 100.0f, (P.pos.y - P.size.y * 0.5f) * 100.0f, (P.pos.x + P.size.x * 0.5f) * 100.0f,
							  (P.pos.y + P.size.y * 0.5f) * 100.0f};
			TArray<FRect2> Next;
			for (const FRect2& R : Rects)
			{
				BCArenaDetail::Subtract(R, Hole, Next);
			}
			Rects = MoveTemp(Next);
		}
		for (const FRect2& R : Rects)
		{
			if (R.MaxX - R.MinX < 1.0f || R.MaxY - R.MinY < 1.0f)
			{
				continue;
			}
			UStaticMeshComponent* Tile = AddBox(FVector((R.MinX + R.MaxX) * 0.5f, (R.MinY + R.MaxY) * 0.5f, FloorHeight - Thick * 0.5f),
												FVector(R.MaxX - R.MinX, R.MaxY - R.MinY, Thick), FRotator::ZeroRotator, FloorColor);
			Tile->SetPhysMaterialOverride(FloorMaterial);
		}
	}
	else
	{
		AddPolygonFloor(ArenaDef->shape == crash::ArenaShape::Octagon ? 8 : 48, HalfX, FloorHeight, FloorColor);
	}
	// Painted center circle / lane lines for readability.
	AddBox(FVector(0, 0, FloorHeight + 0.5f), FVector(FMath::Min(HalfX, HalfY) * 0.3f, 30.0f, 1.0f), FRotator::ZeroRotator, ThemeColor(3) * 1.2f, false);
	AddBox(FVector(0, 0, FloorHeight + 0.5f), FVector(30.0f, FMath::Min(HalfX, HalfY) * 0.3f, 1.0f), FRotator::ZeroRotator, ThemeColor(3) * 1.2f, false);
	// Elevated platforms stand on a pillar so the drop is readable.
	if (ArenaDef->elevation > 1.0f)
	{
		AddBox(FVector(0, 0, FloorHeight * 0.5f - Thick), FVector(HalfX * 0.5f, HalfY * 0.5f, FloorHeight - Thick), FRotator::ZeroRotator,
			   ThemeColor(2) * 0.6f, false);
		// Ground far below.
		AddBox(FVector(0, 0, ArenaDef->killZ * 100.0f - 600.0f), FVector(HalfX * 8.0f, HalfY * 8.0f, 100.0f), FRotator::ZeroRotator, ThemeColor(0) * 0.5f, true);
	}
	else if (!ArenaDef->walls)
	{
		AddBox(FVector(0, 0, ArenaDef->killZ * 100.0f - 600.0f), FVector(HalfX * 6.0f, HalfY * 6.0f, 100.0f), FRotator::ZeroRotator, ThemeColor(0) * 0.5f, true);
	}
}

void ABCArenaBuilder::BuildWalls()
{
	if (!ArenaDef->walls || ArenaDef->wallHeight <= 0.0f)
	{
		return;
	}
	const float H = ArenaDef->wallHeight * 100.0f;
	const float T = 120.0f;
	const FLinearColor WallColor = ThemeColor(1);
	const FLinearColor Trim = ThemeColor(2);
	if (ArenaDef->shape == crash::ArenaShape::Rect)
	{
		const float HX = ArenaDef->HalfX() * 100.0f;
		const float HY = ArenaDef->HalfY() * 100.0f;
		for (const float S : {-1.0f, 1.0f})
		{
			AddBox(FVector(S * (HX + T * 0.5f), 0, FloorHeight + H * 0.5f), FVector(T, HY * 2.0f + T * 2.0f, H), FRotator::ZeroRotator, WallColor);
			AddBox(FVector(0, S * (HY + T * 0.5f), FloorHeight + H * 0.5f), FVector(HX * 2.0f + T * 2.0f, T, H), FRotator::ZeroRotator, WallColor);
			AddBox(FVector(S * (HX + T * 0.5f), 0, FloorHeight + H + 10.0f), FVector(T + 20.0f, HY * 2.0f + T * 2.0f, 20.0f), FRotator::ZeroRotator, Trim, false);
			AddBox(FVector(0, S * (HY + T * 0.5f), FloorHeight + H + 10.0f), FVector(HX * 2.0f + T * 2.0f, T + 20.0f, 20.0f), FRotator::ZeroRotator, Trim, false);
		}
		return;
	}
	const int32 Sides = ArenaDef->shape == crash::ArenaShape::Octagon ? 8 : 40;
	const float R = ArenaDef->HalfX() * 100.0f;
	const float Inradius = R * FMath::Cos(UE_PI / Sides);
	const float SegLen = 2.0f * R * FMath::Sin(UE_PI / Sides) + T;
	for (int32 i = 0; i < Sides; ++i)
	{
		const float A = UE_TWO_PI * (static_cast<float>(i) + 0.5f) / static_cast<float>(Sides) + (Sides == 8 ? UE_PI / 8.0f : 0.0f);
		const FVector Dir(FMath::Cos(A), FMath::Sin(A), 0.0f);
		const FRotator Rot(0.0f, FMath::RadiansToDegrees(A), 0.0f);
		AddBox(Dir * (Inradius + T * 0.5f) + FVector(0, 0, FloorHeight + H * 0.5f), FVector(T, SegLen, H), Rot, (i % 2 == 0) ? WallColor : WallColor * 0.85f);
		AddBox(Dir * (Inradius + T * 0.5f) + FVector(0, 0, FloorHeight + H + 10.0f), FVector(T + 20.0f, SegLen, 20.0f), Rot, Trim, false);
	}
}

void ABCArenaBuilder::AddRamp(const FVector& Center, float YawDeg, const FVector& SizeCm, const FLinearColor& Color)
{
	// Wedge rising toward local +X: width = Y, length = X, height = Z.
	UProceduralMeshComponent* Mesh = NewObject<UProceduralMeshComponent>(this);
	Mesh->SetupAttachment(RootComponent);
	Mesh->bUseComplexAsSimpleCollision = true;
	const float L = SizeCm.X * 0.5f;
	const float W = SizeCm.Y * 0.5f;
	const float H = SizeCm.Z;
	const TArray<FVector> V = {FVector(-L, -W, 0), FVector(L, -W, 0), FVector(L, W, 0), FVector(-L, W, 0), FVector(L, -W, H), FVector(L, W, H)};
	TArray<int32> T;
	BCArenaDetail::AddTwoSided(T, 0, 4, 5); // slope
	BCArenaDetail::AddTwoSided(T, 0, 5, 3);
	BCArenaDetail::AddTwoSided(T, 1, 2, 5); // back
	BCArenaDetail::AddTwoSided(T, 1, 5, 4);
	BCArenaDetail::AddTwoSided(T, 0, 1, 4); // sides
	BCArenaDetail::AddTwoSided(T, 3, 5, 2);
	const FVector SlopeN = FVector(-H, 0.0f, 2.0f * L).GetSafeNormal();
	const TArray<FVector> N = {SlopeN, FVector(1, 0, 0), FVector(1, 0, 0), SlopeN, SlopeN, SlopeN};
	const TArray<FVector2D> UV = {FVector2D(0, 0), FVector2D(1, 0), FVector2D(1, 1), FVector2D(0, 1), FVector2D(1, 0), FVector2D(1, 1)};
	Mesh->CreateMeshSection(0, V, T, N, UV, TArray<FColor>(), TArray<FProcMeshTangent>(), true);
	Mesh->SetWorldLocationAndRotation(Center, FRotator(0.0f, YawDeg, 0.0f));
	Mesh->SetMaterial(0, BCProps::Material(this, Color));
	Mesh->RegisterComponent();
	Geometry.Add(Mesh);
	// Hazard stripes on the lip.
	AddBox(Center + FRotator(0.0f, YawDeg, 0.0f).RotateVector(FVector(L - 10.0f, 0.0f, H + 2.0f)), FVector(20.0f, SizeCm.Y, 4.0f),
		   FRotator(0.0f, YawDeg, 0.0f), FLinearColor(1.0f, 0.8f, 0.0f), false);
}

void ABCArenaBuilder::BuildProps(bool bSpawnDynamicProps)
{
	using BCArenaDetail::P;
	UWorld* World = GetWorld();
	int32 ColorSlot = 0;
	for (const crash::PropDef& Prop : ArenaDef->props)
	{
		const FVector Pos = P(Prop.pos) + FVector(0, 0, FloorHeight);
		const FLinearColor Color = Prop.hasColor ? BC::ToLinear(Prop.color) : ThemeColor(++ColorSlot);
		const FRotator Yaw(0.0f, Prop.yaw, 0.0f);
		switch (Prop.type)
		{
		case crash::PropType::Ramp:
			AddRamp(Pos, Prop.yaw, P(Prop.size), Color);
			break;
		case crash::PropType::Block:
		{
			UStaticMeshComponent* B = AddBox(Pos + FVector(0, 0, Prop.size.z * 50.0f), P(Prop.size), Yaw, Color);
			if (Prop.soft)
			{
				B->ComponentTags.Add(TEXT("Soft"));
			}
			break;
		}
		case crash::PropType::Container:
			AddBox(Pos + FVector(0, 0, 130.0f), FVector(1220.0f, 244.0f, 260.0f), Yaw, Color);
			AddBox(Pos + FVector(0, 0, 262.0f), FVector(1220.0f, 250.0f, 6.0f), Yaw, Color * 0.7f, false);
			break;
		case crash::PropType::Cylinder:
		{
			UStaticMeshComponent* C = NewObject<UStaticMeshComponent>(this);
			C->SetMobility(EComponentMobility::Static);
			C->SetStaticMesh(BCProps::Cylinder());
			C->SetupAttachment(RootComponent);
			C->SetWorldLocation(Pos + FVector(0, 0, Prop.height * 50.0f));
			C->SetWorldScale3D(FVector(Prop.radius * 2.0f, Prop.radius * 2.0f, Prop.height));
			C->SetMaterial(0, BCProps::Material(this, Color));
			C->SetCollisionProfileName(TEXT("BlockAll"));
			C->RegisterComponent();
			Geometry.Add(C);
			break;
		}
		case crash::PropType::Light:
		{
			UPointLightComponent* L = NewObject<UPointLightComponent>(this);
			L->SetupAttachment(RootComponent);
			L->SetMobility(EComponentMobility::Movable);
			L->SetWorldLocation(Pos + FVector(0, 0, 600.0f));
			L->SetLightColor(Prop.hasColor ? Color : FLinearColor(1.0f, 0.85f, 0.6f));
			L->SetIntensity(50000.0f * Prop.intensity);
			L->SetAttenuationRadius(FMath::Max(Prop.radius * 100.0f, 2500.0f));
			L->SetCastShadows(false);
			L->RegisterComponent();
			AddBox(Pos + FVector(0, 0, 300.0f), FVector(30.0f, 30.0f, 600.0f), FRotator::ZeroRotator, FLinearColor(0.1f, 0.1f, 0.1f));
			break;
		}
		case crash::PropType::HillZone:
		{
			UStaticMeshComponent* Ring = NewObject<UStaticMeshComponent>(this);
			Ring->SetMobility(EComponentMobility::Static);
			Ring->SetStaticMesh(BCProps::Cylinder());
			Ring->SetupAttachment(RootComponent);
			Ring->SetWorldLocation(Pos + FVector(0, 0, 1.0f));
			Ring->SetWorldScale3D(FVector(Prop.radius * 2.0f, Prop.radius * 2.0f, 0.02f));
			Ring->SetMaterial(0, BCProps::Material(this, FLinearColor(0.9f, 0.75f, 0.05f)));
			Ring->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Ring->RegisterComponent();
			Geometry.Add(Ring);
			for (int32 k = 0; k < 6; ++k)
			{
				const float A = UE_TWO_PI * k / 6.0f;
				AddBox(Pos + FVector(FMath::Cos(A), FMath::Sin(A), 0.0f) * Prop.radius * 100.0f + FVector(0, 0, 150.0f), FVector(25.0f, 25.0f, 300.0f),
					   FRotator::ZeroRotator, FLinearColor(1.0f, 0.85f, 0.1f), false);
			}
			break;
		}
		case crash::PropType::Pit:
		{
			ABCPitActor* Pit = World->SpawnActor<ABCPitActor>(Pos, FRotator::ZeroRotator);
			if (Pit)
			{
				Pit->Setup(FVector2D(Prop.size.x * 100.0f, Prop.size.y * 100.0f), ArenaDef->shape != crash::ArenaShape::Rect);
				Pits.Add(Pit);
				SpawnedActors.Add(Pit);
			}
			break;
		}
		default:
			break;
		}
		if (!bSpawnDynamicProps)
		{
			continue;
		}
		// Interactive props: server only, replicated to clients.
		FActorSpawnParameters Params;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		switch (Prop.type)
		{
		case crash::PropType::JumpPad:
		case crash::PropType::BoostPad:
			if (ABCPadActor* Pad = World->SpawnActor<ABCPadActor>(Pos + FVector(0, 0, 120.0f), Yaw, Params))
			{
				Pad->Setup(Prop.type == crash::PropType::JumpPad ? EBCPadType::Jump : EBCPadType::Boost, FVector2D(Prop.size.x * 100.0f, Prop.size.y * 100.0f),
						   Prop.strength);
				SpawnedActors.Add(Pad);
			}
			break;
		case crash::PropType::Spinner:
			if (ABCHazardActor* H = World->SpawnActor<ABCHazardActor>(Pos, Yaw, Params))
			{
				H->SetupSpinner(Prop.radius * 100.0f, Prop.height * 100.0f, Prop.speed, Color);
				SpawnedActors.Add(H);
			}
			break;
		case crash::PropType::Crusher:
			if (ABCHazardActor* H = World->SpawnActor<ABCHazardActor>(Pos, Yaw, Params))
			{
				H->SetupCrusher(P(Prop.size), Prop.period, Prop.travel * 100.0f, Color);
				SpawnedActors.Add(H);
			}
			break;
		case crash::PropType::CrateStack:
		{
			const float Edge = Prop.size.x * 100.0f;
			for (int32 i = 0; i < Prop.count; ++i)
			{
				const int32 Layer = i / 4;
				const int32 Slot = i % 4;
				const FVector Offset((Slot % 2) * Edge * 1.05f, (Slot / 2) * Edge * 1.05f, Edge * (0.5f + Layer) + Layer * 2.0f);
				if (ABCCrateActor* Crate = World->SpawnActor<ABCCrateActor>(Pos + Yaw.RotateVector(Offset), Yaw, Params))
				{
					Crate->Setup(Edge, Color);
					SpawnedActors.Add(Crate);
				}
			}
			break;
		}
		case crash::PropType::Barrel:
			for (int32 i = 0; i < Prop.count; ++i)
			{
				const FVector Offset((i % 3) * 80.0f, (i / 3) * 80.0f, 45.0f);
				if (ABCBarrelActor* Barrel = World->SpawnActor<ABCBarrelActor>(Pos + Yaw.RotateVector(Offset), Yaw, Params))
				{
					SpawnedActors.Add(Barrel);
				}
			}
			break;
		case crash::PropType::Wreck:
			if (ABCWreckProp* Wreck = World->SpawnActor<ABCWreckProp>(Pos + FVector(0, 0, 120.0f), Yaw, Params))
			{
				Wreck->Setup(BC::ToFString(std::string(crash::ToString(Prop.vehicleClass))), Prop.hasColor ? Color : FLinearColor(0.3f, 0.16f, 0.08f));
				SpawnedActors.Add(Wreck);
			}
			break;
		case crash::PropType::Fence:
			if (ABCFenceActor* Fence = World->SpawnActor<ABCFenceActor>(Pos, FRotator::ZeroRotator, Params))
			{
				Fence->Setup(Pos, P(Prop.to) + FVector(0, 0, FloorHeight), 160.0f, Color);
				SpawnedActors.Add(Fence);
			}
			break;
		default:
			break;
		}
	}
}

void ABCArenaBuilder::BuildLighting()
{
	const FString Preset = BC::ToFString(ArenaDef->lightingPreset);
	float Pitch = -45.0f;
	float Yaw = 35.0f;
	float Intensity = 10.0f;
	FLinearColor SunColor(1.0f, 0.96f, 0.9f);
	float SkyIntensity = 1.0f;
	float FogDensity = ArenaDef->fog;
	if (Preset == TEXT("sunset"))
	{
		Pitch = -9.0f;
		Intensity = 7.0f;
		SunColor = FLinearColor(1.0f, 0.62f, 0.35f);
	}
	else if (Preset == TEXT("dusk"))
	{
		Pitch = -3.0f;
		Intensity = 3.0f;
		SunColor = FLinearColor(0.9f, 0.5f, 0.45f);
		SkyIntensity = 0.7f;
	}
	else if (Preset == TEXT("night"))
	{
		Pitch = -50.0f;
		Intensity = 0.35f;
		SunColor = FLinearColor(0.55f, 0.65f, 1.0f);
		SkyIntensity = 0.25f;
	}
	else if (Preset == TEXT("overcast"))
	{
		Pitch = -65.0f;
		Intensity = 4.0f;
		SunColor = FLinearColor(0.85f, 0.88f, 0.92f);
		FogDensity = FMath::Max(FogDensity, 0.04f);
	}
	else if (Preset == TEXT("storm"))
	{
		Pitch = -60.0f;
		Intensity = 1.5f;
		SunColor = FLinearColor(0.6f, 0.65f, 0.75f);
		SkyIntensity = 0.5f;
		FogDensity = FMath::Max(FogDensity, 0.08f);
	}
	const FLinearColor Tint = BC::ToLinear(ArenaDef->tint);

	Sun = NewObject<UDirectionalLightComponent>(this);
	Sun->SetMobility(EComponentMobility::Movable);
	Sun->SetupAttachment(RootComponent);
	Sun->SetWorldRotation(FRotator(Pitch, Yaw, 0.0f));
	Sun->SetIntensity(Intensity);
	Sun->SetLightColor(SunColor * Tint);
	Sun->SetAtmosphereSunLight(true);
	Sun->SetCastShadows(true);
	Sun->RegisterComponent();

	Atmosphere = NewObject<USkyAtmosphereComponent>(this);
	Atmosphere->SetupAttachment(RootComponent);
	Atmosphere->RegisterComponent();

	SkyLight = NewObject<USkyLightComponent>(this);
	SkyLight->SetMobility(EComponentMobility::Movable);
	SkyLight->SetupAttachment(RootComponent);
	SkyLight->bRealTimeCapture = true;
	SkyLight->SourceType = ESkyLightSourceType::SLS_CapturedScene;
	SkyLight->SetIntensity(SkyIntensity);
	SkyLight->RegisterComponent();

	Fog = NewObject<UExponentialHeightFogComponent>(this);
	Fog->SetupAttachment(RootComponent);
	Fog->SetWorldLocation(FVector(0, 0, FloorHeight - 500.0f));
	Fog->SetFogDensity(FogDensity);
	Fog->SetFogInscatteringColor(FLinearColor(0.45f, 0.55f, 0.7f) * Tint);
	Fog->SetVolumetricFog(true);
	Fog->RegisterComponent();

	PostProcess = NewObject<UPostProcessComponent>(this);
	PostProcess->SetupAttachment(RootComponent);
	PostProcess->bUnbound = true;
	PostProcess->RegisterComponent();
	ApplyPostProcessSettings();

	// Night arenas get flood lights in the corners.
	if (IsNight())
	{
		const float R = ArenaDef->HalfX() * 100.0f * 0.95f;
		for (int32 i = 0; i < 4; ++i)
		{
			const float A = UE_HALF_PI * i + UE_PI / 4.0f;
			UPointLightComponent* Flood = NewObject<UPointLightComponent>(this);
			Flood->SetupAttachment(RootComponent);
			Flood->SetMobility(EComponentMobility::Movable);
			Flood->SetWorldLocation(FVector(FMath::Cos(A) * R * 0.8f, FMath::Sin(A) * R * 0.8f, FloorHeight + 1500.0f));
			Flood->SetIntensity(400000.0f);
			Flood->SetAttenuationRadius(R * 2.0f);
			Flood->SetLightColor(FLinearColor(1.0f, 0.92f, 0.8f));
			Flood->SetCastShadows(true);
			Flood->RegisterComponent();
		}
	}
}

void ABCArenaBuilder::ApplyPostProcessSettings()
{
	if (!PostProcess)
	{
		return;
	}
	const UBCSettingsSubsystem* Settings = UBCSettingsSubsystem::Get(this);
	auto Value = [Settings](const TCHAR* Key, float Default) { return Settings ? Settings->PostValue(Key, Default) : Default; };
	FPostProcessSettings& S = PostProcess->Settings;
	S.bOverride_VignetteIntensity = true;
	S.VignetteIntensity = Value(TEXT("vignette"), 0.4f);
	S.bOverride_FilmGrainIntensity = true;
	S.FilmGrainIntensity = Value(TEXT("filmGrain"), 0.0f);
	S.bOverride_SceneFringeIntensity = true;
	S.SceneFringeIntensity = Value(TEXT("chromaticAberration"), 0.0f);
	S.bOverride_MotionBlurAmount = true;
	S.MotionBlurAmount = Value(TEXT("motionBlur"), 0.25f);
	S.bOverride_BloomIntensity = true;
	S.BloomIntensity = Value(TEXT("bloom"), 0.675f);
	S.bOverride_LensFlareIntensity = true;
	S.LensFlareIntensity = Value(TEXT("lensFlare"), 1.0f);
	S.bOverride_AmbientOcclusionIntensity = true;
	S.AmbientOcclusionIntensity = Value(TEXT("ambientOcclusion"), 0.5f);
	S.bOverride_AutoExposureBias = true;
	S.AutoExposureBias = Value(TEXT("exposureBias"), 0.0f) + (IsNight() ? 1.0f : 0.0f);
}

float ABCArenaBuilder::KillZ() const
{
	return ArenaDef ? FMath::Min(ArenaDef->killZ * 100.0f, FloorHeight - 1500.0f) : -3000.0f;
}

bool ABCArenaBuilder::IsNight() const
{
	return ArenaDef && (ArenaDef->lightingPreset == "night" || ArenaDef->theme == "neon");
}

bool ABCArenaBuilder::IsInsideFloor(const FVector& WorldPoint, float MarginCm) const
{
	return ArenaDef && ArenaDef->IsInside(BC::ToCrash(WorldPoint), MarginCm / 100.0f);
}

bool ABCArenaBuilder::IsOverPit(const FVector& WorldPoint) const
{
	for (const ABCPitActor* Pit : Pits)
	{
		if (Pit && Pit->Contains(WorldPoint))
		{
			return true;
		}
	}
	return false;
}

bool ABCArenaBuilder::HasHill() const
{
	return ArenaDef && ArenaDef->FindHill() != nullptr;
}

FVector ABCArenaBuilder::HillCenter() const
{
	const crash::PropDef* Hill = ArenaDef ? ArenaDef->FindHill() : nullptr;
	return Hill ? BCArenaDetail::P(Hill->pos) + FVector(0, 0, FloorHeight) : Center();
}

float ABCArenaBuilder::HillRadius() const
{
	const crash::PropDef* Hill = ArenaDef ? ArenaDef->FindHill() : nullptr;
	return Hill ? Hill->radius * 100.0f : 0.0f;
}

TArray<FTransform> ABCArenaBuilder::SpawnTransforms(int32 Team) const
{
	TArray<FTransform> Out;
	if (!ArenaDef)
	{
		Out.Add(FTransform(FVector(0, 0, FloorHeight)));
		return Out;
	}
	for (const crash::SpawnDef& S : ArenaDef->spawns)
	{
		if (Team >= 0 && S.team >= 0 && S.team != Team)
		{
			continue;
		}
		Out.Add(FTransform(FRotator(0.0f, S.yaw, 0.0f), FVector(S.pos.x * 100.0f, S.pos.y * 100.0f, FloorHeight)));
	}
	if (Out.Num() == 0)
	{
		for (const crash::SpawnDef& S : ArenaDef->spawns)
		{
			Out.Add(FTransform(FRotator(0.0f, S.yaw, 0.0f), FVector(S.pos.x * 100.0f, S.pos.y * 100.0f, FloorHeight)));
		}
	}
	return Out;
}

FVector ABCArenaBuilder::Collectible(int32 Index) const
{
	if (!ArenaDef || Index < 0 || Index >= static_cast<int32>(ArenaDef->collectibles.size()))
	{
		return Center();
	}
	return BCArenaDetail::P(ArenaDef->collectibles[static_cast<size_t>(Index)]) + FVector(0, 0, FloorHeight + 80.0f);
}
