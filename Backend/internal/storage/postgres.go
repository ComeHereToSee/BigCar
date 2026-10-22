package storage

import (
	"context"
	"embed"
	"encoding/json"
	"errors"
	"fmt"
	"io/fs"
	"sort"
	"strconv"
	"strings"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
	"github.com/jackc/pgx/v5/pgxpool"

	"github.com/comeheretosee/bigcar/backend/internal/profile"
)

//go:embed migrations/*.sql
var migrationsFS embed.FS

// migrationLockID is the pg_advisory_xact_lock key that serializes concurrent
// migration runs (several backend replicas starting at once).
const migrationLockID = 0x42_69_67_43_61_72 // "BigCar"

// Postgres is a Store backed by PostgreSQL.
type Postgres struct {
	pool *pgxpool.Pool
}

// OpenPostgres connects, applies pending migrations and returns the store.
func OpenPostgres(ctx context.Context, url string) (*Postgres, error) {
	cfg, err := pgxpool.ParseConfig(url)
	if err != nil {
		return nil, fmt.Errorf("storage: parse database url: %w", err)
	}
	pool, err := pgxpool.NewWithConfig(ctx, cfg)
	if err != nil {
		return nil, fmt.Errorf("storage: connect: %w", err)
	}
	if err := pool.Ping(ctx); err != nil {
		pool.Close()
		return nil, fmt.Errorf("storage: ping: %w", err)
	}
	if err := migrate(ctx, pool); err != nil {
		pool.Close()
		return nil, err
	}
	return &Postgres{pool: pool}, nil
}

// Close closes the connection pool.
func (s *Postgres) Close() { s.pool.Close() }

// migrate applies the embedded migrations in file-name order, each in its own
// transaction, recording them in schema_migrations.
func migrate(ctx context.Context, pool *pgxpool.Pool) error {
	names, err := fs.Glob(migrationsFS, "migrations/*.sql")
	if err != nil {
		return err
	}
	sort.Strings(names)
	for _, name := range names {
		version, err := strconv.Atoi(strings.SplitN(strings.TrimPrefix(name, "migrations/"), "_", 2)[0])
		if err != nil {
			return fmt.Errorf("storage: migration %s: bad version prefix", name)
		}
		sqlText, err := migrationsFS.ReadFile(name)
		if err != nil {
			return err
		}
		err = pgx.BeginFunc(ctx, pool, func(tx pgx.Tx) error {
			if _, err := tx.Exec(ctx, "SELECT pg_advisory_xact_lock($1)", int64(migrationLockID)); err != nil {
				return err
			}
			if _, err := tx.Exec(ctx, `CREATE TABLE IF NOT EXISTS schema_migrations (
				version INTEGER PRIMARY KEY, applied_at TIMESTAMPTZ NOT NULL DEFAULT now())`); err != nil {
				return err
			}
			var exists bool
			if err := tx.QueryRow(ctx, "SELECT EXISTS (SELECT 1 FROM schema_migrations WHERE version = $1)", version).Scan(&exists); err != nil {
				return err
			}
			if exists {
				return nil
			}
			// No arguments → simple protocol, which allows multiple statements.
			if _, err := tx.Exec(ctx, string(sqlText)); err != nil {
				return err
			}
			_, err := tx.Exec(ctx, "INSERT INTO schema_migrations (version) VALUES ($1)", version)
			return err
		})
		if err != nil {
			return fmt.Errorf("storage: migration %s: %w", name, err)
		}
	}
	return nil
}

func isRetryable(err error) bool {
	var pgErr *pgconn.PgError
	if errors.As(err, &pgErr) {
		return pgErr.Code == "40001" || pgErr.Code == "40P01" // serialization_failure, deadlock_detected
	}
	return errors.Is(err, errVersionConflict)
}

func isUniqueViolation(err error) bool {
	var pgErr *pgconn.PgError
	return errors.As(err, &pgErr) && pgErr.Code == "23505"
}

// errVersionConflict means a profile changed between read and write; the whole
// transaction is retried.
var errVersionConflict = fmt.Errorf("%w: profile version changed", ErrConflict)

// InTx runs fn in a READ COMMITTED transaction, retrying a few times on
// serialization failures, deadlocks and profile version conflicts.
func (s *Postgres) InTx(ctx context.Context, fn func(tx Tx) error) error {
	const attempts = 4
	var err error
	for i := 0; i < attempts; i++ {
		err = pgx.BeginTxFunc(ctx, s.pool, pgx.TxOptions{IsoLevel: pgx.ReadCommitted}, func(tx pgx.Tx) error {
			return fn(&pgTx{tx: tx, versions: map[string]int64{}})
		})
		if err == nil || !isRetryable(err) || ctx.Err() != nil {
			return err
		}
		time.Sleep(time.Duration(i+1) * 10 * time.Millisecond)
	}
	return err
}

const ladderColumns = "queue, player_id, season, display_name, rating, rd, wins, losses, tier, division, placements_left"

func scanLadder(row pgx.Row) (LadderRow, error) {
	var r LadderRow
	err := row.Scan(&r.Queue, &r.PlayerID, &r.Season, &r.DisplayName, &r.Rating, &r.RD, &r.Wins, &r.Losses,
		&r.Tier, &r.Division, &r.PlacementsLeft)
	return r, err
}

// Ladder implements Store.
func (s *Postgres) Ladder(ctx context.Context, queue, season string, offset, limit int) ([]LadderRow, int, error) {
	var total int
	if err := s.pool.QueryRow(ctx,
		"SELECT count(*) FROM ratings WHERE queue = $1 AND season = $2 AND placements_left = 0",
		queue, season).Scan(&total); err != nil {
		return nil, 0, err
	}
	rows, err := s.pool.Query(ctx, "SELECT "+ladderColumns+` FROM ratings
		WHERE queue = $1 AND season = $2 AND placements_left = 0
		ORDER BY rating DESC, player_id ASC OFFSET $3 LIMIT $4`, queue, season, offset, limit)
	if err != nil {
		return nil, 0, err
	}
	defer rows.Close()
	var out []LadderRow
	for rows.Next() {
		r, err := scanLadder(rows)
		if err != nil {
			return nil, 0, err
		}
		out = append(out, r)
	}
	return out, total, rows.Err()
}

// LadderEntry implements Store.
func (s *Postgres) LadderEntry(ctx context.Context, queue, season, playerID string) (LadderRow, int, bool, error) {
	r, err := scanLadder(s.pool.QueryRow(ctx, "SELECT "+ladderColumns+
		" FROM ratings WHERE queue = $1 AND player_id = $2 AND season = $3", queue, playerID, season))
	if errors.Is(err, pgx.ErrNoRows) {
		return LadderRow{}, 0, false, nil
	}
	if err != nil {
		return LadderRow{}, 0, false, err
	}
	if r.PlacementsLeft > 0 {
		return r, 0, true, nil
	}
	rank, err := ladderRank(ctx, s.pool, queue, season, r.Rating, playerID)
	return r, rank, true, err
}

type querier interface {
	QueryRow(ctx context.Context, sql string, args ...any) pgx.Row
}

func ladderRank(ctx context.Context, q querier, queue, season string, rating float64, playerID string) (int, error) {
	var above int
	err := q.QueryRow(ctx, `SELECT count(*) FROM ratings
		WHERE queue = $1 AND season = $2 AND placements_left = 0 AND player_id <> $4
		  AND (rating > $3 OR (rating = $3 AND player_id < $4))`, queue, season, rating, playerID).Scan(&above)
	return above + 1, err
}

// PurgeIdempotency implements Store.
func (s *Postgres) PurgeIdempotency(ctx context.Context, before time.Time) (int64, error) {
	tag, err := s.pool.Exec(ctx, "DELETE FROM idempotency_keys WHERE created_at < $1", before)
	if err != nil {
		return 0, err
	}
	return tag.RowsAffected(), nil
}

type pgTx struct {
	tx pgx.Tx
	// versions remembers the version of every profile read in this
	// transaction; updates require it to be unchanged.
	versions map[string]int64
}

// execSavepoint runs a statement that may fail with a unique violation inside
// a savepoint, so the failure does not abort the surrounding transaction
// (matching the memory store, where ErrConflict leaves the transaction usable).
func (t *pgTx) execSavepoint(ctx context.Context, sql string, args ...any) (pgconn.CommandTag, error) {
	sp, err := t.tx.Begin(ctx)
	if err != nil {
		return pgconn.CommandTag{}, err
	}
	tag, err := sp.Exec(ctx, sql, args...)
	if err != nil {
		_ = sp.Rollback(ctx)
		return tag, err
	}
	return tag, sp.Commit(ctx)
}

func nullable(s string) *string {
	if s == "" {
		return nil
	}
	return &s
}

func deref(s *string) string {
	if s == nil {
		return ""
	}
	return *s
}

func (t *pgTx) CreateAccount(ctx context.Context, a Account) error {
	_, err := t.execSavepoint(ctx, `INSERT INTO accounts (player_id, device_id, username, password_hash, created_at)
		VALUES ($1, $2, $3, $4, $5)`, a.PlayerID, nullable(a.DeviceID), nullable(a.Username), a.PasswordHash, a.CreatedAt)
	if isUniqueViolation(err) {
		return ErrConflict
	}
	return err
}

func (t *pgTx) accountWhere(ctx context.Context, where string, arg any) (Account, error) {
	var a Account
	var device, username *string
	err := t.tx.QueryRow(ctx, "SELECT player_id, device_id, username, password_hash, created_at FROM accounts WHERE "+where, arg).
		Scan(&a.PlayerID, &device, &username, &a.PasswordHash, &a.CreatedAt)
	if errors.Is(err, pgx.ErrNoRows) {
		return Account{}, ErrNotFound
	}
	a.DeviceID, a.Username = deref(device), deref(username)
	a.CreatedAt = a.CreatedAt.UTC()
	return a, err
}

func (t *pgTx) AccountByID(ctx context.Context, id string) (Account, error) {
	return t.accountWhere(ctx, "player_id = $1", id)
}

func (t *pgTx) AccountByDevice(ctx context.Context, deviceID string) (Account, error) {
	return t.accountWhere(ctx, "device_id = $1", deviceID)
}

func (t *pgTx) AccountByUsername(ctx context.Context, username string) (Account, error) {
	return t.accountWhere(ctx, "username = $1", username)
}

func (t *pgTx) UpdateAccount(ctx context.Context, a Account) error {
	tag, err := t.execSavepoint(ctx, `UPDATE accounts SET device_id = $2, username = $3, password_hash = $4 WHERE player_id = $1`,
		a.PlayerID, nullable(a.DeviceID), nullable(a.Username), a.PasswordHash)
	if isUniqueViolation(err) {
		return ErrConflict
	}
	if err == nil && tag.RowsAffected() == 0 {
		return ErrNotFound
	}
	return err
}

func (t *pgTx) GetProfile(ctx context.Context, playerID string, forUpdate bool) (*profile.Profile, error) {
	q := "SELECT version, doc FROM profiles WHERE player_id = $1"
	if forUpdate {
		q += " FOR UPDATE"
	}
	var version int64
	var doc []byte
	err := t.tx.QueryRow(ctx, q, playerID).Scan(&version, &doc)
	if errors.Is(err, pgx.ErrNoRows) {
		return nil, ErrNotFound
	}
	if err != nil {
		return nil, err
	}
	var p profile.Profile
	if err := json.Unmarshal(doc, &p); err != nil {
		return nil, fmt.Errorf("storage: decode profile %s: %w", playerID, err)
	}
	t.versions[playerID] = version
	return &p, nil
}

func (t *pgTx) PutProfile(ctx context.Context, p *profile.Profile) error {
	doc, err := json.Marshal(p)
	if err != nil {
		return err
	}
	now := time.Now().UTC()
	if v, ok := t.versions[p.PlayerID]; ok {
		tag, err := t.tx.Exec(ctx, `UPDATE profiles SET doc = $2, version = version + 1, updated_at = $3
			WHERE player_id = $1 AND version = $4`, p.PlayerID, string(doc), now, v)
		if err != nil {
			return err
		}
		if tag.RowsAffected() == 0 {
			return errVersionConflict
		}
		t.versions[p.PlayerID] = v + 1
	} else {
		_, err := t.execSavepoint(ctx, `INSERT INTO profiles (player_id, version, doc, updated_at) VALUES ($1, 1, $2, $3)`,
			p.PlayerID, string(doc), now)
		if isUniqueViolation(err) {
			return ErrConflict
		}
		if err != nil {
			return err
		}
		t.versions[p.PlayerID] = 1
	}
	for _, r := range ladderRows(p) {
		_, err := t.tx.Exec(ctx, `INSERT INTO ratings (`+ladderColumns+`, updated_at)
			VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10, $11, $12)
			ON CONFLICT (queue, player_id) DO UPDATE SET season = EXCLUDED.season,
				display_name = EXCLUDED.display_name, rating = EXCLUDED.rating, rd = EXCLUDED.rd,
				wins = EXCLUDED.wins, losses = EXCLUDED.losses, tier = EXCLUDED.tier,
				division = EXCLUDED.division, placements_left = EXCLUDED.placements_left,
				updated_at = EXCLUDED.updated_at`,
			r.Queue, r.PlayerID, r.Season, r.DisplayName, r.Rating, r.RD, r.Wins, r.Losses, r.Tier, r.Division,
			r.PlacementsLeft, now)
		if err != nil {
			return err
		}
	}
	return nil
}

const orderColumns = "order_id, player_id, provider, pack_id, gold, status, external_id, redirect_url, created_at, updated_at"

func (t *pgTx) CreateOrder(ctx context.Context, o Order) error {
	_, err := t.execSavepoint(ctx, "INSERT INTO orders ("+orderColumns+") VALUES ($1, $2, $3, $4, $5, $6, $7, $8, $9, $10)",
		o.ID, o.PlayerID, o.Provider, o.PackID, o.Gold, o.Status, o.ExternalID, o.RedirectURL, o.CreatedAt, o.UpdatedAt)
	if isUniqueViolation(err) {
		return ErrConflict
	}
	return err
}

func (t *pgTx) GetOrder(ctx context.Context, id string, forUpdate bool) (Order, error) {
	q := "SELECT " + orderColumns + " FROM orders WHERE order_id = $1"
	if forUpdate {
		q += " FOR UPDATE"
	}
	var o Order
	err := t.tx.QueryRow(ctx, q, id).Scan(&o.ID, &o.PlayerID, &o.Provider, &o.PackID, &o.Gold, &o.Status,
		&o.ExternalID, &o.RedirectURL, &o.CreatedAt, &o.UpdatedAt)
	if errors.Is(err, pgx.ErrNoRows) {
		return Order{}, ErrNotFound
	}
	o.CreatedAt, o.UpdatedAt = o.CreatedAt.UTC(), o.UpdatedAt.UTC()
	return o, err
}

func (t *pgTx) UpdateOrder(ctx context.Context, o Order) error {
	tag, err := t.tx.Exec(ctx, `UPDATE orders SET status = $2, external_id = $3, redirect_url = $4, updated_at = $5
		WHERE order_id = $1`, o.ID, o.Status, o.ExternalID, o.RedirectURL, o.UpdatedAt)
	if err == nil && tag.RowsAffected() == 0 {
		return ErrNotFound
	}
	return err
}

func (t *pgTx) GetIdempotency(ctx context.Context, playerID, key string) (IdempotencyRecord, bool, error) {
	r := IdempotencyRecord{PlayerID: playerID, Key: key}
	err := t.tx.QueryRow(ctx, `SELECT request_hash, status, body, created_at FROM idempotency_keys
		WHERE player_id = $1 AND key = $2`, playerID, key).Scan(&r.RequestHash, &r.Status, &r.Body, &r.CreatedAt)
	if errors.Is(err, pgx.ErrNoRows) {
		return IdempotencyRecord{}, false, nil
	}
	if err != nil {
		return IdempotencyRecord{}, false, err
	}
	r.CreatedAt = r.CreatedAt.UTC()
	return r, true, nil
}

func (t *pgTx) PutIdempotency(ctx context.Context, r IdempotencyRecord) error {
	_, err := t.execSavepoint(ctx, `INSERT INTO idempotency_keys (player_id, key, request_hash, status, body, created_at)
		VALUES ($1, $2, $3, $4, $5, $6)`, r.PlayerID, r.Key, r.RequestHash, r.Status, r.Body, r.CreatedAt)
	if isUniqueViolation(err) {
		return ErrConflict
	}
	return err
}

func (t *pgTx) InsertMatchResult(ctx context.Context, m MatchRecord) error {
	_, err := t.execSavepoint(ctx, `INSERT INTO match_results (match_id, server_id, queue, result, rewards, created_at)
		VALUES ($1, $2, $3, $4, $5, $6)`, m.MatchID, m.ServerID, m.Queue, string(m.Result), string(m.Rewards), m.CreatedAt)
	if isUniqueViolation(err) {
		return ErrConflict
	}
	return err
}

func (t *pgTx) LadderRank(ctx context.Context, queue, season string, rating float64, playerID string) (int, error) {
	return ladderRank(ctx, t.tx, queue, season, rating, playerID)
}
