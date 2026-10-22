// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Scoring.h"

#include <algorithm>
#include <cmath>

namespace crash
{
ScoreKeeper::ScoreKeeper(const ScoreTable& table) : table_(table)
{
}

void ScoreKeeper::Reset()
{
	for (ParticipantStats& p : participants_)
	{
		const int id = p.id;
		const int team = p.team;
		p = ParticipantStats();
		p.id = id;
		p.team = team;
	}
	records_.clear();
	events_.clear();
	lastTouch_.clear();
}

void ScoreKeeper::AddParticipant(int id, int team)
{
	if (HasParticipant(id))
	{
		SetTeam(id, team);
		return;
	}
	ParticipantStats p;
	p.id = id;
	p.team = team;
	participants_.push_back(p);
}

bool ScoreKeeper::HasParticipant(int id) const
{
	return Get(id) != nullptr;
}

void ScoreKeeper::SetTeam(int id, int team)
{
	if (ParticipantStats* p = Find(id))
	{
		p->team = team;
	}
}

ParticipantStats* ScoreKeeper::Find(int id)
{
	for (ParticipantStats& p : participants_)
	{
		if (p.id == id)
		{
			return &p;
		}
	}
	return nullptr;
}

const ParticipantStats* ScoreKeeper::Get(int id) const
{
	for (const ParticipantStats& p : participants_)
	{
		if (p.id == id)
		{
			return &p;
		}
	}
	return nullptr;
}

bool ScoreKeeper::SameTeam(int a, int b) const
{
	const ParticipantStats* pa = Get(a);
	const ParticipantStats* pb = Get(b);
	return pa && pb && pa->team >= 0 && pa->team == pb->team;
}

float ScoreKeeper::ComboMultiplier(int id, float now) const
{
	const ParticipantStats* p = Get(id);
	if (!p || now - p->lastScoringHit > table_.comboWindow)
	{
		return 1.0f;
	}
	return std::min(1.0f + table_.comboStep * static_cast<float>(p->comboCount), table_.comboMax);
}

float ScoreKeeper::Award(ParticipantStats& p, ScoreEventType type, ImpactKind kind, int basePoints, float multiplier, int other, float now)
{
	const float points = static_cast<float>(basePoints) * multiplier;
	p.scoreExact += points;
	ScoreEvent e;
	e.playerId = p.id;
	e.type = type;
	e.kind = kind;
	e.points = static_cast<int>(std::lround(points));
	e.multiplier = multiplier;
	e.otherId = other;
	e.comboCount = p.comboCount;
	e.time = now;
	if (e.points > 0 || type == ScoreEventType::Takedown)
	{
		events_.push_back(e);
	}
	return points;
}

void ScoreKeeper::OnTouched(int attacker, int victim, float now)
{
	if (attacker < 0 || victim < 0 || attacker == victim || SameTeam(attacker, victim))
	{
		return;
	}
	for (Touch& t : lastTouch_)
	{
		if (t.victim == victim)
		{
			t.attacker = attacker;
			t.time = now;
			return;
		}
	}
	lastTouch_.push_back({attacker, victim, now});
}

int ScoreKeeper::OnImpact(const ImpactResult& impact, float now)
{
	if (!impact.valid || impact.attackerId < 0 || impact.victimId < 0)
	{
		return 0;
	}
	ParticipantStats* attacker = Find(impact.attackerId);
	ParticipantStats* victim = Find(impact.victimId);
	if (!attacker || !victim)
	{
		return 0;
	}
	const float dealt = impact.Victim().damage.Total();
	const float taken = impact.Attacker().damage.Total();
	victim->damageTaken += dealt;
	attacker->damageTaken += taken;
	if (SameTeam(attacker->id, victim->id))
	{
		return 0; // friendly fire hurts but never scores
	}
	OnTouched(attacker->id, victim->id, now);
	attacker->damageDealt += dealt;
	records_.push_back({attacker->id, victim->id, dealt, now, impact.kind});
	if (impact.kind == ImpactKind::HeadOn)
	{
		// Head-ons are mutual: the victim side dealt damage too.
		records_.push_back({victim->id, attacker->id, taken, now, impact.kind});
		victim->damageDealt += taken;
	}
	// Keep the record list bounded.
	if (records_.size() > 512)
	{
		records_.erase(records_.begin(), records_.begin() + 256);
	}

	if (dealt < table_.minDamageForPoints || impact.kind == ImpactKind::None)
	{
		return 0;
	}
	attacker->impacts[static_cast<size_t>(impact.kind)] += 1;
	const int base = table_.impact[static_cast<size_t>(impact.kind)];
	if (base <= 0)
	{
		return 0;
	}
	if (now - attacker->lastScoringHit <= table_.comboWindow)
	{
		attacker->comboCount += 1;
	}
	else
	{
		attacker->comboCount = 0;
	}
	attacker->lastScoringHit = now;
	attacker->bestCombo = std::max(attacker->bestCombo, attacker->comboCount + 1);
	// Bigger hits score more: 70% base + up to 60% bonus by severity.
	const float severityBonus = 0.7f + 0.6f * impact.severity;
	const float multiplier = ComboMultiplier(attacker->id, now) * severityBonus;
	return static_cast<int>(std::lround(Award(*attacker, ScoreEventType::Impact, impact.kind, base, multiplier, victim->id, now)));
}

void ScoreKeeper::OnDamageTaken(int id, float amount)
{
	if (ParticipantStats* p = Find(id))
	{
		p->damageTaken += amount;
	}
}

TakedownInfo ScoreKeeper::OnWrecked(int victimId, float now, bool ringOut)
{
	TakedownInfo info;
	info.victim = victimId;
	info.ringOut = ringOut;
	ParticipantStats* victim = Find(victimId);
	if (!victim)
	{
		return info;
	}
	victim->wrecked += 1;

	// Killer: most recent damaging hit within the credit window, or the last touch for ring-outs.
	float bestTime = -1.0f;
	for (const DamageRecord& r : records_)
	{
		if (r.victim == victimId && now - r.time <= kCreditWindow && r.time >= bestTime && r.attacker != victimId)
		{
			bestTime = r.time;
			info.killer = r.attacker;
			info.finishingKind = r.kind;
		}
	}
	for (const Touch& t : lastTouch_)
	{
		if (t.victim == victimId && now - t.time <= kCreditWindow && t.time > bestTime)
		{
			bestTime = t.time;
			info.killer = t.attacker;
			info.finishingKind = ImpactKind::None;
		}
	}

	// Assists: everyone else who dealt meaningful damage recently.
	std::vector<std::pair<int, float>> totals;
	for (const DamageRecord& r : records_)
	{
		if (r.victim != victimId || now - r.time > kAssistWindow || r.attacker == info.killer || r.attacker == victimId)
		{
			continue;
		}
		auto it = std::find_if(totals.begin(), totals.end(), [&r](const auto& e) { return e.first == r.attacker; });
		if (it == totals.end())
		{
			totals.emplace_back(r.attacker, r.amount);
		}
		else
		{
			it->second += r.amount;
		}
	}
	for (const auto& [id, amount] : totals)
	{
		if (amount >= kAssistMinDamage && !SameTeam(id, victimId))
		{
			info.assists.push_back(id);
		}
	}

	ParticipantStats* killer = info.killer >= 0 ? Find(info.killer) : nullptr;
	if (killer && !SameTeam(killer->id, victimId))
	{
		killer->takedowns += 1;
		if (ringOut)
		{
			killer->ringOuts += 1;
		}
		const float combo = ComboMultiplier(killer->id, now);
		Award(*killer, ringOut ? ScoreEventType::RingOut : ScoreEventType::Takedown, info.finishingKind,
		      ringOut ? table_.ringOut : table_.takedown, combo, victimId, now);
		if (killer->lastKiller == victimId)
		{
			info.revenge = true;
			killer->lastKiller = -1;
			Award(*killer, ScoreEventType::Revenge, ImpactKind::None, table_.revenge, 1.0f, victimId, now);
		}
		if (now - killer->lastTakedownTime <= kMultiWindow)
		{
			killer->multiCount += 1;
			info.multiCount = killer->multiCount + 1;
			Award(*killer, ScoreEventType::MultiTakedown, ImpactKind::None, table_.multiTakedown * killer->multiCount, 1.0f, victimId, now);
		}
		else
		{
			killer->multiCount = 0;
		}
		killer->lastTakedownTime = now;
		if (bountyTarget_ == victimId)
		{
			info.bountyClaimed = true;
			Award(*killer, ScoreEventType::Bounty, ImpactKind::None, table_.takedown, 1.0f, victimId, now);
		}
		victim->lastKiller = killer->id;
	}
	else
	{
		info.killer = -1;
	}
	for (const int id : info.assists)
	{
		if (ParticipantStats* a = Find(id))
		{
			a->assists += 1;
			Award(*a, ScoreEventType::Assist, ImpactKind::None, table_.assist, 1.0f, victimId, now);
		}
	}
	// Clear damage history on the victim (they respawn fresh).
	records_.erase(std::remove_if(records_.begin(), records_.end(), [victimId](const DamageRecord& r) { return r.victim == victimId; }),
	               records_.end());
	lastTouch_.erase(std::remove_if(lastTouch_.begin(), lastTouch_.end(), [victimId](const Touch& t) { return t.victim == victimId; }),
	                 lastTouch_.end());
	return info;
}

void ScoreKeeper::OnAirtime(int id, float seconds)
{
	ParticipantStats* p = Find(id);
	if (!p || seconds < 1.0f)
	{
		return; // tiny hops do not count
	}
	p->airtime += seconds;
	p->bestAirtime = std::max(p->bestAirtime, seconds);
	Award(*p, ScoreEventType::Airtime, ImpactKind::None, static_cast<int>(std::lround(table_.airtimePerSec * seconds)), 1.0f, -1, 0.0f);
}

void ScoreKeeper::OnHillTime(int id, float dt)
{
	if (ParticipantStats* p = Find(id))
	{
		p->hillTime += dt;
		p->scoreExact += static_cast<float>(table_.hillPerSec) * dt;
	}
}

void ScoreKeeper::OnSurvival(int id, float dt)
{
	if (ParticipantStats* p = Find(id))
	{
		p->survivalTime += dt;
		p->scoreExact += static_cast<float>(table_.survivalPerSec) * dt;
	}
}

void ScoreKeeper::OnPropDestroyed(int id, float now)
{
	if (ParticipantStats* p = Find(id))
	{
		p->propsDestroyed += 1;
		Award(*p, ScoreEventType::PropDestroyed, ImpactKind::None, table_.propDestroyed, 1.0f, -1, now);
	}
}

void ScoreKeeper::OnCollectible(int id, float now)
{
	if (ParticipantStats* p = Find(id))
	{
		p->collectibles += 1;
		Award(*p, ScoreEventType::Collectible, ImpactKind::None, 250, 1.0f, -1, now);
	}
}

void ScoreKeeper::AddBonus(int id, int points, float now)
{
	if (ParticipantStats* p = Find(id))
	{
		Award(*p, ScoreEventType::Survival, ImpactKind::None, points, 1.0f, -1, now);
	}
}

std::vector<ScoreEvent> ScoreKeeper::TakeEvents()
{
	std::vector<ScoreEvent> out;
	out.swap(events_);
	return out;
}

int ScoreKeeper::TeamScore(int team) const
{
	float total = 0.0f;
	for (const ParticipantStats& p : participants_)
	{
		if (p.team == team)
		{
			total += p.scoreExact;
		}
	}
	return static_cast<int>(std::lround(total));
}

int ScoreKeeper::TeamTakedowns(int team) const
{
	int total = 0;
	for (const ParticipantStats& p : participants_)
	{
		if (p.team == team)
		{
			total += p.takedowns;
		}
	}
	return total;
}

float ScoreKeeper::TeamHillTime(int team) const
{
	float total = 0.0f;
	for (const ParticipantStats& p : participants_)
	{
		if (p.team == team)
		{
			total += p.hillTime;
		}
	}
	return total;
}

} // namespace crash
