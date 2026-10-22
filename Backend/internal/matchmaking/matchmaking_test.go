package matchmaking

import (
	"context"
	"io"
	"log/slog"
	"sort"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/data/datatest"
	"github.com/comeheretosee/bigcar/backend/internal/party"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
	"github.com/comeheretosee/bigcar/backend/internal/servers"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
)

type clock struct {
	mu sync.Mutex
	t  time.Time
}

func (c *clock) Now() time.Time { c.mu.Lock(); defer c.mu.Unlock(); return c.t }
func (c *clock) Set(t time.Time) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.t = t
}

var quiet = slog.New(slog.NewTextHandler(io.Discard, nil))

type env struct {
	t       *testing.T
	gd      *data.GameData
	st      storage.Store
	parties *party.Manager
	reg     *servers.Registry
	mm      *Service
	clock   *clock
	t0      time.Time
	servers []servers.Heartbeat
}

func newEnv(t *testing.T) *env {
	t.Helper()
	gd := datatest.Load(t)
	t0 := time.Date(2026, 11, 15, 12, 0, 0, 0, time.UTC)
	c := &clock{t: t0}
	st := storage.NewMemory()
	parties := party.NewManager(nil)
	reg := servers.NewRegistry(c.Now, quiet)
	mm, err := New(Config{Data: gd, Store: st, Parties: parties, Allocator: reg, Now: c.Now, Log: quiet})
	if err != nil {
		t.Fatal(err)
	}
	reg.SetListener(mm)
	return &env{t: t, gd: gd, st: st, parties: parties, reg: reg, mm: mm, clock: c, t0: t0}
}

// player creates an account whose ranked rating in every ranked queue is r.
func (e *env) player(id string, r float64) {
	e.t.Helper()
	p := profile.New(id, "N"+id, e.gd)
	for _, q := range RankedQueueIDs() {
		rs := profile.NewRanked(e.gd)
		rs.Rating = r
		p.Ranked[q] = rs
	}
	err := e.st.InTx(context.Background(), func(tx storage.Tx) error {
		if err := tx.CreateAccount(context.Background(), storage.Account{PlayerID: id, DeviceID: "dev-" + id}); err != nil {
			return err
		}
		return tx.PutProfile(context.Background(), p)
	})
	if err != nil {
		e.t.Fatal(err)
	}
}

func (e *env) at(d time.Duration) time.Time {
	now := e.t0.Add(d)
	e.clock.Set(now)
	return now
}

// server registers an idle server; tick keeps it alive with heartbeats like a
// real server (silent servers are not eligible after 20 s).
func (e *env) server(id, region string) {
	e.t.Helper()
	hb := servers.Heartbeat{ServerID: id, Address: "10.1.0." + id[len(id)-1:], Port: 7777,
		Region: region, Version: "1", State: servers.StateIdle}
	if _, err := e.reg.Heartbeat(hb); err != nil {
		e.t.Fatal(err)
	}
	e.servers = append(e.servers, hb)
}

func (e *env) queue(pid, queue, region string) TicketView {
	e.t.Helper()
	v, err := e.mm.CreateTicket(context.Background(), pid, queue, region)
	if err != nil {
		e.t.Fatalf("queue %s: %v", pid, err)
	}
	return v
}

func (e *env) tick(d time.Duration) {
	now := e.at(d)
	for _, hb := range e.servers {
		if _, err := e.reg.Heartbeat(hb); err != nil {
			e.t.Fatal(err)
		}
	}
	e.mm.Tick(now)
}

func (e *env) current(pid string) TicketView {
	e.t.Helper()
	v := e.mm.Current(pid)
	if v == nil {
		e.t.Fatalf("no ticket for %s", pid)
	}
	return *v
}

func (e *env) assignment(serverID string) *servers.Assignment {
	e.t.Helper()
	s, _ := e.reg.ServerSnapshot(serverID)
	a, err := e.reg.Heartbeat(servers.Heartbeat{ServerID: serverID, Address: s.Address, Port: s.Port, Region: s.Region,
		State: servers.StateIdle})
	if err != nil || a == nil {
		e.t.Fatalf("no assignment for %s: %v", serverID, err)
	}
	return a
}

func teamIDs(a *servers.Assignment) []string {
	var out []string
	for _, team := range a.Teams {
		var ids []string
		for _, p := range team {
			ids = append(ids, p.PlayerID)
		}
		sort.Strings(ids)
		out = append(out, strings.Join(ids, "+"))
	}
	return out
}

func TestRanked1v1AndNoServer(t *testing.T) {
	e := newEnv(t)
	e.player("p_a", 1500)
	e.player("p_b", 1520)
	ta := e.queue("p_a", "ranked_1v1", "eu")
	if ta.Status != StatusSearching || ta.EstimatedWaitSec != 30 || ta.Match != nil {
		t.Fatalf("new ticket %+v", ta)
	}
	e.queue("p_b", "ranked_1v1", "eu")

	e.tick(time.Second) // no server yet
	if v := e.current("p_a"); v.Status != StatusSearching || v.Reason != ReasonNoServer || v.WaitSec != 1 {
		t.Fatalf("without server: %+v", v)
	}
	e.server("eu-1", "eu")
	e.tick(2 * time.Second)
	va, vb := e.current("p_a"), e.current("p_b")
	if va.Status != StatusMatched || vb.Status != StatusMatched || va.Match.MatchID != vb.Match.MatchID {
		t.Fatalf("not matched: %+v %+v", va, vb)
	}
	if va.Match.Team == vb.Match.Team || va.Match.JoinToken == vb.Match.JoinToken || len(va.Match.JoinToken) != 32 {
		t.Fatalf("teams/tokens: %+v %+v", va.Match, vb.Match)
	}
	if va.Match.Address != "10.1.0.1:7777" || va.Match.Mode != "team_elimination" || va.Match.Arena == "" || va.WaitSec != 2 {
		t.Fatalf("match info %+v", va)
	}
	a := e.assignment("eu-1")
	if a.MatchID != va.Match.MatchID || !a.Ranked || a.FillWithBots || a.TeamSize != 1 || len(a.Teams) != 2 ||
		a.BotAI != "regular" || a.MaxWaitSec != 60 {
		t.Fatalf("assignment %+v", a)
	}
	for _, team := range a.Teams {
		for _, p := range team {
			want := map[string]string{"p_a": va.Match.JoinToken, "p_b": vb.Match.JoinToken}[p.PlayerID]
			if p.JoinToken != want || p.Loadout.VehicleID != "hoshino_corolla" || p.DisplayName != "N"+p.PlayerID {
				t.Fatalf("assignment player %+v", p)
			}
		}
	}
	// Already in a match: cannot queue again until the match ends.
	if _, err := e.mm.CreateTicket(context.Background(), "p_a", "ranked_1v1", ""); apperr.CodeOf(err) != apperr.Conflict {
		t.Fatalf("queue while in match: %v", err)
	}
	if _, err := e.mm.Cancel("p_a", va.TicketID); apperr.CodeOf(err) != apperr.Conflict {
		t.Fatalf("cancel matched ticket: %v", err)
	}
	e.mm.MatchEnded(va.Match.MatchID, false, "")
	if v := e.current("p_a"); v.Status != StatusMatched {
		t.Fatalf("finished match keeps matched status: %+v", v)
	}
	e.queue("p_a", "ranked_1v1", "")
}

func TestParties3v3(t *testing.T) {
	e := newEnv(t)
	for _, id := range []string{"a1", "a2", "a3", "b1", "b2", "c1"} {
		e.player(id, 1500)
	}
	pa, _ := e.parties.Create("a1", "A1")
	for _, id := range []string{"a2", "a3"} {
		if _, err := e.parties.Join(id, id, pa.Code); err != nil {
			t.Fatal(err)
		}
	}
	pb, _ := e.parties.Create("b1", "B1")
	_, _ = e.parties.Join("b2", "b2", pb.Code)

	// Rules: leader only, everyone ready, party ≤ team size.
	if _, err := e.mm.CreateTicket(context.Background(), "a2", "ranked_3v3", ""); apperr.CodeOf(err) != apperr.NotLeader {
		t.Fatalf("non-leader: %v", err)
	}
	if _, err := e.mm.CreateTicket(context.Background(), "a1", "ranked_3v3", ""); apperr.CodeOf(err) != apperr.NotReady {
		t.Fatalf("not ready: %v", err)
	}
	_, _ = e.parties.SetReady("a2", true)
	_, _ = e.parties.SetReady("a3", true)
	_, _ = e.parties.SetReady("b2", true)
	if _, err := e.mm.CreateTicket(context.Background(), "a1", "ranked_2v2", ""); apperr.CodeOf(err) != apperr.BadRequest {
		t.Fatalf("party too large: %v", err)
	}
	ta := e.queue("a1", "ranked_3v3", "")
	if v := e.current("a3"); v.TicketID != ta.TicketID {
		t.Fatalf("member sees ticket: %+v", v)
	}
	if e.mm.PartyTicketID(pa.ID, "a1") != ta.TicketID {
		t.Fatal("party ticket id")
	}
	e.queue("b1", "ranked_3v3", "")
	e.queue("c1", "ranked_3v3", "")
	e.server("s1", "eu")
	e.tick(time.Second)
	a := e.assignment("s1")
	got := teamIDs(a)
	sort.Strings(got)
	if strings.Join(got, " | ") != "a1+a2+a3 | b1+b2+c1" {
		t.Fatalf("teams %v", got)
	}
	if e.current("b2").Match.Team != e.current("c1").Match.Team {
		t.Fatal("b-party and solo should share a team")
	}
}

func TestSnakeDraftBalancing(t *testing.T) {
	e := newEnv(t)
	ratings := map[string]float64{"r1600": 1600, "r1550": 1550, "r1500": 1500, "r1450": 1450, "r1400": 1400, "r1350": 1350}
	for id, r := range ratings {
		e.player(id, r)
		e.queue(id, "ranked_3v3", "")
	}
	e.server("s1", "eu")
	e.tick(4 * time.Second) // window 230 < spread 250
	if e.current("r1600").Status != StatusSearching {
		t.Fatal("matched before the window covers the spread")
	}
	e.tick(5 * time.Second) // window 250
	got := teamIDs(e.assignment("s1"))
	sort.Strings(got)
	if strings.Join(got, " | ") != "r1350+r1500+r1550 | r1400+r1450+r1600" {
		t.Fatalf("snake draft teams %v", got)
	}
}

func TestBuildTeamsPacking(t *testing.T) {
	q := Queue{ID: "x", TeamSize: 3, Teams: 2}
	mk := func(id string, size int, r float64) *ticket {
		t := &ticket{ID: id, Rating: r}
		for i := 0; i < size; i++ {
			t.Members = append(t.Members, Member{PlayerID: id + string(rune('a'+i))})
		}
		return t
	}
	tests := []struct {
		name  string
		units []*ticket
		ok    bool
	}{
		{"3+3", []*ticket{mk("a", 3, 1500), mk("b", 3, 1500)}, true},
		{"2+2+1+1", []*ticket{mk("a", 2, 1500), mk("b", 2, 1500), mk("c", 1, 1500), mk("d", 1, 1500)}, true},
		{"2+1+1+1+1", []*ticket{mk("a", 2, 1500), mk("b", 1, 1), mk("c", 1, 2), mk("d", 1, 3), mk("e", 1, 4)}, true},
		{"3+1+1+1", []*ticket{mk("a", 3, 1500), mk("b", 1, 1), mk("c", 1, 2), mk("d", 1, 3)}, true},
		{"2+2+2 impossible", []*ticket{mk("a", 2, 1500), mk("b", 2, 1500), mk("c", 2, 1500)}, false},
	}
	for _, tc := range tests {
		teams, ok := buildTeams(q, tc.units)
		if ok != tc.ok {
			t.Errorf("%s: ok=%v", tc.name, ok)
			continue
		}
		for _, team := range teams {
			n := 0
			for _, u := range team {
				n += u.size()
			}
			if n > 3 {
				t.Errorf("%s: team of %d", tc.name, n)
			}
		}
	}
}

func TestRatingWindowWidening(t *testing.T) {
	e := newEnv(t)
	e.server("s1", "eu")
	e.server("s2", "eu")
	e.player("lo", 1500)
	e.player("hi", 1900)
	e.queue("lo", "ranked_1v1", "")
	e.queue("hi", "ranked_1v1", "")
	e.tick(12 * time.Second) // 150 + 20*12 = 390 < 400
	if e.current("lo").Status != StatusSearching {
		t.Fatal("matched too early")
	}
	e.tick(13 * time.Second) // 410 ≥ 400
	if e.current("lo").Status != StatusMatched {
		t.Fatal("not matched once the window covers the gap")
	}

	// Ranked windows are capped at 800: a 1000-point gap never matches and
	// the tickets expire after 10 minutes.
	e2 := newEnv(t)
	e2.server("s1", "eu")
	e2.player("lo", 1000)
	e2.player("hi", 2000)
	e2.queue("lo", "ranked_1v1", "")
	e2.queue("hi", "ranked_1v1", "")
	e2.tick(9*time.Minute + 59*time.Second)
	if e2.current("lo").Status != StatusSearching {
		t.Fatal("capped window matched")
	}
	e2.tick(10 * time.Minute)
	if v := e2.current("hi"); v.Status != StatusExpired || v.Reason != ReasonTimeout {
		t.Fatalf("expiry: %+v", v)
	}
	// Expired tickets stay visible for a while, then disappear.
	e2.tick(10*time.Minute + TicketRetention + time.Second)
	if e2.mm.Current("hi") != nil {
		t.Fatal("expired ticket not cleaned up")
	}
	e2.queue("hi", "ranked_1v1", "") // can queue again
}

func TestCasualUnlimitedWindowAndBotFill(t *testing.T) {
	e := newEnv(t)
	e.server("s1", "eu")
	for i := 0; i < 8; i++ {
		id := "p" + string(rune('0'+i))
		e.player(id, 1000+float64(i)*300) // 2100-point spread
		e.queue(id, "casual_ffa", "")
	}
	e.tick(19 * time.Second)
	if e.current("p0").Status != StatusSearching {
		t.Fatal("casual matched before 20 s")
	}
	e.tick(20 * time.Second)
	a := e.assignment("s1")
	if len(a.Teams) != 8 || a.Ranked || !a.FillWithBots {
		t.Fatalf("ffa assignment %+v", a)
	}
	for _, team := range a.Teams {
		if len(team) != 1 {
			t.Fatalf("ffa teams %v", teamIDs(a))
		}
	}

	// Bot fill: a lone player starts a match after 25 s.
	e2 := newEnv(t)
	e2.server("s1", "eu")
	e2.player("solo", 1500)
	e2.queue("solo", "casual_ffa", "")
	e2.tick(24 * time.Second)
	if e2.current("solo").Status != StatusSearching {
		t.Fatal("bot fill before 25 s")
	}
	e2.tick(25 * time.Second)
	a = e2.assignment("s1")
	humans := 0
	for _, team := range a.Teams {
		humans += len(team)
	}
	if humans != 1 || len(a.Teams) != 8 || !a.FillWithBots || a.Teams[0][0].PlayerID != "solo" {
		t.Fatalf("bot-filled assignment %+v", a)
	}

	// Ranked queues never bot-fill.
	e3 := newEnv(t)
	e3.server("s1", "eu")
	e3.player("solo", 1500)
	e3.queue("solo", "ranked_1v1", "")
	e3.tick(5 * time.Minute)
	if e3.current("solo").Status != StatusSearching {
		t.Fatal("ranked bot fill")
	}
}

func TestCasualTeamsBotFillWithParty(t *testing.T) {
	e := newEnv(t)
	e.server("s1", "eu")
	for _, id := range []string{"a", "b", "c"} {
		e.player(id, 1500)
	}
	p, _ := e.parties.Create("a", "A")
	_, _ = e.parties.Join("b", "B", p.Code)
	_, _ = e.parties.SetReady("b", true)
	e.queue("a", "casual_teams", "")
	e.queue("c", "casual_teams", "")
	e.tick(25 * time.Second)
	got := teamIDs(e.assignment("s1"))
	// The party stays together; the snake draft deals the second unit to the
	// other team, and bots fill the empty slots.
	if strings.Join(got, " | ") != "a+b | c" {
		t.Fatalf("teams %v", got)
	}
}

func TestRegionFallback(t *testing.T) {
	e := newEnv(t)
	e.server("us-1", "us")
	e.player("p_eu", 1500)
	e.player("p_us", 1500)
	e.queue("p_eu", "casual_ffa", "eu")
	e.tick(29 * time.Second) // bot fill allowed, but no eu server
	if v := e.current("p_eu"); v.Status != StatusSearching || v.Reason != ReasonNoServer {
		t.Fatalf("before fallback: %+v", v)
	}
	e.tick(30 * time.Second)
	if v := e.current("p_eu"); v.Status != StatusMatched || v.Match.Address != "10.1.0.1:7777" {
		t.Fatalf("after fallback: %+v", v)
	}

	// Tickets with different regions do not match each other until both
	// have waited 30 s.
	e2 := newEnv(t)
	e2.server("eu-1", "eu")
	e2.server("us-2", "us")
	e2.player("a", 1500)
	e2.player("b", 1500)
	e2.queue("a", "ranked_1v1", "eu")
	e2.queue("b", "ranked_1v1", "us")
	e2.tick(29 * time.Second)
	if e2.current("a").Status != StatusSearching {
		t.Fatal("cross-region match before fallback")
	}
	e2.tick(30 * time.Second)
	if e2.current("a").Status != StatusMatched {
		t.Fatal("no match after fallback")
	}
	if _, err := e2.mm.CreateTicket(context.Background(), "a", "ranked_1v1", "EU WEST"); apperr.CodeOf(err) != apperr.BadRequest {
		t.Fatalf("invalid region: %v", err)
	}
}

func TestModeAndArenaRotation(t *testing.T) {
	e := newEnv(t)
	var modes, arenas []string
	for i := 0; i < 5; i++ {
		id := "p" + string(rune('0'+i))
		e.player(id, 1500)
		e.server("s"+string(rune('0'+i)), "eu")
		start := time.Duration(i) * time.Minute
		e.at(start)
		e.queue(id, "casual_ffa", "")
		e.tick(start + 25*time.Second)
		v := e.current(id)
		if v.Status != StatusMatched {
			t.Fatalf("round %d not matched", i)
		}
		modes = append(modes, v.Match.Mode)
		arenas = append(arenas, v.Match.Arena)
	}
	if strings.Join(modes, ",") != "derby,takedown,ring_out,bounty,derby" {
		t.Fatalf("modes %v", modes)
	}
	// ring_out needs a wall-less arena; derby rotates between walled arenas.
	if arenas[2] != "sky_ring" || arenas[0] == arenas[4] || arenas[0] == "sky_ring" {
		t.Fatalf("arenas %v", arenas)
	}
}

func TestPartyChangeCancelsTicket(t *testing.T) {
	e := newEnv(t)
	e.player("a", 1500)
	e.player("b", 1500)
	e.player("x", 1500)
	p, _ := e.parties.Create("a", "A")
	_, _ = e.parties.Join("b", "B", p.Code)
	_, _ = e.parties.SetReady("b", true)
	tv := e.queue("a", "ranked_2v2", "")
	// Ready flag cleared → ticket cancelled.
	_, _ = e.parties.SetReady("b", false)
	e.mm.ValidateTickets()
	if v := e.current("b"); v.Status != StatusCancelled || v.Reason != ReasonPartyChanged {
		t.Fatalf("after unready: %+v", v)
	}
	_, _ = e.parties.SetReady("b", true)
	tv = e.queue("a", "ranked_2v2", "")
	// Member cancels.
	if _, err := e.mm.Cancel("x", tv.TicketID); apperr.CodeOf(err) != apperr.NotFound {
		t.Fatalf("stranger cancel: %v", err)
	}
	if v, err := e.mm.Cancel("b", tv.TicketID); err != nil || v.Status != StatusCancelled || v.Reason != ReasonCancelled {
		t.Fatalf("member cancel: %+v %v", v, err)
	}
	// Solo ticket is cancelled when its player joins a party.
	e.queue("x", "ranked_1v1", "")
	_, _ = e.parties.Join("x", "X", p.Code)
	e.tick(time.Second)
	if v := e.current("x"); v.Status != StatusCancelled || v.Reason != ReasonPartyChanged {
		t.Fatalf("solo ticket after joining party: %+v", v)
	}
}

func TestAbortCancelsTickets(t *testing.T) {
	e := newEnv(t)
	e.server("s1", "eu")
	e.player("a", 1500)
	e.player("b", 1500)
	e.queue("a", "ranked_1v1", "")
	e.queue("b", "ranked_1v1", "")
	e.tick(time.Second)
	m := e.current("a").Match
	if err := e.reg.Abort("s1", m.MatchID, "crashed"); err != nil {
		t.Fatal(err)
	}
	for _, pid := range []string{"a", "b"} {
		if v := e.current(pid); v.Status != StatusCancelled || v.Reason != servers.ReasonServerAborted || v.Match != nil {
			t.Fatalf("%s after abort: %+v", pid, v)
		}
	}
	e.queue("a", "ranked_1v1", "") // re-queue works
	if _, err := e.mm.CreateTicket(context.Background(), "a", "ranked_1v1", ""); apperr.CodeOf(err) != apperr.Conflict {
		t.Fatalf("double queue: %v", err)
	}
	if _, err := e.mm.CreateTicket(context.Background(), "b", "nope", ""); apperr.CodeOf(err) != apperr.BadRequest {
		t.Fatalf("unknown queue: %v", err)
	}
}

func TestWindow(t *testing.T) {
	ranked := Queue{Ranked: true}
	casual := Queue{}
	tests := []struct {
		q    Queue
		wait time.Duration
		want float64
	}{
		{ranked, 0, 150}, {ranked, 10 * time.Second, 350}, {ranked, 32500 * time.Millisecond, 800},
		{ranked, time.Hour, 800}, {casual, 19 * time.Second, 530},
	}
	for _, tc := range tests {
		if got := tc.q.Window(tc.wait); got != tc.want {
			t.Errorf("Window(%v, ranked=%v) = %v, want %v", tc.wait, tc.q.Ranked, got, tc.want)
		}
	}
	if w := casual.Window(20 * time.Second); w < 1e300 {
		t.Errorf("casual window after 20 s should be unlimited, got %v", w)
	}
}
