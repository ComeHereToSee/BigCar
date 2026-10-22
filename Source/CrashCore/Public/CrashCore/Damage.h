// Copyright BigCar Team. All Rights Reserved.
//
// Per-vehicle damage model: six body zones, the engine and four wheels.
#pragma once

#include "CrashCore/Export.h"
#include "CrashCore/Stats.h"
#include "CrashCore/VehicleSim.h"

#include <array>

namespace crash
{
struct ZoneDamage
{
	std::array<float, kZoneCount> zone{};
	float engine = 0.0f;
	std::array<float, kWheelCount> wheels{};

	CRASHCORE_EXPORT float Total() const;
	CRASHCORE_EXPORT float BodyTotal() const;
	CRASHCORE_EXPORT ZoneDamage& operator*=(float scale);
	CRASHCORE_EXPORT ZoneDamage& operator+=(const ZoneDamage& other);
};

struct DamageEvent
{
	float applied = 0.0f;
	bool becameWrecked = false;
	bool fireStarted = false;
	std::array<bool, kWheelCount> wheelLost{};
	bool zoneBroken = false; // some zone just reached 0 hp
};

// Compact, replication friendly snapshot (fractions 0..1).
struct DamageSnapshot
{
	std::array<float, kZoneCount> zones{};
	float engine = 1.0f;
	std::array<float, kWheelCount> wheels{};
	bool wrecked = false;
	bool onFire = false;
};

class DamageState
{
public:
	CRASHCORE_EXPORT void Init(const EffectiveStats& stats);

	CRASHCORE_EXPORT DamageEvent Apply(const ZoneDamage& damage);
	// Fire damage over time and passive repair. Returns an event (fire can wreck the car).
	CRASHCORE_EXPORT DamageEvent Tick(float dt, float repairPerSec);
	// Restores `fraction` of max hp everywhere (detached wheels come back when fraction >= 1).
	CRASHCORE_EXPORT void Repair(float fraction);
	void RepairFull() { Repair(1.0f); }

	CRASHCORE_EXPORT float ZoneFraction(Zone z) const;
	CRASHCORE_EXPORT float EngineFraction() const;
	CRASHCORE_EXPORT float WheelFraction(int wheel) const;
	CRASHCORE_EXPORT float Integrity() const; // weighted 0..1, shown as the main health bar
	bool IsWrecked() const { return wrecked_; }
	bool IsOnFire() const { return onFire_; }
	bool IsWheelDetached(int wheel) const { return wheelDetached_[static_cast<size_t>(wheel)]; }
	CRASHCORE_EXPORT float TotalLost() const; // hp lost since Init (for stats / damage_under objectives)
	CRASHCORE_EXPORT float TotalMax() const;

	CRASHCORE_EXPORT DrivingDamage DrivingEffects() const;
	CRASHCORE_EXPORT DamageSnapshot Snapshot() const;
	CRASHCORE_EXPORT void ApplySnapshot(const DamageSnapshot& snapshot); // clients mirror the server state

	static constexpr float kWreckIntegrity = 0.12f;
	static constexpr float kFireThreshold = 0.25f;
	static constexpr float kFireDamagePerSec = 0.02f; // fraction of engine max per second

private:
	CRASHCORE_EXPORT void UpdateFlags(DamageEvent& event);

	std::array<float, kZoneCount> zoneHp_{};
	std::array<float, kZoneCount> zoneMax_{};
	float engineHp_ = 1.0f;
	float engineMax_ = 1.0f;
	std::array<float, kWheelCount> wheelHp_{};
	float wheelMax_ = 1.0f;
	std::array<bool, kWheelCount> wheelDetached_{};
	bool wrecked_ = false;
	bool onFire_ = false;
	float lost_ = 0.0f;
};

} // namespace crash
