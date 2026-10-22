// Copyright BigCar Team. All Rights Reserved.

#include "Vehicle/BCBodyMesh.h"

#include "ProceduralMeshComponent.h"

FBCBodyStyle GetBodyStyle(crash::VehicleClass Body)
{
	FBCBodyStyle S;
	switch (Body)
	{
	case crash::VehicleClass::Compact:
	case crash::VehicleClass::Hatchback:
		S = {0.46f, 0.56f, 0.6f, 0.6f, 0.04f, 0.14f, 0.56f, 0.72f, 0.84f, 0.09f};
		break;
	case crash::VehicleClass::Wagon:
		S = {0.46f, 0.57f, 0.6f, 0.6f, 0.03f, 0.08f, 0.6f, 0.74f, 0.85f, 0.07f};
		break;
	case crash::VehicleClass::Coupe:
		S = {0.44f, 0.54f, 0.58f, 0.58f, 0.22f, 0.4f, 0.58f, 0.75f, 0.78f, 0.08f};
		break;
	case crash::VehicleClass::Muscle:
		S = {0.5f, 0.58f, 0.6f, 0.6f, 0.2f, 0.38f, 0.55f, 0.68f, 0.8f, 0.05f};
		break;
	case crash::VehicleClass::Sports:
		S = {0.38f, 0.48f, 0.54f, 0.56f, 0.18f, 0.38f, 0.55f, 0.72f, 0.76f, 0.1f};
		break;
	case crash::VehicleClass::Supercar:
	case crash::VehicleClass::Hypercar:
		S = {0.28f, 0.4f, 0.5f, 0.56f, 0.2f, 0.38f, 0.56f, 0.73f, 0.7f, 0.12f};
		break;
	case crash::VehicleClass::Luxury:
		S = {0.52f, 0.62f, 0.64f, 0.63f, 0.2f, 0.32f, 0.62f, 0.75f, 0.84f, 0.06f};
		break;
	case crash::VehicleClass::Suv:
		S = {0.58f, 0.64f, 0.66f, 0.7f, 0.02f, 0.06f, 0.63f, 0.76f, 0.88f, 0.05f};
		break;
	case crash::VehicleClass::Offroad:
		S = {0.62f, 0.66f, 0.66f, 0.72f, 0.02f, 0.05f, 0.67f, 0.74f, 0.92f, 0.03f};
		break;
	case crash::VehicleClass::Pickup:
		S = {0.6f, 0.65f, 0.64f, 0.64f, 0.42f, 0.44f, 0.66f, 0.76f, 0.88f, 0.04f};
		break;
	case crash::VehicleClass::Van:
		S = {0.42f, 0.48f, 0.55f, 0.95f, 0.0f, 0.02f, 0.8f, 0.9f, 0.92f, 0.03f};
		break;
	case crash::VehicleClass::Truck:
		S = {0.6f, 0.66f, 0.62f, 0.36f, 0.62f, 0.63f, 0.95f, 0.985f, 0.95f, 0.02f};
		break;
	default: // sedan
		S = {0.5f, 0.6f, 0.63f, 0.62f, 0.2f, 0.32f, 0.6f, 0.74f, 0.82f, 0.07f};
		break;
	}
	return S;
}

namespace BCBodyDetail
{
float Piecewise(float X, std::initializer_list<FVector2f> Points)
{
	const FVector2f* Prev = nullptr;
	for (const FVector2f& P : Points)
	{
		if (Prev && X <= P.X)
		{
			const float Span = FMath::Max(P.X - Prev->X, 1.0e-4f);
			return FMath::Lerp(Prev->Y, P.Y, FMath::Clamp((X - Prev->X) / Span, 0.0f, 1.0f));
		}
		Prev = &P;
	}
	return Prev ? Prev->Y : 0.0f;
}
} // namespace BCBodyDetail

void FBCBodyMesh::AddTriangle(FSection& S, int32 A, int32 B, int32 C, const FVector& Center)
{
	// Emit both windings so the body renders correctly whatever the face culling convention; normals are oriented
	// outward from Center explicitly.
	S.Triangles.Append({A, B, C, A, C, B});
	const FVector Centroid = (S.Vertices[A] + S.Vertices[B] + S.Vertices[C]) / 3.0;
	S.OutwardHint.Add(Centroid - Center);
}

void FBCBodyMesh::AddLoftQuad(FSection& S, int32 A, int32 B, int32 C, int32 D, const FVector& Center)
{
	AddTriangle(S, A, B, C, Center);
	AddTriangle(S, A, C, D, Center);
}

void FBCBodyMesh::FSection::ComputeNormals()
{
	Normals.Init(FVector::ZeroVector, Vertices.Num());
	for (int32 T = 0; T + 5 < Triangles.Num() && T / 6 < OutwardHint.Num(); T += 6)
	{
		const int32 A = Triangles[T];
		const int32 B = Triangles[T + 1];
		const int32 C = Triangles[T + 2];
		FVector N = FVector::CrossProduct(Vertices[B] - Vertices[A], Vertices[C] - Vertices[A]);
		if (FVector::DotProduct(N, OutwardHint[T / 6]) < 0.0)
		{
			N = -N;
		}
		Normals[A] += N;
		Normals[B] += N;
		Normals[C] += N;
	}
	for (FVector& N : Normals)
	{
		N = N.GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector);
	}
}

void FBCBodyMesh::Build(const crash::VehicleDef& Def, float BodyCenterHeight, int32 Segments)
{
	using BCBodyDetail::Piecewise;
	const FBCBodyStyle St = GetBodyStyle(Def.body);
	const crash::VehicleDimensions& D = Def.dimensions;
	const float L = D.length * 100.0f;
	const float W = D.width * 100.0f;
	const float H = D.height * 100.0f;
	const float Ground = -BodyCenterHeight * 100.0f; // local z of the ground plane
	const float Clear = D.groundClearance * 100.0f;
	const float WheelR = D.wheelRadius * 100.0f;
	const float WheelW = D.wheelWidth * 100.0f;
	const float AxleF = D.wheelbase * 50.0f;
	const float AxleR = -D.wheelbase * 50.0f;
	HalfExtentsCm = FVector(L * 0.5f, W * 0.5f, (H - Clear) * 0.5f);
	MaxDeformCm = FMath::Clamp(L * 0.08f, 20.0f, 45.0f);
	Segments = FMath::Clamp(Segments, 6, 40);

	for (FSection& S : Sections)
	{
		S = FSection();
	}

	// ---------------- Lower body loft (10 points per ring).
	FSection& Bd = Sections[BodySection];
	constexpr int32 RingPoints = 10;
	TArray<float> Xs;
	for (int32 i = 0; i <= Segments; ++i)
	{
		Xs.Add(static_cast<float>(i) / static_cast<float>(Segments));
	}
	// Extra rings around the wheel arches so the wells are well defined.
	for (const float Axle : {AxleF, AxleR})
	{
		for (const float Off : {-1.2f, -0.85f, 0.0f, 0.85f, 1.2f})
		{
			Xs.Add(FMath::Clamp((Axle + Off * WheelR) / L + 0.5f, 0.0f, 1.0f));
		}
	}
	Xs.Sort();
	for (int32 i = Xs.Num() - 1; i > 0; --i)
	{
		if (FMath::Abs(Xs[i] - Xs[i - 1]) < 0.005f)
		{
			Xs.RemoveAt(i);
		}
	}

	const float ArchTop = Clear + 2.0f * WheelR + 4.0f;
	for (const float Fx : Xs)
	{
		const float X = (Fx - 0.5f) * L;
		const float Top = H * Piecewise(Fx, {{0.0f, St.TailHeight * 0.85f}, {0.05f, St.TailHeight}, {FMath::Max(St.RearWindowBase, 0.06f), St.BeltHeight},
											 {St.WindshieldBase, St.BeltHeight}, {0.9f, St.HoodHeight}, {1.0f, St.NoseHeight}});
		const float EndT = FMath::Min(Fx, 1.0f - Fx);
		const float Taper = 1.0f - St.EndTaper * FMath::Square(1.0f - FMath::Clamp(EndT / 0.12f, 0.0f, 1.0f));
		const float HalfW = W * 0.5f * Taper;
		const float Bottom = Clear + (EndT < 0.06f ? (0.06f - EndT) * H * 0.6f : 0.0f);
		const float Hgt = FMath::Max(Top - Bottom, 10.0f);
		// Wheel arch factor.
		float Arch = 0.0f;
		for (const float Axle : {AxleF, AxleR})
		{
			Arch = FMath::Max(Arch, 1.0f - FMath::SmoothStep(0.8f, 1.15f, FMath::Abs(X - Axle) / WheelR));
		}
		const float Inner = FMath::Max(HalfW - WheelW - 6.0f, HalfW * 0.45f);
		const float ArchZ = FMath::Min(ArchTop, Top - 8.0f);
		const float R1y = FMath::Lerp(HalfW - 5.0f, Inner, Arch);
		const float R2y = FMath::Lerp(HalfW - 2.0f, Inner, Arch);
		const float R2z = FMath::Lerp(Bottom + 0.15f * Hgt, ArchZ, Arch);
		const float R3z = FMath::Lerp(Bottom + 0.3f * Hgt, ArchZ, Arch);
		const float R4z = FMath::Max(Bottom + 0.75f * Hgt, R3z + 4.0f);
		const float R5y = HalfW * 0.92f;
		const FVector2f Right[5] = {{R1y, Bottom}, {R2y, R2z}, {HalfW, R3z}, {HalfW, FMath::Min(R4z, Top - 2.0f)}, {R5y, Top}};
		// Ring order: R1..R5 then L5..L1.
		for (int32 k = 0; k < 5; ++k)
		{
			Bd.Vertices.Add(FVector(X, Right[k].X, Ground + Right[k].Y));
		}
		for (int32 k = 4; k >= 0; --k)
		{
			Bd.Vertices.Add(FVector(X, -Right[k].X, Ground + Right[k].Y));
		}
	}
	const int32 Rings = Xs.Num();
	const float MidZ = Ground + Clear + H * 0.3f;
	for (int32 r = 0; r + 1 < Rings; ++r)
	{
		const FVector Center((Xs[r] + Xs[r + 1] - 1.0f) * 0.5f * L, 0.0f, MidZ);
		for (int32 k = 0; k < RingPoints; ++k)
		{
			const int32 K2 = (k + 1) % RingPoints;
			AddLoftQuad(Bd, r * RingPoints + k, r * RingPoints + K2, (r + 1) * RingPoints + K2, (r + 1) * RingPoints + k, Center);
		}
	}
	// End caps.
	for (const int32 r : {0, Rings - 1})
	{
		FVector Centroid = FVector::ZeroVector;
		for (int32 k = 0; k < RingPoints; ++k)
		{
			Centroid += Bd.Vertices[r * RingPoints + k];
		}
		Centroid /= RingPoints;
		const int32 C = Bd.Vertices.Add(Centroid);
		const FVector Inside = Centroid - FVector(r == 0 ? -50.0f : 50.0f, 0.0f, 0.0f);
		for (int32 k = 0; k < RingPoints; ++k)
		{
			AddTriangle(Bd, C, r * RingPoints + k, r * RingPoints + (k + 1) % RingPoints, Inside);
		}
	}

	// ---------------- Greenhouse (glass) loft between the rear window base and the windshield base.
	FSection& Gl = Sections[GlassSection];
	const int32 GlassRings = FMath::Max(6, Segments / 2);
	const float GStart = St.RearWindowBase;
	const float GEnd = St.WindshieldBase;
	for (int32 i = 0; i <= GlassRings; ++i)
	{
		const float Fx = FMath::Lerp(GStart, GEnd, static_cast<float>(i) / static_cast<float>(GlassRings));
		const float X = (Fx - 0.5f) * L;
		const float Belt = H * St.BeltHeight;
		const float Roof = H * Piecewise(Fx, {{GStart, St.BeltHeight}, {St.RoofRear, 1.0f}, {St.RoofFront, 1.0f}, {GEnd, St.BeltHeight}});
		const float BeltHalf = W * 0.5f * 0.9f;
		const float RoofHalf = W * 0.5f * St.RoofWidth * 0.95f;
		Gl.Vertices.Add(FVector(X, BeltHalf, Ground + Belt - 1.0f));
		Gl.Vertices.Add(FVector(X, RoofHalf, Ground + Roof));
		Gl.Vertices.Add(FVector(X, -RoofHalf, Ground + Roof));
		Gl.Vertices.Add(FVector(X, -BeltHalf, Ground + Belt - 1.0f));
	}
	for (int32 r = 0; r < GlassRings; ++r)
	{
		const FVector Center(((GStart + GEnd) * 0.5f - 0.5f) * L, 0.0f, Ground + H * St.BeltHeight - 20.0f);
		for (int32 k = 0; k < 3; ++k)
		{
			AddLoftQuad(Gl, r * 4 + k, r * 4 + k + 1, (r + 1) * 4 + k + 1, (r + 1) * 4 + k, Center);
		}
	}

	for (FSection& S : Sections)
	{
		S.Original = S.Vertices;
		S.UVs.SetNum(S.Vertices.Num());
		for (int32 i = 0; i < S.Vertices.Num(); ++i)
		{
			S.UVs[i] = FVector2D(S.Vertices[i].X / 100.0, S.Vertices[i].Z / 100.0);
		}
		S.ComputeNormals();
	}
}

void FBCBodyMesh::CreateSections(UProceduralMeshComponent* Mesh) const
{
	if (!Mesh)
	{
		return;
	}
	Mesh->ClearAllMeshSections();
	for (int32 i = 0; i < 2; ++i)
	{
		const FSection& S = Sections[i];
		Mesh->CreateMeshSection(i, S.Vertices, S.Triangles, S.Normals, S.UVs, TArray<FColor>(), TArray<FProcMeshTangent>(), false);
	}
}

void FBCBodyMesh::UpdateSections(UProceduralMeshComponent* Mesh) const
{
	if (!Mesh)
	{
		return;
	}
	for (int32 i = 0; i < 2; ++i)
	{
		const FSection& S = Sections[i];
		Mesh->UpdateMeshSection(i, S.Vertices, S.Normals, S.UVs, TArray<FColor>(), TArray<FProcMeshTangent>());
	}
}

bool FBCBodyMesh::ApplyDent(const FVector& LocalPointCm, const FVector& LocalDir, float RadiusCm, float DepthCm)
{
	bool bMoved = false;
	const FVector Dir = LocalDir.GetSafeNormal();
	if (Dir.IsNearlyZero() || RadiusCm <= 1.0f || DepthCm <= 0.1f)
	{
		return false;
	}
	for (FSection& S : Sections)
	{
		for (int32 i = 0; i < S.Vertices.Num(); ++i)
		{
			const float Dist = FVector::Dist(S.Vertices[i], LocalPointCm);
			if (Dist >= RadiusCm)
			{
				continue;
			}
			const float Falloff = FMath::Square(1.0f - Dist / RadiusCm);
			FVector Moved = S.Vertices[i] + Dir * (DepthCm * Falloff);
			// Limit total deformation and keep the shell inside its original bounds (no spikes through the body).
			const FVector Offset = (Moved - S.Original[i]).GetClampedToMaxSize(MaxDeformCm);
			Moved = S.Original[i] + Offset;
			Moved.X = FMath::Clamp(Moved.X, -HalfExtentsCm.X, HalfExtentsCm.X);
			Moved.Y = FMath::Clamp(Moved.Y, -HalfExtentsCm.Y, HalfExtentsCm.Y);
			S.Vertices[i] = Moved;
			bMoved = true;
		}
		if (bMoved)
		{
			S.ComputeNormals();
		}
	}
	return bMoved;
}

void FBCBodyMesh::Restore()
{
	for (FSection& S : Sections)
	{
		S.Vertices = S.Original;
		S.ComputeNormals();
	}
}
