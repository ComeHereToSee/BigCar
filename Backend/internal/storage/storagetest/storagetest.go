// Package storagetest provides Postgres-backed stores for integration tests.
package storagetest

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"os"
	"strings"
	"testing"

	"github.com/jackc/pgx/v5"

	"github.com/comeheretosee/bigcar/backend/internal/storage"
)

// EnvURL is the environment variable that enables Postgres integration tests.
const EnvURL = "BIGCAR_TEST_DATABASE_URL"

// Postgres returns a store in a fresh schema of the database named by
// BIGCAR_TEST_DATABASE_URL, dropped when the test ends. The test is skipped
// when the variable is not set.
func Postgres(t testing.TB) storage.Store {
	t.Helper()
	url := os.Getenv(EnvURL)
	if url == "" {
		t.Skip(EnvURL + " not set")
	}
	ctx := context.Background()
	admin, err := pgx.Connect(ctx, url)
	if err != nil {
		t.Fatalf("connect: %v", err)
	}
	b := make([]byte, 6)
	_, _ = rand.Read(b)
	schema := "bigcar_test_" + hex.EncodeToString(b)
	if _, err := admin.Exec(ctx, "CREATE SCHEMA "+schema); err != nil {
		t.Fatalf("create schema: %v", err)
	}
	sep := "?"
	if strings.Contains(url, "?") {
		sep = "&"
	}
	st, err := storage.OpenPostgres(ctx, url+sep+"search_path="+schema)
	if err != nil {
		t.Fatalf("open: %v", err)
	}
	t.Cleanup(func() {
		st.Close()
		_, _ = admin.Exec(context.Background(), "DROP SCHEMA "+schema+" CASCADE")
		_ = admin.Close(context.Background())
	})
	return st
}
