// Copyright BigCar Team. All Rights Reserved.
//
// Small, dependency-free math types used by the engine-agnostic gameplay core.
// Axis convention matches Unreal (X forward, Y right, Z up) but units are SI (meters, kg, seconds).
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace crash
{
constexpr float kPi = 3.14159265358979323846f;
constexpr float kTwoPi = 2.0f * kPi;
constexpr float kDegToRad = kPi / 180.0f;
constexpr float kRadToDeg = 180.0f / kPi;
constexpr float kGravity = 9.81f;
constexpr float kAirDensity = 1.225f;
constexpr float kSmallNumber = 1.0e-6f;

inline float Clamp(float v, float lo, float hi)
{
	return v < lo ? lo : (v > hi ? hi : v);
}
inline float Clamp01(float v)
{
	return Clamp(v, 0.0f, 1.0f);
}
inline float Lerp(float a, float b, float t)
{
	return a + (b - a) * t;
}
inline float Sign(float v)
{
	return v > 0.0f ? 1.0f : (v < 0.0f ? -1.0f : 0.0f);
}
inline float Square(float v)
{
	return v * v;
}
inline float MoveTowards(float current, float target, float maxDelta)
{
	if (std::fabs(target - current) <= maxDelta)
	{
		return target;
	}
	return current + Sign(target - current) * maxDelta;
}
// Remaps v from [inLo, inHi] to [outLo, outHi] with clamping.
inline float RemapClamped(float v, float inLo, float inHi, float outLo, float outHi)
{
	if (std::fabs(inHi - inLo) < kSmallNumber)
	{
		return outLo;
	}
	return Lerp(outLo, outHi, Clamp01((v - inLo) / (inHi - inLo)));
}
inline float SmoothStep(float edge0, float edge1, float v)
{
	const float t = RemapClamped(v, edge0, edge1, 0.0f, 1.0f);
	return t * t * (3.0f - 2.0f * t);
}
// Wraps an angle in radians to [-pi, pi].
inline float WrapAngle(float a)
{
	while (a > kPi)
	{
		a -= kTwoPi;
	}
	while (a < -kPi)
	{
		a += kTwoPi;
	}
	return a;
}

struct Vec3
{
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	constexpr Vec3() = default;
	constexpr Vec3(float inX, float inY, float inZ) : x(inX), y(inY), z(inZ) {}

	constexpr Vec3 operator+(const Vec3& o) const { return {x + o.x, y + o.y, z + o.z}; }
	constexpr Vec3 operator-(const Vec3& o) const { return {x - o.x, y - o.y, z - o.z}; }
	constexpr Vec3 operator*(float s) const { return {x * s, y * s, z * s}; }
	constexpr Vec3 operator/(float s) const { return {x / s, y / s, z / s}; }
	constexpr Vec3 operator-() const { return {-x, -y, -z}; }
	Vec3& operator+=(const Vec3& o)
	{
		x += o.x;
		y += o.y;
		z += o.z;
		return *this;
	}
	Vec3& operator-=(const Vec3& o)
	{
		x -= o.x;
		y -= o.y;
		z -= o.z;
		return *this;
	}
	Vec3& operator*=(float s)
	{
		x *= s;
		y *= s;
		z *= s;
		return *this;
	}

	float LengthSq() const { return x * x + y * y + z * z; }
	float Length() const { return std::sqrt(LengthSq()); }
	float Length2D() const { return std::sqrt(x * x + y * y); }
	Vec3 Normalized() const
	{
		const float len = Length();
		return len > kSmallNumber ? (*this) / len : Vec3{};
	}
	Vec3 Flat() const { return {x, y, 0.0f}; }
	bool IsNearlyZero(float tolerance = 1.0e-4f) const { return LengthSq() <= tolerance * tolerance; }
	bool IsFinite() const { return std::isfinite(x) && std::isfinite(y) && std::isfinite(z); }

	static constexpr Vec3 Zero() { return {0.0f, 0.0f, 0.0f}; }
	static constexpr Vec3 Forward() { return {1.0f, 0.0f, 0.0f}; }
	static constexpr Vec3 Right() { return {0.0f, 1.0f, 0.0f}; }
	static constexpr Vec3 Up() { return {0.0f, 0.0f, 1.0f}; }
};

inline constexpr Vec3 operator*(float s, const Vec3& v)
{
	return v * s;
}
inline float Dot(const Vec3& a, const Vec3& b)
{
	return a.x * b.x + a.y * b.y + a.z * b.z;
}
// Same formula as FVector::CrossProduct: Cross(Forward, Right) == Up.
inline Vec3 Cross(const Vec3& a, const Vec3& b)
{
	return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x};
}
inline float Distance(const Vec3& a, const Vec3& b)
{
	return (a - b).Length();
}
inline float Distance2D(const Vec3& a, const Vec3& b)
{
	return (a - b).Length2D();
}
inline Vec3 LerpVec(const Vec3& a, const Vec3& b, float t)
{
	return a + (b - a) * t;
}
inline Vec3 ProjectOnPlane(const Vec3& v, const Vec3& normal)
{
	return v - normal * Dot(v, normal);
}
inline Vec3 ClampLength(const Vec3& v, float maxLen)
{
	const float lenSq = v.LengthSq();
	if (lenSq > maxLen * maxLen && lenSq > 0.0f)
	{
		return v * (maxLen / std::sqrt(lenSq));
	}
	return v;
}

// Unit quaternion (w, x, y, z). Used by the headless simulator; Unreal code uses FQuat.
struct Quat
{
	float w = 1.0f;
	float x = 0.0f;
	float y = 0.0f;
	float z = 0.0f;

	constexpr Quat() = default;
	constexpr Quat(float inW, float inX, float inY, float inZ) : w(inW), x(inX), y(inY), z(inZ) {}

	static Quat FromAxisAngle(const Vec3& axis, float angleRad)
	{
		const Vec3 n = axis.Normalized();
		const float half = 0.5f * angleRad;
		const float s = std::sin(half);
		return {std::cos(half), n.x * s, n.y * s, n.z * s};
	}
	// Yaw about +Z (positive yaw turns forward toward +Y, like Unreal).
	static Quat FromYaw(float yawRad) { return FromAxisAngle(Vec3::Up(), yawRad); }

	Quat operator*(const Quat& o) const
	{
		return {w * o.w - x * o.x - y * o.y - z * o.z, w * o.x + x * o.w + y * o.z - z * o.y, w * o.y - x * o.z + y * o.w + z * o.x,
		        w * o.z + x * o.y - y * o.x + z * o.w};
	}
	Quat Conjugate() const { return {w, -x, -y, -z}; }
	Quat Normalized() const
	{
		const float len = std::sqrt(w * w + x * x + y * y + z * z);
		return len > kSmallNumber ? Quat{w / len, x / len, y / len, z / len} : Quat{};
	}
	Vec3 Rotate(const Vec3& v) const
	{
		// v' = v + 2w(q x v) + 2 q x (q x v)
		const Vec3 q{x, y, z};
		const Vec3 t = Cross(q, v) * 2.0f;
		return v + t * w + Cross(q, t);
	}
	Vec3 Unrotate(const Vec3& v) const { return Conjugate().Rotate(v); }
	Vec3 Forward() const { return Rotate(Vec3::Forward()); }
	Vec3 RightVec() const { return Rotate(Vec3::Right()); }
	Vec3 UpVec() const { return Rotate(Vec3::Up()); }
	// Integrates an angular velocity (world space, rad/s) over dt.
	Quat Integrated(const Vec3& angularVelocity, float dt) const
	{
		const Quat omega{0.0f, angularVelocity.x, angularVelocity.y, angularVelocity.z};
		const Quat dq = omega * (*this);
		return Quat{w + 0.5f * dt * dq.w, x + 0.5f * dt * dq.x, y + 0.5f * dt * dq.y, z + 0.5f * dt * dq.z}.Normalized();
	}
};

// Orthonormal frame of a body in world space.
struct Frame
{
	Vec3 forward = Vec3::Forward();
	Vec3 right = Vec3::Right();
	Vec3 up = Vec3::Up();

	static Frame FromQuat(const Quat& q) { return {q.Forward(), q.RightVec(), q.UpVec()}; }
	Vec3 ToLocal(const Vec3& worldDir) const { return {Dot(worldDir, forward), Dot(worldDir, right), Dot(worldDir, up)}; }
	Vec3 ToWorld(const Vec3& localDir) const { return forward * localDir.x + right * localDir.y + up * localDir.z; }
};

struct Color
{
	uint8_t r = 255;
	uint8_t g = 255;
	uint8_t b = 255;

	constexpr Color() = default;
	constexpr Color(uint8_t inR, uint8_t inG, uint8_t inB) : r(inR), g(inG), b(inB) {}
	bool operator==(const Color& o) const { return r == o.r && g == o.g && b == o.b; }
	float R() const { return r / 255.0f; }
	float G() const { return g / 255.0f; }
	float B() const { return b / 255.0f; }
};

} // namespace crash
