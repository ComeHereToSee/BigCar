// Copyright BigCar Team. All Rights Reserved.
//
// Mode-agnostic match flow: countdown, rounds, lives/respawns, eliminations, hill control, bounty, time limits,
// win conditions and final placements. The Unreal GameMode drives this and mirrors its state to clients.
#pragma once

#include "CrashCore/Database.h"
#include "CrashCore/Export.h"
#include "CrashCore/Scoring.h"

#include <vector>

namespace crash
{
enum class MatchPhase : uint8_t
{
	Waiting,
	Countdown,
	Playing,
	RoundEnd,
	Finished
};

struct MatchParticipant
{
	int id = -1;
	int team = -1; // FFA: unique per player
	bool bot = false;
	bool alive = false;
	bool eliminated = false;
	bool connected = true;
	int livesLeft = 0; // ignored when lives are unlimited
	float respawnAt = -1.0f;
	float eliminatedAt = -1.0f;
};

struct MatchOutcome
{
	bool decided = false;
	bool draw = false;
	int winnerTeam = -1;
	int winnerId = -1;               // FFA winner
	std::vector<int> placements;     // participant ids, best first
	std::vector<int> teamPlacements; // team ids, best first
};

class MatchRules
{
public:
	CRASHCORE_EXPORT MatchRules(const ModeDef& mode, const ScoreTable& scoreTable, float timeLimitOverride = 0.0f);

	CRASHCORE_EXPORT void AddParticipant(int id, int team, bool bot);
	CRASHCORE_EXPORT void SetConnected(int id, bool connected);
	CRASHCORE_EXPORT void Start(float now, float countdownSeconds = 3.0f);
	// Advances timers and phases. Call every tick.
	CRASHCORE_EXPORT void Update(float now);

	// The host reports wrecks (any cause). killer is resolved by the ScoreKeeper.
	CRASHCORE_EXPORT TakedownInfo OnWrecked(int victim, float now, bool ringOut);
	// Ids standing inside the hill this tick.
	CRASHCORE_EXPORT void OnHillOccupants(const std::vector<int>& ids, float dt);

	// Participants whose respawn timer elapsed (host spawns them, then calls OnRespawned).
	CRASHCORE_EXPORT std::vector<int> TakeRespawns(float now);
	CRASHCORE_EXPORT void OnRespawned(int id);
	// True once after a round ends and the next round starts (host resets every car).
	CRASHCORE_EXPORT bool TakeRoundReset();

	MatchPhase Phase() const { return phase_; }
	CRASHCORE_EXPORT float TimeRemaining(float now) const;
	CRASHCORE_EXPORT float CountdownRemaining(float now) const;
	int Round() const { return round_; }
	CRASHCORE_EXPORT int RoundWins(int team) const;
	bool IsTeamMode() const { return mode_.teams >= 2; }
	int HillOwnerTeam() const { return hillOwnerTeam_; }
	bool HillContested() const { return hillContested_; }
	int BountyTarget() const { return scores_.BountyTarget(); }
	bool CanDrive() const { return phase_ == MatchPhase::Playing; }

	const ModeDef& Mode() const { return mode_; }
	ScoreKeeper& Scores() { return scores_; }
	const ScoreKeeper& Scores() const { return scores_; }
	const std::vector<MatchParticipant>& Participants() const { return participants_; }
	CRASHCORE_EXPORT const MatchParticipant* Find(int id) const;
	CRASHCORE_EXPORT int AliveCount() const;
	CRASHCORE_EXPORT int AliveInTeam(int team) const;
	CRASHCORE_EXPORT std::vector<int> AliveTeams() const;

	const MatchOutcome& Outcome() const { return outcome_; }
	// Ends the match immediately (objective modes, aborts). winnerTeam -1 = no winner.
	CRASHCORE_EXPORT void ForceFinish(float now, int winnerTeam);

	static constexpr float kRoundEndDelay = 4.0f;

private:
	CRASHCORE_EXPORT MatchParticipant* FindMutable(int id);
	CRASHCORE_EXPORT void CheckEnd(float now);
	CRASHCORE_EXPORT void EndRound(float now, int winnerTeam);
	CRASHCORE_EXPORT void Finish(float now, int winnerTeam);
	CRASHCORE_EXPORT void ComputePlacements(int winnerTeam);
	CRASHCORE_EXPORT int LeadingTeamByScore(bool* tie) const;
	CRASHCORE_EXPORT std::vector<int> Teams() const;

	ModeDef mode_;
	ScoreKeeper scores_;
	std::vector<MatchParticipant> participants_;
	MatchPhase phase_ = MatchPhase::Waiting;
	float timeLimit_ = 0.0f;
	float phaseStart_ = 0.0f;
	float countdownEnd_ = 0.0f;
	float roundEndAt_ = 0.0f;
	int round_ = 1;
	std::vector<std::pair<int, int>> roundWins_; // team -> wins
	bool roundReset_ = false;
	int hillOwnerTeam_ = -1;
	bool hillContested_ = false;
	std::vector<std::pair<int, float>> hillTime_; // team -> seconds
	MatchOutcome outcome_;
	std::vector<int> eliminationOrder_;
};

} // namespace crash
