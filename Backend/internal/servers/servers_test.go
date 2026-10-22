package servers

import (
	"context"
	"io"
	"log/slog"
	"math"
	"sync"
	"testing"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/data/datatest"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
	"github.com/comeheretosee/bigcar/backend/internal/rating"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
)

type clock struct {
	mu sync.Mutex
	t  time.Time
}

func (c *clock) Now() time.Time { c.mu.Lock(); defer c.mu.Unlock(); return c.t }
func (c *clock) Add(d time.Duration) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.t = c.t.Add(d)
}

type recorder struct {
	mu    sync.Mutex
	ended []string
}

func (r *recorder) MatchEnded(id string, aborted bool, reason string) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.ended = append(r.ended, id+":"+reason)
}

var quiet = slog.New(slog.NewTextHandler(io.Discard, nil))

func newRegistry() (*Registry, *clock, *recorder) {
	c := &clock{t: time.Date(2026, 11, 15, 12, 0, 0, 0, time.UTC)}
	r := NewRegistry(c.Now, quiet)
	rec := &recorder{}
	r.SetListener(rec)
	return r, c, rec
}

func hb(id, region, state, match string) Heartbeat {
	return Heartbeat{ServerID: id, Address: "10.0.0.1", Port: 7777, Region: region, Version: "1.0", State: state, MatchID: match}
}

func assignment(id string, teams ...[]string) Assignment {
	a := Assignment{MatchID: id, Queue: "ranked_1v1", Mode: "team_elimination", Arena: "steel_dome", Ranked: true,
		TeamSize: 1, BotAI: "regular", MaxWaitSec: 60}
	for _, t := range teams {
		var team []AssignmentPlayer
		for _, pid := range t {
			team = append(team, AssignmentPlayer{PlayerID: pid, DisplayName: pid, JoinToken: "tok-" + pid, Rating: 1500})
		}
		a.Teams = append(a.Teams, team)
	}
	return a
}

func TestHeartbeatValidation(t *testing.T) {
	r, _, _ := newRegistry()
	bad := []Heartbeat{
		{ServerID: "", Address: "a", Port: 1, State: "idle"},
		{ServerID: "s 1", Address: "a", Port: 1, State: "idle"},
		{ServerID: "s1", Address: "", Port: 1, State: "idle"},
		{ServerID: "s1", Address: "a", Port: 0, State: "idle"},
		{ServerID: "s1", Address: "a", Port: 70000, State: "idle"},
		{ServerID: "s1", Address: "a", Port: 1, State: "busy"},
	}
	for _, h := range bad {
		if _, err := r.Heartbeat(h); apperr.CodeOf(err) != apperr.BadRequest {
			t.Errorf("%+v: %v", h, err)
		}
	}
}

func TestAllocationAndAssignmentDelivery(t *testing.T) {
	r, c, rec := newRegistry()
	if _, ok := r.Allocate(AllocateRequest{Assignment: assignment("m_0")}); ok {
		t.Fatal("allocated without servers")
	}
	_, _ = r.Heartbeat(hb("eu-2", "eu", StateIdle, ""))
	_, _ = r.Heartbeat(hb("eu-1", "eu", StateIdle, ""))
	_, _ = r.Heartbeat(hb("us-1", "us", StateIdle, ""))
	_, _ = r.Heartbeat(hb("us-busy", "us", StateInMatch, "m_other"))

	if _, ok := r.Allocate(AllocateRequest{Regions: []string{"asia"}, Assignment: assignment("m_x")}); ok {
		t.Fatal("allocated in a region without servers")
	}
	al, ok := r.Allocate(AllocateRequest{Preferred: "us", Assignment: assignment("m_1", []string{"a"}, []string{"b"})})
	if !ok || al.ServerID != "us-1" || al.Address != "10.0.0.1:7777" {
		t.Fatalf("preferred region: %+v %v", al, ok)
	}
	al2, ok := r.Allocate(AllocateRequest{Regions: []string{"eu"}, Assignment: assignment("m_2", []string{"c"}, []string{"d"})})
	if !ok || al2.ServerID != "eu-1" {
		t.Fatalf("region restricted: %+v", al2)
	}
	if _, ok := r.Allocate(AllocateRequest{Regions: []string{"us"}, Assignment: assignment("m_3")}); ok {
		t.Fatal("us-1 is reserved and us-busy is busy")
	}

	// Assignment is delivered on idle heartbeats until acknowledged.
	for i := 0; i < 2; i++ {
		a, err := r.Heartbeat(hb("us-1", "us", StateIdle, ""))
		if err != nil || a == nil || a.MatchID != "m_1" || len(a.Teams) != 2 {
			t.Fatalf("delivery %d: %+v %v", i, a, err)
		}
	}
	a, _ := r.Heartbeat(hb("us-1", "us", StateReserved, "m_1"))
	if a == nil || a.MatchID != "m_1" {
		t.Fatal("assignment should still be returned after ack")
	}
	if m, _ := r.MatchSnapshot("m_1"); !m.Acked {
		t.Fatal("not acked")
	}
	// After ack, an idle heartbeat means the server lost the match.
	if a, _ := r.Heartbeat(hb("us-1", "us", StateIdle, "")); a != nil {
		t.Fatal("lost match must not be re-delivered")
	}
	if m, _ := r.MatchSnapshot("m_1"); m.State != MatchAborted {
		t.Fatalf("state %s", m.State)
	}
	if len(rec.ended) != 1 || rec.ended[0] != "m_1:server_lost" {
		t.Fatalf("listener %v", rec.ended)
	}
	if s, _ := r.ServerSnapshot("us-1"); s.State != StateIdle || s.MatchID != "" {
		t.Fatalf("server not idle again: %+v", s)
	}

	// Start/abort permissions.
	if err := r.MatchStart("us-1", "m_2"); apperr.CodeOf(err) != apperr.Forbidden {
		t.Fatalf("wrong server start: %v", err)
	}
	if err := r.MatchStart("eu-1", "m_nope"); apperr.CodeOf(err) != apperr.NotFound {
		t.Fatalf("unknown match: %v", err)
	}
	if err := r.MatchStart("", "m_2"); apperr.CodeOf(err) != apperr.BadRequest {
		t.Fatalf("missing server id: %v", err)
	}
	if err := r.MatchStart("eu-1", "m_2"); err != nil {
		t.Fatal(err)
	}
	if err := r.MatchStart("eu-1", "m_2"); err != nil {
		t.Fatalf("start is idempotent: %v", err)
	}
	if err := r.Abort("eu-1", "m_2", "crash"); err != nil {
		t.Fatal(err)
	}
	if err := r.MatchStart("eu-1", "m_2"); apperr.CodeOf(err) != apperr.Conflict {
		t.Fatalf("start after abort: %v", err)
	}
	if len(rec.ended) != 2 || rec.ended[1] != "m_2:server_aborted" {
		t.Fatalf("listener %v", rec.ended)
	}

	// Silent servers are dropped after 20 s and their match aborted.
	_, _ = r.Heartbeat(hb("eu-2", "eu", StateIdle, ""))
	al3, ok := r.Allocate(AllocateRequest{Regions: []string{"eu"}, Assignment: assignment("m_4", []string{"e"}, []string{"f"})})
	if !ok {
		t.Fatal("no eu server")
	}
	_, _ = r.Heartbeat(hb(al3.ServerID, "eu", StateReserved, "m_4"))
	c.Add(21 * time.Second)
	r.Sweep(c.Now())
	if _, ok := r.ServerSnapshot(al3.ServerID); ok {
		t.Fatal("silent server not dropped")
	}
	if rec.ended[len(rec.ended)-1] != "m_4:server_lost" {
		t.Fatalf("listener %v", rec.ended)
	}
}

func TestUnacknowledgedAssignmentTimesOut(t *testing.T) {
	r, c, rec := newRegistry()
	_, _ = r.Heartbeat(hb("s1", "eu", StateIdle, ""))
	if _, ok := r.Allocate(AllocateRequest{Assignment: assignment("m_1")}); !ok {
		t.Fatal("no server")
	}
	for i := 0; i < 4; i++ { // keeps heartbeating idle, never acks
		c.Add(8 * time.Second)
		_, _ = r.Heartbeat(hb("s1", "eu", StateIdle, ""))
		r.Sweep(c.Now())
	}
	if len(rec.ended) != 1 || rec.ended[0] != "m_1:server_lost" {
		t.Fatalf("listener %v", rec.ended)
	}
	if s, _ := r.ServerSnapshot("s1"); s.State != StateIdle || s.MatchID != "" {
		t.Fatalf("server %+v", s)
	}
}

// --- results ---

type env struct {
	gd    *data.GameData
	st    storage.Store
	reg   *Registry
	rp    *ResultProcessor
	clock *clock
	rec   *recorder
}

func newEnv(t *testing.T, players ...string) *env {
	gd := datatest.Load(t)
	st := storage.NewMemory()
	reg, c, rec := newRegistry()
	for _, pid := range players {
		err := st.InTx(context.Background(), func(tx storage.Tx) error {
			if err := tx.CreateAccount(context.Background(), storage.Account{PlayerID: pid, DeviceID: "d-" + pid}); err != nil {
				return err
			}
			return tx.PutProfile(context.Background(), profile.New(pid, "N"+pid, gd))
		})
		if err != nil {
			t.Fatal(err)
		}
	}
	return &env{gd: gd, st: st, reg: reg, rp: NewResultProcessor(reg, st, gd, c.Now, quiet), clock: c, rec: rec}
}

func (e *env) start(t *testing.T, a Assignment, server string) {
	t.Helper()
	_, _ = e.reg.Heartbeat(hb(server, "eu", StateIdle, ""))
	if _, ok := e.reg.Allocate(AllocateRequest{Assignment: a}); !ok {
		t.Fatal("allocate failed")
	}
	if err := e.reg.MatchStart(server, a.MatchID); err != nil {
		t.Fatal(err)
	}
}

func (e *env) profile(t *testing.T, pid string) *profile.Profile {
	p, err := storage.ReadProfile(context.Background(), e.st, pid)
	if err != nil {
		t.Fatal(err)
	}
	return p
}

func intp(n int) *int { return &n }

func TestResultValidation(t *testing.T) {
	e := newEnv(t, "p_a", "p_b")
	e.start(t, assignment("m_1", []string{"p_a"}, []string{"p_b"}), "s1")
	ctx := context.Background()
	res := MatchResult{MatchID: "m_1", WinnerTeam: intp(0), Players: []PlayerResult{
		{PlayerID: "p_a", Team: 0, Score: 1000}, {PlayerID: "p_b", Team: 1, Score: 500},
	}}
	if _, err := e.rp.Process(ctx, "s2", res); apperr.CodeOf(err) != apperr.Forbidden {
		t.Fatalf("other server: %v", err)
	}
	if _, err := e.rp.Process(ctx, "s1", MatchResult{MatchID: "m_nope"}); apperr.CodeOf(err) != apperr.NotFound {
		t.Fatalf("unknown match: %v", err)
	}
	dup := res
	dup.Players = append(append([]PlayerResult{}, res.Players...), PlayerResult{PlayerID: "p_a"})
	if _, err := e.rp.Process(ctx, "s1", dup); apperr.CodeOf(err) != apperr.BadRequest {
		t.Fatalf("duplicate player: %v", err)
	}
	// The failed attempt released the claim, so the server can retry.
	rewards, err := e.rp.Process(ctx, "s1", res)
	if err != nil || len(rewards) != 2 {
		t.Fatalf("process: %v %v", rewards, err)
	}
	if _, err := e.rp.Process(ctx, "s1", res); apperr.CodeOf(err) != apperr.Conflict {
		t.Fatalf("second report: %v", err)
	}
	if len(e.rec.ended) != 1 || e.rec.ended[0] != "m_1:" {
		t.Fatalf("listener %v", e.rec.ended)
	}
	if err := e.reg.Abort("s1", "m_1", "late"); apperr.CodeOf(err) != apperr.Conflict {
		t.Fatalf("abort after result: %v", err)
	}
}

func TestRankedRewardsAndRatings(t *testing.T) {
	e := newEnv(t, "p_a", "p_b")
	e.start(t, assignment("m_1", []string{"p_a"}, []string{"p_b"}), "s1")
	res := MatchResult{MatchID: "m_1", DurationSec: 240, WinnerTeam: intp(0), Players: []PlayerResult{
		{PlayerID: "p_a", Team: 1 /* ignored: roster says 0 */, Score: 1000, Takedowns: 3, Wrecked: 1, DamageDealt: 2500.4,
			Impacts: map[string]float64{"t_bone": 2, "laser": 9}},
		{PlayerID: "p_b", Team: 1, Score: 2000, Takedowns: 1, Wrecked: 3},
		{PlayerID: "bot_7", Team: 1, Score: 99999},
	}}
	rewards, err := e.rp.Process(context.Background(), "s1", res)
	if err != nil {
		t.Fatal(err)
	}
	byID := map[string]Reward{}
	for _, r := range rewards {
		byID[r.PlayerID] = r
	}
	if len(byID) != 2 {
		t.Fatalf("rewards %+v", rewards)
	}
	ra, rb := byID["p_a"], byID["p_b"]
	// Winner: (800 + 1000*1.5) * 1.5 * 1.25 = 4312.5 → 4313 credits; (150 + 800) * 1.875 = 1781.25 → 1781 xp.
	if ra.Credits != 4313 || ra.XP != 1781 || ra.LevelBefore != 1 || ra.LevelAfter != 2 {
		t.Fatalf("winner reward %+v", ra)
	}
	// Loser: (800 + 3000) * 1.25 = 4750; (150 + 1600) * 1.25 = 2187.5 → 2188.
	if rb.Credits != 4750 || rb.XP != 2188 || rb.LevelAfter != 3 {
		t.Fatalf("loser reward %+v", rb)
	}
	want := rating.Update(rating.Default(), []rating.Result{{Opponent: rating.Default(), Score: 1}}, rating.Tau)
	if *ra.RatingBefore != 1500 || math.Abs(*ra.RatingAfter-want.R) > 1e-9 || *rb.RatingAfter >= 1500 {
		t.Fatalf("ratings %v→%v / %v", *ra.RatingBefore, *ra.RatingAfter, *rb.RatingAfter)
	}
	if *ra.Tier != "bronze" || *ra.Division != 3 {
		t.Fatalf("placement tier %s %d", *ra.Tier, *ra.Division)
	}

	pa, pb := e.profile(t, "p_a"), e.profile(t, "p_b")
	// level 2 reward (+5000 credits) on top of the match credits.
	if pa.Wallet.Credits != 25000+4313+5000 {
		t.Fatalf("p_a credits %d", pa.Wallet.Credits)
	}
	r := pa.Ranked["ranked_1v1"]
	if r.Wins != 1 || r.Losses != 0 || r.PlacementsLeft != 4 || r.RD >= 350 {
		t.Fatalf("p_a ranked %+v", r)
	}
	if pb.Ranked["ranked_1v1"].Losses != 1 {
		t.Fatalf("p_b ranked %+v", pb.Ranked["ranked_1v1"])
	}
	s := pa.Stats
	if s.Matches != 1 || s.Wins != 1 || s.Takedowns != 3 || s.Wrecked != 1 || s.DamageDealt != 2500 ||
		s.Impacts["t_bone"] != 2 || len(s.Impacts) != 1 {
		t.Fatalf("stats %+v", s)
	}
	if pb.Stats.Wins != 0 || pb.Stats.Matches != 1 {
		t.Fatalf("p_b stats %+v", pb.Stats)
	}
	// Season pass XP accrues (the test clock is inside season_1).
	if pa.SeasonPass.XP != 1781 {
		t.Fatalf("season xp %d", pa.SeasonPass.XP)
	}
}

func TestClampingDisconnectsAndDraws(t *testing.T) {
	e := newEnv(t, "p_a", "p_b", "p_c", "p_d")
	a := assignment("m_1", []string{"p_a", "p_b"}, []string{"p_c", "p_d"})
	a.Queue, a.TeamSize = "ranked_2v2", 2
	e.start(t, a, "s1")
	res := MatchResult{MatchID: "m_1", WinnerTeam: intp(0), Players: []PlayerResult{
		{PlayerID: "p_a", Score: 1e9, Takedowns: 1e6, DamageDealt: -50},
		{PlayerID: "p_b", Score: 100, Disconnected: true},
		{PlayerID: "p_c", Score: math.NaN()},
		// p_d missing → disconnected, zero score
	}}
	rewards, err := e.rp.Process(context.Background(), "s1", res)
	if err != nil {
		t.Fatal(err)
	}
	byID := map[string]Reward{}
	for _, r := range rewards {
		byID[r.PlayerID] = r
	}
	if byID["p_a"].Credits != 40000 || byID["p_a"].XP != 20000 {
		t.Fatalf("clamped score must hit the caps: %+v", byID["p_a"])
	}
	// Disconnected winner-team member: no win bonus → (800 + 150) * 1.25.
	if byID["p_b"].Credits != 1188 {
		t.Fatalf("disconnected reward %+v", byID["p_b"])
	}
	if byID["p_d"].Credits != 1000 {
		t.Fatalf("missing player reward %+v", byID["p_d"])
	}
	pa, pb := e.profile(t, "p_a"), e.profile(t, "p_b")
	if pa.Stats.Takedowns != MaxTakedowns || pa.Stats.DamageDealt != 0 {
		t.Fatalf("clamp stats %+v", pa.Stats)
	}
	if pb.Ranked["ranked_2v2"].Losses != 1 || pb.Ranked["ranked_2v2"].Rating >= 1500 {
		t.Fatalf("disconnected counts as a loss: %+v", pb.Ranked["ranked_2v2"])
	}
	if pa.Ranked["ranked_2v2"].Wins != 1 || pa.Ranked["ranked_2v2"].Rating <= 1500 {
		t.Fatalf("winner %+v", pa.Ranked["ranked_2v2"])
	}

	// A match without a winner is a draw for connected players.
	e2 := newEnv(t, "p_x", "p_y")
	e2.start(t, assignment("m_2", []string{"p_x"}, []string{"p_y"}), "s2")
	if _, err := e2.rp.Process(context.Background(), "s2", MatchResult{MatchID: "m_2", WinnerTeam: intp(-1),
		Players: []PlayerResult{{PlayerID: "p_x"}, {PlayerID: "p_y"}}}); err != nil {
		t.Fatal(err)
	}
	px := e2.profile(t, "p_x").Ranked["ranked_1v1"]
	if px.Wins != 0 || px.Losses != 0 || math.Abs(px.Rating-1500) > 1e-9 || px.PlacementsLeft != 4 {
		t.Fatalf("draw %+v", px)
	}
}

func TestPlacementsThenTier(t *testing.T) {
	e := newEnv(t, "p_a", "p_b")
	for i := 0; i < 5; i++ {
		id := "m_" + string(rune('0'+i))
		server := "s" + string(rune('0'+i))
		e.start(t, assignment(id, []string{"p_a"}, []string{"p_b"}), server)
		if _, err := e.rp.Process(context.Background(), server, MatchResult{MatchID: id, WinnerTeam: intp(0),
			Players: []PlayerResult{{PlayerID: "p_a"}, {PlayerID: "p_b"}}}); err != nil {
			t.Fatal(err)
		}
	}
	ra := e.profile(t, "p_a").Ranked["ranked_1v1"]
	rb := e.profile(t, "p_b").Ranked["ranked_1v1"]
	if ra.PlacementsLeft != 0 || ra.Wins != 5 || rb.Losses != 5 {
		t.Fatalf("after placements %+v / %+v", ra, rb)
	}
	wantTier, wantDiv := e.gd.RankFor(ra.Rating, 1)
	if ra.Tier != wantTier || ra.Division != wantDiv || ra.Tier == "bronze" {
		t.Fatalf("tier %s %d (rating %.1f), want %s %d", ra.Tier, ra.Division, ra.Rating, wantTier, wantDiv)
	}
	rows, total, _ := e.st.Ladder(context.Background(), "ranked_1v1", "season_1", 0, 10)
	if total != 2 || rows[0].PlayerID != "p_a" {
		t.Fatalf("ladder %+v", rows)
	}
}

func TestCasualMatchHasNoRating(t *testing.T) {
	e := newEnv(t, "p_a")
	a := Assignment{MatchID: "m_c", Queue: "casual_ffa", Mode: "derby", Arena: "rust_bowl", TeamSize: 1, FillWithBots: true,
		Teams: [][]AssignmentPlayer{{{PlayerID: "p_a"}}, {}, {}, {}, {}, {}, {}, {}}}
	e.start(t, a, "s1")
	rewards, err := e.rp.Process(context.Background(), "s1", MatchResult{MatchID: "m_c", WinnerTeam: intp(0),
		Players: []PlayerResult{{PlayerID: "p_a", Score: 100, Placement: 1}}})
	if err != nil {
		t.Fatal(err)
	}
	// (800 + 150) * 1.5 = 1425 credits, no ranked multiplier.
	if len(rewards) != 1 || rewards[0].Credits != 1425 || rewards[0].RatingAfter != nil || rewards[0].Tier != nil {
		t.Fatalf("casual reward %+v", rewards)
	}
	if len(e.profile(t, "p_a").Ranked) != 0 {
		t.Fatal("casual match created ranked state")
	}
}
