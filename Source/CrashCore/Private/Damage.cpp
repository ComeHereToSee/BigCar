// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Damage.h"

#include <cmath>

namespace crash
{
float ZoneDamage::BodyTotal() const
{
	float total = 0.0f;
	for (const float z : zone)
	{
		total += z;
	}
	return total;
}

float ZoneDamage::Total() const
{
	float total = BodyTotal() + engine;
	for (const float w : wheels)
	{
		total += w;
	}
	return total;
}

ZoneDamage& ZoneDamage::operator*=(float scale)
{
	for (float& z : zone)
	{
		z *= scale;
	}
	engine *= scale;
	for (float& w : wheels)
	{
		w *= scale;
	}
	return *this;
}

ZoneDamage& ZoneDamage::operator+=(const ZoneDamage& other)
{
	for (size_t i = 0; i < zone.size(); ++i)
	{
		zone[i] += other.zone[i];
	}
	engine += other.engine;
	for (size_t i = 0; i < wheels.size(); ++i)
	{
		wheels[i] += other.wheels[i];
	}
	return *this;
}

void DamageState::Init(const EffectiveStats& stats)
{
	for (size_t i = 0; i < zoneHp_.size(); ++i)
	{
		zoneMax_[i] = std::max(1.0f, stats.zoneHp[i]);
		zoneHp_[i] = zoneMax_[i];
	}
	engineMax_ = std::max(1.0f, stats.engineHp);
	engineHp_ = engineMax_;
	wheelMax_ = std::max(1.0f, stats.wheelHp);
	wheelHp_.fill(wheelMax_);
	wheelDetached_ = {};
	wrecked_ = false;
	onFire_ = false;
	lost_ = 0.0f;
}

DamageEvent DamageState::Apply(const ZoneDamage& damage)
{
	DamageEvent event;
	if (wrecked_)
	{
		return event;
	}
	float spillToEngine = 0.0f;
	float spillStructural = 0.0f;
	for (size_t i = 0; i < zoneHp_.size(); ++i)
	{
		const float d = std::max(0.0f, damage.zone[i]);
		if (d <= 0.0f)
		{
			continue;
		}
		const float before = zoneHp_[i];
		const float taken = std::min(before, d);
		zoneHp_[i] -= taken;
		event.applied += taken;
		if (before > 0.0f && zoneHp_[i] <= 0.0f)
		{
			event.zoneBroken = true;
		}
		// A broken zone no longer protects the car: the excess goes into the engine and the frame.
		const float excess = d - taken;
		spillToEngine += excess * 0.5f;
		spillStructural += excess * 0.5f;
	}
	if (spillStructural > 0.0f)
	{
		int intact = 0;
		for (const float hp : zoneHp_)
		{
			intact += hp > 0.0f ? 1 : 0;
		}
		if (intact > 0)
		{
			const float share = spillStructural / static_cast<float>(intact);
			for (float& hp : zoneHp_)
			{
				if (hp > 0.0f)
				{
					const float taken = std::min(hp, share);
					hp -= taken;
					event.applied += taken;
				}
			}
		}
		else
		{
			spillToEngine += spillStructural;
		}
	}
	const float engineDamage = std::max(0.0f, damage.engine) + spillToEngine;
	const float engineTaken = std::min(engineHp_, engineDamage);
	engineHp_ -= engineTaken;
	event.applied += engineTaken;
	for (size_t i = 0; i < wheelHp_.size(); ++i)
	{
		if (wheelDetached_[i])
		{
			continue;
		}
		const float taken = std::min(wheelHp_[i], std::max(0.0f, damage.wheels[i]));
		wheelHp_[i] -= taken;
		event.applied += taken;
		if (wheelHp_[i] <= 0.0f)
		{
			wheelDetached_[i] = true;
			event.wheelLost[i] = true;
		}
	}
	lost_ += event.applied;
	UpdateFlags(event);
	return event;
}

void DamageState::UpdateFlags(DamageEvent& event)
{
	const bool wasOnFire = onFire_;
	onFire_ = !wrecked_ && EngineFraction() < kFireThreshold;
	event.fireStarted = onFire_ && !wasOnFire;
	if (!wrecked_ && (engineHp_ <= 0.0f || Integrity() <= kWreckIntegrity))
	{
		wrecked_ = true;
		onFire_ = false;
		event.becameWrecked = true;
	}
}

DamageEvent DamageState::Tick(float dt, float repairPerSec)
{
	DamageEvent event;
	if (wrecked_ || dt <= 0.0f)
	{
		return event;
	}
	if (repairPerSec > 0.0f)
	{
		// repairPerSec is a fraction of max hp per second.
		Repair(repairPerSec * dt);
	}
	if (onFire_)
	{
		const float burn = std::min(engineHp_, kFireDamagePerSec * engineMax_ * dt);
		engineHp_ -= burn;
		lost_ += burn;
		event.applied = burn;
	}
	UpdateFlags(event);
	return event;
}

void DamageState::Repair(float fraction)
{
	if (wrecked_ || fraction <= 0.0f)
	{
		return;
	}
	for (size_t i = 0; i < zoneHp_.size(); ++i)
	{
		zoneHp_[i] = std::min(zoneMax_[i], zoneHp_[i] + zoneMax_[i] * fraction);
	}
	engineHp_ = std::min(engineMax_, engineHp_ + engineMax_ * fraction);
	for (size_t i = 0; i < wheelHp_.size(); ++i)
	{
		if (wheelDetached_[i] && fraction < 1.0f)
		{
			continue;
		}
		wheelDetached_[i] = false;
		wheelHp_[i] = std::min(wheelMax_, wheelHp_[i] + wheelMax_ * fraction);
	}
	onFire_ = EngineFraction() < kFireThreshold;
}

float DamageState::ZoneFraction(Zone z) const
{
	const size_t i = static_cast<size_t>(z);
	return Clamp01(zoneHp_[i] / zoneMax_[i]);
}

float DamageState::EngineFraction() const
{
	return Clamp01(engineHp_ / engineMax_);
}

float DamageState::WheelFraction(int wheel) const
{
	const size_t i = static_cast<size_t>(wheel);
	return wheelDetached_[i] ? 0.0f : Clamp01(wheelHp_[i] / wheelMax_);
}

float DamageState::Integrity() const
{
	float hp = 0.0f;
	float maxHp = 0.0f;
	for (size_t i = 0; i < zoneHp_.size(); ++i)
	{
		hp += zoneHp_[i];
		maxHp += zoneMax_[i];
	}
	// The engine counts double: a dead engine is a dead car.
	hp += 2.0f * engineHp_;
	maxHp += 2.0f * engineMax_;
	return maxHp > 0.0f ? Clamp01(hp / maxHp) : 0.0f;
}

float DamageState::TotalMax() const
{
	float total = engineMax_ + wheelMax_ * static_cast<float>(kWheelCount);
	for (const float m : zoneMax_)
	{
		total += m;
	}
	return total;
}

float DamageState::TotalLost() const
{
	return lost_;
}

DrivingDamage DamageState::DrivingEffects() const
{
	DrivingDamage fx;
	const float engine = EngineFraction();
	fx.powerFactor = (0.35f + 0.65f * std::pow(engine, 0.7f)) * (onFire_ ? 0.85f : 1.0f);
	for (int i = 0; i < kWheelCount; ++i)
	{
		const size_t idx = static_cast<size_t>(i);
		fx.wheelMissing[idx] = wheelDetached_[idx];
		fx.wheelGrip[idx] = 0.6f + 0.4f * WheelFraction(i);
	}
	// A bent front end pulls toward the more damaged side.
	fx.steerPullRad = (WheelFraction(0) - WheelFraction(1)) * 0.05f + (ZoneFraction(Zone::Right) - ZoneFraction(Zone::Left)) * 0.02f;
	return fx;
}

DamageSnapshot DamageState::Snapshot() const
{
	DamageSnapshot s;
	for (int i = 0; i < kZoneCount; ++i)
	{
		s.zones[static_cast<size_t>(i)] = ZoneFraction(static_cast<Zone>(i));
	}
	s.engine = EngineFraction();
	for (int i = 0; i < kWheelCount; ++i)
	{
		s.wheels[static_cast<size_t>(i)] = WheelFraction(i);
	}
	s.wrecked = wrecked_;
	s.onFire = onFire_;
	return s;
}

void DamageState::ApplySnapshot(const DamageSnapshot& snapshot)
{
	for (size_t i = 0; i < zoneHp_.size(); ++i)
	{
		zoneHp_[i] = snapshot.zones[i] * zoneMax_[i];
	}
	engineHp_ = snapshot.engine * engineMax_;
	for (size_t i = 0; i < wheelHp_.size(); ++i)
	{
		wheelHp_[i] = snapshot.wheels[i] * wheelMax_;
		wheelDetached_[i] = snapshot.wheels[i] <= 0.0f;
	}
	wrecked_ = snapshot.wrecked;
	onFire_ = snapshot.onFire;
}

} // namespace crash
