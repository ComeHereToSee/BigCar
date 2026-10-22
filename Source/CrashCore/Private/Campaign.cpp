// Copyright BigCar Team. All Rights Reserved.

#include "CrashCore/Campaign.h"

#include <algorithm>

namespace crash
{
bool EvaluateObjective(const ObjectiveDef& o, const LevelRunResult& run)
{
	switch (o.type)
	{
	case ObjectiveType::Win:
		return run.won;
	case ObjectiveType::Takedowns:
		return run.takedowns >= static_cast<int>(o.value);
	case ObjectiveType::TimeUnder:
		return run.won && run.time <= o.value;
	case ObjectiveType::NoWreck:
		return run.won && !run.playerWrecked;
	case ObjectiveType::DamageUnder:
		return run.won && run.damageTakenFraction * 100.0f <= o.value;
	case ObjectiveType::Score:
		return run.score >= static_cast<int>(o.value);
	case ObjectiveType::HitsOfKind:
		return run.hits[static_cast<size_t>(o.kind)] >= static_cast<int>(o.value);
	case ObjectiveType::Survive:
		return run.survived >= o.value;
	case ObjectiveType::DestroyProps:
		return run.propsDestroyed >= static_cast<int>(o.value);
	case ObjectiveType::HillTime:
		return run.hillTime >= o.value;
	case ObjectiveType::Collect:
		return static_cast<int>(run.collectiblesFound.size()) >= static_cast<int>(o.value);
	case ObjectiveType::Boss:
		return run.bossDefeated;
	default:
		return false;
	}
}

LevelCompletion ApplyLevelResult(const GameDatabase& db, Profile& profile, const LevelDef& level, const LevelRunResult& run)
{
	LevelCompletion c;
	LevelProgress& lp = profile.campaign.levels[level.id];
	lp.attempts += 1;
	if (static_cast<int>(lp.collectibles.size()) < level.collectibles)
	{
		lp.collectibles.resize(static_cast<size_t>(level.collectibles), false);
	}
	for (const int idx : run.collectiblesFound)
	{
		if (idx >= 0 && idx < level.collectibles)
		{
			lp.collectibles[static_cast<size_t>(idx)] = true;
		}
	}

	c.success = EvaluateObjective(level.objective, run);
	if (!c.success)
	{
		return c;
	}
	c.firstClear = !lp.completed;
	lp.completed = true;
	lp.bestScore = std::max(lp.bestScore, run.score);
	if (lp.bestTime <= 0.0f || run.time < lp.bestTime)
	{
		lp.bestTime = run.time;
	}
	for (size_t i = 0; i < 3; ++i)
	{
		c.starsThisRun[i] = EvaluateObjective(level.stars[i], run);
		if (c.starsThisRun[i] && !lp.stars[i])
		{
			lp.stars[i] = true;
			c.newStars[i] = true;
		}
	}
	// Full rewards on first clear, 25% credits / 50% xp on replays.
	c.credits = c.firstClear ? level.rewards.credits : level.rewards.credits / 4;
	c.xp = c.firstClear ? level.rewards.xp : level.rewards.xp / 2;
	for (const bool star : c.newStars)
	{
		c.credits += star ? level.rewards.credits / 10 : 0;
	}
	profile.credits += c.credits;
	if (c.firstClear && !level.rewards.items.empty())
	{
		const OpResult granted = GrantItems(db, profile, level.rewards.items);
		c.items = granted.granted;
	}
	c.levelUp = AddXp(db, profile, c.xp);
	return c;
}

int TotalStars(const Profile& profile)
{
	int total = 0;
	for (const auto& [id, lp] : profile.campaign.levels)
	{
		total += lp.StarCount();
	}
	return total;
}

int MaxStars(const GameDatabase& db)
{
	int total = 0;
	for (const ChapterDef& ch : db.Campaign().chapters)
	{
		total += 3 * static_cast<int>(ch.levels.size());
	}
	return total;
}

bool IsChapterUnlocked(const GameDatabase& db, const Profile& profile, const ChapterDef& chapter)
{
	const std::vector<ChapterDef>& chapters = db.Campaign().chapters;
	for (size_t i = 0; i < chapters.size(); ++i)
	{
		if (chapters[i].id != chapter.id)
		{
			continue;
		}
		if (TotalStars(profile) < chapter.unlockStars)
		{
			return false;
		}
		if (i == 0)
		{
			return true;
		}
		// The previous chapter's finale must be beaten.
		const ChapterDef& prev = chapters[i - 1];
		const auto it = profile.campaign.levels.find(prev.levels.back().id);
		return it != profile.campaign.levels.end() && it->second.completed;
	}
	return false;
}

bool IsLevelUnlocked(const GameDatabase& db, const Profile& profile, const LevelDef& level)
{
	const ChapterDef* chapter = db.FindChapter(level.chapter);
	if (!chapter || !IsChapterUnlocked(db, profile, *chapter))
	{
		return false;
	}
	if (TotalStars(profile) < level.unlockStars)
	{
		return false;
	}
	std::string required = level.unlockLevel;
	if (required.empty() && level.indexInChapter > 0)
	{
		required = chapter->levels[static_cast<size_t>(level.indexInChapter - 1)].id;
	}
	if (required.empty())
	{
		return true;
	}
	const auto it = profile.campaign.levels.find(required);
	return it != profile.campaign.levels.end() && it->second.completed;
}

bool MarkHubSecret(Profile& profile, const std::string& chapterId, const std::string& secretId)
{
	HubProgress& hub = profile.campaign.hubs[chapterId];
	if (std::find(hub.secrets.begin(), hub.secrets.end(), secretId) != hub.secrets.end())
	{
		return false;
	}
	hub.secrets.push_back(secretId);
	return true;
}

bool MarkHubCollectible(Profile& profile, const std::string& chapterId, int index)
{
	HubProgress& hub = profile.campaign.hubs[chapterId];
	if (std::find(hub.collectibles.begin(), hub.collectibles.end(), index) != hub.collectibles.end())
	{
		return false;
	}
	hub.collectibles.push_back(index);
	return true;
}

ExplorationStats ComputeExploration(const GameDatabase& db, const Profile& profile, const std::string& chapterId)
{
	ExplorationStats s;
	for (const ChapterDef& ch : db.Campaign().chapters)
	{
		if (!chapterId.empty() && ch.id != chapterId)
		{
			continue;
		}
		for (const LevelDef& l : ch.levels)
		{
			s.levelsTotal += 1;
			s.starsTotal += 3;
			s.collectiblesTotal += l.collectibles;
			const auto it = profile.campaign.levels.find(l.id);
			if (it != profile.campaign.levels.end())
			{
				s.levelsCompleted += it->second.completed ? 1 : 0;
				s.starsEarned += it->second.StarCount();
				s.collectiblesFound += std::min(it->second.CollectiblesFound(), l.collectibles);
			}
		}
		if (const ArenaDef* hubArena = ch.hub.empty() ? nullptr : db.FindArena(ch.hub))
		{
			s.secretsTotal += static_cast<int>(hubArena->secrets.size());
			s.collectiblesTotal += static_cast<int>(hubArena->collectibles.size());
			const auto hit = profile.campaign.hubs.find(ch.id);
			if (hit != profile.campaign.hubs.end())
			{
				s.secretsFound += std::min(static_cast<int>(hit->second.secrets.size()), static_cast<int>(hubArena->secrets.size()));
				s.collectiblesFound +=
				    std::min(static_cast<int>(hit->second.collectibles.size()), static_cast<int>(hubArena->collectibles.size()));
			}
		}
	}
	auto ratio = [](int a, int b) { return b > 0 ? static_cast<float>(a) / static_cast<float>(b) : 1.0f; };
	s.percent = 100.0f * (0.4f * ratio(s.collectiblesFound, s.collectiblesTotal) + 0.3f * ratio(s.starsEarned, s.starsTotal) +
	                      0.2f * ratio(s.levelsCompleted, s.levelsTotal) + 0.1f * ratio(s.secretsFound, s.secretsTotal));
	return s;
}

VehicleBuild MakeCampaignBuild(const CampaignCarDef& car)
{
	VehicleBuild b;
	b.vehicleId = car.vehicle;
	b.upgrades = car.upgrades;
	b.equipment = car.equipment;
	return b;
}

} // namespace crash
