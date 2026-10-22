// Package storage persists accounts, profiles, orders, idempotency records
// and match results.
//
// All reads and writes happen inside Store.InTx. Implementations guarantee
// that a transaction that loads a profile with forUpdate=true holds an
// exclusive lock on it until commit, so read-modify-write sequences on a
// profile (purchases, rewards) are serialized and cannot double-spend:
//
//   - Memory: one mutex serializes every transaction (used by tests and for
//     local development without a database).
//   - Postgres: SELECT ... FOR UPDATE row locks plus a version column that is
//     checked on every update (optimistic concurrency as a second line of
//     defence); serialization failures and deadlocks are retried.
//
// Transaction functions may be re-run on retry, so they must not have side
// effects outside the transaction.
package storage

import (
	"context"
	"errors"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/profile"
)

// Sentinel errors.
var (
	ErrNotFound = errors.New("storage: not found")
	ErrConflict = errors.New("storage: conflict")
)

// Account is a login identity. A guest account has a DeviceID; linking or
// registering sets Username/PasswordHash.
type Account struct {
	PlayerID     string
	DeviceID     string
	Username     string
	PasswordHash string
	CreatedAt    time.Time
}

// Order statuses.
const (
	OrderPending   = "pending"
	OrderCompleted = "completed"
	OrderFailed    = "failed"
)

// Order is a real-money gold purchase.
type Order struct {
	ID          string
	PlayerID    string
	Provider    string
	PackID      string
	Gold        int64 // pack gold + bonus, fixed at checkout time
	Status      string
	ExternalID  string
	RedirectURL string
	CreatedAt   time.Time
	UpdatedAt   time.Time
}

// IdempotencyRecord stores the first response to an Idempotency-Key.
type IdempotencyRecord struct {
	PlayerID    string
	Key         string
	RequestHash string
	Status      int
	Body        []byte
	CreatedAt   time.Time
}

// LadderRow is one player's standing in one ranked queue (derived from the
// profile's "ranked" entry whenever the profile is saved).
type LadderRow struct {
	Queue          string
	PlayerID       string
	Season         string
	DisplayName    string
	Rating         float64
	RD             float64
	Wins           int
	Losses         int
	Tier           string
	Division       int
	PlacementsLeft int
}

// MatchRecord is the audit record of a processed match result. Its primary
// key guarantees a result is applied at most once.
type MatchRecord struct {
	MatchID   string
	ServerID  string
	Queue     string
	Result    []byte
	Rewards   []byte
	CreatedAt time.Time
}

// Tx is a storage transaction.
type Tx interface {
	CreateAccount(ctx context.Context, a Account) error // ErrConflict on duplicate id/device/username
	AccountByID(ctx context.Context, playerID string) (Account, error)
	AccountByDevice(ctx context.Context, deviceID string) (Account, error)
	AccountByUsername(ctx context.Context, username string) (Account, error)
	UpdateAccount(ctx context.Context, a Account) error // ErrConflict on duplicate username

	// GetProfile loads a profile; forUpdate locks it until the transaction ends.
	GetProfile(ctx context.Context, playerID string, forUpdate bool) (*profile.Profile, error)
	// PutProfile inserts or updates a profile (and its ladder rows).
	PutProfile(ctx context.Context, p *profile.Profile) error

	CreateOrder(ctx context.Context, o Order) error // ErrConflict if the id exists
	GetOrder(ctx context.Context, orderID string, forUpdate bool) (Order, error)
	UpdateOrder(ctx context.Context, o Order) error

	GetIdempotency(ctx context.Context, playerID, key string) (IdempotencyRecord, bool, error)
	PutIdempotency(ctx context.Context, r IdempotencyRecord) error

	InsertMatchResult(ctx context.Context, m MatchRecord) error // ErrConflict if already recorded

	// LadderRank returns the 1-based position a player with the given rating
	// would have in the queue's ladder for season (placement players are not
	// on the ladder; ties are ordered by player id; playerID itself is
	// excluded from the count).
	LadderRank(ctx context.Context, queue, season string, rating float64, playerID string) (int, error)
}

// Store is a storage backend.
type Store interface {
	InTx(ctx context.Context, fn func(tx Tx) error) error
	// Ladder returns a page of the queue's ladder (placement players
	// excluded), ordered by rating desc then player id, and the total count.
	Ladder(ctx context.Context, queue, season string, offset, limit int) ([]LadderRow, int, error)
	// LadderEntry returns a player's row and 1-based rank (rank 0 while the
	// player is in placements). found is false when the player has no row.
	LadderEntry(ctx context.Context, queue, season, playerID string) (row LadderRow, rank int, found bool, err error)
	// PurgeIdempotency deletes idempotency records created before t.
	PurgeIdempotency(ctx context.Context, before time.Time) (int64, error)
	Close()
}

// ladderRows derives the ladder rows of a profile.
func ladderRows(p *profile.Profile) []LadderRow {
	rows := make([]LadderRow, 0, len(p.Ranked))
	for q, r := range p.Ranked {
		rows = append(rows, LadderRow{
			Queue: q, PlayerID: p.PlayerID, Season: p.SeasonPass.Season, DisplayName: p.DisplayName,
			Rating: r.Rating, RD: r.RD, Wins: r.Wins, Losses: r.Losses, Tier: r.Tier, Division: r.Division,
			PlacementsLeft: r.PlacementsLeft,
		})
	}
	return rows
}

// ReadProfile loads a profile without locking it.
func ReadProfile(ctx context.Context, st Store, playerID string) (*profile.Profile, error) {
	var p *profile.Profile
	err := st.InTx(ctx, func(tx Tx) error {
		var err error
		p, err = tx.GetProfile(ctx, playerID, false)
		return err
	})
	return p, err
}
