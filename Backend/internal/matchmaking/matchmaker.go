package matchmaking

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"io"
	"log/slog"
	"math"
	"regexp"
	"sort"
	"sync"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/party"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
	"github.com/comeheretosee/bigcar/backend/internal/servers"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
)

// Status is a ticket status.
type Status string

// Ticket statuses.
const (
	StatusSearching Status = "searching"
	StatusMatched   Status = "matched"
	StatusCancelled Status = "cancelled"
	StatusExpired   Status = "expired"
)

// Ticket reasons.
const (
	ReasonCancelled    = "cancelled"
	ReasonTimeout      = "timeout"
	ReasonPartyChanged = "party_changed"
	ReasonNoServer     = "no_server" // informational while searching
)

// Member is one player on a ticket, captured when the ticket is created.
type Member struct {
	PlayerID    string
	DisplayName string
	Rating      float64
	Loadout     profile.Loadout
}

// ticket is the internal ticket state.
type ticket struct {
	ID            string
	Queue         string
	Status        Status
	Reason        string
	CreatedAt     time.Time
	EndedAt       time.Time // when it left "searching"
	PartyID       string
	PartyRevision uint64
	Region        string
	Members       []Member
	Rating        float64 // average member rating

	// set when matched
	MatchID     string
	Address     string
	Mode        string
	Arena       string
	Teams       map[string]int
	Tokens      map[string]string
	matchActive bool
	matchEnded  time.Time
}

func (t *ticket) size() int { return len(t.Members) }

func (t *ticket) has(pid string) bool {
	for _, m := range t.Members {
		if m.PlayerID == pid {
			return true
		}
	}
	return false
}

// active reports whether the ticket blocks its players from queueing again.
func (t *ticket) active() bool {
	return t.Status == StatusSearching || (t.Status == StatusMatched && t.matchActive)
}

// MatchView is the per-player match info of a ticket.
type MatchView struct {
	MatchID   string `json:"matchId"`
	Address   string `json:"address"`
	JoinToken string `json:"joinToken"`
	Team      int    `json:"team"`
	Mode      string `json:"mode"`
	Arena     string `json:"arena"`
}

// TicketView is the ticket JSON as seen by one of its players.
type TicketView struct {
	TicketID         string     `json:"ticketId"`
	Queue            string     `json:"queue"`
	Status           Status     `json:"status"`
	CreatedAt        time.Time  `json:"createdAt"`
	WaitSec          int        `json:"waitSec"`
	EstimatedWaitSec int        `json:"estimatedWaitSec"`
	Reason           string     `json:"reason,omitempty"`
	Match            *MatchView `json:"match,omitempty"`
}

// Allocator reserves dedicated servers (implemented by *servers.Registry).
type Allocator interface {
	Allocate(req servers.AllocateRequest) (servers.Allocation, bool)
}

// Service is the matchmaker. All ticket state is guarded by mu. Lock order:
// Service.mu → party.Manager / servers.Registry (never the reverse).
type Service struct {
	mu        sync.Mutex
	queues    map[string]Queue
	queueIDs  []string
	tickets   map[string]*ticket
	byPlayer  map[string]string   // player → latest ticket id
	byMatch   map[string][]string // match → ticket ids
	modeRot   map[string]int      // queue → next mode index
	arenaRot  map[string]int      // mode → next arena index
	waitEst   map[string]float64  // queue → EMA of wait seconds at match time
	gd        *data.GameData
	st        storage.Store
	parties   *party.Manager
	alloc     Allocator
	now       func() time.Time
	rand      io.Reader
	log       *slog.Logger
	maxSearch int // DFS step budget per anchor
}

// Config bundles the matchmaker dependencies.
type Config struct {
	Data      *data.GameData
	Store     storage.Store
	Parties   *party.Manager
	Allocator Allocator
	Queues    []Queue // nil = DefaultQueues()
	Now       func() time.Time
	Rand      io.Reader
	Log       *slog.Logger
}

// New creates the matchmaker. It verifies that every queue mode exists in
// modes.json.
func New(cfg Config) (*Service, error) {
	if cfg.Queues == nil {
		cfg.Queues = DefaultQueues()
	}
	if cfg.Now == nil {
		cfg.Now = time.Now
	}
	if cfg.Rand == nil {
		cfg.Rand = rand.Reader
	}
	if cfg.Log == nil {
		cfg.Log = slog.Default()
	}
	s := &Service{
		queues: map[string]Queue{}, tickets: map[string]*ticket{}, byPlayer: map[string]string{},
		byMatch: map[string][]string{}, modeRot: map[string]int{}, arenaRot: map[string]int{}, waitEst: map[string]float64{},
		gd: cfg.Data, st: cfg.Store, parties: cfg.Parties, alloc: cfg.Allocator, now: cfg.Now, rand: cfg.Rand,
		log: cfg.Log, maxSearch: 20000,
	}
	for _, q := range cfg.Queues {
		for _, m := range q.Modes {
			if _, ok := cfg.Data.Mode(m); !ok {
				return nil, errors.New("matchmaking: queue " + q.ID + " uses unknown mode " + m)
			}
		}
		s.queues[q.ID] = q
		s.queueIDs = append(s.queueIDs, q.ID)
	}
	sort.Strings(s.queueIDs)
	return s, nil
}

// Queue returns a queue definition.
func (s *Service) Queue(id string) (Queue, bool) { q, ok := s.queues[id]; return q, ok }

func (s *Service) randomHex(n int) (string, error) {
	b := make([]byte, n)
	if _, err := io.ReadFull(s.rand, b); err != nil {
		return "", err
	}
	return hex.EncodeToString(b), nil
}

var regionPattern = regexp.MustCompile(`^[a-z0-9_-]{0,32}$`)

// CreateTicket queues the player (solo) or the player's party (leader only,
// all members ready, party size ≤ team size). region is optional: empty
// means any region.
func (s *Service) CreateTicket(ctx context.Context, playerID, queueID, region string) (TicketView, error) {
	q, ok := s.queues[queueID]
	if !ok {
		return TicketView{}, apperr.New(apperr.BadRequest, "unknown queue %q", queueID)
	}
	if !regionPattern.MatchString(region) {
		return TicketView{}, apperr.New(apperr.BadRequest, "invalid region")
	}
	pids := []string{playerID}
	pt, inParty := s.parties.Get(playerID)
	if inParty {
		if pt.LeaderID != playerID {
			return TicketView{}, apperr.New(apperr.NotLeader, "only the party leader can queue")
		}
		if len(pt.Members) > q.TeamSize {
			return TicketView{}, apperr.New(apperr.BadRequest, "party of %d is too large for %s (team size %d)",
				len(pt.Members), q.ID, q.TeamSize)
		}
		for _, m := range pt.Members {
			if !m.Ready && m.PlayerID != pt.LeaderID {
				return TicketView{}, apperr.New(apperr.NotReady, "%s is not ready", m.DisplayName)
			}
		}
		pids = pt.MemberIDs()
	}

	// Snapshot ratings and loadouts (outside the matchmaker lock).
	members := make([]Member, 0, len(pids))
	var sum float64
	for _, pid := range pids {
		p, err := storage.ReadProfile(ctx, s.st, pid)
		if err != nil {
			return TicketView{}, err
		}
		r := p.HiddenMMR()
		if q.Ranked {
			r = p.RankedState(s.gd, q.ID).Rating
		}
		sum += r
		members = append(members, Member{PlayerID: pid, DisplayName: p.DisplayName, Rating: r, Loadout: p.Loadout()})
	}
	id, err := s.randomHex(8)
	if err != nil {
		return TicketView{}, err
	}
	now := s.now()
	t := &ticket{
		ID: "t_" + id, Queue: q.ID, Status: StatusSearching, CreatedAt: now, Region: region,
		Members: members, Rating: sum / float64(len(members)),
	}

	s.mu.Lock()
	defer s.mu.Unlock()
	for _, pid := range pids {
		if old, ok := s.tickets[s.byPlayer[pid]]; ok && old.active() {
			if old.Status == StatusSearching {
				return TicketView{}, apperr.New(apperr.Conflict, "already searching (ticket %s)", old.ID)
			}
			return TicketView{}, apperr.New(apperr.Conflict, "already in a match")
		}
	}
	if inParty {
		// The party must not have changed since the snapshot above; any
		// later change is caught by validateLocked.
		rev, ok := s.parties.Revision(pt.ID)
		if !ok || rev != pt.Revision {
			return TicketView{}, apperr.New(apperr.Conflict, "party changed, please retry")
		}
		t.PartyID, t.PartyRevision = pt.ID, pt.Revision
	} else if s.parties.PartyOf(playerID) != "" {
		return TicketView{}, apperr.New(apperr.Conflict, "party changed, please retry")
	}
	s.tickets[t.ID] = t
	for _, pid := range pids {
		s.byPlayer[pid] = t.ID
	}
	s.log.Info("ticket created", "ticket", t.ID, "queue", q.ID, "players", len(pids), "rating", t.Rating, "region", region)
	return s.viewLocked(t, playerID, now), nil
}

// Cancel cancels a searching ticket (any member may cancel).
func (s *Service) Cancel(playerID, ticketID string) (TicketView, error) {
	s.mu.Lock()
	defer s.mu.Unlock()
	t, ok := s.tickets[ticketID]
	if !ok || !t.has(playerID) {
		return TicketView{}, apperr.New(apperr.NotFound, "ticket not found")
	}
	now := s.now()
	switch t.Status {
	case StatusSearching:
		s.endLocked(t, StatusCancelled, ReasonCancelled, now)
	case StatusCancelled:
	default:
		return TicketView{}, apperr.New(apperr.Conflict, "ticket is %s", t.Status)
	}
	return s.viewLocked(t, playerID, now), nil
}

// Current returns the player's latest ticket (nil if none). Finished tickets
// stay visible for a while so clients can read the final status.
func (s *Service) Current(playerID string) *TicketView {
	s.mu.Lock()
	defer s.mu.Unlock()
	t, ok := s.tickets[s.byPlayer[playerID]]
	if !ok {
		return nil
	}
	v := s.viewLocked(t, playerID, s.now())
	return &v
}

// PartyTicketID returns the active ticket of a party ("" if none).
func (s *Service) PartyTicketID(partyID string, leaderID string) string {
	s.mu.Lock()
	defer s.mu.Unlock()
	t, ok := s.tickets[s.byPlayer[leaderID]]
	if ok && t.PartyID == partyID && t.active() {
		return t.ID
	}
	return ""
}

func (s *Service) estimateLocked(queue string) int {
	est, ok := s.waitEst[queue]
	if !ok {
		est = initialWaitEstimate
	}
	return int(math.Round(est))
}

func (s *Service) viewLocked(t *ticket, playerID string, now time.Time) TicketView {
	end := now
	if t.Status != StatusSearching {
		end = t.EndedAt
	}
	v := TicketView{
		TicketID: t.ID, Queue: t.Queue, Status: t.Status, CreatedAt: t.CreatedAt.UTC(),
		WaitSec: int(end.Sub(t.CreatedAt).Seconds()), EstimatedWaitSec: s.estimateLocked(t.Queue), Reason: t.Reason,
	}
	if t.Status == StatusMatched {
		v.Match = &MatchView{
			MatchID: t.MatchID, Address: t.Address, JoinToken: t.Tokens[playerID], Team: t.Teams[playerID],
			Mode: t.Mode, Arena: t.Arena,
		}
	}
	return v
}

func (s *Service) endLocked(t *ticket, st Status, reason string, now time.Time) {
	t.Status, t.Reason, t.EndedAt = st, reason, now
	s.log.Info("ticket ended", "ticket", t.ID, "status", st, "reason", reason)
}

// MatchEnded implements servers.Listener: a finished match frees its players;
// an aborted match cancels its tickets with the given reason so players can
// queue again.
func (s *Service) MatchEnded(matchID string, aborted bool, reason string) {
	s.mu.Lock()
	defer s.mu.Unlock()
	now := s.now()
	for _, id := range s.byMatch[matchID] {
		t, ok := s.tickets[id]
		if !ok {
			continue
		}
		t.matchActive = false
		t.matchEnded = now
		if aborted {
			t.Status, t.Reason = StatusCancelled, reason
			t.MatchID, t.Address, t.Tokens, t.Teams = "", "", nil, nil
		}
	}
	delete(s.byMatch, matchID)
}

// ValidateTickets cancels searching tickets whose party changed (member
// joined/left/kicked, ready flag cleared) and solo tickets of players who
// joined a party. Called after party operations and on every tick.
func (s *Service) ValidateTickets() {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.validateLocked(s.now())
}

func (s *Service) validateLocked(now time.Time) {
	for _, t := range s.tickets {
		if t.Status != StatusSearching {
			continue
		}
		if t.PartyID != "" {
			if rev, ok := s.parties.Revision(t.PartyID); !ok || rev != t.PartyRevision {
				s.endLocked(t, StatusCancelled, ReasonPartyChanged, now)
			}
		} else if s.parties.PartyOf(t.Members[0].PlayerID) != "" {
			s.endLocked(t, StatusCancelled, ReasonPartyChanged, now)
		}
	}
}

// Tick runs one matchmaking pass at time now. It is deterministic given the
// ticket set, the servers and the random source, which makes it directly
// testable.
func (s *Service) Tick(now time.Time) {
	s.mu.Lock()
	defer s.mu.Unlock()
	s.validateLocked(now)
	for _, qid := range s.queueIDs {
		q := s.queues[qid]
		var pending []*ticket
		for _, t := range s.tickets {
			if t.Queue != qid || t.Status != StatusSearching {
				continue
			}
			if now.Sub(t.CreatedAt) >= TicketTTL {
				s.endLocked(t, StatusExpired, ReasonTimeout, now)
				continue
			}
			t.Reason = "" // re-set below if allocation fails again
			pending = append(pending, t)
		}
		s.matchQueueLocked(q, pending, now)
	}
	s.cleanupLocked(now)
}

func (s *Service) cleanupLocked(now time.Time) {
	for id, t := range s.tickets {
		var drop bool
		switch {
		case t.Status == StatusSearching:
		case t.Status == StatusMatched && t.matchActive:
			drop = now.Sub(t.EndedAt) > MaxMatchedTicketAge
		case t.Status == StatusMatched:
			drop = now.Sub(t.matchEnded) > TicketRetention
		default:
			ref := t.EndedAt
			if t.matchEnded.After(ref) {
				ref = t.matchEnded
			}
			drop = now.Sub(ref) > TicketRetention
		}
		if !drop {
			continue
		}
		delete(s.tickets, id)
		for _, m := range t.Members {
			if s.byPlayer[m.PlayerID] == id {
				delete(s.byPlayer, m.PlayerID)
			}
		}
	}
}

// --- grouping ---

// regionOf is the ticket's region constraint: "" (any) when it has none or
// has waited 30 s or more.
func regionOf(t *ticket, now time.Time) string {
	if now.Sub(t.CreatedAt) >= RegionFallbackAfter {
		return ""
	}
	return t.Region
}

// compatible reports whether a group can play together: every ticket accepts
// every other ticket's rating within its own window (mutual acceptance), and
// all region constraints agree. It returns the group's region ("" = any).
func compatible(q Queue, group []*ticket, now time.Time) (string, bool) {
	minR, maxR := math.Inf(1), math.Inf(-1)
	region := ""
	for _, t := range group {
		minR = math.Min(minR, t.Rating)
		maxR = math.Max(maxR, t.Rating)
		if r := regionOf(t, now); r != "" {
			if region != "" && region != r {
				return "", false
			}
			region = r
		}
	}
	for _, t := range group {
		w := q.Window(now.Sub(t.CreatedAt))
		if maxR-t.Rating > w || t.Rating-minR > w {
			return "", false
		}
	}
	return region, true
}

// buildTeams packs tickets (parties stay together) into q.Teams teams of at
// most q.TeamSize players: units are taken largest first, then by rating
// (highest first), and dealt in snake-draft order 0,1,…,T-1,T-1,…,0,0,1,…;
// if the snake's team has no room the unit goes to the team with room that
// has the fewest players (then the lowest rating sum). Returns false if the
// units cannot be packed.
func buildTeams(q Queue, group []*ticket) ([][]*ticket, bool) {
	units := append([]*ticket(nil), group...)
	sort.SliceStable(units, func(i, j int) bool {
		if units[i].size() != units[j].size() {
			return units[i].size() > units[j].size()
		}
		if units[i].Rating != units[j].Rating {
			return units[i].Rating > units[j].Rating
		}
		return units[i].ID < units[j].ID
	})
	teams := make([][]*ticket, q.Teams)
	count := make([]int, q.Teams)
	ratingSum := make([]float64, q.Teams)
	snake := func(k int) int {
		round, pos := k/q.Teams, k%q.Teams
		if round%2 == 1 {
			return q.Teams - 1 - pos
		}
		return pos
	}
	for k, u := range units {
		target := snake(k)
		if count[target]+u.size() > q.TeamSize {
			target = -1
			for i := 0; i < q.Teams; i++ {
				if count[i]+u.size() > q.TeamSize {
					continue
				}
				if target < 0 || count[i] < count[target] || (count[i] == count[target] && ratingSum[i] < ratingSum[target]) {
					target = i
				}
			}
			if target < 0 {
				return nil, false
			}
		}
		teams[target] = append(teams[target], u)
		count[target] += u.size()
		ratingSum[target] += u.Rating * float64(u.size())
	}
	return teams, true
}

// findFull searches (depth-first, closest ratings first) for a set of tickets
// including anchor that fills every slot of the queue.
func (s *Service) findFull(q Queue, anchor *ticket, cands []*ticket, now time.Time) []*ticket {
	need := q.Players()
	steps := 0
	group := []*ticket{anchor}
	var dfs func(start, total int) bool
	dfs = func(start, total int) bool {
		if total == need {
			_, ok := buildTeams(q, group)
			return ok
		}
		for i := start; i < len(cands); i++ {
			if steps++; steps > s.maxSearch {
				return false
			}
			c := cands[i]
			if total+c.size() > need {
				continue
			}
			group = append(group, c)
			if _, ok := compatible(q, group, now); ok && dfs(i+1, total+c.size()) {
				return true
			}
			group = group[:len(group)-1]
		}
		return false
	}
	if dfs(0, anchor.size()) {
		return group
	}
	return nil
}

// findPartial greedily builds the largest compatible, packable group around
// anchor (used for bot-filled casual matches).
func findPartial(q Queue, anchor *ticket, cands []*ticket, now time.Time) []*ticket {
	group := []*ticket{anchor}
	total := anchor.size()
	for _, c := range cands {
		if total+c.size() > q.Players() {
			continue
		}
		next := append(append([]*ticket(nil), group...), c)
		if _, ok := compatible(q, next, now); !ok {
			continue
		}
		if _, ok := buildTeams(q, next); !ok {
			continue
		}
		group, total = next, total+c.size()
	}
	return group
}

func (s *Service) matchQueueLocked(q Queue, pending []*ticket, now time.Time) {
	// Oldest tickets anchor first (fairness); ties by id for determinism.
	sort.Slice(pending, func(i, j int) bool {
		if !pending[i].CreatedAt.Equal(pending[j].CreatedAt) {
			return pending[i].CreatedAt.Before(pending[j].CreatedAt)
		}
		return pending[i].ID < pending[j].ID
	})
	used := map[*ticket]bool{}
	for _, anchor := range pending {
		if used[anchor] {
			continue
		}
		var cands []*ticket
		for _, t := range pending {
			if t != anchor && !used[t] {
				cands = append(cands, t)
			}
		}
		sort.SliceStable(cands, func(i, j int) bool {
			return math.Abs(cands[i].Rating-anchor.Rating) < math.Abs(cands[j].Rating-anchor.Rating)
		})
		group := s.findFull(q, anchor, cands, now)
		if group == nil && q.BotFill && now.Sub(anchor.CreatedAt) >= BotFillAfter {
			group = findPartial(q, anchor, cands, now)
		}
		if group == nil {
			continue
		}
		if s.createMatchLocked(q, anchor, group, now) {
			for _, t := range group {
				used[t] = true
			}
		}
	}
}

// --- match creation ---

// arenaCandidates lists, sorted, the arenas that suit a mode: never hubs;
// ring-out modes need an arena without walls, other modes one with walls;
// hill modes need a hill_zone prop; team modes need enough team spawns; there
// must be a spawn per player slot. Falls back to every non-hub arena.
func (s *Service) arenaCandidates(q Queue, modeID string) []string {
	mode, _ := s.gd.Mode(modeID)
	var strict, loose []string
	for i := range s.gd.Arenas {
		a := &s.gd.Arenas[i]
		if a.Hub {
			continue
		}
		loose = append(loose, a.ID)
		if mode.RingOut == a.Walls || len(a.Spawns) < q.Players() {
			continue
		}
		if mode.Hill && !a.HasProp("hill_zone") {
			continue
		}
		if mode.Teams >= 2 {
			ts := a.TeamSpawns()
			if ts[0] < q.TeamSize || ts[1] < q.TeamSize {
				continue
			}
		}
		strict = append(strict, a.ID)
	}
	list := strict
	if len(list) == 0 {
		list = loose
	}
	sort.Strings(list)
	return list
}

func (s *Service) createMatchLocked(q Queue, anchor *ticket, group []*ticket, now time.Time) bool {
	region, ok := compatible(q, group, now)
	if !ok {
		return false
	}
	teams, ok := buildTeams(q, group)
	if !ok {
		return false
	}
	mid, err := s.randomHex(8)
	if err != nil {
		s.log.Error("random source failed", "err", err)
		return false
	}
	matchID := "m_" + mid
	mode := q.Modes[s.modeRot[q.ID]%len(q.Modes)]

	a := servers.Assignment{
		MatchID: matchID, Queue: q.ID, Mode: mode, Ranked: q.Ranked, TeamSize: q.TeamSize,
		FillWithBots: q.BotFill, BotAI: DefaultBotAI, MaxWaitSec: AssignmentMaxWaitSec,
		Teams: make([][]servers.AssignmentPlayer, q.Teams),
	}
	teamOf := map[string]int{}
	tokens := map[string]string{}
	for ti, team := range teams {
		a.Teams[ti] = []servers.AssignmentPlayer{}
		for _, t := range team {
			for _, m := range t.Members {
				tok, err := s.randomHex(16)
				if err != nil {
					s.log.Error("random source failed", "err", err)
					return false
				}
				tokens[m.PlayerID] = tok
				teamOf[m.PlayerID] = ti
				a.Teams[ti] = append(a.Teams[ti], servers.AssignmentPlayer{
					PlayerID: m.PlayerID, DisplayName: m.DisplayName, JoinToken: tok, Rating: m.Rating, Loadout: m.Loadout,
				})
			}
		}
	}
	// Mode and arena rotate per match: both counters advance only after a
	// server was actually allocated.
	if arenas := s.arenaCandidates(q, mode); len(arenas) > 0 {
		a.Arena = arenas[s.arenaRot[mode]%len(arenas)]
	}
	req := servers.AllocateRequest{Preferred: anchor.Region, Assignment: a}
	if region != "" {
		req.Regions = []string{region}
	}
	alloc, ok := s.alloc.Allocate(req)
	if !ok {
		for _, t := range group {
			t.Reason = ReasonNoServer
		}
		return false
	}
	s.arenaRot[mode]++
	s.modeRot[q.ID]++
	ids := make([]string, 0, len(group))
	for _, t := range group {
		wait := now.Sub(t.CreatedAt).Seconds()
		if est, ok := s.waitEst[q.ID]; ok {
			s.waitEst[q.ID] = 0.8*est + 0.2*wait
		} else {
			s.waitEst[q.ID] = wait
		}
		t.Status, t.Reason, t.EndedAt = StatusMatched, "", now
		t.MatchID, t.Address, t.Mode, t.Arena = matchID, alloc.Address, mode, a.Arena
		t.Teams, t.Tokens, t.matchActive = map[string]int{}, map[string]string{}, true
		for _, m := range t.Members {
			t.Teams[m.PlayerID] = teamOf[m.PlayerID]
			t.Tokens[m.PlayerID] = tokens[m.PlayerID]
		}
		ids = append(ids, t.ID)
	}
	s.byMatch[matchID] = ids
	s.log.Info("match created", "match", matchID, "queue", q.ID, "mode", mode, "arena", a.Arena,
		"server", alloc.ServerID, "tickets", len(group))
	return true
}
