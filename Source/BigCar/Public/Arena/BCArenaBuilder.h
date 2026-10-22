// Copyright BigCar Team. All Rights Reserved.
//
// Builds a whole arena at runtime from arenas.json: floor (with real holes for pits), walls, ramps, blocks,
// containers, interactive props, lighting presets (sun/sky/fog/post-process) and spawn points. No hand-made maps
// are required; an arena can still point at a hand-built level later.
#pragma once

#include "CoreMinimal.h"
#include "CrashCore/Database.h"
#include "GameFramework/Actor.h"

#include "BCArenaBuilder.generated.h"

class UStaticMeshComponent;
class UProceduralMeshComponent;
class UDirectionalLightComponent;
class USkyLightComponent;
class USkyAtmosphereComponent;
class UExponentialHeightFogComponent;
class UPostProcessComponent;
class ABCPitActor;
class UPhysicalMaterial;

UCLASS()
class BIGCAR_API ABCArenaBuilder : public AActor
{
	GENERATED_BODY()

public:
	ABCArenaBuilder();

	// Server and clients both build the static geometry; interactive props are spawned on the server only.
	void Build(const FString& ArenaId, bool bSpawnDynamicProps);
	virtual void Tick(float DeltaSeconds) override;

	const crash::ArenaDef* Def() const { return ArenaDef; }
	float FloorZ() const { return FloorHeight; }
	float KillZ() const;
	bool IsNight() const;
	bool IsInsideFloor(const FVector& WorldPoint, float MarginCm = 0.0f) const;
	bool IsOverPit(const FVector& WorldPoint) const;
	FVector HillCenter() const;
	float HillRadius() const;
	bool HasHill() const;
	// Spawn transforms (z = floor). Team -1 = all FFA spawns.
	TArray<FTransform> SpawnTransforms(int32 Team) const;
	FVector Collectible(int32 Index) const;
	FVector Center() const { return FVector(0.0f, 0.0f, FloorHeight); }

	void ApplyPostProcessSettings();

private:
	UStaticMeshComponent* AddBox(const FVector& Center, const FVector& SizeCm, const FRotator& Rot, const FLinearColor& Color, bool bCollision = true);
	void BuildFloor();
	void BuildWalls();
	void BuildProps(bool bSpawnDynamicProps);
	void BuildLighting();
	void AddRamp(const FVector& Center, float YawDeg, const FVector& SizeCm, const FLinearColor& Color);
	void AddPolygonFloor(int32 Sides, float RadiusCm, float Z, const FLinearColor& Color);
	FLinearColor ThemeColor(int32 Slot) const;

	UPROPERTY()
	TArray<TObjectPtr<UPrimitiveComponent>> Geometry;

	UPROPERTY()
	TArray<TObjectPtr<AActor>> SpawnedActors;

	UPROPERTY()
	TObjectPtr<UDirectionalLightComponent> Sun;

	UPROPERTY()
	TObjectPtr<USkyLightComponent> SkyLight;

	UPROPERTY()
	TObjectPtr<USkyAtmosphereComponent> Atmosphere;

	UPROPERTY()
	TObjectPtr<UExponentialHeightFogComponent> Fog;

	UPROPERTY()
	TObjectPtr<UPostProcessComponent> PostProcess;

	UPROPERTY()
	TObjectPtr<UPhysicalMaterial> FloorMaterial;

	UPROPERTY()
	TArray<TObjectPtr<ABCPitActor>> Pits;

	const crash::ArenaDef* ArenaDef = nullptr;
	float FloorHeight = 0.0f;
	FDelegateHandle SettingsHandle;
};
