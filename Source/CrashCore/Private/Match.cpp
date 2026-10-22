// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Match.h"

#include <algorithm>
#include <map>
#include <set>

namespace crash
{
MatchRules::MatchRules(const ModeDef& mode, const ScoreTable& scoreTable, float timeLimitOverride) : mode_(mode), scores_(scoreTable)
{
	timeLimit_ = timeLimitOverride > 0.0f ? timeLimitOverride : mode.timeLimit;
}

void MatchRules::AddParticipant(int id, int team, bool bot)
{
	if (FindMutable(id))
	{
		return;
	}
	MatchParticipant p;
	p.id = id;
	p.team = team;
	p.bot = bot;
	p.livesLeft = mode_.lives;
	participants_.push_back(p);
	scores_.AddParticipant(id, team);
}

void MatchRules::SetConnected(int id, bool connected)
{
	if (MatchParticipant* p = FindMutable(id))
	{
		p->connected = connected;
	}
}

MatchParticipant* MatchRules::FindMutable(int id)
{
	for (MatchParticipant& p : participants_)
	{
		if (p.id == id)
		{
			return &p;
		}
	}
	return nullptr;
}

const MatchParticipant* MatchRules::Find(int id) const
{
	for (const MatchParticipant& p : participants_)
	{
		if (p.id == id)
		{
			return &p;
		}
	}
	return nullptr;
}

void MatchRules::Start(float now, float countdownSeconds)
{
	phase_ = MatchPhase::Countdown;
	phaseStart_ = now;
	countdownEnd_ = now + countdownSeconds;
	for (MatchParticipant& p : participants_)
	{
		p.alive = true;
		p.eliminated = false;
		p.livesLeft = mode_.lives;
		p.respawnAt = -1.0f;
	}
	if (mode_.bounty && !participants_.empty())
	{
		scores_.SetBountyTarget(participants_.front().id);
	}
}

float MatchRules::CountdownRemaining(float now) const
{
	return phase_ == MatchPhase::Countdown ? std::max(0.0f, countdownEnd_ - now) : 0.0f;
}

float MatchRules::TimeRemaining(float now) const
{
	if (timeLimit_ <= 0.0f)
	{
		return -1.0f;
	}
	if (phase_ == MatchPhase::Countdown || phase_ == MatchPhase::Waiting)
	{
		return timeLimit_;
	}
	if (phase_ != MatchPhase::Playing)
	{
		return 0.0f;
	}
	return std::max(0.0f, timeLimit_ - (now - phaseStart_));
}

int MatchRules::RoundWins(int team) const
{
	for (const auto& [t, wins] : roundWins_)
	{
		if (t == team)
		{
			return wins;
		}
	}
	return 0;
}

std::vector<int> MatchRules::Teams() const
{
	std::set<int> teams;
	for (const MatchParticipant& p : participants_)
	{
		teams.insert(p.team);
	}
	return std::vector<int>(teams.begin(), teams.end());
}

int MatchRules::AliveCount() const
{
	int n = 0;
	for (const MatchParticipant& p : participants_)
	{
		n += (!p.eliminated && p.connected) ? 1 : 0;
	}
	return n;
}

int MatchRules::AliveInTeam(int team) const
{
	int n = 0;
	for (const MatchParticipant& p : participants_)
	{
		n += (p.team == team && !p.eliminated && p.connected) ? 1 : 0;
	}
	return n;
}

std::vector<int> MatchRules::AliveTeams() const
{
	std::set<int> teams;
	for (const MatchParticipant& p : participants_)
	{
		if (!p.eliminated && p.connected)
		{
			teams.insert(p.team);
		}
	}
	return std::vector<int>(teams.begin(), teams.end());
}

void MatchRules::Update(float now)
{
	switch (phase_)
	{
	case MatchPhase::Countdown:
		if (now >= countdownEnd_)
		{
			phase_ = MatchPhase::Playing;
			phaseStart_ = now;
		}
		break;
	case MatchPhase::Playing:
		CheckEnd(now);
		break;
	case MatchPhase::RoundEnd:
		if (now >= roundEndAt_)
		{
			++round_;
			for (MatchParticipant& p : participants_)
			{
				p.alive = true;
				p.eliminated = false;
				p.livesLeft = mode_.lives;
				p.respawnAt = -1.0f;
			}
			roundReset_ = true;
			phase_ = MatchPhase::Countdown;
			countdownEnd_ = now + 3.0f;
		}
		break;
	default:
		break;
	}
}

TakedownInfo MatchRules::OnWrecked(int victim, float now, bool ringOut)
{
	TakedownInfo info;
	MatchParticipant* p = FindMutable(victim);
	if (!p || !p->alive || phase_ != MatchPhase::Playing)
	{
		return info;
	}
	info = scores_.OnWrecked(victim, now, ringOut);
	p->alive = false;
	if (mode_.bounty && info.killer >= 0 && scores_.BountyTarget() == victim)
	{
		scores_.SetBountyTarget(info.killer);
	}
	bool canRespawn = mode_.respawn;
	if (canRespawn && mode_.lives > 0)
	{
		p->livesLeft -= 1;
		canRespawn = p->livesLeft > 0;
	}
	if (canRespawn)
	{
		p->respawnAt = now + mode_.respawnDelay;
	}
	else
	{
		p->eliminated = true;
		p->eliminatedAt = now;
		eliminationOrder_.push_back(victim);
	}
	CheckEnd(now);
	return info;
}

void MatchRules::OnHillOccupants(const std::vector<int>& ids, float dt)
{
	if (phase_ != MatchPhase::Playing || !mode_.hill)
	{
		return;
	}
	std::set<int> teams;
	for (const int id : ids)
	{
		if (const MatchParticipant* p = Find(id))
		{
			if (p->alive)
			{
				teams.insert(p->team);
			}
		}
	}
	hillContested_ = teams.size() > 1;
	if (teams.size() == 1)
	{
		hillOwnerTeam_ = *teams.begin();
		for (const int id : ids)
		{
			scores_.OnHillTime(id, dt / static_cast<float>(ids.size()));
		}
		bool found = false;
		for (auto& [team, seconds] : hillTime_)
		{
			if (team == hillOwnerTeam_)
			{
				seconds += dt;
				found = true;
			}
		}
		if (!found)
		{
			hillTime_.emplace_back(hillOwnerTeam_, dt);
		}
	}
	else if (teams.empty())
	{
		hillOwnerTeam_ = -1;
	}
}

std::vector<int> MatchRules::TakeRespawns(float now)
{
	std::vector<int> out;
	if (phase_ != MatchPhase::Playing)
	{
		return out;
	}
	for (MatchParticipant& p : participants_)
	{
		if (!p.alive && !p.eliminated && p.respawnAt >= 0.0f && now >= p.respawnAt && p.connected)
		{
			p.respawnAt = -1.0f;
			out.push_back(p.id);
		}
	}
	return out;
}

void MatchRules::OnRespawned(int id)
{
	if (MatchParticipant* p = FindMutable(id))
	{
		p->alive = true;
	}
}

bool MatchRules::TakeRoundReset()
{
	const bool r = roundReset_;
	roundReset_ = false;
	return r;
}

int MatchRules::LeadingTeamByScore(bool* tie) const
{
	int best = -1;
	float bestValue = -1.0f;
	bool isTie = false;
	for (const int team : Teams())
	{
		float value = 0.0f;
		switch (mode_.win)
		{
		case WinCondition::Takedowns:
			value = static_cast<float>(scores_.TeamTakedowns(team)) * 100000.0f + static_cast<float>(scores_.TeamScore(team));
			break;
		case WinCondition::HillTime:
		{
			for (const auto& [t, seconds] : hillTime_)
			{
				if (t == team)
				{
					value = seconds;
				}
			}
			break;
		}
		case WinCondition::LastStanding:
			value = static_cast<float>(AliveInTeam(team)) * 1.0e6f + static_cast<float>(scores_.TeamScore(team));
			break;
		default:
			value = static_cast<float>(scores_.TeamScore(team));
			break;
		}
		if (value > bestValue)
		{
			bestValue = value;
			best = team;
			isTie = false;
		}
		else if (value == bestValue)
		{
			isTie = true;
		}
	}
	if (tie)
	{
		*tie = isTie;
	}
	return best;
}

void MatchRules::CheckEnd(float now)
{
	if (phase_ != MatchPhase::Playing)
	{
		return;
	}
	const bool timeUp = timeLimit_ > 0.0f && now - phaseStart_ >= timeLimit_;
	switch (mode_.win)
	{
	case WinCondition::LastStanding:
	{
		const std::vector<int> alive = AliveTeams();
		if (alive.size() <= 1 && Teams().size() > 1)
		{
			EndRound(now, alive.empty() ? -1 : alive.front());
			return;
		}
		if (timeUp)
		{
			bool tie = false;
			const int leader = LeadingTeamByScore(&tie);
			EndRound(now, tie ? -1 : leader);
		}
		return;
	}
	case WinCondition::Score:
	case WinCondition::Takedowns:
	{
		if (mode_.scoreLimit > 0)
		{
			for (const int team : Teams())
			{
				const int value = mode_.win == WinCondition::Score ? scores_.TeamScore(team) : scores_.TeamTakedowns(team);
				if (value >= mode_.scoreLimit)
				{
					Finish(now, team);
					return;
				}
			}
		}
		if (timeUp)
		{
			bool tie = false;
			const int leader = LeadingTeamByScore(&tie);
			Finish(now, tie ? -1 : leader);
		}
		return;
	}
	case WinCondition::HillTime:
	{
		if (mode_.scoreLimit > 0)
		{
			for (const auto& [team, seconds] : hillTime_)
			{
				if (seconds >= static_cast<float>(mode_.scoreLimit))
				{
					Finish(now, team);
					return;
				}
			}
		}
		if (timeUp)
		{
			bool tie = false;
			const int leader = LeadingTeamByScore(&tie);
			Finish(now, tie ? -1 : leader);
		}
		return;
	}
	case WinCondition::Survive:
	{
		// The player team (0) wins by surviving until the time runs out.
		if (AliveInTeam(0) == 0)
		{
			Finish(now, -1);
			return;
		}
		if (timeUp)
		{
			Finish(now, 0);
		}
		return;
	}
	case WinCondition::Objective:
	default:
		if (timeUp)
		{
			Finish(now, -1);
		}
		return;
	}
}

void MatchRules::EndRound(float now, int winnerTeam)
{
	if (mode_.rounds <= 1)
	{
		Finish(now, winnerTeam);
		return;
	}
	if (winnerTeam >= 0)
	{
		bool found = false;
		for (auto& [team, wins] : roundWins_)
		{
			if (team == winnerTeam)
			{
				++wins;
				found = true;
			}
		}
		if (!found)
		{
			roundWins_.emplace_back(winnerTeam, 1);
		}
	}
	if (winnerTeam >= 0 && RoundWins(winnerTeam) >= mode_.roundsToWin)
	{
		Finish(now, winnerTeam);
		return;
	}
	if (round_ >= mode_.rounds)
	{
		// Out of rounds: most round wins, then score.
		int best = -1;
		int bestWins = -1;
		bool tie = false;
		for (const int team : Teams())
		{
			const int wins = RoundWins(team);
			if (wins > bestWins)
			{
				bestWins = wins;
				best = team;
				tie = false;
			}
			else if (wins == bestWins)
			{
				tie = true;
			}
		}
		if (tie)
		{
			best = LeadingTeamByScore(&tie);
		}
		Finish(now, tie ? -1 : best);
		return;
	}
	phase_ = MatchPhase::RoundEnd;
	roundEndAt_ = now + kRoundEndDelay;
}

void MatchRules::Finish(float now, int winnerTeam)
{
	(void)now;
	phase_ = MatchPhase::Finished;
	outcome_.decided = true;
	outcome_.draw = winnerTeam < 0;
	outcome_.winnerTeam = winnerTeam;
	ComputePlacements(winnerTeam);
}

void MatchRules::ForceFinish(float now, int winnerTeam)
{
	if (phase_ == MatchPhase::Finished)
	{
		return;
	}
	Finish(now, winnerTeam);
}

void MatchRules::ComputePlacements(int winnerTeam)
{
	std::vector<int> ids;
	for (const MatchParticipant& p : participants_)
	{
		ids.push_back(p.id);
	}
	// Order: winner team first, then (last-standing modes) by survival, then by score.
	auto eliminationIndex = [this](int id)
	{
		for (size_t i = 0; i < eliminationOrder_.size(); ++i)
		{
			if (eliminationOrder_[i] == id)
			{
				return static_cast<int>(i);
			}
		}
		return static_cast<int>(eliminationOrder_.size()) + 1; // never eliminated = survived longest
	};
	const bool survivalOrder = mode_.win == WinCondition::LastStanding;
	std::stable_sort(ids.begin(), ids.end(),
	                 [&](int a, int b)
	                 {
		                 const MatchParticipant* pa = Find(a);
		                 const MatchParticipant* pb = Find(b);
		                 if (!pa || !pb)
		                 {
			                 return pa != nullptr && pb == nullptr;
		                 }
		                 const bool wa = pa->team == winnerTeam;
		                 const bool wb = pb->team == winnerTeam;
		                 if (wa != wb)
		                 {
			                 return wa;
		                 }
		                 if (survivalOrder)
		                 {
			                 const int ea = eliminationIndex(a);
			                 const int eb = eliminationIndex(b);
			                 if (ea != eb)
			                 {
				                 return ea > eb;
			                 }
		                 }
		                 const ParticipantStats* sa = scores_.Get(a);
		                 const ParticipantStats* sb = scores_.Get(b);
		                 return (sa ? sa->scoreExact : 0.0f) > (sb ? sb->scoreExact : 0.0f);
	                 });
	outcome_.placements = ids;
	if (!ids.empty() && winnerTeam >= 0)
	{
		outcome_.winnerId = ids.front();
	}
	outcome_.teamPlacements.clear();
	for (const int id : ids)
	{
		const MatchParticipant* p = Find(id);
		if (!p)
		{
			continue;
		}
		const int team = p->team;
		if (std::find(outcome_.teamPlacements.begin(), outcome_.teamPlacements.end(), team) == outcome_.teamPlacements.end())
		{
			outcome_.teamPlacements.push_back(team);
		}
	}
}

} // namespace crash
