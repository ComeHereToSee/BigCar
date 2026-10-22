// Command bigcar-backend runs the BigCar online backend (accounts, garage,
// store, parties, matchmaking, ranked ladder, dedicated-server allocation).
//
// Configuration comes from flags, each with an environment variable fallback;
// see README.md.
package main

import (
	"context"
	"errors"
	"flag"
	"fmt"
	"io"
	"log/slog"
	"net"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/auth"
	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/httpapi"
	"github.com/comeheretosee/bigcar/backend/internal/ladder"
	"github.com/comeheretosee/bigcar/backend/internal/matchmaking"
	"github.com/comeheretosee/bigcar/backend/internal/party"
	"github.com/comeheretosee/bigcar/backend/internal/payments"
	"github.com/comeheretosee/bigcar/backend/internal/servers"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
	"github.com/comeheretosee/bigcar/backend/internal/store"
)

// Development defaults, used only with --dev when a secret is not configured.
const (
	devTokenSecret   = "dev-token-secret-do-not-use-in-production"
	devServerKey     = "dev-server-key"
	devWebhookSecret = "dev-webhook-secret"
)

type config struct {
	Addr          string
	DataDir       string
	DatabaseURL   string
	TokenSecret   string
	ServerKey     string
	WebhookSecret string
	Dev           bool
	TrustProxy    bool
	LogLevel      string
	LogFormat     string
	HealthCheck   bool
}

func envBool(getenv func(string) string, key string) bool {
	switch strings.ToLower(getenv(key)) {
	case "1", "true", "yes", "on":
		return true
	}
	return false
}

func envOr(getenv func(string) string, key, def string) string {
	if v := getenv(key); v != "" {
		return v
	}
	return def
}

// parseConfig reads flags; every flag defaults to its environment variable.
func parseConfig(args []string, getenv func(string) string, stderr io.Writer) (config, error) {
	var c config
	fs := flag.NewFlagSet("bigcar-backend", flag.ContinueOnError)
	fs.SetOutput(stderr)
	fs.StringVar(&c.Addr, "addr", envOr(getenv, "BIGCAR_ADDR", ":8080"), "listen address (BIGCAR_ADDR)")
	fs.StringVar(&c.DataDir, "data-dir", envOr(getenv, "BIGCAR_DATA_DIR", "../Content/BigCar/Data"), "game data directory (BIGCAR_DATA_DIR)")
	fs.StringVar(&c.DatabaseURL, "database-url", getenv("BIGCAR_DATABASE_URL"), "Postgres URL; empty = in-memory (BIGCAR_DATABASE_URL)")
	fs.StringVar(&c.TokenSecret, "token-secret", getenv("BIGCAR_TOKEN_SECRET"), "HMAC secret for player tokens (BIGCAR_TOKEN_SECRET)")
	fs.StringVar(&c.ServerKey, "server-key", getenv("BIGCAR_SERVER_KEY"), "shared secret of dedicated servers (BIGCAR_SERVER_KEY)")
	fs.StringVar(&c.WebhookSecret, "webhook-secret", getenv("BIGCAR_WEBHOOK_SECRET"), "payment webhook HMAC secret (BIGCAR_WEBHOOK_SECRET)")
	fs.BoolVar(&c.Dev, "dev", envBool(getenv, "BIGCAR_DEV"), "development mode: sandbox payments, permissive CORS (BIGCAR_DEV=1)")
	fs.BoolVar(&c.TrustProxy, "trust-proxy", envBool(getenv, "BIGCAR_TRUST_PROXY"), "use X-Forwarded-For for client IPs (BIGCAR_TRUST_PROXY=1)")
	fs.StringVar(&c.LogLevel, "log-level", envOr(getenv, "BIGCAR_LOG_LEVEL", "info"), "debug, info, warn or error (BIGCAR_LOG_LEVEL)")
	fs.StringVar(&c.LogFormat, "log-format", envOr(getenv, "BIGCAR_LOG_FORMAT", ""), "json or text; default json, text with --dev (BIGCAR_LOG_FORMAT)")
	fs.BoolVar(&c.HealthCheck, "healthcheck", false, "probe GET /healthz on --addr and exit (for container health checks)")
	if err := fs.Parse(args); err != nil {
		return c, err
	}
	if fs.NArg() > 0 {
		return c, fmt.Errorf("unexpected arguments: %v", fs.Args())
	}
	if c.LogFormat == "" {
		c.LogFormat = "json"
		if c.Dev {
			c.LogFormat = "text"
		}
	}
	if c.HealthCheck {
		return c, nil
	}
	if c.Dev {
		if c.TokenSecret == "" {
			c.TokenSecret = devTokenSecret
		}
		if c.ServerKey == "" {
			c.ServerKey = devServerKey
		}
		if c.WebhookSecret == "" {
			c.WebhookSecret = devWebhookSecret
		}
	}
	switch {
	case len(c.TokenSecret) < 32:
		return c, errors.New("--token-secret / BIGCAR_TOKEN_SECRET must be at least 32 characters")
	case len(c.ServerKey) < 8:
		return c, errors.New("--server-key / BIGCAR_SERVER_KEY must be at least 8 characters")
	}
	return c, nil
}

func newLogger(c config, w io.Writer) *slog.Logger {
	var level slog.Level
	if err := level.UnmarshalText([]byte(c.LogLevel)); err != nil {
		level = slog.LevelInfo
	}
	opts := &slog.HandlerOptions{Level: level}
	if c.LogFormat == "text" {
		return slog.New(slog.NewTextHandler(w, opts))
	}
	return slog.New(slog.NewJSONHandler(w, opts))
}

// app holds the wired services.
type app struct {
	store    storage.Store
	registry *servers.Registry
	mm       *matchmaking.Service
	handler  http.Handler
}

func build(ctx context.Context, c config, log *slog.Logger) (*app, error) {
	gd, err := data.Load(c.DataDir)
	if err != nil {
		return nil, err
	}
	log.Info("game data loaded", "dir", c.DataDir, "hash", gd.Hash, "vehicles", len(gd.Vehicles),
		"offers", len(gd.Store.Offers), "season", gd.Store.SeasonPass.ID)

	var st storage.Store
	if c.DatabaseURL == "" {
		log.Warn("no --database-url: using the in-memory store (all data is lost on restart)")
		st = storage.NewMemory()
	} else {
		pg, err := storage.OpenPostgres(ctx, c.DatabaseURL)
		if err != nil {
			return nil, err
		}
		log.Info("connected to postgres, migrations applied")
		st = pg
	}

	authSvc, err := auth.NewService(st, gd, auth.NewSigner([]byte(c.TokenSecret), auth.TokenTTL, nil), auth.Hasher{}, nil)
	if err != nil {
		st.Close()
		return nil, err
	}
	var providers []payments.Provider
	if c.Dev {
		providers = append(providers, &payments.Sandbox{WebhookSecret: []byte(c.WebhookSecret)})
		log.Warn("development mode: sandbox payments and permissive CORS are enabled")
	}
	shop := store.NewService(gd, st, payments.NewRegistry(providers...), nil, log)
	parties := party.NewManager(nil)
	registry := servers.NewRegistry(nil, log)
	mm, err := matchmaking.New(matchmaking.Config{Data: gd, Store: st, Parties: parties, Allocator: registry, Log: log})
	if err != nil {
		st.Close()
		return nil, err
	}
	registry.SetListener(mm)
	api := httpapi.New(httpapi.Deps{
		Data: gd, Store: st, Auth: authSvc, Shop: shop, Parties: parties, Matchmaker: mm, Registry: registry,
		Results:   servers.NewResultProcessor(registry, st, gd, nil, log),
		Ladder:    ladder.NewService(st, gd, matchmaking.RankedQueueIDs()),
		ServerKey: c.ServerKey, Dev: c.Dev, TrustProxy: c.TrustProxy, Log: log,
	})
	return &app{store: st, registry: registry, mm: mm, handler: api.Handler()}, nil
}

// runLoops runs the 1 s matchmaker/server sweep and the hourly idempotency
// cleanup until ctx is cancelled.
func (a *app) runLoops(ctx context.Context, log *slog.Logger) {
	tick := time.NewTicker(time.Second)
	defer tick.Stop()
	purge := time.NewTicker(time.Hour)
	defer purge.Stop()
	for {
		select {
		case <-ctx.Done():
			return
		case now := <-tick.C:
			a.registry.Sweep(now)
			a.mm.Tick(now)
		case now := <-purge.C:
			n, err := a.store.PurgeIdempotency(ctx, now.Add(-24*time.Hour))
			if err != nil {
				log.Error("idempotency purge failed", "err", err)
			} else if n > 0 {
				log.Info("purged idempotency records", "count", n)
			}
		}
	}
}

func healthCheck(addr string) error {
	host, port, err := net.SplitHostPort(addr)
	if err != nil {
		return err
	}
	if host == "" || host == "0.0.0.0" || host == "::" {
		host = "127.0.0.1"
	}
	client := &http.Client{Timeout: 3 * time.Second}
	resp, err := client.Get("http://" + net.JoinHostPort(host, port) + "/healthz")
	if err != nil {
		return err
	}
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		return fmt.Errorf("healthz returned %s", resp.Status)
	}
	return nil
}

func run(args []string, getenv func(string) string, stdout, stderr io.Writer) error {
	c, err := parseConfig(args, getenv, stderr)
	if err != nil {
		return err
	}
	if c.HealthCheck {
		return healthCheck(c.Addr)
	}
	log := newLogger(c, stdout)
	slog.SetDefault(log)

	ctx, stop := signal.NotifyContext(context.Background(), os.Interrupt, syscall.SIGTERM)
	defer stop()

	a, err := build(ctx, c, log)
	if err != nil {
		return err
	}
	defer a.store.Close()

	srv := &http.Server{
		Addr:              c.Addr,
		Handler:           a.handler,
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       15 * time.Second,
		WriteTimeout:      30 * time.Second,
		IdleTimeout:       120 * time.Second,
		MaxHeaderBytes:    32 << 10,
		ErrorLog:          slog.NewLogLogger(log.Handler(), slog.LevelWarn),
	}
	loopCtx, stopLoops := context.WithCancel(context.Background())
	loopsDone := make(chan struct{})
	go func() {
		defer close(loopsDone)
		a.runLoops(loopCtx, log)
	}()

	serveErr := make(chan error, 1)
	go func() {
		log.Info("listening", "addr", c.Addr, "dev", c.Dev)
		serveErr <- srv.ListenAndServe()
	}()

	select {
	case err := <-serveErr:
		stopLoops()
		<-loopsDone
		if !errors.Is(err, http.ErrServerClosed) {
			return err
		}
		return nil
	case <-ctx.Done():
	}
	log.Info("shutting down")
	shutdownCtx, cancel := context.WithTimeout(context.Background(), 15*time.Second)
	defer cancel()
	err = srv.Shutdown(shutdownCtx)
	stopLoops()
	<-loopsDone
	if err != nil {
		return fmt.Errorf("shutdown: %w", err)
	}
	log.Info("stopped")
	return nil
}

func main() {
	if err := run(os.Args[1:], os.Getenv, os.Stdout, os.Stderr); err != nil {
		if errors.Is(err, flag.ErrHelp) {
			return
		}
		fmt.Fprintln(os.Stderr, "bigcar-backend:", err)
		os.Exit(1)
	}
}
