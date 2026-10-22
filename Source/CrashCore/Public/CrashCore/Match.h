// Copyright BigCar Team. All Rights Reserved.
//
// Mode-agnostic match flow: countdown, rounds, lives/respawns, eliminations, hill control, bounty, time limits,
// win conditions and final placements. The Unreal GameMode drives this and mirrors its state to clients.
#pragma once

#include "CrashCore/Database.h"
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
	MatchRules(const ModeDef& mode, const ScoreTable& scoreTable, float timeLimitOverride = 0.0f);

	void AddParticipant(int id, int team, bool bot);
	void SetConnected(int id, bool connected);
	void Start(float now, float countdownSeconds = 3.0f);
	// Advances timers and phases. Call every tick.
	void Update(float now);

	// The host reports wrecks (any cause). killer is resolved by the ScoreKeeper.
	TakedownInfo OnWrecked(int victim, float now, bool ringOut);
	// Ids standing inside the hill this tick.
	void OnHillOccupants(const std::vector<int>& ids, float dt);

	// Participants whose respawn timer elapsed (host spawns them, then calls OnRespawned).
	std::vector<int> TakeRespawns(float now);
	void OnRespawned(int id);
	// True once after a round ends and the next round starts (host resets every car).
	bool TakeRoundReset();

	MatchPhase Phase() const { return phase_; }
	float TimeRemaining(float now) const;
	float CountdownRemaining(float now) const;
	int Round() const { return round_; }
	int RoundWins(int team) const;
	bool IsTeamMode() const { return mode_.teams >= 2; }
	int HillOwnerTeam() const { return hillOwnerTeam_; }
	bool HillContested() const { return hillContested_; }
	int BountyTarget() const { return scores_.BountyTarget(); }
	bool CanDrive() const { return phase_ == MatchPhase::Playing; }

	const ModeDef& Mode() const { return mode_; }
	ScoreKeeper& Scores() { return scores_; }
	const ScoreKeeper& Scores() const { return scores_; }
	const std::vector<MatchParticipant>& Participants() const { return participants_; }
	const MatchParticipant* Find(int id) const;
	int AliveCount() const;
	int AliveInTeam(int team) const;
	std::vector<int> AliveTeams() const;

	const MatchOutcome& Outcome() const { return outcome_; }
	// Ends the match immediately (objective modes, aborts). winnerTeam -1 = no winner.
	void ForceFinish(float now, int winnerTeam);

	static constexpr float kRoundEndDelay = 4.0f;

private:
	MatchParticipant* FindMutable(int id);
	void CheckEnd(float now);
	void EndRound(float now, int winnerTeam);
	void Finish(float now, int winnerTeam);
	void ComputePlacements(int winnerTeam);
	int LeadingTeamByScore(bool* tie) const;
	std::vector<int> Teams() const;

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
