package data

import (
	"fmt"
	"math"
)

// MaxUpgradeLevel is the highest upgrade stage.
const MaxUpgradeLevel = 5

// CrashKingTopN is how many ladder places can hold the crash_king tier.
const CrashKingTopN = 100

// RoundTo100 implements DATA_SCHEMA's roundTo100(x) = floor((x + 50) / 100) * 100.
//
// x is first rounded to 6 decimals so that binary floating point noise
// (4000 * 1.6 = 6400.000000000001) can never move a value across a rounding
// boundary.
func RoundTo100(x float64) int64 {
	x = math.Round(x*1e6) / 1e6
	return int64(math.Floor((x+50)/100)) * 100
}

// UpgradeCost returns the credits price of upgrade stage level (1..5) for a
// vehicle of the given tier (1..5):
//
//	roundTo100(levels[level-1].cost * tierCostMultiplier[tier-1])
func (gd *GameData) UpgradeCost(category string, level, tier int) (int64, error) {
	c, ok := gd.upgrades[category]
	if !ok {
		return 0, fmt.Errorf("unknown upgrade category %q", category)
	}
	if level < 1 || level > len(c.Levels) {
		return 0, fmt.Errorf("upgrade level %d out of range", level)
	}
	if tier < 1 || tier > len(gd.Upgrades.TierCostMultiplier) {
		return 0, fmt.Errorf("vehicle tier %d out of range", tier)
	}
	return RoundTo100(c.Levels[level-1].Cost * gd.Upgrades.TierCostMultiplier[tier-1]), nil
}

// XPToNext returns the XP needed to go from level n to n+1:
// round(xpBase * xpGrowth^(n-1)).
func (gd *GameData) XPToNext(level int) int64 {
	p := gd.Progression.Player
	if level < 1 {
		level = 1
	}
	return int64(math.Round(p.XPBase * math.Pow(p.XPGrowth, float64(level-1))))
}

// LevelRewardsFor returns the reward items for reaching level.
func (gd *GameData) LevelRewardsFor(level int) []Item {
	var out []Item
	for _, lr := range gd.Progression.LevelRewards {
		if lr.Level == level {
			out = append(out, lr.Items...)
		}
	}
	return out
}

// MatchReward computes credits and XP for one player per progression.json:
//
//	c = participationCredits + score * creditsPerScore
//	c *= winMultiplier   (winner)
//	c *= rankedMultiplier (ranked)
//	credits = min(round(c), maxCredits)
//
// and the same with the xp fields.
func (gd *GameData) MatchReward(score float64, winner, ranked bool) (credits, xp int64) {
	m := gd.Progression.Match
	c := m.ParticipationCredits + score*m.CreditsPerScore
	x := m.ParticipationXP + score*m.XPPerScore
	if winner {
		c *= m.WinMultiplier
		x *= m.WinMultiplier
	}
	if ranked {
		c *= m.RankedMultiplier
		x *= m.RankedMultiplier
	}
	credits = int64(math.Min(math.Round(c), m.MaxCredits))
	xp = int64(math.Min(math.Round(x), m.MaxXP))
	return credits, xp
}

// RankFor maps a rating to (tier id, division). Division 1 is the highest.
//
// The tier is the highest rank whose minRating <= rating. Within a tier the
// span [minRating, nextTier.minRating) is split evenly into `divisions`
// parts; the top part is division 1. The last tier has no upper bound and is
// always division 1. crash_king additionally requires a ladder position of
// 1..100 (ladderRank, 1-based; 0 = not on the ladder): otherwise the player is
// shown as the highest division of the tier below.
func (gd *GameData) RankFor(rating float64, ladderRank int) (string, int) {
	ranks := gd.Progression.Ranks
	if len(ranks) == 0 {
		return "", 0
	}
	i := 0
	for j := range ranks {
		if rating >= ranks[j].MinRating {
			i = j
		}
	}
	if ranks[i].ID == "crash_king" && (ladderRank < 1 || ladderRank > CrashKingTopN) && i > 0 {
		i--
	}
	r := ranks[i]
	if rating < r.MinRating {
		return r.ID, r.Divisions // below the lowest tier
	}
	if i == len(ranks)-1 || r.Divisions <= 1 {
		return r.ID, 1
	}
	step := (ranks[i+1].MinRating - r.MinRating) / float64(r.Divisions)
	idx := int(math.Floor((rating - r.MinRating) / step))
	if idx < 0 {
		idx = 0
	}
	if idx > r.Divisions-1 {
		idx = r.Divisions - 1
	}
	return r.ID, r.Divisions - idx
}

// PlacementRank is the tier/division shown while placement matches remain:
// the lowest division of the lowest tier.
func (gd *GameData) PlacementRank() (string, int) {
	ranks := gd.Progression.Ranks
	if len(ranks) == 0 {
		return "", 0
	}
	return ranks[0].ID, ranks[0].Divisions
}
