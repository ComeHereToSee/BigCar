// Package servers tracks dedicated game servers and the matches assigned to
// them, and applies match results (rewards, Glicko-2 ratings, stats).
//
// Lifecycle of one server process: boot → heartbeat "idle" → the matchmaker
// reserves it and the next heartbeat response carries the Assignment → the
// server acknowledges by heartbeating "reserved"/"in_match" with the matchId →
// match-start → match-result (or match-abort) → process exits and the
// orchestrator starts a fresh one.
package servers

import (
	"log/slog"
	"net"
	"regexp"
	"sort"
	"strconv"
	"sync"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
)

// Server states reported by heartbeats.
const (
	StateIdle     = "idle"
	StateReserved = "reserved"
	StateInMatch  = "in_match"
)

// Match states.
const (
	MatchAssigned = "assigned"
	MatchStarted  = "started"
	MatchFinished = "finished"
	MatchAborted  = "aborted"
)

// Ticket cancellation reasons used when a match ends early.
const (
	ReasonServerAborted = "server_aborted"
	ReasonServerLost    = "server_lost"
)

// Timing defaults.
const (
	DefaultServerTimeout  = 20 * time.Second // silent servers are dropped
	DefaultAckTimeout     = 30 * time.Second // assignment must be acknowledged
	DefaultMatchRetention = 2 * time.Hour    // ended matches kept for duplicate detection
)

// Heartbeat is the body of POST /v1/servers/heartbeat.
type Heartbeat struct {
	ServerID string `json:"serverId"`
	Address  string `json:"address"`
	Port     int    `json:"port"`
	Region   string `json:"region"`
	Version  string `json:"version"`
	State    string `json:"state"`
	MatchID  string `json:"matchId"`
}

// AssignmentPlayer is one human player in an assignment.
type AssignmentPlayer struct {
	PlayerID    string          `json:"playerId"`
	DisplayName string          `json:"displayName"`
	JoinToken   string          `json:"joinToken"`
	Rating      float64         `json:"rating"`
	Loadout     profile.Loadout `json:"loadout"`
}

// Assignment tells a server which match to host. Teams has one entry per
// team (one per player slot in FFA); missing players are bot slots when
// FillWithBots is set.
type Assignment struct {
	MatchID      string               `json:"matchId"`
	Queue        string               `json:"queue"`
	Mode         string               `json:"mode"`
	Arena        string               `json:"arena"`
	Ranked       bool                 `json:"ranked"`
	TeamSize     int                  `json:"teamSize"`
	FillWithBots bool                 `json:"fillWithBots"`
	BotAI        string               `json:"botAi"`
	MaxWaitSec   int                  `json:"maxWaitSec"`
	Teams        [][]AssignmentPlayer `json:"teams"`
}

// Players returns every player with their team index.
func (a *Assignment) Players() map[string]int {
	out := map[string]int{}
	for ti, team := range a.Teams {
		for _, p := range team {
			out[p.PlayerID] = ti
		}
	}
	return out
}

// Server is a registered dedicated server.
type Server struct {
	ID       string
	Address  string
	Port     int
	Region   string
	Version  string
	State    string // as last reported (or "reserved" once the matchmaker picked it)
	MatchID  string // match the backend assigned and that has not ended
	LastSeen time.Time
}

// Endpoint is "host:port".
func (s *Server) Endpoint() string { return net.JoinHostPort(s.Address, strconv.Itoa(s.Port)) }

// Match is a match assigned to a server.
type Match struct {
	Assignment Assignment
	ServerID   string
	Address    string
	State      string
	CreatedAt  time.Time
	StartedAt  time.Time
	EndedAt    time.Time
	// Acked is set once the server reported the match id back.
	Acked bool

	resultInFlight bool
}

// Listener is notified when a match ends. It is never called with the
// registry lock held.
type Listener interface {
	MatchEnded(matchID string, aborted bool, reason string)
}

// Registry is the in-memory server and match registry.
type Registry struct {
	mu       sync.Mutex
	servers  map[string]*Server
	matches  map[string]*Match
	listener Listener
	now      func() time.Time
	log      *slog.Logger

	ServerTimeout  time.Duration
	AckTimeout     time.Duration
	MatchRetention time.Duration
}

// NewRegistry creates a registry.
func NewRegistry(now func() time.Time, log *slog.Logger) *Registry {
	if now == nil {
		now = time.Now
	}
	if log == nil {
		log = slog.Default()
	}
	return &Registry{
		servers: map[string]*Server{}, matches: map[string]*Match{}, now: now, log: log,
		ServerTimeout: DefaultServerTimeout, AckTimeout: DefaultAckTimeout, MatchRetention: DefaultMatchRetention,
	}
}

// SetListener sets the match-end listener (the matchmaker).
func (r *Registry) SetListener(l Listener) {
	r.mu.Lock()
	defer r.mu.Unlock()
	r.listener = l
}

type endedMatch struct {
	id      string
	aborted bool
	reason  string
}

func (r *Registry) notify(ended []endedMatch) {
	r.mu.Lock()
	l := r.listener
	r.mu.Unlock()
	if l == nil {
		return
	}
	for _, e := range ended {
		l.MatchEnded(e.id, e.aborted, e.reason)
	}
}

var idPattern = regexp.MustCompile(`^[A-Za-z0-9_.:-]{1,64}$`)

func validateHeartbeat(hb Heartbeat) error {
	switch {
	case !idPattern.MatchString(hb.ServerID):
		return apperr.New(apperr.BadRequest, "serverId must be 1-64 characters of A-Z a-z 0-9 _ . : -")
	case hb.Address == "" || len(hb.Address) > 255:
		return apperr.New(apperr.BadRequest, "address is required")
	case hb.Port < 1 || hb.Port > 65535:
		return apperr.New(apperr.BadRequest, "port must be 1-65535")
	case len(hb.Region) > 32 || len(hb.Version) > 64:
		return apperr.New(apperr.BadRequest, "region/version too long")
	case hb.State != StateIdle && hb.State != StateReserved && hb.State != StateInMatch:
		return apperr.New(apperr.BadRequest, "state must be idle, reserved or in_match")
	}
	return nil
}

func (m *Match) active() bool { return m.State == MatchAssigned || m.State == MatchStarted }

// endLocked marks a match ended and frees its server.
func (r *Registry) endLocked(m *Match, state, reason string, now time.Time) endedMatch {
	m.State = state
	m.EndedAt = now
	if s, ok := r.servers[m.ServerID]; ok && s.MatchID == m.Assignment.MatchID {
		s.MatchID = ""
	}
	return endedMatch{id: m.Assignment.MatchID, aborted: state == MatchAborted, reason: reason}
}

// Heartbeat registers/refreshes a server and returns its assignment, if any.
//
// Reconciliation with the server's reported state:
//   - backend has an active match for the server:
//   - server reports the match id → acknowledged; the assignment is
//     returned again (idempotent) until the match ends;
//   - server reports idle before acknowledging → assignment still in
//     flight, returned again;
//   - server reports idle (or another match) after acknowledging → it lost
//     the match (crash/restart): the match is aborted with "server_lost";
//   - no active match: the server's reported state is taken as is; only
//     idle servers are eligible for new matches.
func (r *Registry) Heartbeat(hb Heartbeat) (*Assignment, error) {
	if err := validateHeartbeat(hb); err != nil {
		return nil, err
	}
	now := r.now()
	var ended []endedMatch
	defer func() { r.notify(ended) }()

	r.mu.Lock()
	defer r.mu.Unlock()
	s, ok := r.servers[hb.ServerID]
	if !ok {
		s = &Server{ID: hb.ServerID}
		r.servers[hb.ServerID] = s
		r.log.Info("server registered", "server", hb.ServerID, "region", hb.Region, "address", hb.Address, "port", hb.Port)
	}
	s.Address, s.Port, s.Region, s.Version, s.LastSeen = hb.Address, hb.Port, hb.Region, hb.Version, now

	if m, ok := r.matches[s.MatchID]; ok && m.active() {
		if hb.State != StateIdle && hb.MatchID == m.Assignment.MatchID {
			m.Acked = true
			s.State = hb.State
			a := m.Assignment
			return &a, nil
		}
		if !m.Acked && hb.State == StateIdle {
			s.State = StateReserved
			a := m.Assignment
			return &a, nil
		}
		r.log.Warn("server lost its match", "server", s.ID, "match", m.Assignment.MatchID, "reported", hb.State, "reportedMatch", hb.MatchID)
		ended = append(ended, r.endLocked(m, MatchAborted, ReasonServerLost, now))
	}
	s.MatchID = ""
	s.State = hb.State
	return nil, nil
}

// AllocateRequest asks for an idle server.
type AllocateRequest struct {
	// Regions restricts the server region; empty means any region.
	Regions []string
	// Preferred is tried first when Regions is empty.
	Preferred  string
	Assignment Assignment
}

// Allocation is the reserved server.
type Allocation struct {
	ServerID string
	Address  string // host:port for clients
}

// Allocate reserves an idle server for the assignment and records the match.
// Servers are chosen deterministically (preferred region first, then by id).
// It never calls the listener.
func (r *Registry) Allocate(req AllocateRequest) (Allocation, bool) {
	now := r.now()
	r.mu.Lock()
	defer r.mu.Unlock()
	allowed := map[string]bool{}
	for _, reg := range req.Regions {
		allowed[reg] = true
	}
	var cands []*Server
	for _, s := range r.servers {
		if s.State != StateIdle || s.MatchID != "" || now.Sub(s.LastSeen) > r.ServerTimeout {
			continue
		}
		if len(allowed) > 0 && !allowed[s.Region] {
			continue
		}
		cands = append(cands, s)
	}
	if len(cands) == 0 {
		return Allocation{}, false
	}
	sort.Slice(cands, func(i, j int) bool {
		pi, pj := cands[i].Region == req.Preferred, cands[j].Region == req.Preferred
		if pi != pj {
			return pi
		}
		return cands[i].ID < cands[j].ID
	})
	s := cands[0]
	s.State = StateReserved
	s.MatchID = req.Assignment.MatchID
	r.matches[req.Assignment.MatchID] = &Match{
		Assignment: req.Assignment, ServerID: s.ID, Address: s.Endpoint(), State: MatchAssigned, CreatedAt: now,
	}
	r.log.Info("match assigned", "match", req.Assignment.MatchID, "server", s.ID, "queue", req.Assignment.Queue,
		"mode", req.Assignment.Mode, "arena", req.Assignment.Arena)
	return Allocation{ServerID: s.ID, Address: s.Endpoint()}, true
}

// authorize finds a match and checks that serverID is the assigned server.
func (r *Registry) authorize(serverID, matchID string) (*Match, error) {
	if serverID == "" {
		return nil, apperr.New(apperr.BadRequest, "serverId is required (body field or X-Server-Id header)")
	}
	m, ok := r.matches[matchID]
	if !ok {
		return nil, apperr.New(apperr.NotFound, "unknown match %q", matchID)
	}
	if m.ServerID != serverID {
		return nil, apperr.New(apperr.Forbidden, "match %q is not assigned to server %q", matchID, serverID)
	}
	return m, nil
}

// MatchStart records that the match began.
func (r *Registry) MatchStart(serverID, matchID string) error {
	r.mu.Lock()
	defer r.mu.Unlock()
	m, err := r.authorize(serverID, matchID)
	if err != nil {
		return err
	}
	switch m.State {
	case MatchStarted:
		return nil
	case MatchAssigned:
		m.State, m.StartedAt, m.Acked = MatchStarted, r.now(), true
		if s, ok := r.servers[serverID]; ok {
			s.State = StateInMatch
		}
		return nil
	default:
		return apperr.New(apperr.Conflict, "match %q already ended", matchID)
	}
}

// Abort ends a match early; its tickets are cancelled with "server_aborted".
func (r *Registry) Abort(serverID, matchID, reason string) error {
	var ended []endedMatch
	defer func() { r.notify(ended) }()
	r.mu.Lock()
	defer r.mu.Unlock()
	m, err := r.authorize(serverID, matchID)
	if err != nil {
		return err
	}
	switch {
	case m.State == MatchAborted:
		return nil
	case !m.active() || m.resultInFlight:
		return apperr.New(apperr.Conflict, "match %q already reported", matchID)
	}
	r.log.Warn("match aborted by server", "match", matchID, "server", serverID, "reason", reason)
	ended = append(ended, r.endLocked(m, MatchAborted, ReasonServerAborted, r.now()))
	return nil
}

// beginResult claims the right to report a match's result (once).
func (r *Registry) beginResult(serverID, matchID string) (Match, error) {
	r.mu.Lock()
	defer r.mu.Unlock()
	m, err := r.authorize(serverID, matchID)
	if err != nil {
		return Match{}, err
	}
	switch {
	case m.State == MatchFinished || m.resultInFlight:
		return Match{}, apperr.New(apperr.Conflict, "result for match %q was already reported", matchID)
	case m.State == MatchAborted:
		return Match{}, apperr.New(apperr.Conflict, "match %q was aborted", matchID)
	}
	m.resultInFlight = true
	return *m, nil
}

// endResult finishes (ok) or releases (!ok) a result claimed by beginResult.
func (r *Registry) endResult(matchID string, ok bool) {
	var ended []endedMatch
	defer func() { r.notify(ended) }()
	r.mu.Lock()
	defer r.mu.Unlock()
	m, found := r.matches[matchID]
	if !found {
		return
	}
	m.resultInFlight = false
	if ok {
		ended = append(ended, r.endLocked(m, MatchFinished, "", r.now()))
	}
}

// Sweep drops servers that stopped heartbeating, aborts matches whose server
// is gone or never acknowledged the assignment, and forgets old matches.
func (r *Registry) Sweep(now time.Time) {
	var ended []endedMatch
	defer func() { r.notify(ended) }()
	r.mu.Lock()
	defer r.mu.Unlock()
	for id, s := range r.servers {
		if now.Sub(s.LastSeen) <= r.ServerTimeout {
			continue
		}
		delete(r.servers, id)
		r.log.Warn("server timed out", "server", id, "lastSeen", s.LastSeen)
		if m, ok := r.matches[s.MatchID]; ok && m.active() && !m.resultInFlight {
			ended = append(ended, r.endLocked(m, MatchAborted, ReasonServerLost, now))
		}
	}
	for id, m := range r.matches {
		if m.State == MatchAssigned && !m.Acked && now.Sub(m.CreatedAt) > r.AckTimeout {
			r.log.Warn("assignment not acknowledged", "match", id, "server", m.ServerID)
			ended = append(ended, r.endLocked(m, MatchAborted, ReasonServerLost, now))
			if s, ok := r.servers[m.ServerID]; ok && s.State == StateReserved {
				s.State = StateIdle
			}
		}
		if !m.active() && now.Sub(m.EndedAt) > r.MatchRetention {
			delete(r.matches, id)
		}
	}
}

// ServerSnapshot returns a copy of a server (for tests and diagnostics).
func (r *Registry) ServerSnapshot(id string) (Server, bool) {
	r.mu.Lock()
	defer r.mu.Unlock()
	s, ok := r.servers[id]
	if !ok {
		return Server{}, false
	}
	return *s, true
}

// MatchSnapshot returns a copy of a match.
func (r *Registry) MatchSnapshot(id string) (Match, bool) {
	r.mu.Lock()
	defer r.mu.Unlock()
	m, ok := r.matches[id]
	if !ok {
		return Match{}, false
	}
	return *m, true
}
