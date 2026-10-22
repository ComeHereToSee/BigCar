package auth

import (
	"context"
	"crypto/rand"
	"encoding/hex"
	"errors"
	"regexp"
	"strings"
	"time"
	"unicode"
	"unicode/utf8"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/data"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
	"github.com/comeheretosee/bigcar/backend/internal/storage"
)

var usernamePattern = regexp.MustCompile(`^[a-z0-9_]{3,24}$`)

// Password length limits. The upper bound caps PBKDF2 work per request.
const (
	minPasswordLen = 8
	maxPasswordLen = 128
)

// Result is the response of guest/register/login.
type Result struct {
	Token       string `json:"token"`
	PlayerID    string `json:"playerId"`
	DisplayName string `json:"displayName"`
	IsNew       bool   `json:"isNew"`
}

// Service implements the auth endpoints.
type Service struct {
	store  storage.Store
	gd     *data.GameData
	signer *Signer
	hasher Hasher
	now    func() time.Time
	// dummyHash is verified when a username does not exist so that login
	// timing does not reveal which usernames are registered.
	dummyHash string
}

// NewService creates the auth service.
func NewService(st storage.Store, gd *data.GameData, signer *Signer, hasher Hasher, now func() time.Time) (*Service, error) {
	if now == nil {
		now = time.Now
	}
	dummy, err := hasher.Hash("dummy-password-for-timing")
	if err != nil {
		return nil, err
	}
	return &Service{store: st, gd: gd, signer: signer, hasher: hasher, now: now, dummyHash: dummy}, nil
}

// Signer returns the token signer (used by the HTTP middleware).
func (s *Service) Signer() *Signer { return s.signer }

// NewPlayerID returns "p_" + 12 random hex characters.
func NewPlayerID() (string, error) {
	b := make([]byte, 6)
	if _, err := rand.Read(b); err != nil {
		return "", err
	}
	return "p_" + hex.EncodeToString(b), nil
}

func validateDeviceID(id string) error {
	n := utf8.RuneCountInString(id)
	if n < 8 || n > 128 || !utf8.ValidString(id) {
		return apperr.New(apperr.BadRequest, "deviceId must be 8-128 characters")
	}
	for _, r := range id {
		if unicode.IsControl(r) {
			return apperr.New(apperr.BadRequest, "deviceId contains invalid characters")
		}
	}
	return nil
}

func normalizeCredentials(username, password string) (string, error) {
	username = strings.ToLower(strings.TrimSpace(username))
	if !usernamePattern.MatchString(username) {
		return "", apperr.New(apperr.BadRequest, "username must be 3-24 characters of a-z, 0-9 and _")
	}
	if len(password) < minPasswordLen || len(password) > maxPasswordLen {
		return "", apperr.New(apperr.BadRequest, "password must be %d-%d characters", minPasswordLen, maxPasswordLen)
	}
	return username, nil
}

func (s *Service) result(playerID, displayName string, isNew bool) Result {
	tok, _ := s.signer.Sign(playerID)
	return Result{Token: tok, PlayerID: playerID, DisplayName: displayName, IsNew: isNew}
}

// createAccount inserts the account and its new profile. It retries on
// ErrConflict caused by a player id collision; exists reports a conflicting
// device/username (checked inside the same transaction).
func (s *Service) createAccount(ctx context.Context, acc storage.Account, displayName string,
	exists func(tx storage.Tx) (storage.Account, bool, error)) (Result, error) {
	for attempt := 0; attempt < 3; attempt++ {
		pid, err := NewPlayerID()
		if err != nil {
			return Result{}, err
		}
		acc.PlayerID = pid
		acc.CreatedAt = s.now().UTC()
		name := displayName
		if name == "" {
			name = profile.DefaultDisplayName(pid)
		}
		var res Result
		err = s.store.InTx(ctx, func(tx storage.Tx) error {
			if existing, ok, err := exists(tx); err != nil {
				return err
			} else if ok {
				p, err := tx.GetProfile(ctx, existing.PlayerID, false)
				if err != nil {
					return err
				}
				res = s.result(existing.PlayerID, p.DisplayName, false)
				return nil
			}
			if err := tx.CreateAccount(ctx, acc); err != nil {
				return err
			}
			if err := tx.PutProfile(ctx, profile.New(pid, name, s.gd)); err != nil {
				return err
			}
			res = s.result(pid, name, true)
			return nil
		})
		if errors.Is(err, storage.ErrConflict) {
			continue // id collision or a concurrent request created the same device/username
		}
		return res, err
	}
	return Result{}, apperr.New(apperr.Conflict, "could not create account, please retry")
}

// Guest logs in (or creates) the guest account bound to deviceID.
func (s *Service) Guest(ctx context.Context, deviceID string) (Result, error) {
	if err := validateDeviceID(deviceID); err != nil {
		return Result{}, err
	}
	return s.createAccount(ctx, storage.Account{DeviceID: deviceID}, "", func(tx storage.Tx) (storage.Account, bool, error) {
		a, err := tx.AccountByDevice(ctx, deviceID)
		if errors.Is(err, storage.ErrNotFound) {
			return storage.Account{}, false, nil
		}
		return a, err == nil, err
	})
}

// Register creates a named account.
func (s *Service) Register(ctx context.Context, username, password, displayName string) (Result, error) {
	username, err := normalizeCredentials(username, password)
	if err != nil {
		return Result{}, err
	}
	if strings.TrimSpace(displayName) != "" {
		if displayName, err = profile.ValidateDisplayName(displayName); err != nil {
			return Result{}, err
		}
	} else {
		displayName = ""
	}
	hash, err := s.hasher.Hash(password) // slow: done outside any transaction
	if err != nil {
		return Result{}, err
	}
	// A taken username is an error, not a login: the exists callback fails
	// the transaction with conflict.
	return s.createAccount(ctx, storage.Account{Username: username, PasswordHash: hash}, displayName,
		func(tx storage.Tx) (storage.Account, bool, error) {
			_, err := tx.AccountByUsername(ctx, username)
			switch {
			case errors.Is(err, storage.ErrNotFound):
				return storage.Account{}, false, nil
			case err == nil:
				return storage.Account{}, false, apperr.New(apperr.Conflict, "username is already taken")
			default:
				return storage.Account{}, false, err
			}
		})
}

// Login checks username/password.
func (s *Service) Login(ctx context.Context, username, password string) (Result, error) {
	invalid := apperr.New(apperr.Unauthorized, "invalid username or password")
	username = strings.ToLower(strings.TrimSpace(username))
	if !usernamePattern.MatchString(username) || len(password) > maxPasswordLen {
		return Result{}, invalid
	}
	var acc storage.Account
	err := s.store.InTx(ctx, func(tx storage.Tx) error {
		var err error
		acc, err = tx.AccountByUsername(ctx, username)
		return err
	})
	if errors.Is(err, storage.ErrNotFound) || (err == nil && acc.PasswordHash == "") {
		_, _ = VerifyPassword(s.dummyHash, password)
		return Result{}, invalid
	}
	if err != nil {
		return Result{}, err
	}
	ok, err := VerifyPassword(acc.PasswordHash, password)
	if err != nil {
		return Result{}, err
	}
	if !ok {
		return Result{}, invalid
	}
	p, err := storage.ReadProfile(ctx, s.store, acc.PlayerID)
	if err != nil {
		return Result{}, err
	}
	return s.result(acc.PlayerID, p.DisplayName, false), nil
}

// Link attaches username/password to an existing (guest) account.
func (s *Service) Link(ctx context.Context, playerID, username, password string) error {
	username, err := normalizeCredentials(username, password)
	if err != nil {
		return err
	}
	hash, err := s.hasher.Hash(password)
	if err != nil {
		return err
	}
	err = s.store.InTx(ctx, func(tx storage.Tx) error {
		acc, err := tx.AccountByID(ctx, playerID)
		if errors.Is(err, storage.ErrNotFound) {
			return apperr.New(apperr.NotFound, "account not found")
		}
		if err != nil {
			return err
		}
		if acc.Username != "" {
			return apperr.New(apperr.Conflict, "account already has a username")
		}
		if _, err := tx.AccountByUsername(ctx, username); err == nil {
			return apperr.New(apperr.Conflict, "username is already taken")
		} else if !errors.Is(err, storage.ErrNotFound) {
			return err
		}
		acc.Username, acc.PasswordHash = username, hash
		return tx.UpdateAccount(ctx, acc)
	})
	if errors.Is(err, storage.ErrConflict) {
		return apperr.New(apperr.Conflict, "username is already taken")
	}
	return err
}
