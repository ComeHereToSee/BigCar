// Copyright BigCar Team. All Rights Reserved.
//
// Procedural, deformable car body built from the vehicle's real dimensions and body style. It is the stand-in until
// artists provide licensed models: the shape reads clearly as hatchback / sedan / SUV / pickup / supercar / van /
// truck, has wheel wells, a glass greenhouse, and dents where the car gets hit.
#pragma once

#include "CoreMinimal.h"
#include "CrashCore/Database.h"

class UProceduralMeshComponent;

struct FBCBodyStyle
{
	float NoseHeight = 0.5f;	  // fractions of total height
	float HoodHeight = 0.6f;
	float BeltHeight = 0.63f;
	float TailHeight = 0.62f;
	float RearWindowBase = 0.2f; // fractions of length from the rear
	float RoofRear = 0.32f;
	float RoofFront = 0.6f;
	float WindshieldBase = 0.74f;
	float RoofWidth = 0.82f;
	float EndTaper = 0.08f;
};

FBCBodyStyle GetBodyStyle(crash::VehicleClass Body);

class BIGCAR_API FBCBodyMesh
{
public:
	// Dimensions in meters; BodyCenterHeight = height of the body origin above the ground at rest.
	void Build(const crash::VehicleDef& Def, float BodyCenterHeight, int32 Segments);
	void CreateSections(UProceduralMeshComponent* Mesh) const;
	void UpdateSections(UProceduralMeshComponent* Mesh) const;

	// Pushes the body in around LocalPointCm along LocalDir (unit, pointing into the car). Returns true if anything moved.
	bool ApplyDent(const FVector& LocalPointCm, const FVector& LocalDir, float RadiusCm, float DepthCm);
	void Restore();

	static constexpr int32 BodySection = 0;
	static constexpr int32 GlassSection = 1;

private:
	struct FSection
	{
		TArray<FVector> Original;
		TArray<FVector> Vertices;
		TArray<int32> Triangles; // both windings: visible regardless of culling convention
		TArray<FVector> Normals;
		TArray<FVector2D> UVs;
		TArray<FVector> OutwardHint; // per-triangle reference used to orient normals
		void ComputeNormals();
	};

	void AddLoftQuad(FSection& S, int32 A, int32 B, int32 C, int32 D, const FVector& Center);
	void AddTriangle(FSection& S, int32 A, int32 B, int32 C, const FVector& Center);

	FSection Sections[2];
	FVector HalfExtentsCm = FVector::ZeroVector;
	float MaxDeformCm = 35.0f;
};
