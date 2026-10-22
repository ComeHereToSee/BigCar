package httpapi

import (
	"sync"
	"time"
)

// Limiter is a set of token buckets keyed by string (player id or client IP).
// Each bucket refills at Rate tokens per second up to Burst. Idle buckets are
// garbage-collected lazily.
type Limiter struct {
	mu          sync.Mutex
	rate        float64
	burst       float64
	buckets     map[string]*bucket
	now         func() time.Time
	lastCleanup time.Time
}

type bucket struct {
	tokens float64
	last   time.Time
}

// NewLimiter creates a limiter allowing rate requests/second with the given burst.
func NewLimiter(rate, burst float64, now func() time.Time) *Limiter {
	if now == nil {
		now = time.Now
	}
	return &Limiter{rate: rate, burst: burst, buckets: map[string]*bucket{}, now: now}
}

// Allow takes one token from key's bucket.
func (l *Limiter) Allow(key string) bool {
	l.mu.Lock()
	defer l.mu.Unlock()
	now := l.now()
	if now.Sub(l.lastCleanup) > time.Minute {
		l.cleanupLocked(now)
	}
	b, ok := l.buckets[key]
	if !ok {
		b = &bucket{tokens: l.burst, last: now}
		l.buckets[key] = b
	}
	if elapsed := now.Sub(b.last).Seconds(); elapsed > 0 {
		b.tokens = min(l.burst, b.tokens+elapsed*l.rate)
		b.last = now
	}
	if b.tokens < 1 {
		return false
	}
	b.tokens--
	return true
}

// cleanupLocked drops buckets that have refilled completely (they behave
// exactly like a new bucket).
func (l *Limiter) cleanupLocked(now time.Time) {
	l.lastCleanup = now
	for k, b := range l.buckets {
		if b.tokens+now.Sub(b.last).Seconds()*l.rate >= l.burst {
			delete(l.buckets, k)
		}
	}
}

// Len returns the number of tracked buckets (for tests).
func (l *Limiter) Len() int {
	l.mu.Lock()
	defer l.mu.Unlock()
	return len(l.buckets)
}
