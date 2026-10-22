// Copyright BigCar Team. All Rights Reserved.
//
// Per-vehicle damage model: six body zones, the engine and four wheels.
#pragma once

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

	float Total() const;
	float BodyTotal() const;
	ZoneDamage& operator*=(float scale);
	ZoneDamage& operator+=(const ZoneDamage& other);
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
	void Init(const EffectiveStats& stats);

	DamageEvent Apply(const ZoneDamage& damage);
	// Fire damage over time and passive repair. Returns an event (fire can wreck the car).
	DamageEvent Tick(float dt, float repairPerSec);
	// Restores `fraction` of max hp everywhere (detached wheels come back when fraction >= 1).
	void Repair(float fraction);
	void RepairFull() { Repair(1.0f); }

	float ZoneFraction(Zone z) const;
	float EngineFraction() const;
	float WheelFraction(int wheel) const;
	float Integrity() const; // weighted 0..1, shown as the main health bar
	bool IsWrecked() const { return wrecked_; }
	bool IsOnFire() const { return onFire_; }
	bool IsWheelDetached(int wheel) const { return wheelDetached_[static_cast<size_t>(wheel)]; }
	float TotalLost() const; // hp lost since Init (for stats / damage_under objectives)
	float TotalMax() const;

	DrivingDamage DrivingEffects() const;
	DamageSnapshot Snapshot() const;
	void ApplySnapshot(const DamageSnapshot& snapshot); // clients mirror the server state

	static constexpr float kWreckIntegrity = 0.12f;
	static constexpr float kFireThreshold = 0.25f;
	static constexpr float kFireDamagePerSec = 0.02f; // fraction of engine max per second

private:
	void UpdateFlags(DamageEvent& event);

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
