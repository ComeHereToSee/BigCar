package storage

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"encoding/json"
	"errors"
	"net/http"
	"time"

	"github.com/comeheretosee/bigcar/backend/internal/apperr"
	"github.com/comeheretosee/bigcar/backend/internal/profile"
)

// Idempotency identifies a client retry: the Idempotency-Key header and a
// hash of the request it was first used with.
type Idempotency struct {
	Key         string
	RequestHash string
}

// HashRequest fingerprints a request so a key reused for a different request
// can be detected.
func HashRequest(method, path string, body []byte) string {
	h := sha256.New()
	h.Write([]byte(method))
	h.Write([]byte{0})
	h.Write([]byte(path))
	h.Write([]byte{0})
	h.Write(body)
	return hex.EncodeToString(h.Sum(nil))
}

// Response is a JSON response produced inside a transaction.
type Response struct {
	Status   int
	Body     []byte
	Replayed bool
}

// MaxIdempotencyKeyLen bounds the Idempotency-Key header.
const MaxIdempotencyKeyLen = 128

// MutateProfile runs fn on the locked profile of playerID and saves it, in one
// transaction. fn returns the response value, which is JSON-encoded inside the
// transaction.
//
// With idem != nil the first successful response for (playerID, key) is
// stored atomically with the mutation; a later request with the same key gets
// that stored response back without running fn. The profile row is locked
// before the key is checked, so two concurrent requests with the same key
// cannot both apply. Failed requests are not recorded (nothing changed, so a
// retry simply runs again).
func MutateProfile(ctx context.Context, st Store, playerID string, idem *Idempotency, now func() time.Time,
	fn func(tx Tx, p *profile.Profile) (any, error)) (Response, error) {
	if idem != nil && (idem.Key == "" || len(idem.Key) > MaxIdempotencyKeyLen) {
		return Response{}, apperr.New(apperr.BadRequest, "Idempotency-Key must be 1-%d characters", MaxIdempotencyKeyLen)
	}
	var resp Response
	err := st.InTx(ctx, func(tx Tx) error {
		resp = Response{}
		p, err := tx.GetProfile(ctx, playerID, true)
		if errors.Is(err, ErrNotFound) {
			return apperr.New(apperr.NotFound, "profile not found")
		}
		if err != nil {
			return err
		}
		if idem != nil {
			rec, ok, err := tx.GetIdempotency(ctx, playerID, idem.Key)
			if err != nil {
				return err
			}
			if ok {
				if rec.RequestHash != idem.RequestHash {
					return apperr.New(apperr.Conflict, "Idempotency-Key was already used for a different request")
				}
				resp = Response{Status: rec.Status, Body: rec.Body, Replayed: true}
				return nil
			}
		}
		v, err := fn(tx, p)
		if err != nil {
			return err
		}
		if err := tx.PutProfile(ctx, p); err != nil {
			return err
		}
		body, err := json.Marshal(v)
		if err != nil {
			return err
		}
		resp = Response{Status: http.StatusOK, Body: body}
		if idem != nil {
			return tx.PutIdempotency(ctx, IdempotencyRecord{
				PlayerID: playerID, Key: idem.Key, RequestHash: idem.RequestHash,
				Status: resp.Status, Body: body, CreatedAt: now(),
			})
		}
		return nil
	})
	return resp, err
}
