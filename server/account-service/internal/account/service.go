package account

import (
	"context"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/avatar"
)

const (
	accessTokenLifetime     = 15 * time.Minute
	refreshRetryGrace       = 30 * time.Second
	deviceChallengeLifetime = 10 * time.Minute
	trustedRecoveryLifetime = 10 * time.Minute
	usernameRenameCooldown  = 30 * 24 * time.Hour
	createAttemptWindow     = time.Hour
	createAttemptLimit      = 10
	createUsernameWindow    = time.Hour
	createUsernameLimit     = 5
	createSuccessWindow     = 24 * time.Hour
	createSuccessLimit      = 2
	createGlobalWindow      = 10 * time.Minute
	signInWindow            = 15 * time.Minute
	signInLimit             = 10
	recoveryWindow          = 30 * time.Minute
	recoveryLimit           = 5
	reauthWindow            = 15 * time.Minute
	reauthLimit             = 5
)

type Service struct {
	pool                    *pgxpool.Pool
	passwordPolicy          PasswordPolicy
	passwordHasher          *PasswordHasher
	dummyPasswordHash       string
	passwordVerify          func(encoded, password string) (bool, error)
	recoveryVerifier        *RecoveryKeyVerifier
	sessionCipher           *SessionCipher
	syncCipher              *SyncPayloadCipher
	syncMaxFutureSkew       time.Duration
	rateLimiter             *RateLimiter
	avatarStore             avatar.Store
	clock                   Clock
	registrationGlobalLimit int
}

type Dependencies struct {
	Pool                    *pgxpool.Pool
	PasswordPolicy          PasswordPolicy
	PasswordHasher          *PasswordHasher
	RecoveryVerifier        *RecoveryKeyVerifier
	SessionCipher           *SessionCipher
	SyncCipher              *SyncPayloadCipher
	SyncMaxFutureSkew       time.Duration
	RateLimiter             *RateLimiter
	AvatarStore             avatar.Store
	Clock                   Clock
	RegistrationGlobalLimit int
}

func NewService(dependencies Dependencies) (*Service, error) {
	if dependencies.Pool == nil {
		return nil, fmt.Errorf("account service requires a database pool")
	}
	if dependencies.PasswordHasher == nil {
		return nil, fmt.Errorf("account service requires a password hasher")
	}
	if dependencies.RecoveryVerifier == nil {
		return nil, fmt.Errorf("account service requires a recovery verifier")
	}
	if dependencies.SessionCipher == nil {
		return nil, fmt.Errorf("account service requires a session cipher")
	}
	if dependencies.SyncCipher == nil {
		return nil, fmt.Errorf("account service requires a sync payload cipher")
	}
	if dependencies.SyncMaxFutureSkew <= 0 {
		return nil, fmt.Errorf("account service requires a positive sync future-skew limit")
	}
	if dependencies.RateLimiter == nil {
		return nil, fmt.Errorf("account service requires a rate limiter")
	}
	if dependencies.AvatarStore == nil {
		dependencies.AvatarStore = avatar.DisabledStore{}
	}
	if dependencies.Clock == nil {
		dependencies.Clock = SystemClock{}
	}
	if dependencies.RegistrationGlobalLimit <= 0 {
		return nil, fmt.Errorf("account service requires a positive registration global limit")
	}

	dummyHash, err := dependencies.PasswordHasher.Hash(
		"colosseum-auth-dummy-password-not-a-user-secret")
	if err != nil {
		return nil, fmt.Errorf("build dummy password hash: %w", err)
	}

	return &Service{
		pool:                    dependencies.Pool,
		passwordPolicy:          dependencies.PasswordPolicy,
		passwordHasher:          dependencies.PasswordHasher,
		dummyPasswordHash:       dummyHash,
		passwordVerify:          dependencies.PasswordHasher.Verify,
		recoveryVerifier:        dependencies.RecoveryVerifier,
		sessionCipher:           dependencies.SessionCipher,
		syncCipher:              dependencies.SyncCipher,
		syncMaxFutureSkew:       dependencies.SyncMaxFutureSkew,
		rateLimiter:             dependencies.RateLimiter,
		avatarStore:             dependencies.AvatarStore,
		clock:                   dependencies.Clock,
		registrationGlobalLimit: dependencies.RegistrationGlobalLimit,
	}, nil
}

func (s *Service) PruneAuthRateEvents(ctx context.Context, before time.Time) error {
	return s.rateLimiter.Prune(ctx, before)
}

func (s *Service) RunSecurityMaintenanceOnce(ctx context.Context) error {
	now := s.clock.Now()

	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return fmt.Errorf("begin security maintenance: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, err := tx.Exec(ctx, `
        UPDATE trusted_recovery_challenges
        SET state = 'denied',
            decided_at = COALESCE(decided_at, $1),
            new_password_hash = ''
        WHERE state IN ('pending', 'approved')
          AND expires_at <= $1
    `, now); err != nil {
		return fmt.Errorf("expire trusted recovery challenges: %w", err)
	}

	if _, err := tx.Exec(ctx, `
        UPDATE device_signin_challenges
        SET state = 'denied',
            decided_at = COALESCE(decided_at, $1)
        WHERE state IN ('pending', 'approved')
          AND expires_at <= $1
    `, now); err != nil {
		return fmt.Errorf("expire device sign-in challenges: %w", err)
	}

	if _, err := tx.Exec(ctx, `
        UPDATE sessions
        SET previous_refresh_token_hash = NULL,
            previous_refresh_expires_at = NULL,
            refresh_retry_ciphertext = NULL
        WHERE previous_refresh_expires_at IS NOT NULL
          AND previous_refresh_expires_at <= $1
    `, now); err != nil {
		return fmt.Errorf("clear expired refresh retry material: %w", err)
	}

	cutoff := now.Add(-24 * time.Hour)
	if _, err := tx.Exec(ctx, `
        DELETE FROM trusted_recovery_challenges
        WHERE state IN ('denied', 'consumed')
          AND COALESCE(consumed_at, decided_at, expires_at) < $1
    `, cutoff); err != nil {
		return fmt.Errorf("prune trusted recovery challenges: %w", err)
	}

	if _, err := tx.Exec(ctx, `
        DELETE FROM device_signin_challenges
        WHERE state IN ('denied', 'consumed')
          AND COALESCE(consumed_at, decided_at, expires_at) < $1
    `, cutoff); err != nil {
		return fmt.Errorf("prune device sign-in challenges: %w", err)
	}

	if err := tx.Commit(ctx); err != nil {
		return fmt.Errorf("commit security maintenance: %w", err)
	}
	return nil
}
