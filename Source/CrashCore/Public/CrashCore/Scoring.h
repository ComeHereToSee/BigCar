// Copyright BigCar Team. All Rights Reserved.
//
// Score keeping: crash-type points, combos, takedown/assist credit, revenge, multi-takedowns, airtime, hill time.
#pragma once

#include "CrashCore/Database.h"
#include "CrashCore/Export.h"
#include "CrashCore/Impact.h"

#include <array>
#include <string>
#include <vector>

namespace crash
{
enum class ScoreEventType : uint8_t
{
	Impact,
	Takedown,
	Assist,
	RingOut,
	Revenge,
	MultiTakedown,
	Airtime,
	Hill,
	Survival,
	PropDestroyed,
	Collectible,
	Bounty,
	Count
};

struct ScoreEvent
{
	int playerId = -1;
	ScoreEventType type = ScoreEventType::Impact;
	ImpactKind kind = ImpactKind::None;
	int points = 0;
	float multiplier = 1.0f;
	int otherId = -1; // victim / killer
	int comboCount = 0;
	float time = 0.0f;
};

struct TakedownInfo
{
	int victim = -1;
	int killer = -1; // -1 = self-wreck (no credit)
	std::vector<int> assists;
	ImpactKind finishingKind = ImpactKind::None;
	bool ringOut = false;
	bool revenge = false;
	int multiCount = 0;
	bool bountyClaimed = false;
};

struct ParticipantStats
{
	int id = -1;
	int team = -1;
	float scoreExact = 0.0f;
	int takedowns = 0;
	int assists = 0;
	int wrecked = 0;
	int ringOuts = 0;
	float damageDealt = 0.0f;
	float damageTaken = 0.0f;
	std::array<int, kImpactKindCount> impacts{};
	float airtime = 0.0f;
	float bestAirtime = 0.0f;
	float hillTime = 0.0f;
	float survivalTime = 0.0f;
	int propsDestroyed = 0;
	int collectibles = 0;
	int bestCombo = 0;

	int Score() const { return static_cast<int>(scoreExact + 0.5f); }

	// Internal tracking.
	int comboCount = 0;
	float lastScoringHit = -1000.0f;
	int lastKiller = -1;
	float lastTakedownTime = -1000.0f;
	int multiCount = 0;
};

class ScoreKeeper
{
public:
	explicit CRASHCORE_EXPORT ScoreKeeper(const ScoreTable& table = ScoreTable());

	CRASHCORE_EXPORT void Reset();
	CRASHCORE_EXPORT void AddParticipant(int id, int team);
	CRASHCORE_EXPORT bool HasParticipant(int id) const;
	CRASHCORE_EXPORT void SetTeam(int id, int team);
	void SetBountyTarget(int id) { bountyTarget_ = id; }
	int BountyTarget() const { return bountyTarget_; }

	// Physical contact without damage still counts as "last touched" (ring-out credit).
	CRASHCORE_EXPORT void OnTouched(int attacker, int victim, float now);
	// Valid vehicle-vehicle impacts. Returns the points awarded to the attacker (0 if none).
	CRASHCORE_EXPORT int OnImpact(const ImpactResult& impact, float now);
	// Self-inflicted / world damage (counts for damage taken only).
	CRASHCORE_EXPORT void OnDamageTaken(int id, float amount);
	CRASHCORE_EXPORT TakedownInfo OnWrecked(int victim, float now, bool ringOut);
	CRASHCORE_EXPORT void OnAirtime(int id, float seconds);
	CRASHCORE_EXPORT void OnHillTime(int id, float dt);
	CRASHCORE_EXPORT void OnSurvival(int id, float dt);
	CRASHCORE_EXPORT void OnPropDestroyed(int id, float now);
	CRASHCORE_EXPORT void OnCollectible(int id, float now);
	CRASHCORE_EXPORT void AddBonus(int id, int points, float now);

	CRASHCORE_EXPORT std::vector<ScoreEvent> TakeEvents();
	CRASHCORE_EXPORT const ParticipantStats* Get(int id) const;
	const std::vector<ParticipantStats>& All() const { return participants_; }
	CRASHCORE_EXPORT int TeamScore(int team) const;
	CRASHCORE_EXPORT int TeamTakedowns(int team) const;
	CRASHCORE_EXPORT float TeamHillTime(int team) const;
	CRASHCORE_EXPORT float ComboMultiplier(int id, float now) const;

	static constexpr float kCreditWindow = 8.0f; // seconds a hit counts toward the takedown
	static constexpr float kAssistWindow = 10.0f;
	static constexpr float kAssistMinDamage = 100.0f;
	static constexpr float kMultiWindow = 6.0f;

private:
	struct DamageRecord
	{
		int attacker;
		int victim;
		float amount;
		float time;
		ImpactKind kind;
	};

	CRASHCORE_EXPORT ParticipantStats* Find(int id);
	CRASHCORE_EXPORT bool SameTeam(int a, int b) const;
	CRASHCORE_EXPORT float Award(ParticipantStats& p, ScoreEventType type, ImpactKind kind, int basePoints, float multiplier, int other,
	                             float now);

	ScoreTable table_;
	std::vector<ParticipantStats> participants_;
	std::vector<DamageRecord> records_;
	std::vector<ScoreEvent> events_;
	struct Touch
	{
		int attacker;
		int victim;
		float time;
	};
	std::vector<Touch> lastTouch_;
	int bountyTarget_ = -1;
};

} // namespace crash
