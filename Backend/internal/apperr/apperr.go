// Package apperr defines the API error type shared by all domain packages.
//
// Every error that should reach a client carries one of the snake_case codes
// listed in docs/BACKEND_API.md. The HTTP layer maps the code to a status and
// renders {"error": {"code": ..., "message": ...}}. Any other error is treated
// as an internal error and its text is never shown to clients.
package apperr

import (
	"errors"
	"fmt"
	"net/http"
)

// Error codes from docs/BACKEND_API.md.
const (
	BadRequest        = "bad_request"
	Unauthorized      = "unauthorized"
	Forbidden         = "forbidden"
	NotFound          = "not_found"
	Conflict          = "conflict"
	InsufficientFunds = "insufficient_funds"
	NotOwned          = "not_owned"
	AlreadyOwned      = "already_owned"
	LevelLocked       = "level_locked"
	MaxLevel          = "max_level"
	InvalidSlot       = "invalid_slot"
	LimitReached      = "limit_reached"
	PartyFull         = "party_full"
	NotLeader         = "not_leader"
	NotReady          = "not_ready"
	RateLimited       = "rate_limited"
	Internal          = "internal"
)

// statusByCode maps error codes to HTTP status codes. Business-rule failures
// use 409 (the request was valid but conflicts with the current state), except
// where a more specific status exists.
var statusByCode = map[string]int{
	BadRequest:        http.StatusBadRequest,
	Unauthorized:      http.StatusUnauthorized,
	Forbidden:         http.StatusForbidden,
	NotFound:          http.StatusNotFound,
	Conflict:          http.StatusConflict,
	InsufficientFunds: http.StatusPaymentRequired,
	NotOwned:          http.StatusForbidden,
	AlreadyOwned:      http.StatusConflict,
	LevelLocked:       http.StatusForbidden,
	MaxLevel:          http.StatusConflict,
	InvalidSlot:       http.StatusBadRequest,
	LimitReached:      http.StatusConflict,
	PartyFull:         http.StatusConflict,
	NotLeader:         http.StatusForbidden,
	NotReady:          http.StatusConflict,
	RateLimited:       http.StatusTooManyRequests,
	Internal:          http.StatusInternalServerError,
}

// Error is a client-visible error.
type Error struct {
	Code    string
	Message string
}

func (e *Error) Error() string { return e.Code + ": " + e.Message }

// Status returns the HTTP status for the error code (500 for unknown codes).
func (e *Error) Status() int { return StatusFor(e.Code) }

// StatusFor returns the HTTP status for an error code.
func StatusFor(code string) int {
	if s, ok := statusByCode[code]; ok {
		return s
	}
	return http.StatusInternalServerError
}

// New creates an Error with a formatted message.
func New(code, format string, args ...any) *Error {
	msg := format
	if len(args) > 0 {
		msg = fmt.Sprintf(format, args...)
	}
	return &Error{Code: code, Message: msg}
}

// As extracts an *Error from err.
func As(err error) (*Error, bool) {
	var e *Error
	if errors.As(err, &e) {
		return e, true
	}
	return nil, false
}

// CodeOf returns the error code of err, Internal for non-API errors and "" for nil.
func CodeOf(err error) string {
	if err == nil {
		return ""
	}
	if e, ok := As(err); ok {
		return e.Code
	}
	return Internal
}
