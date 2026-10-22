// Package datatest gives tests in other packages access to the fixture data
// set in internal/data/testdata.
package datatest

import (
	"os"
	"path/filepath"
	"runtime"
	"testing"

	"github.com/comeheretosee/bigcar/backend/internal/data"
)

// Dir returns the absolute path of the fixture data directory.
func Dir() string {
	_, file, _, ok := runtime.Caller(0)
	if !ok {
		panic("datatest: cannot locate source file")
	}
	return filepath.Join(filepath.Dir(file), "..", "testdata")
}

// Load loads and validates the fixture data set or fails the test.
func Load(tb testing.TB) *data.GameData {
	tb.Helper()
	gd, err := data.Load(Dir())
	if err != nil {
		tb.Fatalf("load fixture data: %v", err)
	}
	return gd
}

// Files returns the raw fixture files (name → bytes), for tests that mutate them.
func Files(tb testing.TB) map[string][]byte {
	tb.Helper()
	entries, err := os.ReadDir(Dir())
	if err != nil {
		tb.Fatalf("read fixture dir: %v", err)
	}
	out := map[string][]byte{}
	for _, e := range entries {
		if e.IsDir() || filepath.Ext(e.Name()) != ".json" {
			continue
		}
		b, err := os.ReadFile(filepath.Join(Dir(), e.Name()))
		if err != nil {
			tb.Fatalf("read %s: %v", e.Name(), err)
		}
		out[e.Name()] = b
	}
	return out
}
