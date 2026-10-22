package auth

import (
	"crypto/pbkdf2"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"encoding/base64"
	"errors"
	"fmt"
	"strconv"
	"strings"
)

// Password hashing parameters from docs/BACKEND_API.md.
const (
	DefaultIterations = 210_000
	saltLen           = 16
	keyLen            = 32
	hashScheme        = "pbkdf2_sha256"
)

var errBadHash = errors.New("auth: malformed password hash")

// Hasher hashes passwords with PBKDF2-HMAC-SHA256. The encoded form is
//
//	pbkdf2_sha256$<iterations>$<b64 salt>$<b64 key>
//
// (standard base64 without padding), so the iteration count can be raised
// later without invalidating stored hashes.
type Hasher struct {
	Iterations int
}

// Hash returns the encoded hash of password with a fresh 16-byte salt.
func (h Hasher) Hash(password string) (string, error) {
	iter := h.Iterations
	if iter <= 0 {
		iter = DefaultIterations
	}
	salt := make([]byte, saltLen)
	if _, err := rand.Read(salt); err != nil {
		return "", err
	}
	key, err := pbkdf2.Key(sha256.New, password, salt, iter, keyLen)
	if err != nil {
		return "", err
	}
	enc := base64.RawStdEncoding
	return fmt.Sprintf("%s$%d$%s$%s", hashScheme, iter, enc.EncodeToString(salt), enc.EncodeToString(key)), nil
}

// VerifyPassword reports whether password matches the encoded hash, in
// constant time with respect to the key comparison.
func VerifyPassword(encoded, password string) (bool, error) {
	parts := strings.Split(encoded, "$")
	if len(parts) != 4 || parts[0] != hashScheme {
		return false, errBadHash
	}
	iter, err := strconv.Atoi(parts[1])
	if err != nil || iter <= 0 || iter > 10_000_000 {
		return false, errBadHash
	}
	enc := base64.RawStdEncoding
	salt, err := enc.DecodeString(parts[2])
	if err != nil {
		return false, errBadHash
	}
	want, err := enc.DecodeString(parts[3])
	if err != nil || len(want) == 0 {
		return false, errBadHash
	}
	got, err := pbkdf2.Key(sha256.New, password, salt, iter, len(want))
	if err != nil {
		return false, err
	}
	return subtle.ConstantTimeCompare(got, want) == 1, nil
}
