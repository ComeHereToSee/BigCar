// Package party manages pre-match parties (groups of up to three players that
// queue together). Parties are ephemeral and live in memory.
//
// Every change to a party's membership or ready flags bumps its Revision; the
// matchmaker records the revision when the leader queues and cancels the
// ticket if the party changes afterwards.
package party

import (
	"crypto/rand"
	"encoding/hex"
	"io"
	"strings"
	"sync"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
)

// MaxMembers is the largest team size.
const MaxMembers = 3

// CodeAlphabet excludes the look-alike characters 0, O, 1 and I.
const CodeAlphabet = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"

// CodeLength is the length of a join code.
const CodeLength = 6

// Member is one party member.
type Member struct {
	PlayerID    string `json:"playerId"`
	DisplayName string `json:"displayName"`
	Ready       bool   `json:"ready"`
}

// Party is a snapshot of a party.
type Party struct {
	ID       string
	Code     string
	LeaderID string
	Members  []Member
	Revision uint64
}

// MemberIDs lists the member player ids in join order.
func (p Party) MemberIDs() []string {
	ids := make([]string, len(p.Members))
	for i, m := range p.Members {
		ids[i] = m.PlayerID
	}
	return ids
}

// Manager holds all parties.
type Manager struct {
	mu       sync.Mutex
	parties  map[string]*Party // party id → party
	byCode   map[string]string // code → party id
	byPlayer map[string]string // player id → party id
	rand     io.Reader
}

// NewManager creates an empty manager. rnd may be nil (crypto/rand).
func NewManager(rnd io.Reader) *Manager {
	if rnd == nil {
		rnd = rand.Reader
	}
	return &Manager{
		parties: map[string]*Party{}, byCode: map[string]string{}, byPlayer: map[string]string{}, rand: rnd,
	}
}

func (p *Party) snapshot() Party {
	c := *p
	c.Members = append([]Member(nil), p.Members...)
	return c
}

func (p *Party) index(playerID string) int {
	for i, m := range p.Members {
		if m.PlayerID == playerID {
			return i
		}
	}
	return -1
}

// Get returns the player's party.
func (m *Manager) Get(playerID string) (Party, bool) {
	m.mu.Lock()
	defer m.mu.Unlock()
	p, ok := m.parties[m.byPlayer[playerID]]
	if !ok {
		return Party{}, false
	}
	return p.snapshot(), true
}

// Revision returns the current revision of a party.
func (m *Manager) Revision(partyID string) (uint64, bool) {
	m.mu.Lock()
	defer m.mu.Unlock()
	p, ok := m.parties[partyID]
	if !ok {
		return 0, false
	}
	return p.Revision, true
}

// PartyOf returns the id of the player's party ("" if none).
func (m *Manager) PartyOf(playerID string) string {
	m.mu.Lock()
	defer m.mu.Unlock()
	return m.byPlayer[playerID]
}

func (m *Manager) randomString(alphabet string, n int) (string, error) {
	b := make([]byte, n)
	if _, err := io.ReadFull(m.rand, b); err != nil {
		return "", err
	}
	out := make([]byte, n)
	for i, x := range b {
		out[i] = alphabet[int(x)%len(alphabet)] // 256 % 32 == 0: unbiased
	}
	return string(out), nil
}

func (m *Manager) newIDs() (id, code string, err error) {
	b := make([]byte, 8)
	if _, err := io.ReadFull(m.rand, b); err != nil {
		return "", "", err
	}
	id = "pt_" + hex.EncodeToString(b)
	for i := 0; i < 100; i++ {
		code, err = m.randomString(CodeAlphabet, CodeLength)
		if err != nil {
			return "", "", err
		}
		if _, taken := m.byCode[code]; !taken {
			return id, code, nil
		}
	}
	return "", "", apperr.New(apperr.Internal, "could not allocate a party code")
}

// leaveLocked removes the player from their party, promoting the next member
// if the leader leaves and deleting the party when it becomes empty.
func (m *Manager) leaveLocked(playerID string) {
	pid, ok := m.byPlayer[playerID]
	if !ok {
		return
	}
	delete(m.byPlayer, playerID)
	p := m.parties[pid]
	if i := p.index(playerID); i >= 0 {
		p.Members = append(p.Members[:i], p.Members[i+1:]...)
	}
	p.Revision++
	if len(p.Members) == 0 {
		delete(m.parties, pid)
		delete(m.byCode, p.Code)
		return
	}
	if p.LeaderID == playerID {
		p.LeaderID = p.Members[0].PlayerID
		p.Members[0].Ready = true // queueing is the leader's ready signal
	}
}

// Create creates a party led by the player, leaving any current party.
func (m *Manager) Create(playerID, displayName string) (Party, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	id, code, err := m.newIDs()
	if err != nil {
		return Party{}, err
	}
	m.leaveLocked(playerID)
	p := &Party{
		ID: id, Code: code, LeaderID: playerID, Revision: 1,
		Members: []Member{{PlayerID: playerID, DisplayName: displayName, Ready: true}},
	}
	m.parties[id] = p
	m.byCode[code] = id
	m.byPlayer[playerID] = id
	return p.snapshot(), nil
}

// NormalizeCode upper-cases and trims a join code.
func NormalizeCode(code string) string { return strings.ToUpper(strings.TrimSpace(code)) }

// Join adds the player to the party with the given code, leaving any other
// party first. New members start not ready.
func (m *Manager) Join(playerID, displayName, code string) (Party, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	code = NormalizeCode(code)
	pid, ok := m.byCode[code]
	if !ok {
		return Party{}, apperr.New(apperr.NotFound, "no party with code %q", code)
	}
	p := m.parties[pid]
	if p.index(playerID) >= 0 {
		return p.snapshot(), nil
	}
	if len(p.Members) >= MaxMembers {
		return Party{}, apperr.New(apperr.PartyFull, "party is full")
	}
	m.leaveLocked(playerID)
	p.Members = append(p.Members, Member{PlayerID: playerID, DisplayName: displayName})
	p.Revision++
	m.byPlayer[playerID] = pid
	return p.snapshot(), nil
}

// Leave removes the player from their party (a no-op when not in one).
func (m *Manager) Leave(playerID string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	m.leaveLocked(playerID)
}

// SetReady sets the player's ready flag.
func (m *Manager) SetReady(playerID string, ready bool) (Party, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	p, ok := m.parties[m.byPlayer[playerID]]
	if !ok {
		return Party{}, apperr.New(apperr.NotFound, "not in a party")
	}
	i := p.index(playerID)
	if p.Members[i].Ready != ready {
		p.Members[i].Ready = ready
		p.Revision++
	}
	return p.snapshot(), nil
}

// Kick removes a member (leader only).
func (m *Manager) Kick(leaderID, targetID string) (Party, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	p, ok := m.parties[m.byPlayer[leaderID]]
	if !ok {
		return Party{}, apperr.New(apperr.NotFound, "not in a party")
	}
	if p.LeaderID != leaderID {
		return Party{}, apperr.New(apperr.NotLeader, "only the party leader can kick")
	}
	if targetID == leaderID {
		return Party{}, apperr.New(apperr.BadRequest, "use leave to leave your own party")
	}
	if p.index(targetID) < 0 {
		return Party{}, apperr.New(apperr.NotFound, "player is not in your party")
	}
	m.leaveLocked(targetID)
	return p.snapshot(), nil
}

// SetDisplayName refreshes a member's display name (after a rename).
func (m *Manager) SetDisplayName(playerID, name string) {
	m.mu.Lock()
	defer m.mu.Unlock()
	if p, ok := m.parties[m.byPlayer[playerID]]; ok {
		if i := p.index(playerID); i >= 0 {
			p.Members[i].DisplayName = name
		}
	}
}
