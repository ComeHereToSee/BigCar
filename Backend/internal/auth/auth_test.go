package auth

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"strings"
	"sync"
	"testing"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data/datatest"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
)

type fakeClock struct {
	mu sync.Mutex
	t  time.Time
}

func (c *fakeClock) Now() time.Time { c.mu.Lock(); defer c.mu.Unlock(); return c.t }
func (c *fakeClock) Add(d time.Duration) {
	c.mu.Lock()
	defer c.mu.Unlock()
	c.t = c.t.Add(d)
}

func TestTokenSignVerify(t *testing.T) {
	clock := &fakeClock{t: time.Unix(1_800_000_000, 0)}
	s := NewSigner([]byte("secret-secret-secret-secret-1234"), TokenTTL, clock.Now)
	tok, exp := s.Sign("p_abcdef123456")
	if !exp.Equal(clock.Now().Add(7 * 24 * time.Hour)) {
		t.Fatalf("exp = %v", exp)
	}
	parts := strings.Split(tok, ".")
	if len(parts) != 3 || parts[0] != "v1" {
		t.Fatalf("format: %q", tok)
	}
	raw, err := base64.RawURLEncoding.DecodeString(parts[1])
	if err != nil {
		t.Fatal(err)
	}
	var payload map[string]any
	if err := json.Unmarshal(raw, &payload); err != nil || payload["pid"] != "p_abcdef123456" ||
		payload["exp"].(float64) != float64(exp.Unix()) || len(payload) != 2 {
		t.Fatalf("payload %v (%v)", payload, err)
	}

	other := NewSigner([]byte("another-secret-another-secret-12"), TokenTTL, clock.Now)
	forged := func() string {
		p, _ := json.Marshal(map[string]any{"pid": "p_victim", "exp": exp.Unix()})
		return "v1." + base64.RawURLEncoding.EncodeToString(p) + "." + parts[2]
	}()
	tests := []struct {
		name    string
		token   string
		signer  *Signer
		advance time.Duration
		wantPID string
		wantErr error
	}{
		{"valid", tok, s, 0, "p_abcdef123456", nil},
		{"valid just before expiry", tok, s, TokenTTL - time.Second, "p_abcdef123456", nil},
		{"expired", tok, s, TokenTTL, "", ErrExpiredToken},
		{"wrong secret", tok, other, 0, "", ErrInvalidToken},
		{"tampered payload", forged, s, 0, "", ErrInvalidToken},
		{"tampered signature", tok[:len(tok)-2] + "AA", s, 0, "", ErrInvalidToken},
		{"wrong version", "v2" + tok[2:], s, 0, "", ErrInvalidToken},
		{"two parts", "v1.abc", s, 0, "", ErrInvalidToken},
		{"garbage", "not a token", s, 0, "", ErrInvalidToken},
		{"empty", "", s, 0, "", ErrInvalidToken},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			c := &fakeClock{t: clock.Now().Add(tc.advance)}
			signer := NewSigner(tc.signer.secret, TokenTTL, c.Now)
			pid, err := signer.Verify(tc.token)
			if !errors.Is(err, tc.wantErr) || pid != tc.wantPID {
				t.Fatalf("Verify = %q, %v; want %q, %v", pid, err, tc.wantPID, tc.wantErr)
			}
		})
	}
}

func TestPasswordHashing(t *testing.T) {
	h := Hasher{Iterations: 1000}
	enc, err := h.Hash("correct horse")
	if err != nil {
		t.Fatal(err)
	}
	parts := strings.Split(enc, "$")
	if len(parts) != 4 || parts[0] != "pbkdf2_sha256" || parts[1] != "1000" {
		t.Fatalf("encoding %q", enc)
	}
	if salt, _ := base64.RawStdEncoding.DecodeString(parts[2]); len(salt) != 16 {
		t.Fatalf("salt length %d", len(salt))
	}
	enc2, _ := h.Hash("correct horse")
	if enc == enc2 {
		t.Fatal("salt must be random")
	}
	tests := []struct {
		name, hash, password string
		want                 bool
		wantErr              bool
	}{
		{"match", enc, "correct horse", true, false},
		{"mismatch", enc, "correct horsE", false, false},
		{"empty password", enc, "", false, false},
		{"bad scheme", "md5$1$a$b", "x", false, true},
		{"bad iterations", "pbkdf2_sha256$abc$AAAA$AAAA", "x", false, true},
		{"bad base64", "pbkdf2_sha256$1000$!!$AAAA", "x", false, true},
		{"too few parts", "pbkdf2_sha256$1000", "x", false, true},
	}
	for _, tc := range tests {
		ok, err := VerifyPassword(tc.hash, tc.password)
		if ok != tc.want || (err != nil) != tc.wantErr {
			t.Errorf("%s: ok=%v err=%v", tc.name, ok, err)
		}
	}
}

func TestDefaultIterations(t *testing.T) {
	if testing.Short() {
		t.Skip("slow")
	}
	enc, err := Hasher{}.Hash("password1")
	if err != nil {
		t.Fatal(err)
	}
	if !strings.HasPrefix(enc, "pbkdf2_sha256$210000$") {
		t.Fatalf("default hash %q", enc)
	}
	if ok, _ := VerifyPassword(enc, "password1"); !ok {
		t.Fatal("verify failed")
	}
}

func newService(t *testing.T) (*Service, storage.Store, *fakeClock) {
	t.Helper()
	gd := datatest.Load(t)
	st := storage.NewMemory()
	clock := &fakeClock{t: time.Date(2026, 11, 15, 12, 0, 0, 0, time.UTC)}
	svc, err := NewService(st, gd, NewSigner([]byte("k"), TokenTTL, clock.Now), Hasher{Iterations: 1000}, clock.Now)
	if err != nil {
		t.Fatal(err)
	}
	return svc, st, clock
}

func TestGuest(t *testing.T) {
	svc, st, _ := newService(t)
	ctx := context.Background()
	r1, err := svc.Guest(ctx, "device-0001")
	if err != nil {
		t.Fatal(err)
	}
	if !r1.IsNew || !strings.HasPrefix(r1.PlayerID, "p_") || len(r1.PlayerID) != 14 ||
		r1.DisplayName != "Driver"+r1.PlayerID[2:6] {
		t.Fatalf("result %+v", r1)
	}
	if pid, err := svc.Signer().Verify(r1.Token); err != nil || pid != r1.PlayerID {
		t.Fatalf("token: %v", err)
	}
	r2, err := svc.Guest(ctx, "device-0001")
	if err != nil || r2.IsNew || r2.PlayerID != r1.PlayerID {
		t.Fatalf("second login %+v %v", r2, err)
	}
	p, err := storage.ReadProfile(ctx, st, r1.PlayerID)
	if err != nil || p.Wallet.Credits != 25000 {
		t.Fatalf("profile %+v %v", p, err)
	}
	for _, bad := range []string{"short", strings.Repeat("x", 129), "has\nnewline"} {
		if _, err := svc.Guest(ctx, bad); apperr.CodeOf(err) != apperr.BadRequest {
			t.Errorf("deviceId %q: %v", bad, err)
		}
	}
}

func TestConcurrentGuestSameDevice(t *testing.T) {
	svc, _, _ := newService(t)
	var wg sync.WaitGroup
	ids := make([]string, 8)
	for i := range ids {
		wg.Add(1)
		go func(i int) {
			defer wg.Done()
			r, err := svc.Guest(context.Background(), "device-shared")
			if err != nil {
				t.Error(err)
			}
			ids[i] = r.PlayerID
		}(i)
	}
	wg.Wait()
	for _, id := range ids {
		if id != ids[0] {
			t.Fatalf("one device created several accounts: %v", ids)
		}
	}
}

func TestRegisterLoginLink(t *testing.T) {
	svc, _, _ := newService(t)
	ctx := context.Background()

	reg, err := svc.Register(ctx, "Alice_1", "password123", "  Alice  ")
	if err != nil || !reg.IsNew || reg.DisplayName != "Alice" {
		t.Fatalf("register %+v %v", reg, err)
	}
	regErrs := []struct {
		name, user, pass, display, code string
	}{
		{"taken", "alice_1", "password123", "", apperr.Conflict},
		{"short username", "ab", "password123", "", apperr.BadRequest},
		{"bad chars", "al-ice", "password123", "", apperr.BadRequest},
		{"short password", "bob_1", "1234567", "", apperr.BadRequest},
		{"bad display name", "bob_2", "password123", "x", apperr.BadRequest},
	}
	for _, tc := range regErrs {
		if _, err := svc.Register(ctx, tc.user, tc.pass, tc.display); apperr.CodeOf(err) != tc.code {
			t.Errorf("%s: %v, want %s", tc.name, err, tc.code)
		}
	}
	if r, err := svc.Register(ctx, "carol", "password123", ""); err != nil || r.DisplayName != "Driver"+r.PlayerID[2:6] {
		t.Errorf("default display name: %+v %v", r, err)
	}

	login, err := svc.Login(ctx, "alice_1", "password123")
	if err != nil || login.PlayerID != reg.PlayerID || login.IsNew || login.DisplayName != "Alice" {
		t.Fatalf("login %+v %v", login, err)
	}
	for _, tc := range []struct{ user, pass string }{
		{"alice_1", "wrong-password"}, {"nobody", "password123"}, {"", ""},
	} {
		if _, err := svc.Login(ctx, tc.user, tc.pass); apperr.CodeOf(err) != apperr.Unauthorized {
			t.Errorf("login %q: %v", tc.user, err)
		}
	}

	guest, _ := svc.Guest(ctx, "device-guest-1")
	if err := svc.Link(ctx, guest.PlayerID, "alice_1", "password123"); apperr.CodeOf(err) != apperr.Conflict {
		t.Fatalf("link taken name: %v", err)
	}
	if err := svc.Link(ctx, guest.PlayerID, "guesty", "pw"); apperr.CodeOf(err) != apperr.BadRequest {
		t.Fatalf("link short password: %v", err)
	}
	if err := svc.Link(ctx, guest.PlayerID, "guesty", "password456"); err != nil {
		t.Fatal(err)
	}
	if err := svc.Link(ctx, guest.PlayerID, "guesty2", "password456"); apperr.CodeOf(err) != apperr.Conflict {
		t.Fatalf("second link: %v", err)
	}
	l2, err := svc.Login(ctx, "guesty", "password456")
	if err != nil || l2.PlayerID != guest.PlayerID {
		t.Fatalf("login after link: %+v %v", l2, err)
	}
	// The device still logs into the same (now named) account.
	g2, err := svc.Guest(ctx, "device-guest-1")
	if err != nil || g2.PlayerID != guest.PlayerID {
		t.Fatalf("guest after link: %+v %v", g2, err)
	}
}
