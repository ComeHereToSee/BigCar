// Package payments abstracts real-money payment providers.
//
// The store never trusts the client for real-money purchases: gold is only
// credited when a provider reports a completed payment, either synchronously
// from Checkout (sandbox) or through a signed webhook. Production providers
// (Steam MicroTxn, Epic, WeChat Pay, Alipay, Apple/Google) implement Provider.
package payments

import (
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"net/http"
	"sort"
	"strings"
)

// Status of a payment.
type Status string

// Payment statuses.
const (
	StatusPending   Status = "pending"
	StatusCompleted Status = "completed"
	StatusFailed    Status = "failed"
)

// CheckoutRequest describes an order to pay.
type CheckoutRequest struct {
	OrderID  string
	PlayerID string
	PackID   string
	Gold     int64
	Prices   map[string]float64 // currency code → amount
}

// CheckoutResult is the provider's answer to a checkout.
type CheckoutResult struct {
	Status      Status
	RedirectURL string // where the client completes the payment (empty for sandbox)
	ExternalID  string // provider transaction id
}

// WebhookEvent is a verified payment notification.
type WebhookEvent struct {
	OrderID    string
	ExternalID string
	Status     Status
}

// Provider is a payment provider.
type Provider interface {
	Name() string
	Checkout(ctx context.Context, req CheckoutRequest) (CheckoutResult, error)
	// ParseWebhook authenticates and decodes a webhook request body.
	ParseWebhook(header http.Header, body []byte) (WebhookEvent, error)
}

// Errors.
var (
	ErrBadSignature = errors.New("payments: bad webhook signature")
	ErrBadPayload   = errors.New("payments: bad webhook payload")
)

// SignatureHeader carries hex(hmac_sha256(secret, body)).
const SignatureHeader = "X-Signature"

// Sign returns hex(hmac_sha256(secret, body)).
func Sign(secret, body []byte) string {
	m := hmac.New(sha256.New, secret)
	m.Write(body)
	return hex.EncodeToString(m.Sum(nil))
}

// VerifySignature checks a hex HMAC-SHA256 signature in constant time. An
// empty secret never verifies.
func VerifySignature(secret, body []byte, sigHex string) bool {
	if len(secret) == 0 {
		return false
	}
	got, err := hex.DecodeString(strings.TrimSpace(sigHex))
	if err != nil {
		return false
	}
	m := hmac.New(sha256.New, secret)
	m.Write(body)
	return hmac.Equal(got, m.Sum(nil))
}

// Registry holds the enabled providers.
type Registry struct {
	providers map[string]Provider
}

// NewRegistry creates a registry with the given providers.
func NewRegistry(ps ...Provider) *Registry {
	r := &Registry{providers: map[string]Provider{}}
	for _, p := range ps {
		r.providers[p.Name()] = p
	}
	return r
}

// Get returns a provider by name.
func (r *Registry) Get(name string) (Provider, bool) {
	p, ok := r.providers[name]
	return p, ok
}

// Names lists the enabled providers.
func (r *Registry) Names() []string {
	out := make([]string, 0, len(r.providers))
	for n := range r.providers {
		out = append(out, n)
	}
	sort.Strings(out)
	return out
}

// Sandbox is the development provider: checkouts complete immediately. Its
// webhook accepts {"orderId", "status", "externalId"} signed with the webhook
// secret, which lets tests and tools exercise the asynchronous path too.
// It must only be enabled with --dev.
type Sandbox struct {
	WebhookSecret []byte
}

// Name implements Provider.
func (s *Sandbox) Name() string { return "sandbox" }

// Checkout implements Provider: always completed.
func (s *Sandbox) Checkout(ctx context.Context, req CheckoutRequest) (CheckoutResult, error) {
	return CheckoutResult{Status: StatusCompleted, ExternalID: "sbx_" + req.OrderID}, nil
}

// ParseWebhook implements Provider.
func (s *Sandbox) ParseWebhook(header http.Header, body []byte) (WebhookEvent, error) {
	if !VerifySignature(s.WebhookSecret, body, header.Get(SignatureHeader)) {
		return WebhookEvent{}, ErrBadSignature
	}
	var p struct {
		OrderID    string `json:"orderId"`
		Status     Status `json:"status"`
		ExternalID string `json:"externalId"`
	}
	if err := json.Unmarshal(body, &p); err != nil || p.OrderID == "" {
		return WebhookEvent{}, ErrBadPayload
	}
	if p.Status != StatusCompleted && p.Status != StatusFailed {
		return WebhookEvent{}, ErrBadPayload
	}
	return WebhookEvent{OrderID: p.OrderID, ExternalID: p.ExternalID, Status: p.Status}, nil
}
