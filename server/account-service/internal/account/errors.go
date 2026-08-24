package account

import (
	"errors"
	"fmt"
	"time"
)

var (
	ErrInvalidUsername       = errors.New("invalid username")
	ErrUsernameUnavailable   = errors.New("username unavailable")
	ErrInvalidPassword       = errors.New("invalid password")
	ErrInvalidCredentials    = errors.New("invalid credentials")
	ErrSessionInvalid        = errors.New("session invalid")
	ErrSessionRevoked        = errors.New("session revoked")
	ErrRecoveryKeyInvalid    = errors.New("recovery key invalid")
	ErrApprovalRequired      = errors.New("approval required")
	ErrChallengeInvalid      = errors.New("challenge invalid")
	ErrChallengeExpired      = errors.New("challenge expired")
	ErrChallengeDenied       = errors.New("challenge denied")
	ErrRenameCooldown        = errors.New("username rename cooldown")
	ErrDeviceNotFound        = errors.New("device not found")
	ErrAvatarInvalid         = errors.New("avatar invalid")
	ErrAvatarStorageDisabled = errors.New("avatar storage disabled")
	ErrTrustedRecoveryNeeded = errors.New("trusted recovery approval required")
)

type RateLimitError struct {
	Scope      string
	RetryAfter time.Duration
}

func (e *RateLimitError) Error() string {
	if e == nil {
		return "rate limited"
	}
	return fmt.Sprintf("rate limited; retry after %s", e.RetryAfter.Round(time.Second))
}
