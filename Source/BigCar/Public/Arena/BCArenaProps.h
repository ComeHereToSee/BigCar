// Copyright BigCar Team. All Rights Reserved.
//
// Interactive arena props: jump/boost pads, rotating spinners, crushers, destructible crates, explosive barrels,
// heavy car wrecks, breakable fences, pits, campaign collectibles and exploration secrets.
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

#include "BCArenaProps.generated.h"

class UBoxComponent;
class UStaticMeshComponent;
class UProceduralMeshComponent;
class UStaticMesh;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UPointLightComponent;
class ABCVehiclePawn;

// Shared helpers for prop visuals.
namespace BCProps
{
UStaticMesh* Cube();
UStaticMesh* Cylinder();
UStaticMesh* Cone();
UStaticMesh* Sphere();
UMaterialInstanceDynamic* Material(UObject* Outer, const FLinearColor& Color);
UStaticMeshComponent* AddMesh(AActor* Owner, USceneComponent* Parent, UStaticMesh* Mesh, const FTransform& Relative,
							  const FLinearColor& Color, bool bCollision);
} // namespace BCProps

UENUM()
enum class EBCPadType : uint8
{
	Jump,
	Boost
};

UCLASS()
class BIGCAR_API ABCPadActor : public AActor
{
	GENERATED_BODY()

public:
	ABCPadActor();
	void Setup(EBCPadType InType, const FVector2D& SizeCm, float Strength);

private:
	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep,
				   const FHitResult& Sweep);

	UPROPERTY()
	TObjectPtr<UBoxComponent> Trigger;

	EBCPadType Type = EBCPadType::Jump;
	float StrengthMs = 10.0f;
	TMap<TWeakObjectPtr<AActor>, double> Cooldowns;
};

// Kinematic hazard: rotating arm (spinner) or vertical piston (crusher). Hits deal extra damage.
UCLASS()
class BIGCAR_API ABCHazardActor : public AActor
{
	GENERATED_BODY()

public:
	ABCHazardActor();
	void SetupSpinner(float RadiusCm, float HeightCm, float DegPerSec, const FLinearColor& Color);
	void SetupCrusher(const FVector& SizeCm, float PeriodSec, float TravelCm, const FLinearColor& Color);
	virtual void Tick(float DeltaSeconds) override;
	float DamageMultiplier() const { return bCrusher ? 2.5f : 1.6f; }

private:
	UPROPERTY()
	TObjectPtr<USceneComponent> Pivot;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Moving;

	bool bCrusher = false;
	float Speed = 90.0f;
	float Period = 4.0f;
	float Travel = 300.0f;
	float Phase = 0.0f;
	FVector BaseLocation = FVector::ZeroVector;
};

// Base class for things vehicles can smash.
UCLASS(Abstract)
class BIGCAR_API ABCBreakableProp : public AActor
{
	GENERATED_BODY()

public:
	// Server: a vehicle hit this prop with the given closing speed (m/s). Returns true if it broke.
	virtual bool TakeHit(ABCVehiclePawn* By, float ClosingSpeed, const FVector& WorldPoint);
	bool IsBroken() const { return bBroken; }
	// Damage scale the car takes from hitting this prop (soft props barely hurt).
	virtual float CarDamageScale() const { return 0.15f; }

protected:
	UFUNCTION(NetMulticast, Reliable)
	void MulticastBreak(FVector_NetQuantize Point);

	virtual void OnBroken(const FVector& Point);

	float Health = 1.0f;
	float BreakSpeed = 6.0f;
	bool bBroken = false;
	FLinearColor DebrisColor = FLinearColor(0.5f, 0.35f, 0.2f);
};

UCLASS()
class BIGCAR_API ABCCrateActor : public ABCBreakableProp
{
	GENERATED_BODY()

public:
	ABCCrateActor();
	void Setup(float EdgeCm, const FLinearColor& Color);

protected:
	virtual void OnBroken(const FVector& Point) override;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;
};

UCLASS()
class BIGCAR_API ABCBarrelActor : public ABCBreakableProp
{
	GENERATED_BODY()

public:
	ABCBarrelActor();
	virtual float CarDamageScale() const override { return 0.2f; }

protected:
	virtual void OnBroken(const FVector& Point) override;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Mesh;
};

UCLASS()
class BIGCAR_API ABCWreckProp : public ABCBreakableProp
{
	GENERATED_BODY()

public:
	ABCWreckProp();
	void Setup(const FString& VehicleClass, const FLinearColor& Color);
	virtual float CarDamageScale() const override { return 0.45f; }

protected:
	virtual void OnBroken(const FVector& Point) override;

	UPROPERTY()
	TObjectPtr<UBoxComponent> Box;

	UPROPERTY()
	TObjectPtr<UProceduralMeshComponent> Shell;
};

UCLASS()
class BIGCAR_API ABCFenceActor : public ABCBreakableProp
{
	GENERATED_BODY()

public:
	ABCFenceActor();
	void Setup(const FVector& From, const FVector& To, float HeightCm, const FLinearColor& Color);
	virtual float CarDamageScale() const override { return 0.05f; }

protected:
	virtual void OnBroken(const FVector& Point) override;

	UPROPERTY()
	TArray<TObjectPtr<UStaticMeshComponent>> Boards;
};

// Hole / kill zone: vehicles entering it are ring-outs.
UCLASS()
class BIGCAR_API ABCPitActor : public AActor
{
	GENERATED_BODY()

public:
	ABCPitActor();
	void Setup(const FVector2D& SizeCm, bool bVisualHole);
	bool Contains(const FVector& WorldPoint) const;

private:
	FVector2D Extent = FVector2D(500.0f, 500.0f);
};

// Campaign collectible ("scrap crown"): spins, glows, picked up by the player.
UCLASS()
class BIGCAR_API ABCCollectibleActor : public AActor
{
	GENERATED_BODY()

public:
	ABCCollectibleActor();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	int32 Index = 0;
	bool bHub = false;

private:
	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep,
				   const FHitResult& Sweep);

	UPROPERTY()
	TObjectPtr<UBoxComponent> Trigger;

	UPROPERTY()
	TObjectPtr<UStaticMeshComponent> Visual;

	UPROPERTY()
	TObjectPtr<UPointLightComponent> Glow;

	float Time = 0.0f;
	bool bTaken = false;
};

// Hub exploration secret: discovered by driving into it.
UCLASS()
class BIGCAR_API ABCSecretZone : public AActor
{
	GENERATED_BODY()

public:
	ABCSecretZone();
	void Setup(const FString& InSecretId, float RadiusCm);
	FString SecretId;

private:
	UFUNCTION()
	void OnOverlap(UPrimitiveComponent* Comp, AActor* Other, UPrimitiveComponent* OtherComp, int32 BodyIndex, bool bFromSweep,
				   const FHitResult& Sweep);

	UPROPERTY()
	TObjectPtr<UBoxComponent> Trigger;

	bool bFound = false;
};
