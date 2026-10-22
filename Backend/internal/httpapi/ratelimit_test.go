package httpapi

import (
	"sync"
	"testing"
	"time"
)

func TestLimiterBucketsAndCleanup(t *testing.T) {
	var mu sync.Mutex
	now := time.Unix(1000, 0)
	clock := func() time.Time { mu.Lock(); defer mu.Unlock(); return now }
	advance := func(d time.Duration) { mu.Lock(); defer mu.Unlock(); now = now.Add(d) }

	l := NewLimiter(2, 4, clock)
	for i := 0; i < 4; i++ {
		if !l.Allow("a") {
			t.Fatalf("burst request %d denied", i)
		}
	}
	if l.Allow("a") {
		t.Fatal("bucket a should be empty")
	}
	if !l.Allow("b") {
		t.Fatal("buckets must be independent")
	}
	advance(500 * time.Millisecond) // +1 token
	if !l.Allow("a") || l.Allow("a") {
		t.Fatal("refill should give exactly one token")
	}
	// After a minute every bucket has refilled; the next call garbage-collects
	// them, leaving only the bucket it creates.
	advance(2 * time.Minute)
	if !l.Allow("c") {
		t.Fatal("c denied")
	}
	if n := l.Len(); n != 1 {
		t.Fatalf("buckets after cleanup = %d, want 1", n)
	}
}
