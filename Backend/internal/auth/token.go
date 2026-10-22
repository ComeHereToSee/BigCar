// Package auth implements player authentication: HMAC-signed bearer tokens,
// PBKDF2 password hashing and the guest/register/login/link flows.
package auth

import (
	"crypto/hmac"
	"crypto/sha256"
	"encoding/base64"
	"encoding/json"
	"errors"
	"strings"
	"time"
)

// TokenTTL is the lifetime of a player token.
const TokenTTL = 7 * 24 * time.Hour

// Token errors.
var (
	ErrInvalidToken = errors.New("auth: invalid token")
	ErrExpiredToken = errors.New("auth: token expired")
)

var b64 = base64.RawURLEncoding

type tokenPayload struct {
	PID string `json:"pid"`
	Exp int64  `json:"exp"`
}

// Signer issues and verifies tokens of the form
//
//	v1.<b64url(payload)>.<b64url(sig)>
//
// where payload is {"pid": "...", "exp": unix seconds}, b64url is unpadded
// base64url and sig = HMAC-SHA256(secret, "v1." + b64url(payload)).
type Signer struct {
	secret []byte
	ttl    time.Duration
	now    func() time.Time
}

// NewSigner creates a signer. now may be nil (time.Now).
func NewSigner(secret []byte, ttl time.Duration, now func() time.Time) *Signer {
	if now == nil {
		now = time.Now
	}
	return &Signer{secret: append([]byte(nil), secret...), ttl: ttl, now: now}
}

func (s *Signer) mac(input string) []byte {
	m := hmac.New(sha256.New, s.secret)
	m.Write([]byte(input))
	return m.Sum(nil)
}

// Sign issues a token for playerID and returns it with its expiry.
func (s *Signer) Sign(playerID string) (string, time.Time) {
	exp := s.now().Add(s.ttl).Truncate(time.Second)
	payload, _ := json.Marshal(tokenPayload{PID: playerID, Exp: exp.Unix()})
	input := "v1." + b64.EncodeToString(payload)
	return input + "." + b64.EncodeToString(s.mac(input)), exp
}

// Verify checks the signature and expiry and returns the player id.
func (s *Signer) Verify(token string) (string, error) {
	parts := strings.Split(token, ".")
	if len(parts) != 3 || parts[0] != "v1" {
		return "", ErrInvalidToken
	}
	sig, err := b64.DecodeString(parts[2])
	if err != nil {
		return "", ErrInvalidToken
	}
	if !hmac.Equal(sig, s.mac(parts[0]+"."+parts[1])) {
		return "", ErrInvalidToken
	}
	raw, err := b64.DecodeString(parts[1])
	if err != nil {
		return "", ErrInvalidToken
	}
	var p tokenPayload
	if err := json.Unmarshal(raw, &p); err != nil || p.PID == "" {
		return "", ErrInvalidToken
	}
	if s.now().Unix() >= p.Exp {
		return "", ErrExpiredToken
	}
	return p.PID, nil
}
