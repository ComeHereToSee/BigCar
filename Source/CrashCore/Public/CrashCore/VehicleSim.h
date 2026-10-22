// Copyright BigCar Team. All Rights Reserved.
//
// Engine-agnostic vehicle dynamics: raycast suspension, slip-based tires with a friction circle, engine torque
// curve + automatic gearbox, aero, boost, air control and driving assists.
//
// The host (Unreal or the headless simulator) owns the rigid body. Every physics step it
//   1. casts one ray per wheel from MountWorld(i) along -up with length RayLength(),
//   2. calls Step() with the body state and the four contacts,
//   3. applies the returned forces at their points (and the central force / angular acceleration).
// All units are SI: meters, kilograms, seconds, Newtons.
#pragma once

#include "CrashCore/Math.h"
#include "CrashCore/Stats.h"

#include <array>
#include <vector>

namespace crash
{
struct VehicleControls
{
	float throttle = 0.0f; // 0..1
	float brake = 0.0f;    // 0..1 (also reverse)
	float steer = 0.0f;    // -1 (left) .. 1 (right)
	bool handbrake = false;
	bool boost = false;
};

struct WheelGeometry
{
	Vec3 mountLocal; // top of the suspension, relative to the body origin
	bool front = true;
	bool left = true;
	bool steered = false;
	float driveShare = 0.0f; // fraction of total drive force on this wheel
	float brakeShare = 0.25f;
};

// Everything the simulator needs, derived from EffectiveStats and the vehicle dimensions.
struct VehicleSimSetup
{
	float mass = 1400.0f;
	Vec3 comLocal;                 // center of mass relative to body origin
	Vec3 bodyHalfExtents;          // collision box half extents
	float bodyCenterHeight = 0.8f; // body origin height above flat ground at rest
	Vec3 inertiaDiagonal;          // box inertia (kg m^2) about local axes

	float powerW = 100000.0f;
	float torqueNm = 200.0f;
	float idleRpm = 900.0f;
	float redlineRpm = 6500.0f;
	std::vector<float> gears;
	float reverseRatio = 3.2f;
	float finalDrive = 4.0f;
	float efficiency = 0.85f;
	float shiftTime = 0.2f;
	bool electric = false;

	float topSpeed = 50.0f;
	float grip = 1.0f;
	float brakeDecel = 9.0f;
	float steerLockRad = 0.6f;
	float dragArea = 0.7f; // Cd * A
	float downforce = 0.0f;

	float wheelRadius = 0.33f;
	float wheelWidth = 0.23f;
	float restLength = 0.3f;
	float travel = 0.15f;
	std::array<float, 2> springRate{};   // front, rear (N/m)
	std::array<float, 2> damperRate{};   // front, rear (N s/m)
	std::array<float, 2> antiRollRate{}; // front, rear (N/m)
	float tireForceLift = 0.25f;         // tire forces act this far above the contact patch (reduces roll moment)

	float boostAccel = 6.0f;
	float boostCapacity = 100.0f;
	float boostRegen = 6.0f;
	float boostDrainPerSec = 40.0f;

	std::array<WheelGeometry, kWheelCount> wheels;
};

VehicleSimSetup MakeSimSetup(const EffectiveStats& stats);

struct WheelContact
{
	bool hit = false;
	float distance = 0.0f; // from mount point along -up
	Vec3 point;
	Vec3 normal = Vec3::Up();
	Vec3 pointVelocity; // velocity of the chassis at the contact point (world)
	float friction = 1.0f;
};

struct SimBodyState
{
	Vec3 position; // body origin (world)
	Frame frame;
	Vec3 linearVelocity;
	Vec3 angularVelocity;
};

struct ForceAtPoint
{
	Vec3 force;
	Vec3 point;
};

struct SimOutput
{
	std::array<ForceAtPoint, kWheelCount> suspension{};
	std::array<ForceAtPoint, kWheelCount> tire{};
	Vec3 centralForce;        // apply at the center of mass
	Vec3 angularAcceleration; // apply as an acceleration change (mass independent), rad/s^2
	int groundedWheels = 0;
	float forwardSpeed = 0.0f; // m/s, signed
	float speed = 0.0f;
	float engineRpm = 0.0f;
	int gear = 1; // -1 reverse, 0 neutral, 1..n
	bool boosting = false;
	float maxSlip = 0.0f; // m/s lateral slip of the worst wheel (skid audio/marks)
	float throttleAudio = 0.0f;
};

struct WheelState
{
	float compression = 0.0f;
	float prevCompression = 0.0f;
	bool grounded = false;
	float load = 0.0f;
	float steerAngle = 0.0f;
	float spinRate = 0.0f; // rad/s
	float spinAngle = 0.0f;
	float lateralSlip = 0.0f;
	Vec3 contactPoint;
};

// Effects of damage on driving (from DamageState::Effects()).
struct DrivingDamage
{
	float powerFactor = 1.0f;
	std::array<float, kWheelCount> wheelGrip{1.0f, 1.0f, 1.0f, 1.0f};
	std::array<bool, kWheelCount> wheelMissing{};
	float steerPullRad = 0.0f;
};

struct DrivingAssists
{
	float yawStability = 0.6f; // 0..1
	float antiRoll = 1.0f;     // 0..1
	bool suppressed = false;   // true right after a big impact so crashes play out physically
};

class VehicleSimulator
{
public:
	void Setup(const VehicleSimSetup& setup);
	const VehicleSimSetup& GetSetup() const { return setup_; }
	void Reset();

	void Step(float dt, const VehicleControls& controls, const SimBodyState& body, const std::array<WheelContact, kWheelCount>& contacts,
	          SimOutput& out);

	Vec3 MountWorld(int wheel, const SimBodyState& body) const;
	float RayLength() const { return setup_.restLength + setup_.wheelRadius; }
	// Distance from the mount point down to the wheel center (for visuals).
	float WheelCenterDrop(int wheel) const;
	const WheelState& GetWheel(int wheel) const { return wheels_[static_cast<size_t>(wheel)]; }

	void SetDamage(const DrivingDamage& damage) { damage_ = damage; }
	void SetAssists(const DrivingAssists& assists) { assists_ = assists; }
	// Temporary multipliers (abilities).
	void SetDriveMultiplier(float m) { driveMultiplier_ = m; }
	void SetBoostMultiplier(float m) { boostMultiplier_ = m; }

	float Boost() const { return boost_; }
	float BoostFraction() const { return setup_.boostCapacity > 0.0f ? boost_ / setup_.boostCapacity : 0.0f; }
	void AddBoost(float amount);
	int Gear() const { return gear_; }
	float EngineRpm() const { return rpm_; }

	static float TorqueCurve(float rpmFraction, bool electric);
	static bool IsUpsideDown(const Frame& frame) { return Dot(frame.up, Vec3::Up()) < 0.1f; }

private:
	VehicleSimSetup setup_;
	std::array<WheelState, kWheelCount> wheels_{};
	DrivingDamage damage_;
	DrivingAssists assists_;
	float steerAngle_ = 0.0f;
	float rpm_ = 900.0f;
	int gear_ = 1;
	float shiftTimer_ = 0.0f;
	float reverseHold_ = 0.0f;
	float boost_ = 0.0f;
	float driveMultiplier_ = 1.0f;
	float boostMultiplier_ = 1.0f;
};

} // namespace crash
