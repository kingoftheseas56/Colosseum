package account

import (
	"context"
	"errors"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5"
	"golang.org/x/text/unicode/norm"
)

func (s *Service) RecoverPassword(ctx context.Context,
	input RecoverPasswordInput) (RecoveryResult, error) {
	sourceKey := normalizedSourceKey(input.SourceKey)
	if err := s.rateLimiter.Allow(
		ctx,
		"recovery_source",
		[]string{sourceKey},
		recoveryWindow,
		recoveryLimit); err != nil {
		return RecoveryResult{}, err
	}

	_, canonical, err := NormalizeUsername(input.Username)
	if err != nil {
		return RecoveryResult{}, ErrInvalidCredentials
	}
	if err := s.rateLimiter.Allow(
		ctx,
		"recovery_identity",
		[]string{sourceKey, canonical},
		recoveryWindow,
		recoveryLimit); err != nil {
		return RecoveryResult{}, err
	}

	accountRecord, err := s.loadAuthAccountByCanonical(ctx, canonical)
	if err != nil {
		if errors.Is(err, ErrInvalidCredentials) {
			return RecoveryResult{}, ErrInvalidCredentials
		}
		return RecoveryResult{}, err
	}
	if !s.recoveryVerifier.Verify(input.RecoveryKey, accountRecord.RecoveryVerifier) {
		return RecoveryResult{}, ErrInvalidCredentials
	}

	newPassword, err := s.passwordPolicy.Validate(
		input.NewPassword,
		accountRecord.CanonicalUsername)
	if err != nil {
		return RecoveryResult{}, err
	}
	newPasswordHash, err := s.passwordHasher.Hash(newPassword)
	if err != nil {
		return RecoveryResult{}, fmt.Errorf("hash recovered password: %w", err)
	}
	newRecoveryKey, err := GenerateRecoveryKey()
	if err != nil {
		return RecoveryResult{}, err
	}
	newRecoveryVerifier, err := s.recoveryVerifier.Sum(newRecoveryKey)
	if err != nil {
		return RecoveryResult{}, err
	}

	now := s.clock.Now()
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return RecoveryResult{}, fmt.Errorf("begin password recovery: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var currentVerifier []byte
	if err := tx.QueryRow(ctx, `
        SELECT recovery_key_verifier
        FROM accounts
        WHERE id = $1::uuid
        FOR UPDATE
    `, accountRecord.ID).Scan(&currentVerifier); err != nil {
		return RecoveryResult{}, fmt.Errorf("lock account for recovery: %w", err)
	}
	if !s.recoveryVerifier.Verify(input.RecoveryKey, currentVerifier) {
		return RecoveryResult{}, ErrInvalidCredentials
	}

	if _, err := tx.Exec(ctx, `
        UPDATE accounts
        SET password_hash = $2,
            recovery_key_verifier = $3,
            recovery_key_version = recovery_key_version + 1,
            updated_at = $4
        WHERE id = $1::uuid
    `,
		accountRecord.ID,
		newPasswordHash,
		newRecoveryVerifier,
		now); err != nil {
		return RecoveryResult{}, fmt.Errorf("recover account credentials: %w", err)
	}

	if _, err := tx.Exec(ctx, `
        UPDATE sessions
        SET revoked_at = $2
        WHERE account_id = $1::uuid
          AND revoked_at IS NULL
    `, accountRecord.ID, now); err != nil {
		return RecoveryResult{}, fmt.Errorf("revoke sessions after recovery: %w", err)
	}

	if _, err := tx.Exec(ctx, `
        UPDATE device_signin_challenges
        SET state = 'denied',
            decided_at = $2
        WHERE account_id = $1::uuid
          AND state IN ('pending', 'approved')
    `, accountRecord.ID, now); err != nil {
		return RecoveryResult{}, fmt.Errorf("cancel device challenges after recovery: %w", err)
	}
	if _, err := tx.Exec(ctx, `
        UPDATE trusted_recovery_challenges
        SET state = 'denied',
            decided_at = $2,
            new_password_hash = ''
        WHERE account_id = $1::uuid
          AND state IN ('pending', 'approved')
    `, accountRecord.ID, now); err != nil {
		return RecoveryResult{}, fmt.Errorf("cancel recovery challenges after recovery: %w", err)
	}

	if err := recordSecurityEventTx(
		ctx,
		tx,
		accountRecord.ID,
		"password_recovered_with_key",
		"",
		now,
		map[string]any{}); err != nil {
		return RecoveryResult{}, err
	}

	if err := tx.Commit(ctx); err != nil {
		return RecoveryResult{}, fmt.Errorf("commit password recovery: %w", err)
	}
	return RecoveryResult{RecoveryKey: newRecoveryKey}, nil
}

func (s *Service) ReplaceRecoveryKey(ctx context.Context,
	auth AuthenticatedSession,
	input ReplaceRecoveryKeyInput) (RecoveryResult, error) {
	if err := s.rateLimiter.Allow(
		ctx,
		"recovery_key_reauth",
		[]string{auth.Account.ID, auth.Device.ID},
		reauthWindow,
		reauthLimit); err != nil {
		return RecoveryResult{}, err
	}

	accountRecord, err := s.loadAuthAccountByID(ctx, auth.Account.ID)
	if err != nil {
		return RecoveryResult{}, err
	}
	currentPassword := norm.NFC.String(input.CurrentPassword)
	valid, err := s.passwordHasher.Verify(accountRecord.PasswordHash, currentPassword)
	if err != nil {
		return RecoveryResult{}, fmt.Errorf("verify password for recovery-key replacement: %w", err)
	}
	if !valid {
		return RecoveryResult{}, ErrInvalidCredentials
	}

	newRecoveryKey, err := GenerateRecoveryKey()
	if err != nil {
		return RecoveryResult{}, err
	}
	newVerifier, err := s.recoveryVerifier.Sum(newRecoveryKey)
	if err != nil {
		return RecoveryResult{}, err
	}

	now := s.clock.Now()
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return RecoveryResult{}, fmt.Errorf("begin recovery-key replacement: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var lockedPasswordHash string
	if err := tx.QueryRow(ctx, `
        SELECT password_hash
        FROM accounts
        WHERE id = $1::uuid
        FOR UPDATE
    `, auth.Account.ID).Scan(&lockedPasswordHash); err != nil {
		return RecoveryResult{}, fmt.Errorf("lock account for recovery-key replacement: %w", err)
	}
	valid, err = s.passwordHasher.Verify(lockedPasswordHash, currentPassword)
	if err != nil {
		return RecoveryResult{}, fmt.Errorf("verify locked password for recovery-key replacement: %w", err)
	}
	if !valid {
		return RecoveryResult{}, ErrInvalidCredentials
	}

	if _, err := tx.Exec(ctx, `
        UPDATE accounts
        SET recovery_key_verifier = $2,
            recovery_key_version = recovery_key_version + 1,
            updated_at = $3
        WHERE id = $1::uuid
    `, auth.Account.ID, newVerifier, now); err != nil {
		return RecoveryResult{}, fmt.Errorf("replace recovery key: %w", err)
	}
	if err := recordSecurityEventTx(
		ctx,
		tx,
		auth.Account.ID,
		"recovery_key_replaced",
		auth.Device.ID,
		now,
		map[string]any{}); err != nil {
		return RecoveryResult{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return RecoveryResult{}, fmt.Errorf("commit recovery-key replacement: %w", err)
	}
	return RecoveryResult{RecoveryKey: newRecoveryKey}, nil
}

func (s *Service) StartTrustedRecovery(ctx context.Context,
	input TrustedRecoveryInput) (TrustedRecoveryResult, error) {
	sourceKey := normalizedSourceKey(input.SourceKey)
	if err := s.rateLimiter.Allow(
		ctx,
		"trusted_recovery_source",
		[]string{sourceKey},
		recoveryWindow,
		recoveryLimit); err != nil {
		return TrustedRecoveryResult{}, err
	}

	_, canonical, err := NormalizeUsername(input.Username)
	if err != nil {
		return TrustedRecoveryResult{}, ErrInvalidCredentials
	}
	if err := ValidateDeviceIdentity(
		input.DeviceInstallID,
		input.DeviceLabel,
		input.Platform); err != nil {
		return TrustedRecoveryResult{}, ErrInvalidCredentials
	}

	if err := s.rateLimiter.Allow(
		ctx,
		"trusted_recovery_identity",
		[]string{sourceKey, canonical},
		recoveryWindow,
		recoveryLimit); err != nil {
		return TrustedRecoveryResult{}, err
	}

	accountRecord, err := s.loadAuthAccountByCanonical(ctx, canonical)
	if err != nil {
		return TrustedRecoveryResult{}, ErrTrustedRecoveryNeeded
	}

	newPassword, err := s.passwordPolicy.Validate(
		input.NewPassword,
		accountRecord.CanonicalUsername)
	if err != nil {
		return TrustedRecoveryResult{}, err
	}
	newPasswordHash, err := s.passwordHasher.Hash(newPassword)
	if err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("hash trusted recovery password: %w", err)
	}

	var trustedSessionExists bool
	if err := s.pool.QueryRow(ctx, `
        SELECT EXISTS(
            SELECT 1
            FROM sessions s
            JOIN devices d ON d.id = s.device_id
            WHERE s.account_id = $1::uuid
              AND s.revoked_at IS NULL
              AND d.trusted = true
              AND d.revoked_at IS NULL
        )
    `, accountRecord.ID).Scan(&trustedSessionExists); err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("check trusted recovery availability: %w", err)
	}
	if !trustedSessionExists {
		return TrustedRecoveryResult{}, ErrTrustedRecoveryNeeded
	}

	token, err := GenerateToken()
	if err != nil {
		return TrustedRecoveryResult{}, err
	}
	now := s.clock.Now()
	expiresAt := now.Add(trustedRecoveryLifetime)

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("begin trusted recovery challenge: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, err := tx.Exec(ctx, `
        UPDATE trusted_recovery_challenges
        SET state = 'denied',
            decided_at = $3,
            new_password_hash = ''
        WHERE account_id = $1::uuid
          AND target_install_id = $2::uuid
          AND state IN ('pending', 'approved')
    `, accountRecord.ID, input.DeviceInstallID, now); err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("supersede trusted recovery challenge: %w", err)
	}

	if _, err := tx.Exec(ctx, `
        INSERT INTO trusted_recovery_challenges(
            account_id,
            target_install_id,
            target_label,
            target_platform,
            challenge_token_hash,
            new_password_hash,
            state,
            expires_at,
            created_at
        )
        VALUES($1::uuid, $2::uuid, $3, $4, $5, $6, 'pending', $7, $8)
    `,
		accountRecord.ID,
		input.DeviceInstallID,
		input.DeviceLabel,
		input.Platform,
		TokenHash(token),
		newPasswordHash,
		expiresAt,
		now); err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("create trusted recovery challenge: %w", err)
	}
	if err := recordSecurityEventTx(
		ctx,
		tx,
		accountRecord.ID,
		"trusted_recovery_challenge_created",
		"",
		now,
		map[string]any{"target_install_id": input.DeviceInstallID}); err != nil {
		return TrustedRecoveryResult{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("commit trusted recovery challenge: %w", err)
	}
	return TrustedRecoveryResult{
		Status:             "approval_required",
		ChallengeToken:     token,
		ChallengeExpiresAt: expiresAt,
	}, nil
}

func (s *Service) PollTrustedRecovery(ctx context.Context,
	challengeToken string) (TrustedRecoveryResult, error) {
	if challengeToken == "" {
		return TrustedRecoveryResult{}, ErrChallengeInvalid
	}
	now := s.clock.Now()
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("begin trusted recovery poll: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var challengeID string
	var accountID string
	var state string
	var newPasswordHash string
	var expiresAt time.Time
	err = tx.QueryRow(ctx, `
        SELECT
            id::text,
            account_id::text,
            state,
            new_password_hash,
            expires_at
        FROM trusted_recovery_challenges
        WHERE challenge_token_hash = $1
        FOR UPDATE
    `, TokenHash(challengeToken)).Scan(
		&challengeID,
		&accountID,
		&state,
		&newPasswordHash,
		&expiresAt)
	if errors.Is(err, pgx.ErrNoRows) {
		return TrustedRecoveryResult{}, ErrChallengeInvalid
	}
	if err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("load trusted recovery challenge: %w", err)
	}

	if now.After(expiresAt) && state != "consumed" {
		if _, err := tx.Exec(ctx, `
            UPDATE trusted_recovery_challenges
            SET state = 'denied',
                decided_at = $2,
                new_password_hash = ''
            WHERE id = $1::uuid
        `, challengeID, now); err != nil {
			return TrustedRecoveryResult{}, fmt.Errorf("expire trusted recovery challenge: %w", err)
		}
		if err := tx.Commit(ctx); err != nil {
			return TrustedRecoveryResult{}, fmt.Errorf("commit trusted recovery expiry: %w", err)
		}
		return TrustedRecoveryResult{}, ErrChallengeExpired
	}

	switch state {
	case "pending":
		return TrustedRecoveryResult{
			Status:             "pending",
			ChallengeExpiresAt: expiresAt,
		}, nil
	case "denied":
		return TrustedRecoveryResult{}, ErrChallengeDenied
	case "consumed":
		return TrustedRecoveryResult{}, ErrChallengeInvalid
	case "approved":
	default:
		return TrustedRecoveryResult{}, ErrChallengeInvalid
	}

	var currentVerifier []byte
	var recoveryVersion int
	if err := tx.QueryRow(ctx, `
        SELECT recovery_key_verifier, recovery_key_version
        FROM accounts
        WHERE id = $1::uuid
        FOR UPDATE
    `, accountID).Scan(&currentVerifier, &recoveryVersion); err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("lock account for trusted recovery: %w", err)
	}
	_ = currentVerifier
	_ = recoveryVersion

	newRecoveryKey, err := GenerateRecoveryKey()
	if err != nil {
		return TrustedRecoveryResult{}, err
	}
	newRecoveryVerifier, err := s.recoveryVerifier.Sum(newRecoveryKey)
	if err != nil {
		return TrustedRecoveryResult{}, err
	}

	if _, err := tx.Exec(ctx, `
        UPDATE accounts
        SET password_hash = $2,
            recovery_key_verifier = $3,
            recovery_key_version = recovery_key_version + 1,
            updated_at = $4
        WHERE id = $1::uuid
    `, accountID, newPasswordHash, newRecoveryVerifier, now); err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("apply trusted recovery: %w", err)
	}
	if _, err := tx.Exec(ctx, `
        UPDATE sessions
        SET revoked_at = $2
        WHERE account_id = $1::uuid
          AND revoked_at IS NULL
    `, accountID, now); err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("revoke sessions after trusted recovery: %w", err)
	}
	if _, err := tx.Exec(ctx, `
        UPDATE trusted_recovery_challenges
        SET state = 'consumed',
            consumed_at = $2,
            new_password_hash = ''
        WHERE id = $1::uuid
          AND state = 'approved'
    `, challengeID, now); err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("consume trusted recovery challenge: %w", err)
	}
	if _, err := tx.Exec(ctx, `
        UPDATE device_signin_challenges
        SET state = 'denied',
            decided_at = $2
        WHERE account_id = $1::uuid
          AND state IN ('pending', 'approved')
    `, accountID, now); err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("cancel device challenges after trusted recovery: %w", err)
	}
	if err := recordSecurityEventTx(
		ctx,
		tx,
		accountID,
		"password_recovered_by_trusted_device",
		"",
		now,
		map[string]any{}); err != nil {
		return TrustedRecoveryResult{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return TrustedRecoveryResult{}, fmt.Errorf("commit trusted recovery: %w", err)
	}
	return TrustedRecoveryResult{
		Status:      "recovered",
		RecoveryKey: newRecoveryKey,
	}, nil
}
