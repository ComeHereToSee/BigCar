// Package httpapi exposes the backend over HTTP/JSON exactly as described in
// docs/BACKEND_API.md.
package httpapi

import (
	"bytes"
	"context"
	"crypto/rand"
	"crypto/subtle"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"log/slog"
	"net"
	"net/http"
	"runtime/debug"
	"strings"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/auth"
	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/ladder"
	"github.com/comeheretosee/bigcar/backend/internal/matchmaking"
	"github.com/comeheretosee/bigcar/backend/internal/party"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
	"github.com/comeheretosee/bigcar/backend/internal/servers"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
	"github.com/comeheretosee/bigcar/backend/internal/store"
)

// APIVersion is reported by GET /v1/meta.
const APIVersion = 1

// Body size limits.
const (
	maxBodyBytes       = 64 << 10 // regular requests
	maxResultBodyBytes = 1 << 20  // match results (many players and impacts)
)

// Rate limits from docs/BACKEND_API.md.
const (
	playerRate  = 20
	playerBurst = 40
	authRate    = 5
	authBurst   = 10
)

// Deps are the API's dependencies.
type Deps struct {
	Data        *data.GameData
	Store       storage.Store
	Auth        *auth.Service
	Shop        *store.Service
	Parties     *party.Manager
	Matchmaker  *matchmaking.Service
	Registry    *servers.Registry
	Results     *servers.ResultProcessor
	Ladder      *ladder.Service
	ServerKey   string
	Dev         bool // permissive CORS
	TrustProxy  bool // take the client IP from X-Forwarded-For
	Log         *slog.Logger
	Now         func() time.Time
	PlayerLimit *Limiter // nil = 20 req/s, burst 40
	AuthLimit   *Limiter // nil = 5 req/s, burst 10
}

// API is the HTTP API.
type API struct {
	Deps
}

// New creates the API.
func New(d Deps) *API {
	if d.Now == nil {
		d.Now = time.Now
	}
	if d.Log == nil {
		d.Log = slog.Default()
	}
	if d.PlayerLimit == nil {
		d.PlayerLimit = NewLimiter(playerRate, playerBurst, d.Now)
	}
	if d.AuthLimit == nil {
		d.AuthLimit = NewLimiter(authRate, authBurst, d.Now)
	}
	return &API{Deps: d}
}

// handlerFunc is an HTTP handler that returns an error to be rendered.
type handlerFunc func(w http.ResponseWriter, r *http.Request) error

// Handler returns the root handler with all middleware.
func (a *API) Handler() http.Handler {
	mux := http.NewServeMux()
	public := func(h handlerFunc) http.Handler { return a.limitIP(a.PlayerLimit, a.wrap(h)) }
	authEP := func(h handlerFunc) http.Handler { return a.limitIP(a.AuthLimit, a.wrap(h)) }
	player := func(h handlerFunc) http.Handler { return a.requireAuth(a.limitPlayer(a.wrap(h))) }
	server := func(h handlerFunc) http.Handler { return a.requireServerKey(a.wrap(h)) }

	mux.Handle("GET /healthz", a.wrap(a.healthz))
	mux.Handle("GET /v1/meta", public(a.meta))

	mux.Handle("POST /v1/auth/guest", authEP(a.authGuest))
	mux.Handle("POST /v1/auth/register", authEP(a.authRegister))
	mux.Handle("POST /v1/auth/login", authEP(a.authLogin))
	mux.Handle("POST /v1/auth/link", a.limitIP(a.AuthLimit, player(a.authLink)))

	mux.Handle("GET /v1/profile", player(a.getProfile))
	mux.Handle("POST /v1/profile/name", player(a.setName))
	mux.Handle("POST /v1/garage/buy-vehicle", player(a.buyVehicle))
	mux.Handle("POST /v1/garage/select", player(a.selectVehicle))
	mux.Handle("POST /v1/garage/upgrade", player(a.upgrade))
	mux.Handle("POST /v1/garage/buy-equipment", player(a.buyEquipment))
	mux.Handle("POST /v1/garage/equip", player(a.equip))
	mux.Handle("POST /v1/garage/buy-cosmetic", player(a.buyCosmetic))
	mux.Handle("POST /v1/garage/customize", player(a.customize))

	mux.Handle("GET /v1/store", player(a.getStore))
	mux.Handle("POST /v1/store/purchase", player(a.purchase))
	mux.Handle("POST /v1/store/gold/checkout", player(a.goldCheckout))
	mux.Handle("POST /v1/payments/webhook/{provider}", public(a.paymentWebhook))
	mux.Handle("POST /v1/seasonpass/buy-premium", player(a.buyPremium))
	mux.Handle("POST /v1/seasonpass/claim", player(a.claimSeasonPass))

	mux.Handle("GET /v1/party", player(a.getParty))
	mux.Handle("POST /v1/party", player(a.createParty))
	mux.Handle("POST /v1/party/join", player(a.joinParty))
	mux.Handle("POST /v1/party/leave", player(a.leaveParty))
	mux.Handle("POST /v1/party/ready", player(a.readyParty))
	mux.Handle("POST /v1/party/kick", player(a.kickParty))

	mux.Handle("POST /v1/matchmaking/tickets", player(a.createTicket))
	mux.Handle("GET /v1/matchmaking/current", player(a.currentTicket))
	mux.Handle("DELETE /v1/matchmaking/tickets/{ticketId}", player(a.cancelTicket))

	mux.Handle("GET /v1/ladder/{queue}", public(a.ladderPage))
	mux.Handle("GET /v1/ladder/{queue}/me", player(a.ladderMe))
	mux.Handle("GET /v1/seasons/current", public(a.currentSeason))

	mux.Handle("POST /v1/servers/heartbeat", server(a.serverHeartbeat))
	mux.Handle("POST /v1/servers/match-start", server(a.serverMatchStart))
	mux.Handle("POST /v1/servers/match-result", server(a.serverMatchResult))
	mux.Handle("POST /v1/servers/match-abort", server(a.serverMatchAbort))

	mux.Handle("/", a.wrap(func(w http.ResponseWriter, r *http.Request) error {
		return apperr.New(apperr.NotFound, "no route for %s %s", r.Method, r.URL.Path)
	}))

	var h http.Handler = mux
	h = a.cors(h)
	h = a.recoverPanics(h)
	h = a.logRequests(h)
	return h
}

// --- JSON helpers ---

func writeJSON(w http.ResponseWriter, status int, v any) {
	b, err := json.Marshal(v)
	if err != nil {
		status, b = http.StatusInternalServerError, []byte(`{"error":{"code":"internal","message":"encoding failed"}}`)
	}
	writeRaw(w, status, b)
}

func writeRaw(w http.ResponseWriter, status int, body []byte) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	_, _ = w.Write(body)
	_, _ = w.Write([]byte("\n"))
}

type errorBody struct {
	Error struct {
		Code    string `json:"code"`
		Message string `json:"message"`
	} `json:"error"`
}

func (a *API) writeError(w http.ResponseWriter, r *http.Request, err error) {
	var body errorBody
	ae, ok := apperr.As(err)
	switch {
	case ok:
		body.Error.Code, body.Error.Message = ae.Code, ae.Message
	case errors.Is(err, storage.ErrNotFound):
		body.Error.Code, body.Error.Message = apperr.NotFound, "not found"
	case errors.Is(err, context.Canceled) || errors.Is(err, context.DeadlineExceeded):
		body.Error.Code, body.Error.Message = apperr.Internal, "request cancelled"
	default:
		a.Log.Error("internal error", "method", r.Method, "path", r.URL.Path, "err", err)
		body.Error.Code, body.Error.Message = apperr.Internal, "internal server error"
	}
	if body.Error.Code == apperr.RateLimited {
		w.Header().Set("Retry-After", "1")
	}
	writeJSON(w, apperr.StatusFor(body.Error.Code), body)
}

func (a *API) wrap(h handlerFunc) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if err := h(w, r); err != nil {
			a.writeError(w, r, err)
		}
	})
}

// readBody reads the request body up to limit bytes.
func readBody(r *http.Request, limit int64) ([]byte, error) {
	if r.Body == nil {
		return nil, nil
	}
	b, err := io.ReadAll(io.LimitReader(r.Body, limit+1))
	if err != nil {
		return nil, apperr.New(apperr.BadRequest, "could not read request body")
	}
	if int64(len(b)) > limit {
		return nil, apperr.New(apperr.BadRequest, "request body larger than %d bytes", limit)
	}
	return b, nil
}

// decodeBody decodes a JSON object body. Unknown fields are ignored for
// forward compatibility. An empty body decodes as {} when allowEmpty.
func decodeBody(body []byte, v any, allowEmpty bool) error {
	if len(bytes.TrimSpace(body)) == 0 {
		if allowEmpty {
			return nil
		}
		return apperr.New(apperr.BadRequest, "request body must be a JSON object")
	}
	dec := json.NewDecoder(bytes.NewReader(body))
	if err := dec.Decode(v); err != nil {
		return apperr.New(apperr.BadRequest, "invalid JSON body: %v", err)
	}
	if dec.More() {
		return apperr.New(apperr.BadRequest, "unexpected data after JSON body")
	}
	return nil
}

// readJSON reads and decodes the body, returning the raw bytes too (used for
// idempotency fingerprints).
func readJSON(r *http.Request, v any) ([]byte, error) {
	b, err := readBody(r, maxBodyBytes)
	if err != nil {
		return nil, err
	}
	return b, decodeBody(b, v, false)
}

func required(name, value string) error {
	if strings.TrimSpace(value) == "" {
		return apperr.New(apperr.BadRequest, "%s is required", name)
	}
	return nil
}

// --- middleware ---

type ctxKey int

const playerKey ctxKey = 1

func playerID(r *http.Request) string {
	pid, _ := r.Context().Value(playerKey).(string)
	return pid
}

func (a *API) requireAuth(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		h := r.Header.Get("Authorization")
		tok, ok := strings.CutPrefix(h, "Bearer ")
		if !ok || tok == "" {
			a.writeError(w, r, apperr.New(apperr.Unauthorized, "missing bearer token"))
			return
		}
		pid, err := a.Auth.Signer().Verify(strings.TrimSpace(tok))
		if errors.Is(err, auth.ErrExpiredToken) {
			a.writeError(w, r, apperr.New(apperr.Unauthorized, "token expired"))
			return
		}
		if err != nil {
			a.writeError(w, r, apperr.New(apperr.Unauthorized, "invalid token"))
			return
		}
		if lw, ok := w.(*logWriter); ok {
			lw.player = pid
		}
		next.ServeHTTP(w, r.WithContext(context.WithValue(r.Context(), playerKey, pid)))
	})
}

func (a *API) requireServerKey(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		got := r.Header.Get("X-Server-Key")
		if a.ServerKey == "" || subtle.ConstantTimeCompare([]byte(got), []byte(a.ServerKey)) != 1 {
			a.writeError(w, r, apperr.New(apperr.Unauthorized, "invalid server key"))
			return
		}
		next.ServeHTTP(w, r)
	})
}

// clientIP returns the caller's IP. With TrustProxy the last X-Forwarded-For
// entry (the address seen by our own reverse proxy) is used.
func (a *API) clientIP(r *http.Request) string {
	if a.TrustProxy {
		if xff := r.Header.Get("X-Forwarded-For"); xff != "" {
			parts := strings.Split(xff, ",")
			if ip := strings.TrimSpace(parts[len(parts)-1]); ip != "" {
				return ip
			}
		}
	}
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err != nil {
		return r.RemoteAddr
	}
	return host
}

func (a *API) limitIP(l *Limiter, next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !l.Allow("ip:" + a.clientIP(r)) {
			a.writeError(w, r, apperr.New(apperr.RateLimited, "too many requests"))
			return
		}
		next.ServeHTTP(w, r)
	})
}

func (a *API) limitPlayer(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if !a.PlayerLimit.Allow("player:" + playerID(r)) {
			a.writeError(w, r, apperr.New(apperr.RateLimited, "too many requests"))
			return
		}
		next.ServeHTTP(w, r)
	})
}

func (a *API) cors(next http.Handler) http.Handler {
	if !a.Dev {
		return next
	}
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		h := w.Header()
		h.Set("Access-Control-Allow-Origin", "*")
		h.Set("Access-Control-Allow-Methods", "GET, POST, DELETE, OPTIONS")
		h.Set("Access-Control-Allow-Headers", "Authorization, Content-Type, Idempotency-Key, X-Server-Key, X-Server-Id, X-Signature")
		h.Set("Access-Control-Max-Age", "600")
		if r.Method == http.MethodOptions {
			w.WriteHeader(http.StatusNoContent)
			return
		}
		next.ServeHTTP(w, r)
	})
}

func (a *API) recoverPanics(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		defer func() {
			if v := recover(); v != nil {
				if v == http.ErrAbortHandler {
					panic(v)
				}
				a.Log.Error("panic", "method", r.Method, "path", r.URL.Path, "panic", fmt.Sprint(v), "stack", string(debug.Stack()))
				a.writeError(w, r, apperr.New(apperr.Internal, "internal server error"))
			}
		}()
		next.ServeHTTP(w, r)
	})
}

type logWriter struct {
	http.ResponseWriter
	status int
	bytes  int
	player string
}

func (w *logWriter) WriteHeader(status int) {
	if w.status == 0 {
		w.status = status
	}
	w.ResponseWriter.WriteHeader(status)
}

func (w *logWriter) Write(b []byte) (int, error) {
	if w.status == 0 {
		w.status = http.StatusOK
	}
	n, err := w.ResponseWriter.Write(b)
	w.bytes += n
	return n, err
}

func newRequestID() string {
	b := make([]byte, 8)
	_, _ = rand.Read(b)
	return hex.EncodeToString(b)
}

func (a *API) logRequests(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		start := time.Now()
		id := newRequestID()
		w.Header().Set("X-Request-Id", id)
		w.Header().Set("X-Content-Type-Options", "nosniff")
		lw := &logWriter{ResponseWriter: w}
		next.ServeHTTP(lw, r)
		level := slog.LevelInfo
		switch {
		case lw.status >= 500:
			level = slog.LevelError
		case r.URL.Path == "/healthz" || r.URL.Path == "/v1/servers/heartbeat":
			level = slog.LevelDebug
		}
		a.Log.Log(r.Context(), level, "request", "id", id, "method", r.Method, "route", r.Pattern, "path", r.URL.Path,
			"status", lw.status, "bytes", lw.bytes, "durationMs", time.Since(start).Milliseconds(),
			"ip", a.clientIP(r), "player", lw.player)
	})
}

// --- shared profile helpers ---

// loadProfile reads the player's profile, persisting it first if Normalize
// changed it (season rollover, new starter cars, ...).
func (a *API) loadProfile(ctx context.Context, pid string) (*profile.Profile, error) {
	p, err := storage.ReadProfile(ctx, a.Store, pid)
	if errors.Is(err, storage.ErrNotFound) {
		return nil, apperr.New(apperr.NotFound, "profile not found")
	}
	if err != nil {
		return nil, err
	}
	if !profile.Normalize(p, a.Data, a.Now()) {
		return p, nil
	}
	var out *profile.Profile
	_, err = storage.MutateProfile(ctx, a.Store, pid, nil, a.Now, func(tx storage.Tx, q *profile.Profile) (any, error) {
		profile.Normalize(q, a.Data, a.Now())
		out = q
		return nil, nil
	})
	return out, err
}

// mutate runs fn on the locked, normalized profile and writes the JSON it
// returns. The Idempotency-Key header makes the call replay-safe.
func (a *API) mutate(w http.ResponseWriter, r *http.Request, body []byte,
	fn func(tx storage.Tx, p *profile.Profile) (any, error)) error {
	var idem *storage.Idempotency
	if key := r.Header.Get("Idempotency-Key"); key != "" {
		idem = &storage.Idempotency{Key: key, RequestHash: storage.HashRequest(r.Method, r.URL.Path, body)}
	}
	resp, err := storage.MutateProfile(r.Context(), a.Store, playerID(r), idem, a.Now,
		func(tx storage.Tx, p *profile.Profile) (any, error) {
			profile.Normalize(p, a.Data, a.Now())
			return fn(tx, p)
		})
	if err != nil {
		return err
	}
	if resp.Replayed {
		w.Header().Set("Idempotent-Replayed", "true")
	}
	writeRaw(w, resp.Status, resp.Body)
	return nil
}

type profileResponse struct {
	Profile *profile.Profile `json:"profile"`
}

type grantResponse struct {
	Profile *profile.Profile `json:"profile"`
	Granted []data.Item      `json:"granted"`
}
