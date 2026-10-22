package payments

import (
	"context"
	"errors"
	"net/http"
	"testing"
)

func TestSignatures(t *testing.T) {
	secret := []byte("whsec")
	body := []byte(`{"orderId":"o_1","status":"completed"}`)
	sig := Sign(secret, body)
	tests := []struct {
		name   string
		secret []byte
		body   []byte
		sig    string
		want   bool
	}{
		{"valid", secret, body, sig, true},
		{"valid with whitespace", secret, body, " " + sig + "\n", true},
		{"wrong secret", []byte("other"), body, sig, false},
		{"modified body", secret, []byte(`{"orderId":"o_2","status":"completed"}`), sig, false},
		{"not hex", secret, body, "zz", false},
		{"empty sig", secret, body, "", false},
		{"empty secret", nil, body, Sign(nil, body), false},
	}
	for _, tc := range tests {
		if got := VerifySignature(tc.secret, tc.body, tc.sig); got != tc.want {
			t.Errorf("%s: got %v", tc.name, got)
		}
	}
}

func TestSandbox(t *testing.T) {
	s := &Sandbox{WebhookSecret: []byte("whsec")}
	res, err := s.Checkout(context.Background(), CheckoutRequest{OrderID: "o_1"})
	if err != nil || res.Status != StatusCompleted || res.ExternalID != "sbx_o_1" {
		t.Fatalf("checkout %+v %v", res, err)
	}
	body := []byte(`{"orderId":"o_1","status":"completed","externalId":"x"}`)
	h := http.Header{}
	h.Set(SignatureHeader, Sign([]byte("whsec"), body))
	ev, err := s.ParseWebhook(h, body)
	if err != nil || ev.OrderID != "o_1" || ev.Status != StatusCompleted || ev.ExternalID != "x" {
		t.Fatalf("webhook %+v %v", ev, err)
	}
	h.Set(SignatureHeader, Sign([]byte("nope"), body))
	if _, err := s.ParseWebhook(h, body); !errors.Is(err, ErrBadSignature) {
		t.Fatalf("bad signature: %v", err)
	}
	bad := []byte(`{"orderId":"o_1","status":"refunded"}`)
	h.Set(SignatureHeader, Sign([]byte("whsec"), bad))
	if _, err := s.ParseWebhook(h, bad); !errors.Is(err, ErrBadPayload) {
		t.Fatalf("bad status: %v", err)
	}
	r := NewRegistry(s)
	if _, ok := r.Get("sandbox"); !ok || len(r.Names()) != 1 {
		t.Fatal("registry")
	}
	if _, ok := r.Get("steam"); ok {
		t.Fatal("unexpected provider")
	}
}
