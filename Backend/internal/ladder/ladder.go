// Package ladder serves the ranked leaderboards and the current season.
package ladder

import (
	"context"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
)

// Paging limits.
const (
	DefaultLimit = 50
	MaxLimit     = 100
)

// Entry is one ladder line.
type Entry struct {
	Rank        int     `json:"rank"`
	PlayerID    string  `json:"playerId"`
	DisplayName string  `json:"displayName"`
	Rating      float64 `json:"rating"`
	Tier        string  `json:"tier"`
	Division    int     `json:"division"`
	Wins        int     `json:"wins"`
	Losses      int     `json:"losses"`
}

// Page is the GET /v1/ladder/{queue} response.
type Page struct {
	Queue   string  `json:"queue"`
	Season  string  `json:"season"`
	Total   int     `json:"total"`
	Entries []Entry `json:"entries"`
}

// Me is the GET /v1/ladder/{queue}/me response.
type Me struct {
	Entry          *Entry `json:"entry"`
	PlacementsLeft int    `json:"placementsLeft"`
}

// Season is the GET /v1/seasons/current response.
type Season struct {
	ID    string    `json:"id"`
	Name  string    `json:"name"`
	Start time.Time `json:"start"`
	End   time.Time `json:"end"`
}

// Service implements the ladder endpoints.
type Service struct {
	st     storage.Store
	gd     *data.GameData
	queues map[string]bool
}

// NewService creates the ladder service for the given ranked queues.
func NewService(st storage.Store, gd *data.GameData, rankedQueues []string) *Service {
	q := map[string]bool{}
	for _, id := range rankedQueues {
		q[id] = true
	}
	return &Service{st: st, gd: gd, queues: q}
}

// CurrentSeason is the season defined by store.json's seasonPass.
func (s *Service) CurrentSeason() Season {
	sp := s.gd.Store.SeasonPass
	return Season{ID: sp.ID, Name: sp.Name, Start: sp.Start, End: sp.End}
}

func (s *Service) checkQueue(queue string) error {
	if !s.queues[queue] {
		return apperr.New(apperr.NotFound, "no ladder for queue %q", queue)
	}
	return nil
}

// entry converts a row. Tier and division are recomputed from the rating and
// the current rank, so the crash_king top-100 rule always reflects the
// current standings (the tier stored in a profile is as of its last match).
func (s *Service) entry(r storage.LadderRow, rank int) Entry {
	tier, div := s.gd.RankFor(r.Rating, rank)
	return Entry{
		Rank: rank, PlayerID: r.PlayerID, DisplayName: r.DisplayName, Rating: r.Rating,
		Tier: tier, Division: div, Wins: r.Wins, Losses: r.Losses,
	}
}

// Page returns a page of a queue's ladder (placement players excluded).
func (s *Service) Page(ctx context.Context, queue string, offset, limit int) (Page, error) {
	if err := s.checkQueue(queue); err != nil {
		return Page{}, err
	}
	if offset < 0 || limit < 1 || limit > MaxLimit {
		return Page{}, apperr.New(apperr.BadRequest, "offset must be >= 0 and limit 1-%d", MaxLimit)
	}
	season := s.gd.Store.SeasonPass.ID
	rows, total, err := s.st.Ladder(ctx, queue, season, offset, limit)
	if err != nil {
		return Page{}, err
	}
	p := Page{Queue: queue, Season: season, Total: total, Entries: make([]Entry, 0, len(rows))}
	for i, r := range rows {
		p.Entries = append(p.Entries, s.entry(r, offset+i+1))
	}
	return p, nil
}

// Me returns the player's own ladder entry (null while in placements).
func (s *Service) Me(ctx context.Context, playerID, queue string) (Me, error) {
	if err := s.checkQueue(queue); err != nil {
		return Me{}, err
	}
	row, rank, found, err := s.st.LadderEntry(ctx, queue, s.gd.Store.SeasonPass.ID, playerID)
	if err != nil {
		return Me{}, err
	}
	if !found {
		return Me{PlacementsLeft: profile.PlacementMatches}, nil
	}
	if rank == 0 {
		return Me{PlacementsLeft: row.PlacementsLeft}, nil
	}
	e := s.entry(row, rank)
	return Me{Entry: &e, PlacementsLeft: 0}, nil
}
