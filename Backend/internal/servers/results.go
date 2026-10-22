package servers

import (
	"context"
	"encoding/json"
	"errors"
	"log/slog"
	"math"
	"sort"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
	"github.com/comeheretosee/bigcar/backend/internal/rating"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
)

// Clamping limits for reported values.
const (
	MaxScore       = 200000
	MaxTakedowns   = 100
	MaxAssists     = 100
	MaxWrecked     = 100
	MaxDamage      = 10_000_000
	MaxImpactCount = 10_000
	MaxDurationSec = 4 * 3600
)

// ImpactKinds are the impact kinds accepted in results (DATA_SCHEMA).
var ImpactKinds = []string{"head_on", "t_bone", "side_swipe", "rear_end", "launch", "crush", "pit", "glance"}

// PlayerResult is one player line of a match result. Numbers are decoded as
// floats so servers may send either integers or floats.
type PlayerResult struct {
	PlayerID     string             `json:"playerId"`
	Team         int                `json:"team"`
	Score        float64            `json:"score"`
	Takedowns    float64            `json:"takedowns"`
	Assists      float64            `json:"assists"`
	Wrecked      float64            `json:"wrecked"`
	DamageDealt  float64            `json:"damageDealt"`
	Placement    int                `json:"placement"`
	Disconnected bool               `json:"disconnected"`
	Impacts      map[string]float64 `json:"impacts"`
}

// MatchResult is the body of POST /v1/servers/match-result. ServerID is an
// addition to the documented body (the X-Server-Id header works too) so the
// backend can check that only the assigned server reports.
type MatchResult struct {
	MatchID     string         `json:"matchId"`
	ServerID    string         `json:"serverId,omitempty"`
	DurationSec float64        `json:"durationSec"`
	WinnerTeam  *int           `json:"winnerTeam"`
	Players     []PlayerResult `json:"players"`
}

// Reward is one player's reward line. Rating fields are null for casual matches.
type Reward struct {
	PlayerID     string   `json:"playerId"`
	Credits      int64    `json:"credits"`
	XP           int64    `json:"xp"`
	LevelBefore  int      `json:"levelBefore"`
	LevelAfter   int      `json:"levelAfter"`
	RatingBefore *float64 `json:"ratingBefore"`
	RatingAfter  *float64 `json:"ratingAfter"`
	Tier         *string  `json:"tier"`
	Division     *int     `json:"division"`
}

// ResultProcessor validates match results and applies them to profiles.
type ResultProcessor struct {
	reg *Registry
	st  storage.Store
	gd  *data.GameData
	now func() time.Time
	log *slog.Logger
}

// NewResultProcessor creates a processor.
func NewResultProcessor(reg *Registry, st storage.Store, gd *data.GameData, now func() time.Time, log *slog.Logger) *ResultProcessor {
	if now == nil {
		now = time.Now
	}
	if log == nil {
		log = slog.Default()
	}
	return &ResultProcessor{reg: reg, st: st, gd: gd, now: now, log: log}
}

func clamp(v, lo, hi float64) float64 {
	if math.IsNaN(v) {
		return lo
	}
	return math.Max(lo, math.Min(hi, v))
}

// line is a sanitized player result.
type line struct {
	team         int
	score        float64
	stats        profile.MatchStats
	disconnected bool
}

// sanitize validates the reported lines against the roster: unknown players
// (e.g. bots) are ignored, duplicates are rejected, values are clamped, the
// team always comes from the assignment, and roster players missing from the
// report count as disconnected with zero stats.
func (rp *ResultProcessor) sanitize(a *Assignment, res *MatchResult) (map[string]line, error) {
	roster := a.Players()
	known := map[string]bool{}
	for _, k := range ImpactKinds {
		known[k] = true
	}
	lines := map[string]line{}
	seen := map[string]bool{}
	for _, pr := range res.Players {
		if seen[pr.PlayerID] {
			return nil, apperr.New(apperr.BadRequest, "player %q listed twice", pr.PlayerID)
		}
		seen[pr.PlayerID] = true
		team, ok := roster[pr.PlayerID]
		if !ok {
			continue
		}
		impacts := map[string]int64{}
		for k, v := range pr.Impacts {
			if known[k] {
				if n := int64(clamp(v, 0, MaxImpactCount)); n > 0 {
					impacts[k] = n
				}
			}
		}
		lines[pr.PlayerID] = line{
			team:         team,
			score:        math.Round(clamp(pr.Score, 0, MaxScore)),
			disconnected: pr.Disconnected,
			stats: profile.MatchStats{
				Takedowns:   int64(clamp(pr.Takedowns, 0, MaxTakedowns)),
				Wrecked:     int64(clamp(pr.Wrecked, 0, MaxWrecked)),
				DamageDealt: int64(math.Round(clamp(pr.DamageDealt, 0, MaxDamage))),
				Impacts:     impacts,
			},
		}
	}
	for pid, team := range roster {
		if _, ok := lines[pid]; !ok {
			lines[pid] = line{team: team, disconnected: true, stats: profile.MatchStats{Impacts: map[string]int64{}}}
		}
	}
	return lines, nil
}

// Process validates and applies a match result reported by serverID. It can
// succeed only once per match; all affected profiles are updated in one
// transaction (locked in player-id order to avoid deadlocks).
func (rp *ResultProcessor) Process(ctx context.Context, serverID string, res MatchResult) ([]Reward, error) {
	if res.MatchID == "" {
		return nil, apperr.New(apperr.BadRequest, "matchId is required")
	}
	m, err := rp.reg.beginResult(serverID, res.MatchID)
	if err != nil {
		return nil, err
	}
	ok := false
	defer func() { rp.reg.endResult(res.MatchID, ok) }()

	a := &m.Assignment
	lines, err := rp.sanitize(a, &res)
	if err != nil {
		return nil, err
	}
	winner := -1
	if res.WinnerTeam != nil && *res.WinnerTeam >= 0 && *res.WinnerTeam < len(a.Teams) {
		winner = *res.WinnerTeam
	}
	res.DurationSec = clamp(res.DurationSec, 0, MaxDurationSec)

	pids := make([]string, 0, len(lines))
	for pid := range lines {
		pids = append(pids, pid)
	}
	sort.Strings(pids)
	season := rp.gd.Store.SeasonPass.ID
	now := rp.now()
	seasonActive := rp.gd.Store.SeasonPass.ActiveAt(now)

	var rewards []Reward
	err = rp.st.InTx(ctx, func(tx storage.Tx) error {
		rewards = rewards[:0]
		profiles := map[string]*profile.Profile{}
		for _, pid := range pids {
			p, err := tx.GetProfile(ctx, pid, true)
			if errors.Is(err, storage.ErrNotFound) {
				continue // account deleted meanwhile
			}
			if err != nil {
				return err
			}
			profile.Normalize(p, rp.gd, now)
			profiles[pid] = p
		}
		// Pre-match ratings, so every player is rated against the same
		// snapshot (one Glicko-2 rating period per match).
		pre := map[string]profile.Ranked{}
		if a.Ranked {
			for pid, p := range profiles {
				pre[pid] = p.RankedState(rp.gd, a.Queue)
			}
		}
		for _, pid := range pids {
			p, found := profiles[pid]
			if !found {
				continue
			}
			l := lines[pid]
			won := !l.disconnected && winner >= 0 && l.team == winner
			draw := !l.disconnected && winner < 0
			credits, xp := rp.gd.MatchReward(l.score, won, a.Ranked)
			rw := Reward{PlayerID: pid, Credits: credits, XP: xp, LevelBefore: p.Level}
			p.Credit(data.Credits, credits)
			profile.AddXP(p, rp.gd, xp)
			profile.AddSeasonXP(p, &rp.gd.Store.SeasonPass, xp, seasonActive)
			rw.LevelAfter = p.Level
			l.stats.Won = won
			p.RecordMatch(l.stats)

			if a.Ranked {
				if err := rp.rate(ctx, tx, p, a, pre, lines, pid, won, draw, season, &rw); err != nil {
					return err
				}
			}
			if err := tx.PutProfile(ctx, p); err != nil {
				return err
			}
			rewards = append(rewards, rw)
		}
		resJSON, _ := json.Marshal(res)
		rwJSON, _ := json.Marshal(rewards)
		err := tx.InsertMatchResult(ctx, storage.MatchRecord{
			MatchID: res.MatchID, ServerID: serverID, Queue: a.Queue, Result: resJSON, Rewards: rwJSON, CreatedAt: now.UTC(),
		})
		if errors.Is(err, storage.ErrConflict) {
			return apperr.New(apperr.Conflict, "result for match %q was already reported", res.MatchID)
		}
		return err
	})
	if err != nil {
		return nil, err
	}
	ok = true
	rp.log.Info("match result applied", "match", res.MatchID, "server", serverID, "queue", a.Queue,
		"winnerTeam", winner, "players", len(rewards))
	return rewards, nil
}

// rate applies the Glicko-2 update for one player of a ranked match. Each
// player is rated against a single virtual opponent built from all players
// on other teams (mean rating, RMS RD). A disconnected player always scores
// 0 (a loss); a match without a winner is a draw for connected players.
func (rp *ResultProcessor) rate(ctx context.Context, tx storage.Tx, p *profile.Profile, a *Assignment,
	pre map[string]profile.Ranked, lines map[string]line, pid string, won, draw bool, season string, rw *Reward) error {
	me := pre[pid]
	var enemies []rating.Rating
	for other, r := range pre {
		if lines[other].team != lines[pid].team {
			enemies = append(enemies, r.Glicko())
		}
	}
	before := me.Rating
	if len(enemies) > 0 {
		score := 0.0
		switch {
		case won:
			score = 1
		case draw:
			score = 0.5
		}
		next := rating.Update(me.Glicko(), []rating.Result{{Opponent: rating.VirtualOpponent(enemies), Score: score}}, rating.Tau)
		me.Rating, me.RD, me.Vol = next.R, next.RD, next.Vol
	}
	switch {
	case won:
		me.Wins++
	case !draw:
		me.Losses++
	}
	if me.PlacementsLeft > 0 {
		me.PlacementsLeft--
	}
	if me.PlacementsLeft > 0 {
		me.Tier, me.Division = rp.gd.PlacementRank()
	} else {
		rank, err := tx.LadderRank(ctx, a.Queue, season, me.Rating, pid)
		if err != nil {
			return err
		}
		me.Tier, me.Division = rp.gd.RankFor(me.Rating, rank)
	}
	p.Ranked[a.Queue] = me
	after, tier, div := me.Rating, me.Tier, me.Division
	rw.RatingBefore, rw.RatingAfter, rw.Tier, rw.Division = &before, &after, &tier, &div
	return nil
}
