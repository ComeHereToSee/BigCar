// Package matchmaking implements queues, tickets and the matchmaker that
// groups tickets into matches and reserves dedicated servers for them.
package matchmaking

import (
	"math"
	"time"
)

// Queue is a matchmaking queue (docs/BACKEND_API.md "Matchmaking").
type Queue struct {
	ID       string
	TeamSize int
	// Teams is the number of teams; free-for-all queues have one team per
	// player slot (casual_ffa: 8 teams of 1).
	Teams   int
	Modes   []string
	BotFill bool
	Ranked  bool
}

// Players is the number of player slots.
func (q Queue) Players() int { return q.Teams * q.TeamSize }

// Matchmaker tuning from docs/BACKEND_API.md.
const (
	BaseWindow          = 150.0
	WindowPerSec        = 20.0
	RankedMaxWindow     = 800.0
	CasualUnlimitedWait = 20 * time.Second
	BotFillAfter        = 25 * time.Second
	RegionFallbackAfter = 30 * time.Second
	TicketTTL           = 10 * time.Minute
	// TicketRetention keeps finished tickets visible to GET /current.
	TicketRetention = 10 * time.Minute
	// MaxMatchedTicketAge forgets matched tickets whose match never reported.
	MaxMatchedTicketAge  = 3 * time.Hour
	AssignmentMaxWaitSec = 60
	DefaultBotAI         = "regular"
	initialWaitEstimate  = 30.0
)

// DefaultQueues returns the queue table.
func DefaultQueues() []Queue {
	return []Queue{
		{ID: "casual_ffa", TeamSize: 1, Teams: 8, Modes: []string{"derby", "takedown", "ring_out", "bounty"}, BotFill: true},
		{ID: "casual_teams", TeamSize: 3, Teams: 2, Modes: []string{"team_takedown", "king_of_hill", "team_elimination"}, BotFill: true},
		{ID: "ranked_1v1", TeamSize: 1, Teams: 2, Modes: []string{"team_elimination"}, Ranked: true},
		{ID: "ranked_2v2", TeamSize: 2, Teams: 2, Modes: []string{"team_elimination"}, Ranked: true},
		{ID: "ranked_3v3", TeamSize: 3, Teams: 2, Modes: []string{"team_elimination"}, Ranked: true},
	}
}

// RankedQueueIDs lists the ranked queues (ladders).
func RankedQueueIDs() []string {
	var out []string
	for _, q := range DefaultQueues() {
		if q.Ranked {
			out = append(out, q.ID)
		}
	}
	return out
}

// Window is the rating window of a ticket that has waited `wait`:
// 150 + 20 * waitSec, capped at 800 for ranked; unlimited for casual after 20 s.
func (q Queue) Window(wait time.Duration) float64 {
	if !q.Ranked && wait >= CasualUnlimitedWait {
		return math.Inf(1)
	}
	w := BaseWindow + WindowPerSec*wait.Seconds()
	if q.Ranked && w > RankedMaxWindow {
		w = RankedMaxWindow
	}
	return w
}
