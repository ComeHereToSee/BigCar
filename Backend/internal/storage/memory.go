package storage

import (
	"context"
	"encoding/json"
	"sort"
	"sync"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/profile"
)

// Memory is an in-memory Store. A single mutex serializes all transactions,
// which trivially provides the locking guarantees of Tx. Writes are applied
// in place and undone on error (undo log), so a failed transaction leaves no
// trace. Profiles are kept as JSON so callers never share memory with the store.
type Memory struct {
	mu         sync.Mutex
	accounts   map[string]Account
	byDevice   map[string]string
	byUsername map[string]string
	profiles   map[string][]byte
	ladder     map[string]map[string]LadderRow // queue → player → row
	orders     map[string]Order
	idem       map[idemKey]IdempotencyRecord
	matches    map[string]MatchRecord
}

type idemKey struct{ player, key string }

// NewMemory creates an empty in-memory store.
func NewMemory() *Memory {
	return &Memory{
		accounts:   map[string]Account{},
		byDevice:   map[string]string{},
		byUsername: map[string]string{},
		profiles:   map[string][]byte{},
		ladder:     map[string]map[string]LadderRow{},
		orders:     map[string]Order{},
		idem:       map[idemKey]IdempotencyRecord{},
		matches:    map[string]MatchRecord{},
	}
}

// InTx runs fn with exclusive access to the store.
func (m *Memory) InTx(ctx context.Context, fn func(tx Tx) error) (err error) {
	if err := ctx.Err(); err != nil {
		return err
	}
	m.mu.Lock()
	defer m.mu.Unlock()
	tx := &memTx{m: m}
	defer func() {
		if r := recover(); r != nil {
			tx.rollback()
			panic(r)
		}
	}()
	if err := fn(tx); err != nil {
		tx.rollback()
		return err
	}
	return nil
}

// Close is a no-op.
func (m *Memory) Close() {}

// Ladder implements Store.
func (m *Memory) Ladder(ctx context.Context, queue, season string, offset, limit int) ([]LadderRow, int, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	rows := m.sortedLadder(queue, season)
	total := len(rows)
	if offset > total {
		offset = total
	}
	end := offset + limit
	if end > total {
		end = total
	}
	return append([]LadderRow(nil), rows[offset:end]...), total, nil
}

// LadderEntry implements Store.
func (m *Memory) LadderEntry(ctx context.Context, queue, season, playerID string) (LadderRow, int, bool, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	row, ok := m.ladder[queue][playerID]
	if !ok || row.Season != season {
		return LadderRow{}, 0, false, nil
	}
	if row.PlacementsLeft > 0 {
		return row, 0, true, nil
	}
	return row, m.rank(queue, season, row.Rating, playerID), true, nil
}

// PurgeIdempotency implements Store.
func (m *Memory) PurgeIdempotency(ctx context.Context, before time.Time) (int64, error) {
	m.mu.Lock()
	defer m.mu.Unlock()
	var n int64
	for k, r := range m.idem {
		if r.CreatedAt.Before(before) {
			delete(m.idem, k)
			n++
		}
	}
	return n, nil
}

func (m *Memory) sortedLadder(queue, season string) []LadderRow {
	var rows []LadderRow
	for _, r := range m.ladder[queue] {
		if r.Season == season && r.PlacementsLeft == 0 {
			rows = append(rows, r)
		}
	}
	sort.Slice(rows, func(i, j int) bool {
		if rows[i].Rating != rows[j].Rating {
			return rows[i].Rating > rows[j].Rating
		}
		return rows[i].PlayerID < rows[j].PlayerID
	})
	return rows
}

func (m *Memory) rank(queue, season string, rating float64, playerID string) int {
	n := 1
	for pid, r := range m.ladder[queue] {
		if pid == playerID || r.Season != season || r.PlacementsLeft > 0 {
			continue
		}
		if r.Rating > rating || (r.Rating == rating && pid < playerID) {
			n++
		}
	}
	return n
}

type memTx struct {
	m    *Memory
	undo []func()
}

func (tx *memTx) rollback() {
	for i := len(tx.undo) - 1; i >= 0; i-- {
		tx.undo[i]()
	}
	tx.undo = nil
}

// set writes mp[k] = v and records how to undo it.
func set[K comparable, V any](tx *memTx, mp map[K]V, k K, v V) {
	old, had := mp[k]
	tx.undo = append(tx.undo, func() {
		if had {
			mp[k] = old
		} else {
			delete(mp, k)
		}
	})
	mp[k] = v
}

func del[K comparable, V any](tx *memTx, mp map[K]V, k K) {
	old, had := mp[k]
	if !had {
		return
	}
	tx.undo = append(tx.undo, func() { mp[k] = old })
	delete(mp, k)
}

func (tx *memTx) CreateAccount(ctx context.Context, a Account) error {
	m := tx.m
	if _, ok := m.accounts[a.PlayerID]; ok {
		return ErrConflict
	}
	if a.DeviceID != "" {
		if _, ok := m.byDevice[a.DeviceID]; ok {
			return ErrConflict
		}
	}
	if a.Username != "" {
		if _, ok := m.byUsername[a.Username]; ok {
			return ErrConflict
		}
	}
	set(tx, m.accounts, a.PlayerID, a)
	if a.DeviceID != "" {
		set(tx, m.byDevice, a.DeviceID, a.PlayerID)
	}
	if a.Username != "" {
		set(tx, m.byUsername, a.Username, a.PlayerID)
	}
	return nil
}

func (tx *memTx) AccountByID(ctx context.Context, id string) (Account, error) {
	a, ok := tx.m.accounts[id]
	if !ok {
		return Account{}, ErrNotFound
	}
	return a, nil
}

func (tx *memTx) AccountByDevice(ctx context.Context, deviceID string) (Account, error) {
	id, ok := tx.m.byDevice[deviceID]
	if !ok {
		return Account{}, ErrNotFound
	}
	return tx.AccountByID(ctx, id)
}

func (tx *memTx) AccountByUsername(ctx context.Context, username string) (Account, error) {
	id, ok := tx.m.byUsername[username]
	if !ok {
		return Account{}, ErrNotFound
	}
	return tx.AccountByID(ctx, id)
}

func (tx *memTx) UpdateAccount(ctx context.Context, a Account) error {
	m := tx.m
	old, ok := m.accounts[a.PlayerID]
	if !ok {
		return ErrNotFound
	}
	if a.Username != old.Username && a.Username != "" {
		if _, taken := m.byUsername[a.Username]; taken {
			return ErrConflict
		}
	}
	if a.DeviceID != old.DeviceID && a.DeviceID != "" {
		if _, taken := m.byDevice[a.DeviceID]; taken {
			return ErrConflict
		}
	}
	if old.Username != a.Username {
		del(tx, m.byUsername, old.Username)
		if a.Username != "" {
			set(tx, m.byUsername, a.Username, a.PlayerID)
		}
	}
	if old.DeviceID != a.DeviceID {
		del(tx, m.byDevice, old.DeviceID)
		if a.DeviceID != "" {
			set(tx, m.byDevice, a.DeviceID, a.PlayerID)
		}
	}
	set(tx, m.accounts, a.PlayerID, a)
	return nil
}

func (tx *memTx) GetProfile(ctx context.Context, playerID string, forUpdate bool) (*profile.Profile, error) {
	b, ok := tx.m.profiles[playerID]
	if !ok {
		return nil, ErrNotFound
	}
	var p profile.Profile
	if err := json.Unmarshal(b, &p); err != nil {
		return nil, err
	}
	return &p, nil
}

func (tx *memTx) PutProfile(ctx context.Context, p *profile.Profile) error {
	b, err := json.Marshal(p)
	if err != nil {
		return err
	}
	set(tx, tx.m.profiles, p.PlayerID, b)
	for _, row := range ladderRows(p) {
		q := tx.m.ladder[row.Queue]
		if q == nil {
			q = map[string]LadderRow{}
			tx.m.ladder[row.Queue] = q
		}
		set(tx, q, row.PlayerID, row)
	}
	return nil
}

func (tx *memTx) CreateOrder(ctx context.Context, o Order) error {
	if _, ok := tx.m.orders[o.ID]; ok {
		return ErrConflict
	}
	set(tx, tx.m.orders, o.ID, o)
	return nil
}

func (tx *memTx) GetOrder(ctx context.Context, id string, forUpdate bool) (Order, error) {
	o, ok := tx.m.orders[id]
	if !ok {
		return Order{}, ErrNotFound
	}
	return o, nil
}

func (tx *memTx) UpdateOrder(ctx context.Context, o Order) error {
	if _, ok := tx.m.orders[o.ID]; !ok {
		return ErrNotFound
	}
	set(tx, tx.m.orders, o.ID, o)
	return nil
}

func (tx *memTx) GetIdempotency(ctx context.Context, playerID, key string) (IdempotencyRecord, bool, error) {
	r, ok := tx.m.idem[idemKey{playerID, key}]
	return r, ok, nil
}

func (tx *memTx) PutIdempotency(ctx context.Context, r IdempotencyRecord) error {
	k := idemKey{r.PlayerID, r.Key}
	if _, ok := tx.m.idem[k]; ok {
		return ErrConflict
	}
	r.Body = append([]byte(nil), r.Body...)
	set(tx, tx.m.idem, k, r)
	return nil
}

func (tx *memTx) InsertMatchResult(ctx context.Context, rec MatchRecord) error {
	if _, ok := tx.m.matches[rec.MatchID]; ok {
		return ErrConflict
	}
	set(tx, tx.m.matches, rec.MatchID, rec)
	return nil
}

func (tx *memTx) LadderRank(ctx context.Context, queue, season string, rating float64, playerID string) (int, error) {
	return tx.m.rank(queue, season, rating, playerID), nil
}
