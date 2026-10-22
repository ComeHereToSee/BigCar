// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/VehicleSim.h"

#include <cmath>

namespace crash
{
namespace
{
// Rotates v around axis (unit) by angle (radians).
Vec3 RotateAround(const Vec3& v, const Vec3& axis, float angle)
{
	return Quat::FromAxisAngle(axis, angle).Rotate(v);
}

constexpr float kTireLateralPeak = 0.14f;       // rad, peak slip angle (~8 deg)
constexpr float kTireSlideFraction = 0.72f;     // lateral grip left when fully sliding
constexpr float kRollingResistance = 0.012f;    // fraction of load
constexpr float kEngineBraking = 0.035f;        // fraction of load on driven wheels, off throttle
constexpr float kHandbrakeRearGrip = 0.38f;     // lateral grip multiplier on rear wheels with handbrake
constexpr float kSteerSpeed = 3.2f;             // rad/s
constexpr float kSteerReturnSpeed = 5.0f;       // rad/s
constexpr float kAirPitchAccel = 4.5f;          // rad/s^2
constexpr float kAirYawAccel = 3.5f;            // rad/s^2
constexpr float kAirRollAccel = 6.0f;           // rad/s^2
constexpr float kYawAssistGain = 2.2f;          // 1/s
constexpr float kRollAssistGain = 7.0f;         // rad/s^2 per unit of tilt
constexpr float kLongitudinalGripScale = 1.15f; // longitudinal / lateral friction ratio
constexpr float kLateralStiffness = 0.8f;       // fraction of sideways velocity removed per step (stability margin)
} // namespace

VehicleSimSetup MakeSimSetup(const EffectiveStats& stats)
{
	VehicleSimSetup s;
	if (!stats.def)
	{
		return s;
	}
	const VehicleDef& def = *stats.def;
	const VehicleDimensions& d = def.dimensions;

	s.mass = stats.massKg;
	const float bodyHeight = d.height - d.groundClearance;
	s.bodyHalfExtents = {d.length * 0.5f, d.width * 0.5f, bodyHeight * 0.5f};
	s.bodyCenterHeight = d.groundClearance + bodyHeight * 0.5f;
	s.comLocal = {d.wheelbase * (def.frontWeight - 0.5f), 0.0f, stats.cgHeight - s.bodyCenterHeight};
	s.inertiaDiagonal = {stats.massKg / 12.0f * (Square(d.width) + Square(bodyHeight)),
	                     stats.massKg / 12.0f * (Square(d.length) + Square(bodyHeight)),
	                     stats.massKg / 12.0f * (Square(d.length) + Square(d.width))};

	s.powerW = stats.powerW;
	s.torqueNm = stats.torqueNm;
	s.idleRpm = std::max(def.idleRpm, 300.0f);
	s.redlineRpm = def.redlineRpm;
	s.gears = def.gears.empty() ? std::vector<float>{3.5f, 2.1f, 1.4f, 1.0f, 0.8f} : def.gears;
	s.reverseRatio = def.reverseRatio;
	s.finalDrive = def.finalDrive;
	s.electric = def.IsElectric();
	s.efficiency = s.electric ? 0.92f : 0.85f;
	s.shiftTime = s.electric ? 0.0f : stats.shiftTime;

	s.topSpeed = stats.topSpeed;
	s.grip = stats.grip;
	s.brakeDecel = stats.brakeDecel;
	s.steerLockRad = stats.steerLockRad;
	s.dragArea = stats.dragCoefficient * d.width * d.height * 0.85f;
	s.downforce = stats.downforce;

	s.wheelRadius = d.wheelRadius;
	s.wheelWidth = d.wheelWidth;
	s.travel = stats.suspensionTravel;
	const float omega = kTwoPi * stats.suspensionFrequency;
	const float staticSag = kGravity / Square(omega);
	s.restLength = staticSag + s.travel;
	const float frontCornerMass = stats.massKg * def.frontWeight * 0.5f;
	const float rearCornerMass = stats.massKg * (1.0f - def.frontWeight) * 0.5f;
	s.springRate = {frontCornerMass * Square(omega), rearCornerMass * Square(omega)};
	s.damperRate = {2.0f * stats.suspensionDamping * std::sqrt(s.springRate[0] * frontCornerMass),
	                2.0f * stats.suspensionDamping * std::sqrt(s.springRate[1] * rearCornerMass)};
	s.antiRollRate = {stats.antiRoll * s.springRate[0], stats.antiRoll * s.springRate[1] * 0.8f};
	s.tireForceLift = 0.55f * stats.cgHeight;

	s.boostAccel = stats.boostAccel;
	s.boostCapacity = stats.boostCapacity;
	s.boostRegen = stats.boostRegen;

	// Wheel layout: axles symmetric around the body center; mount = top of travel above the resting wheel center.
	const float wheelRestZ = d.wheelRadius - s.bodyCenterHeight;
	const float mountZ = wheelRestZ + s.travel;
	float frontDrive = 0.0f;
	float rearDrive = 0.0f;
	switch (def.drivetrain)
	{
	case Drivetrain::FWD:
		frontDrive = 0.5f;
		break;
	case Drivetrain::RWD:
		rearDrive = 0.5f;
		break;
	default:
		frontDrive = 0.2f;
		rearDrive = 0.3f;
		break;
	}
	for (int i = 0; i < kWheelCount; ++i)
	{
		WheelGeometry& w = s.wheels[static_cast<size_t>(i)];
		w.front = i < 2;
		w.left = (i % 2) == 0;
		w.steered = w.front;
		const float x = (w.front ? 0.5f : -0.5f) * d.wheelbase;
		const float y = (w.left ? -0.5f : 0.5f) * (w.front ? d.trackFront : d.trackRear);
		w.mountLocal = {x, y, mountZ};
		w.driveShare = w.front ? frontDrive : rearDrive;
		w.brakeShare = w.front ? 0.3f : 0.2f;
	}
	return s;
}

void VehicleSimulator::Setup(const VehicleSimSetup& setup)
{
	setup_ = setup;
	Reset();
}

void VehicleSimulator::Reset()
{
	wheels_ = {};
	steerAngle_ = 0.0f;
	rpm_ = setup_.idleRpm;
	gear_ = 1;
	shiftTimer_ = 0.0f;
	reverseHold_ = 0.0f;
	boost_ = setup_.boostCapacity;
	driveMultiplier_ = 1.0f;
	boostMultiplier_ = 1.0f;
	damage_ = DrivingDamage();
}

void VehicleSimulator::AddBoost(float amount)
{
	boost_ = Clamp(boost_ + amount, 0.0f, setup_.boostCapacity);
}

Vec3 VehicleSimulator::MountWorld(int wheel, const SimBodyState& body) const
{
	return body.position + body.frame.ToWorld(setup_.wheels[static_cast<size_t>(wheel)].mountLocal);
}

float VehicleSimulator::WheelCenterDrop(int wheel) const
{
	const WheelState& w = wheels_[static_cast<size_t>(wheel)];
	return Clamp(setup_.restLength - w.compression, 0.0f, setup_.restLength);
}

float VehicleSimulator::TorqueCurve(float rpmFraction, bool electric)
{
	if (electric)
	{
		return 1.0f; // power limiting shapes the EV curve
	}
	// Typical turbo-petrol shape: rises to peak around 35-75% of redline, tapers at the top.
	if (rpmFraction < 0.35f)
	{
		return Lerp(0.62f, 1.0f, Clamp01(rpmFraction / 0.35f));
	}
	if (rpmFraction < 0.75f)
	{
		return 1.0f;
	}
	return Lerp(1.0f, 0.82f, Clamp01((rpmFraction - 0.75f) / 0.25f));
}

void VehicleSimulator::Step(float dt, const VehicleControls& controls, const SimBodyState& body,
                            const std::array<WheelContact, kWheelCount>& contacts, SimOutput& out)
{
	out = SimOutput();
	if (dt <= 0.0f)
	{
		return;
	}
	const VehicleSimSetup& s = setup_;
	const Frame& frame = body.frame;
	const float forwardSpeed = Dot(body.linearVelocity, frame.forward);
	const float speed = body.linearVelocity.Length();
	out.forwardSpeed = forwardSpeed;
	out.speed = speed;

	const float throttle = Clamp01(controls.throttle);
	const float brake = Clamp01(controls.brake);

	// ---- Direction / reverse logic (arcade: brake at standstill = reverse).
	if (gear_ >= 0 && brake > 0.1f && throttle < 0.05f && forwardSpeed < 0.8f)
	{
		reverseHold_ += dt;
		if (reverseHold_ > 0.15f)
		{
			gear_ = -1;
			shiftTimer_ = 0.0f;
		}
	}
	else
	{
		reverseHold_ = 0.0f;
	}
	if (gear_ < 0 && throttle > 0.1f && forwardSpeed > -0.8f)
	{
		gear_ = 1;
		shiftTimer_ = 0.0f;
	}
	const bool reversing = gear_ < 0;
	const float driveInput = reversing ? brake : throttle;
	const float brakeInput = reversing ? throttle : brake;

	// ---- Steering (grip-aware speed sensitivity, rate limited). At speed the usable lock is what the front tires can
	// actually turn into lateral grip: atan(L * mu * g / v^2) plus the peak slip angle. Full lock stays available
	// at parking speeds.
	const float wheelbaseLength = std::max(1.0f, s.wheels[0].mountLocal.x - s.wheels[2].mountLocal.x);
	const float v2 = std::max(forwardSpeed * forwardSpeed, 1.0f);
	const float gripSteer = std::atan(wheelbaseLength * s.grip * kGravity / v2) + 1.1f * kTireLateralPeak;
	const float steerLimit = std::max(std::min(s.steerLockRad, gripSteer), 0.06f);
	const float targetSteer = Clamp(controls.steer, -1.0f, 1.0f) * steerLimit;
	const bool returning = std::fabs(targetSteer) < std::fabs(steerAngle_) || Sign(targetSteer) == -Sign(steerAngle_);
	steerAngle_ = MoveTowards(steerAngle_, targetSteer, (returning ? kSteerReturnSpeed : kSteerSpeed) * dt);

	// ---- Engine and gearbox.
	const int gearCount = static_cast<int>(s.gears.size());
	const float ratio = reversing ? s.reverseRatio : s.gears[static_cast<size_t>(std::clamp(gear_, 1, gearCount) - 1)];
	const float wheelRpm = std::fabs(forwardSpeed) / s.wheelRadius * 60.0f / kTwoPi;
	const float rpmFromWheels = wheelRpm * ratio * s.finalDrive;
	if (shiftTimer_ > 0.0f)
	{
		shiftTimer_ -= dt;
	}
	else if (!reversing && gearCount > 1)
	{
		if (rpmFromWheels > 0.93f * s.redlineRpm && gear_ < gearCount && driveInput > 0.1f)
		{
			++gear_;
			shiftTimer_ = s.shiftTime;
		}
		else if (gear_ > 1)
		{
			const float lowerRatio = s.gears[static_cast<size_t>(gear_ - 2)];
			const float rpmInLower = rpmFromWheels * lowerRatio / ratio;
			if (rpmFromWheels < 0.45f * s.redlineRpm && rpmInLower < 0.8f * s.redlineRpm)
			{
				--gear_;
				shiftTimer_ = s.shiftTime * 0.5f;
			}
		}
	}
	const bool shifting = shiftTimer_ > 0.0f;
	// Clutch slip at low speed lets the engine rev above the wheel speed.
	const float launchRpm = s.idleRpm + driveInput * (0.55f * s.redlineRpm - s.idleRpm);
	const float clutchBlend = Clamp01(1.0f - rpmFromWheels / (0.55f * s.redlineRpm));
	const float targetRpm = Clamp(std::max(rpmFromWheels, Lerp(rpmFromWheels, launchRpm, clutchBlend)), s.idleRpm, s.redlineRpm * 1.02f);
	rpm_ = MoveTowards(rpm_, targetRpm, 25000.0f * dt);
	out.engineRpm = rpm_;
	out.gear = gear_;
	out.throttleAudio = driveInput;

	float driveForce = 0.0f;
	if (!shifting && driveInput > 0.0f && rpmFromWheels < s.redlineRpm * 1.01f)
	{
		const float omega = std::max(rpm_, 1000.0f) * kTwoPi / 60.0f;
		float torque = s.torqueNm * TorqueCurve(rpm_ / s.redlineRpm, s.electric);
		torque = std::min(torque, s.powerW / omega);
		torque *= damage_.powerFactor * driveMultiplier_;
		driveForce = torque * ratio * s.finalDrive * s.efficiency / s.wheelRadius * driveInput;
		const float topSpeed = reversing ? std::min(s.topSpeed, 16.0f) : s.topSpeed;
		driveForce *= Clamp01((topSpeed - std::fabs(forwardSpeed)) / (0.04f * topSpeed + 0.5f));
		if (reversing)
		{
			driveForce = -driveForce;
		}
	}

	// ---- Boost.
	if (controls.boost && boost_ > 0.0f)
	{
		out.boosting = true;
		boost_ = std::max(0.0f, boost_ - s.boostDrainPerSec * dt);
		out.centralForce += frame.forward * (s.mass * s.boostAccel * boostMultiplier_);
	}
	else
	{
		boost_ = std::min(s.boostCapacity, boost_ + s.boostRegen * dt);
	}

	// ---- Suspension pass 1: compressions.
	std::array<bool, kWheelCount> hit{};
	for (int i = 0; i < kWheelCount; ++i)
	{
		WheelState& w = wheels_[static_cast<size_t>(i)];
		const WheelContact& c = contacts[static_cast<size_t>(i)];
		w.prevCompression = w.compression;
		w.steerAngle = s.wheels[static_cast<size_t>(i)].steered ? steerAngle_ + damage_.steerPullRad : 0.0f;
		const bool missing = damage_.wheelMissing[static_cast<size_t>(i)];
		const float compression = RayLength() - c.distance;
		hit[static_cast<size_t>(i)] = c.hit && !missing && compression > 0.0f;
		w.compression = hit[static_cast<size_t>(i)] ? std::min(compression, s.restLength * 1.5f) : 0.0f;
		w.grounded = hit[static_cast<size_t>(i)];
	}

	// ---- Pass 2: forces.
	const float g = kGravity;
	float maxSlip = 0.0f;
	for (int i = 0; i < kWheelCount; ++i)
	{
		const size_t idx = static_cast<size_t>(i);
		WheelState& w = wheels_[idx];
		const WheelGeometry& geo = s.wheels[idx];
		const WheelContact& c = contacts[idx];
		const Vec3 mountWorld = MountWorld(i, body);
		if (!hit[idx])
		{
			w.load = 0.0f;
			w.lateralSlip = 0.0f;
			// Free-spinning wheel slowly follows the last ground speed.
			w.spinRate *= std::max(0.0f, 1.0f - 0.5f * dt);
			w.spinAngle = WrapAngle(w.spinAngle + w.spinRate * dt);
			continue;
		}
		++out.groundedWheels;
		const int axle = geo.front ? 0 : 1;
		const float compressionVelocity = Clamp((w.compression - w.prevCompression) / dt, -6.0f, 6.0f);
		float suspensionForce =
		    s.springRate[static_cast<size_t>(axle)] * w.compression + s.damperRate[static_cast<size_t>(axle)] * compressionVelocity;
		if (w.compression > s.restLength)
		{
			// Bump stop.
			suspensionForce += (w.compression - s.restLength) * s.springRate[static_cast<size_t>(axle)] * 8.0f;
		}
		// Anti-roll bar couples left/right of the same axle.
		const size_t other = static_cast<size_t>(geo.left ? i + 1 : i - 1);
		const float otherCompression = hit[other] ? wheels_[other].compression : 0.0f;
		suspensionForce += s.antiRollRate[static_cast<size_t>(axle)] * (w.compression - otherCompression);
		suspensionForce = std::max(0.0f, suspensionForce);
		w.load = suspensionForce;
		w.contactPoint = c.point;
		out.suspension[idx] = {frame.up * suspensionForce, mountWorld};

		// Tire frame on the contact plane.
		const Vec3 n = c.normal.IsNearlyZero() ? frame.up : c.normal.Normalized();
		const Vec3 heading = geo.steered ? RotateAround(frame.forward, frame.up, w.steerAngle) : frame.forward;
		Vec3 wf = ProjectOnPlane(heading, n).Normalized();
		if (wf.IsNearlyZero())
		{
			wf = frame.forward;
		}
		const Vec3 wr = Cross(n, wf);
		const float vLong = Dot(c.pointVelocity, wf);
		const float vLat = Dot(c.pointVelocity, wr);

		const float mu = s.grip * c.friction * damage_.wheelGrip[idx];
		const float maxFriction = mu * suspensionForce;
		const float massShare = Clamp(suspensionForce / g, s.mass * 0.02f, s.mass * 0.6f);

		// Longitudinal: drive, brakes, rolling resistance, engine braking.
		float fLong = driveForce * geo.driveShare;
		float brakeForce = brakeInput * s.mass * s.brakeDecel * geo.brakeShare;
		if (controls.handbrake && !geo.front)
		{
			brakeForce = std::max(brakeForce, s.mass * s.brakeDecel * 0.35f);
		}
		if (driveInput < 0.05f && geo.driveShare > 0.0f)
		{
			brakeForce += kEngineBraking * suspensionForce;
		}
		brakeForce += kRollingResistance * suspensionForce;
		if (brakeForce > 0.0f)
		{
			const float stopForce = std::fabs(vLong) * massShare / dt;
			fLong -= Sign(vLong) * std::min(brakeForce, stopForce);
		}

		// Lateral: kill sideways velocity up to the (slip-angle dependent) friction limit.
		const float slipAngle = std::atan2(std::fabs(vLat), std::max(std::fabs(vLong), 2.0f));
		float lateralScale =
		    slipAngle <= kTireLateralPeak ? 1.0f : Lerp(1.0f, kTireSlideFraction, Clamp01((slipAngle - kTireLateralPeak) / 0.35f));
		if (controls.handbrake && !geo.front)
		{
			lateralScale *= kHandbrakeRearGrip;
		}
		const float lateralMax = maxFriction * lateralScale;
		float fLat = Clamp(-vLat * massShare * kLateralStiffness / dt, -lateralMax, lateralMax);

		// Friction ellipse (tires carry a little more grip longitudinally than laterally).
		const float longCapacity = maxFriction * kLongitudinalGripScale;
		const float combined = std::sqrt(Square(fLong / std::max(longCapacity, 1.0f)) + Square(fLat / std::max(maxFriction, 1.0f)));
		float saturation = 1.0f;
		if (combined > 1.0f)
		{
			saturation = 1.0f / combined;
			fLong *= saturation;
			fLat *= saturation;
		}
		w.lateralSlip = std::fabs(vLat);
		maxSlip = std::max(maxSlip, std::fabs(vLat) + (saturation < 0.95f && driveInput > 0.5f ? 4.0f : 0.0f));

		out.tire[idx] = {wf * fLong + wr * fLat, c.point + frame.up * s.tireForceLift};

		// Visual spin (adds wheelspin when traction-limited under power).
		float spin = vLong / s.wheelRadius;
		if (geo.driveShare > 0.0f && saturation < 0.95f && driveInput > 0.5f)
		{
			spin += Sign(driveForce) * (1.0f - saturation) * 40.0f;
		}
		w.spinRate = spin;
		w.spinAngle = WrapAngle(w.spinAngle + w.spinRate * dt);
	}
	out.maxSlip = maxSlip;

	// ---- Aerodynamics.
	out.centralForce += body.linearVelocity * (-0.5f * kAirDensity * s.dragArea * speed);
	if (forwardSpeed > 0.0f)
	{
		out.centralForce += frame.up * (-s.downforce * forwardSpeed * forwardSpeed);
	}

	// ---- Air control / assists.
	if (out.groundedWheels == 0)
	{
		out.angularAcceleration += frame.right * ((controls.throttle - controls.brake) * kAirPitchAccel);
		if (controls.handbrake)
		{
			out.angularAcceleration += frame.forward * (-controls.steer * kAirRollAccel);
		}
		else
		{
			out.angularAcceleration += frame.up * (controls.steer * kAirYawAccel);
		}
	}
	else if (!assists_.suppressed)
	{
		if (!controls.handbrake && out.groundedWheels >= 3 && std::fabs(forwardSpeed) > 2.0f)
		{
			// Stability control: never ask for more yaw than the tires can deliver (mu * g / v), mostly damp
			// oversteer and only gently help understeer.
			const float gripYawLimit = s.grip * kGravity / std::max(std::fabs(forwardSpeed), 1.0f);
			const float desiredYawRate = Clamp(forwardSpeed * std::tan(steerAngle_) / wheelbaseLength, -gripYawLimit, gripYawLimit);
			const float yawRate = Dot(body.angularVelocity, frame.up);
			const bool oversteer = std::fabs(yawRate) > std::fabs(desiredYawRate) || Sign(yawRate) != Sign(desiredYawRate);
			const float gain = kYawAssistGain * assists_.yawStability * (oversteer ? 1.0f : 0.3f);
			out.angularAcceleration += frame.up * ((desiredYawRate - yawRate) * gain);
		}
		const float tilt = Dot(frame.right, Vec3::Up());
		if (std::fabs(tilt) > 0.2f && Dot(frame.up, Vec3::Up()) > 0.3f)
		{
			out.angularAcceleration += frame.forward * (-tilt * kRollAssistGain * assists_.antiRoll);
		}
	}
}

} // namespace crash
