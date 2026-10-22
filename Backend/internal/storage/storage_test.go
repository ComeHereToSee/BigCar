package storage_test

import (
	"context"
	"errors"
	"fmt"
	"os"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/data/datatest"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
	"github.com/comeheretosee/bigcar/backend/internal/storage/storagetest"
)

func TestMemoryConformance(t *testing.T) {
	runConformance(t, func(t *testing.T) storage.Store { return storage.NewMemory() })
}

// TestPostgresConformance runs the same suite against a real database when
// BIGCAR_TEST_DATABASE_URL is set (CI provides a postgres service). Each
// subtest gets its own schema.
func TestPostgresConformance(t *testing.T) {
	if os.Getenv(storagetest.EnvURL) == "" {
		t.Skip(storagetest.EnvURL + " not set")
	}
	runConformance(t, func(t *testing.T) storage.Store { return storagetest.Postgres(t) })
}

func runConformance(t *testing.T, newStore func(t *testing.T) storage.Store) {
	gd := datatest.Load(t)
	tests := map[string]func(t *testing.T, st storage.Store, gd *data.GameData){
		"accounts":          testAccounts,
		"profiles":          testProfiles,
		"rollback":          testRollback,
		"concurrent spend":  testConcurrentSpend,
		"idempotency":       testIdempotency,
		"orders":            testOrders,
		"match results":     testMatchResults,
		"ladder":            testLadder,
		"purge idempotency": testPurge,
	}
	for name, fn := range tests {
		t.Run(name, func(t *testing.T) { fn(t, newStore(t), gd) })
	}
}

var ctx = context.Background()

func createPlayer(t *testing.T, st storage.Store, gd *data.GameData, id string) {
	t.Helper()
	err := st.InTx(ctx, func(tx storage.Tx) error {
		if err := tx.CreateAccount(ctx, storage.Account{PlayerID: id, DeviceID: "dev-" + id, CreatedAt: time.Now().UTC()}); err != nil {
			return err
		}
		return tx.PutProfile(ctx, profile.New(id, "Name"+id, gd))
	})
	if err != nil {
		t.Fatalf("create player %s: %v", id, err)
	}
}

func testAccounts(t *testing.T, st storage.Store, gd *data.GameData) {
	now := time.Now().UTC().Truncate(time.Microsecond)
	err := st.InTx(ctx, func(tx storage.Tx) error {
		return tx.CreateAccount(ctx, storage.Account{PlayerID: "p_a", DeviceID: "device-a", CreatedAt: now})
	})
	if err != nil {
		t.Fatal(err)
	}
	err = st.InTx(ctx, func(tx storage.Tx) error {
		return tx.CreateAccount(ctx, storage.Account{PlayerID: "p_b", DeviceID: "device-a", CreatedAt: now})
	})
	if !errors.Is(err, storage.ErrConflict) {
		t.Fatalf("duplicate device: %v", err)
	}
	err = st.InTx(ctx, func(tx storage.Tx) error {
		if err := tx.CreateAccount(ctx, storage.Account{PlayerID: "p_c", Username: "taken", PasswordHash: "h", CreatedAt: now}); err != nil {
			return err
		}
		a, err := tx.AccountByDevice(ctx, "device-a")
		if err != nil || a.PlayerID != "p_a" || !a.CreatedAt.Equal(now) {
			return fmt.Errorf("by device: %+v %v", a, err)
		}
		a.Username, a.PasswordHash = "alice", "hash"
		if err := tx.UpdateAccount(ctx, a); err != nil {
			return err
		}
		b, err := tx.AccountByUsername(ctx, "alice")
		if err != nil || b.PlayerID != "p_a" || b.DeviceID != "device-a" || b.PasswordHash != "hash" {
			return fmt.Errorf("by username: %+v %v", b, err)
		}
		if _, err := tx.AccountByID(ctx, "p_zz"); !errors.Is(err, storage.ErrNotFound) {
			return fmt.Errorf("missing id: %v", err)
		}
		if _, err := tx.AccountByUsername(ctx, "bob"); !errors.Is(err, storage.ErrNotFound) {
			return fmt.Errorf("missing username: %v", err)
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	err = st.InTx(ctx, func(tx storage.Tx) error {
		a, _ := tx.AccountByID(ctx, "p_a")
		a.Username = "taken"
		return tx.UpdateAccount(ctx, a)
	})
	if !errors.Is(err, storage.ErrConflict) {
		t.Fatalf("duplicate username: %v", err)
	}
}

func testProfiles(t *testing.T, st storage.Store, gd *data.GameData) {
	createPlayer(t, st, gd, "p_1")
	p, err := storage.ReadProfile(ctx, st, "p_1")
	if err != nil {
		t.Fatal(err)
	}
	want := profile.New("p_1", "Namep_1", gd)
	if fmt.Sprintf("%+v", p) != fmt.Sprintf("%+v", want) {
		t.Fatalf("round trip:\n%+v\n%+v", p, want)
	}
	if _, err := storage.ReadProfile(ctx, st, "p_none"); !errors.Is(err, storage.ErrNotFound) {
		t.Fatalf("missing profile: %v", err)
	}
	// Mutation is visible afterwards.
	_, err = storage.MutateProfile(ctx, st, "p_1", nil, time.Now, func(tx storage.Tx, p *profile.Profile) (any, error) {
		p.Wallet.Gold = 77
		return map[string]int{"ok": 1}, nil
	})
	if err != nil {
		t.Fatal(err)
	}
	p, _ = storage.ReadProfile(ctx, st, "p_1")
	if p.Wallet.Gold != 77 {
		t.Fatalf("gold = %d", p.Wallet.Gold)
	}
}

func testRollback(t *testing.T, st storage.Store, gd *data.GameData) {
	createPlayer(t, st, gd, "p_1")
	boom := errors.New("boom")
	err := st.InTx(ctx, func(tx storage.Tx) error {
		p, err := tx.GetProfile(ctx, "p_1", true)
		if err != nil {
			return err
		}
		p.Wallet.Credits = 1
		if err := tx.PutProfile(ctx, p); err != nil {
			return err
		}
		if err := tx.CreateOrder(ctx, storage.Order{ID: "o_rb", PlayerID: "p_1", Provider: "sandbox", PackID: "gold_s",
			Gold: 1, Status: storage.OrderPending, CreatedAt: time.Now(), UpdatedAt: time.Now()}); err != nil {
			return err
		}
		return boom
	})
	if !errors.Is(err, boom) {
		t.Fatalf("err = %v", err)
	}
	p, _ := storage.ReadProfile(ctx, st, "p_1")
	if p.Wallet.Credits != profile.StartingCredits {
		t.Fatalf("rollback failed: credits %d", p.Wallet.Credits)
	}
	_ = st.InTx(ctx, func(tx storage.Tx) error {
		if _, err := tx.GetOrder(ctx, "o_rb", false); !errors.Is(err, storage.ErrNotFound) {
			t.Errorf("order survived rollback: %v", err)
		}
		return nil
	})
}

// testConcurrentSpend: 25 parallel purchases of 1000 credits against a
// 10 000 credit wallet must succeed exactly 10 times.
func testConcurrentSpend(t *testing.T, st storage.Store, gd *data.GameData) {
	createPlayer(t, st, gd, "p_1")
	_, err := storage.MutateProfile(ctx, st, "p_1", nil, time.Now, func(tx storage.Tx, p *profile.Profile) (any, error) {
		p.Wallet.Credits = 10000
		return nil, nil
	})
	if err != nil {
		t.Fatal(err)
	}
	var wg sync.WaitGroup
	var mu sync.Mutex
	ok, insufficient := 0, 0
	for i := 0; i < 25; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			_, err := storage.MutateProfile(ctx, st, "p_1", nil, time.Now, func(tx storage.Tx, p *profile.Profile) (any, error) {
				return nil, p.Pay(data.CreditsPrice(1000))
			})
			mu.Lock()
			defer mu.Unlock()
			switch apperr.CodeOf(err) {
			case "":
				ok++
			case apperr.InsufficientFunds:
				insufficient++
			default:
				t.Errorf("unexpected error: %v", err)
			}
		}()
	}
	wg.Wait()
	p, _ := storage.ReadProfile(ctx, st, "p_1")
	if ok != 10 || insufficient != 15 || p.Wallet.Credits != 0 {
		t.Fatalf("ok=%d insufficient=%d credits=%d", ok, insufficient, p.Wallet.Credits)
	}
}

func testIdempotency(t *testing.T, st storage.Store, gd *data.GameData) {
	createPlayer(t, st, gd, "p_1")
	idem := &storage.Idempotency{Key: "k1", RequestHash: storage.HashRequest("POST", "/x", []byte(`{}`))}
	spend := func(tx storage.Tx, p *profile.Profile) (any, error) {
		if err := p.Pay(data.CreditsPrice(500)); err != nil {
			return nil, err
		}
		return map[string]int64{"credits": p.Wallet.Credits}, nil
	}
	r1, err := storage.MutateProfile(ctx, st, "p_1", idem, time.Now, spend)
	if err != nil || r1.Replayed {
		t.Fatalf("first: %+v %v", r1, err)
	}
	r2, err := storage.MutateProfile(ctx, st, "p_1", idem, time.Now, spend)
	if err != nil || !r2.Replayed || string(r2.Body) != string(r1.Body) || r2.Status != 200 {
		t.Fatalf("replay: %+v %v", r2, err)
	}
	p, _ := storage.ReadProfile(ctx, st, "p_1")
	if p.Wallet.Credits != profile.StartingCredits-500 {
		t.Fatalf("applied twice: %d", p.Wallet.Credits)
	}
	other := &storage.Idempotency{Key: "k1", RequestHash: "different"}
	if _, err := storage.MutateProfile(ctx, st, "p_1", other, time.Now, spend); apperr.CodeOf(err) != apperr.Conflict {
		t.Fatalf("reused key: %v", err)
	}
	if _, err := storage.MutateProfile(ctx, st, "p_1", &storage.Idempotency{Key: strings.Repeat("x", 200)}, time.Now, spend); apperr.CodeOf(err) != apperr.BadRequest {
		t.Fatalf("long key: %v", err)
	}
}

func testOrders(t *testing.T, st storage.Store, gd *data.GameData) {
	createPlayer(t, st, gd, "p_1")
	now := time.Now().UTC().Truncate(time.Microsecond)
	o := storage.Order{ID: "o_1", PlayerID: "p_1", Provider: "sandbox", PackID: "gold_m", Gold: 550,
		Status: storage.OrderPending, CreatedAt: now, UpdatedAt: now}
	// A conflicting insert must leave the transaction usable (Postgres uses a
	// savepoint for this), so the order is still committed.
	err := st.InTx(ctx, func(tx storage.Tx) error {
		if err := tx.CreateOrder(ctx, o); err != nil {
			return err
		}
		if err := tx.CreateOrder(ctx, o); !errors.Is(err, storage.ErrConflict) {
			return fmt.Errorf("duplicate order: %v", err)
		}
		return nil
	})
	if err != nil {
		t.Fatal(err)
	}
	err = st.InTx(ctx, func(tx storage.Tx) error {
		got, err := tx.GetOrder(ctx, "o_1", true)
		if err != nil {
			return err
		}
		if got != o {
			return fmt.Errorf("order round trip %+v != %+v", got, o)
		}
		got.Status, got.ExternalID = storage.OrderCompleted, "ext"
		return tx.UpdateOrder(ctx, got)
	})
	if err != nil {
		t.Fatal(err)
	}
	_ = st.InTx(ctx, func(tx storage.Tx) error {
		got, _ := tx.GetOrder(ctx, "o_1", false)
		if got.Status != storage.OrderCompleted || got.ExternalID != "ext" {
			t.Errorf("update lost: %+v", got)
		}
		return nil
	})
}

func testMatchResults(t *testing.T, st storage.Store, gd *data.GameData) {
	rec := storage.MatchRecord{MatchID: "m_1", ServerID: "s1", Queue: "ranked_1v1", Result: []byte(`{}`),
		Rewards: []byte(`[]`), CreatedAt: time.Now()}
	if err := st.InTx(ctx, func(tx storage.Tx) error { return tx.InsertMatchResult(ctx, rec) }); err != nil {
		t.Fatal(err)
	}
	err := st.InTx(ctx, func(tx storage.Tx) error { return tx.InsertMatchResult(ctx, rec) })
	if !errors.Is(err, storage.ErrConflict) {
		t.Fatalf("second insert: %v", err)
	}
}

func testLadder(t *testing.T, st storage.Store, gd *data.GameData) {
	ratings := map[string]float64{"p_a": 1600, "p_b": 1700, "p_c": 1600, "p_d": 1400, "p_e": 2000}
	for id, r := range ratings {
		createPlayer(t, st, gd, id)
		placements := 0
		if id == "p_e" {
			placements = 2 // still in placements → not on the ladder
		}
		_, err := storage.MutateProfile(ctx, st, id, nil, time.Now, func(tx storage.Tx, p *profile.Profile) (any, error) {
			p.Ranked["ranked_1v1"] = profile.Ranked{Rating: r, RD: 80, Vol: 0.06, Wins: 3, Losses: 2, Tier: "gold",
				Division: 1, PlacementsLeft: placements}
			p.Ranked["ranked_2v2"] = profile.Ranked{Rating: 1500, RD: 350, Vol: 0.06, PlacementsLeft: 5}
			return nil, nil
		})
		if err != nil {
			t.Fatal(err)
		}
	}
	rows, total, err := st.Ladder(ctx, "ranked_1v1", "season_1", 0, 10)
	if err != nil {
		t.Fatal(err)
	}
	var ids []string
	for _, r := range rows {
		ids = append(ids, r.PlayerID)
	}
	if total != 4 || strings.Join(ids, ",") != "p_b,p_a,p_c,p_d" {
		t.Fatalf("ladder total=%d ids=%v", total, ids)
	}
	if rows[0].DisplayName != "Namep_b" || rows[0].Wins != 3 || rows[0].Tier != "gold" {
		t.Fatalf("row %+v", rows[0])
	}
	rows, total, _ = st.Ladder(ctx, "ranked_1v1", "season_1", 2, 1)
	if total != 4 || len(rows) != 1 || rows[0].PlayerID != "p_c" {
		t.Fatalf("page: %+v", rows)
	}
	rows, total, _ = st.Ladder(ctx, "ranked_1v1", "season_1", 10, 5)
	if total != 4 || len(rows) != 0 {
		t.Fatalf("past end: %+v", rows)
	}
	if _, total, _ := st.Ladder(ctx, "ranked_1v1", "season_9", 0, 10); total != 0 {
		t.Fatalf("other season total %d", total)
	}
	if _, total, _ := st.Ladder(ctx, "ranked_2v2", "season_1", 0, 10); total != 0 {
		t.Fatalf("placement-only queue total %d", total)
	}
	row, rank, found, err := st.LadderEntry(ctx, "ranked_1v1", "season_1", "p_c")
	if err != nil || !found || rank != 3 || row.Rating != 1600 {
		t.Fatalf("entry p_c: %+v rank %d found %v err %v", row, rank, found, err)
	}
	_, rank, found, _ = st.LadderEntry(ctx, "ranked_1v1", "season_1", "p_e")
	if !found || rank != 0 {
		t.Fatalf("placement player rank %d found %v", rank, found)
	}
	if _, _, found, _ := st.LadderEntry(ctx, "ranked_1v1", "season_1", "p_zz"); found {
		t.Fatal("unknown player found")
	}
	_ = st.InTx(ctx, func(tx storage.Tx) error {
		for _, tc := range []struct {
			rating float64
			pid    string
			want   int
		}{{1800, "p_x", 1}, {1700, "p_a", 1}, {1650, "p_e", 2}, {1600, "p_b", 2}, {1000, "p_x", 5}} {
			got, err := tx.LadderRank(ctx, "ranked_1v1", "season_1", tc.rating, tc.pid)
			if err != nil || got != tc.want {
				t.Errorf("LadderRank(%v, %s) = %d (%v), want %d", tc.rating, tc.pid, got, err, tc.want)
			}
		}
		return nil
	})
}

func testPurge(t *testing.T, st storage.Store, gd *data.GameData) {
	createPlayer(t, st, gd, "p_1")
	old := time.Now().Add(-48 * time.Hour)
	_, err := storage.MutateProfile(ctx, st, "p_1", &storage.Idempotency{Key: "old"}, func() time.Time { return old },
		func(tx storage.Tx, p *profile.Profile) (any, error) { return nil, nil })
	if err != nil {
		t.Fatal(err)
	}
	_, err = storage.MutateProfile(ctx, st, "p_1", &storage.Idempotency{Key: "new"}, time.Now,
		func(tx storage.Tx, p *profile.Profile) (any, error) { return nil, nil })
	if err != nil {
		t.Fatal(err)
	}
	n, err := st.PurgeIdempotency(ctx, time.Now().Add(-24*time.Hour))
	if err != nil || n != 1 {
		t.Fatalf("purged %d (%v)", n, err)
	}
}
