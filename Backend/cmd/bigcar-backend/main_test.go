package main

import (
	"context"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"

	"github.com/comeheretosee/bigcar/backend/internal/data/datatest"
)

func env(m map[string]string) func(string) string {
	return func(k string) string { return m[k] }
}

func TestParseConfig(t *testing.T) {
	strong := strings.Repeat("s", 32)
	tests := []struct {
		name    string
		args    []string
		env     map[string]string
		wantErr string
		check   func(t *testing.T, c config)
	}{
		{"production needs secrets", nil, nil, "token-secret", nil},
		{"short server key", []string{"--token-secret", strong, "--server-key", "x"}, nil, "server-key", nil},
		{"flags", []string{"--token-secret", strong, "--server-key", "server-key-1", "--addr", ":9000", "--data-dir", "/d"}, nil, "",
			func(t *testing.T, c config) {
				if c.Addr != ":9000" || c.DataDir != "/d" || c.Dev || c.LogFormat != "json" {
					t.Errorf("%+v", c)
				}
			}},
		{"env", nil, map[string]string{
			"BIGCAR_TOKEN_SECRET": strong, "BIGCAR_SERVER_KEY": "server-key-1", "BIGCAR_DATABASE_URL": "postgres://x",
			"BIGCAR_ADDR": ":7000", "BIGCAR_DEV": "1",
		}, "", func(t *testing.T, c config) {
			if c.TokenSecret != strong || c.DatabaseURL != "postgres://x" || c.Addr != ":7000" || !c.Dev || c.LogFormat != "text" {
				t.Errorf("%+v", c)
			}
		}},
		{"flag overrides env", []string{"--addr", ":1"}, map[string]string{"BIGCAR_ADDR": ":2", "BIGCAR_DEV": "true"}, "",
			func(t *testing.T, c config) {
				if c.Addr != ":1" {
					t.Errorf("addr %s", c.Addr)
				}
			}},
		{"dev defaults", []string{"--dev"}, nil, "", func(t *testing.T, c config) {
			if c.TokenSecret == "" || c.ServerKey == "" || c.WebhookSecret == "" || c.DataDir != "../Content/BigCar/Data" {
				t.Errorf("%+v", c)
			}
		}},
		{"stray args", []string{"--dev", "extra"}, nil, "unexpected arguments", nil},
	}
	for _, tc := range tests {
		t.Run(tc.name, func(t *testing.T) {
			c, err := parseConfig(tc.args, env(tc.env), io.Discard)
			if tc.wantErr != "" {
				if err == nil || !strings.Contains(err.Error(), tc.wantErr) {
					t.Fatalf("err = %v, want %q", err, tc.wantErr)
				}
				return
			}
			if err != nil {
				t.Fatal(err)
			}
			tc.check(t, c)
		})
	}
}

func TestBuildServesHealthAndMeta(t *testing.T) {
	c, err := parseConfig([]string{"--dev", "--data-dir", datatest.Dir()}, env(nil), io.Discard)
	if err != nil {
		t.Fatal(err)
	}
	a, err := build(context.Background(), c, newLogger(config{LogLevel: "error", LogFormat: "text"}, io.Discard))
	if err != nil {
		t.Fatal(err)
	}
	defer a.store.Close()
	srv := httptest.NewServer(a.handler)
	defer srv.Close()
	for _, path := range []string{"/healthz", "/v1/meta", "/v1/seasons/current"} {
		resp, err := http.Get(srv.URL + path)
		if err != nil {
			t.Fatal(err)
		}
		resp.Body.Close()
		if resp.StatusCode != http.StatusOK {
			t.Fatalf("%s: %s", path, resp.Status)
		}
	}
	addr := strings.TrimPrefix(srv.URL, "http://")
	if err := healthCheck(addr); err != nil {
		t.Fatalf("healthcheck: %v", err)
	}
	if _, err := build(context.Background(), config{DataDir: "/nonexistent"}, newLogger(config{LogLevel: "error"}, io.Discard)); err == nil {
		t.Fatal("missing data dir must fail")
	}
}
