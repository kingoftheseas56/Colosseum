package httpserver

import (
	"encoding/json"
	"net/http"
	"strconv"
	"time"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
)

const APIPrefix = "/v1"

type APIError struct {
	Error APIErrorDetail `json:"error"`
}

type APIErrorDetail struct {
	Code    string `json:"code"`
	Message string `json:"message"`
}

func WriteAPIError(w http.ResponseWriter, status int, code, message string) {
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(APIError{
		Error: APIErrorDetail{
			Code:    code,
			Message: message,
		},
	})
}

func writeServiceError(w http.ResponseWriter, err error) {
	if rateLimit, ok := err.(*account.RateLimitError); ok {
		retry := int(rateLimit.RetryAfter.Round(time.Second).Seconds())
		if retry < 1 {
			retry = 1
		}
		w.Header().Set("Retry-After", strconv.Itoa(retry))
		WriteAPIError(w,
			http.StatusTooManyRequests,
			"rate_limited",
			"Too many attempts. Try again later.")
		return
	}

	switch err {
	case account.ErrInvalidUsername:
		WriteAPIError(w, http.StatusBadRequest, "invalid_username", "That username is not valid.")
	case account.ErrUsernameUnavailable:
		WriteAPIError(w, http.StatusConflict, "username_unavailable", "That username is unavailable.")
	case account.ErrInvalidPassword:
		WriteAPIError(w, http.StatusBadRequest, "invalid_password", "That password does not meet the account requirements.")
	case account.ErrInvalidCredentials, account.ErrRecoveryKeyInvalid:
		WriteAPIError(w, http.StatusUnauthorized, "invalid_credentials", "The credentials were not accepted.")
	case account.ErrSessionInvalid:
		WriteAPIError(w, http.StatusUnauthorized, "session_invalid", "The session is no longer valid.")
	case account.ErrSessionRevoked:
		WriteAPIError(w, http.StatusUnauthorized, "session_revoked", "This session was signed out.")
	case account.ErrChallengeInvalid:
		WriteAPIError(w, http.StatusUnauthorized, "challenge_invalid", "The approval request is no longer valid.")
	case account.ErrChallengeExpired:
		WriteAPIError(w, http.StatusGone, "challenge_expired", "The approval request expired.")
	case account.ErrChallengeDenied:
		WriteAPIError(w, http.StatusForbidden, "challenge_denied", "The approval request was denied.")
	case account.ErrRenameCooldown:
		WriteAPIError(w, http.StatusConflict, "username_change_cooldown", "The username cannot be changed again yet.")
	case account.ErrDeviceNotFound:
		WriteAPIError(w, http.StatusNotFound, "device_not_found", "That device was not found.")
	case account.ErrAvatarInvalid:
		WriteAPIError(w, http.StatusBadRequest, "avatar_invalid", "That avatar image is not supported.")
	case account.ErrAvatarStorageDisabled:
		WriteAPIError(w, http.StatusServiceUnavailable, "avatar_unavailable", "Avatar uploads are temporarily unavailable.")
	case account.ErrTrustedRecoveryNeeded:
		WriteAPIError(w, http.StatusConflict, "trusted_device_unavailable", "No signed-in trusted device is available.")
	default:
		WriteAPIError(w, http.StatusInternalServerError, "internal_error", "The request could not be completed.")
	}
}
