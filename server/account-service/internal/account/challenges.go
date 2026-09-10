package account

import (
	"context"
	"errors"
	"fmt"
	"strings"
	"time"

	"github.com/jackc/pgx/v5"
)

func (s *Service) createDeviceSignInChallengeTx(ctx context.Context,
	tx pgx.Tx,
	account Account,
	input SignInInput,
	now time.Time) (string, time.Time, error) {
	token, err := GenerateToken()
	if err != nil {
		return "", time.Time{}, err
	}
	if _, err := tx.Exec(ctx, `
        UPDATE device_signin_challenges
        SET state = 'denied',
            decided_at = $3
        WHERE account_id = $1::uuid
          AND target_install_id = $2::uuid
		AND state IN ('pending', 'approved')
    `, account.ID, input.DeviceInstallID, now); err != nil {
		return "", time.Time{}, fmt.Errorf("supersede device challenge: %w", err)
	}
	now = s.clock.Now()
	expiresAt := now.Add(deviceChallengeLifetime)

	if _, err := tx.Exec(ctx, `
        INSERT INTO device_signin_challenges(
            account_id,
            target_install_id,
            target_label,
            target_platform,
            challenge_token_hash,
            state,
            expires_at,
            created_at
        )
        VALUES($1::uuid, $2::uuid, $3, $4, $5, 'pending', $6, $7)
    `,
		account.ID,
		input.DeviceInstallID,
		strings.TrimSpace(input.DeviceLabel),
		strings.TrimSpace(input.Platform),
		TokenHash(token),
		expiresAt,
		now); err != nil {
		return "", time.Time{}, fmt.Errorf("create device challenge: %w", err)
	}

	if err := recordSecurityEventTx(
		ctx,
		tx,
		account.ID,
		"new_device_challenge_created",
		"",
		now,
		map[string]any{"target_install_id": input.DeviceInstallID}); err != nil {
		return "", time.Time{}, err
	}
	return token, expiresAt, nil
}

func (s *Service) PollDeviceSignInChallenge(ctx context.Context,
	challengeToken string) (SignInResult, error) {
	if challengeToken == "" {
		return SignInResult{}, ErrChallengeInvalid
	}
	now := s.clock.Now()
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return SignInResult{}, fmt.Errorf("begin device challenge poll: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var accountID string
	err = tx.QueryRow(ctx, `
        SELECT account_id::text
        FROM device_signin_challenges
        WHERE challenge_token_hash = $1
    `, TokenHash(challengeToken)).Scan(&accountID)
	if errors.Is(err, pgx.ErrNoRows) {
		return SignInResult{}, ErrChallengeInvalid
	}
	if err != nil {
		return SignInResult{}, fmt.Errorf("identify device challenge account: %w", err)
	}

	accountRecord, err := loadAuthAccountByIDTx(ctx, tx, accountID)
	if errors.Is(err, ErrInvalidCredentials) {
		return SignInResult{}, ErrChallengeInvalid
	}
	if err != nil {
		return SignInResult{}, fmt.Errorf("lock account for device challenge: %w", err)
	}

	var challengeID string
	var installID string
	var label string
	var platform string
	var state string
	var expiresAt time.Time
	err = tx.QueryRow(ctx, `
		SELECT
            id::text,
            target_install_id::text,
            target_label,
            target_platform,
            state,
            expires_at
        FROM device_signin_challenges
        WHERE challenge_token_hash = $1
        FOR UPDATE
	`, TokenHash(challengeToken)).Scan(
		&challengeID,
		&installID,
		&label,
		&platform,
		&state,
		&expiresAt)
	if errors.Is(err, pgx.ErrNoRows) {
		return SignInResult{}, ErrChallengeInvalid
	}
	if err != nil {
		return SignInResult{}, fmt.Errorf("load device challenge: %w", err)
	}
	now = s.clock.Now()

	if now.After(expiresAt) && state != "consumed" {
		if _, err := tx.Exec(ctx, `
            UPDATE device_signin_challenges
            SET state = 'denied',
                decided_at = $2
            WHERE id = $1::uuid
        `, challengeID, now); err != nil {
			return SignInResult{}, fmt.Errorf("expire device challenge: %w", err)
		}
		if err := tx.Commit(ctx); err != nil {
			return SignInResult{}, fmt.Errorf("commit device challenge expiry: %w", err)
		}
		return SignInResult{}, ErrChallengeExpired
	}

	switch state {
	case "pending":
		return SignInResult{
			Status:             "pending",
			ChallengeExpiresAt: expiresAt,
		}, nil
	case "denied":
		return SignInResult{}, ErrChallengeDenied
	case "consumed":
		return SignInResult{}, ErrChallengeInvalid
	case "approved":
	default:
		return SignInResult{}, ErrChallengeInvalid
	}

	command, err := tx.Exec(ctx, `
        UPDATE device_signin_challenges
        SET state = 'consumed',
            consumed_at = $2
        WHERE id = $1::uuid
          AND state = 'approved'
    `, challengeID, now)
	if err != nil {
		return SignInResult{}, fmt.Errorf("consume device challenge: %w", err)
	}
	if command.RowsAffected() != 1 {
		return SignInResult{}, ErrChallengeInvalid
	}

	device, err := upsertTrustedDeviceTx(
		ctx,
		tx,
		accountID,
		installID,
		label,
		platform,
		now)
	if err != nil {
		return SignInResult{}, err
	}
	session, err := s.issueSessionTx(
		ctx,
		tx,
		accountRecord.Account,
		device,
		now)
	if err != nil {
		return SignInResult{}, err
	}

	if err := recordSecurityEventTx(
		ctx,
		tx,
		accountID,
		"new_device_challenge_consumed",
		device.ID,
		now,
		map[string]any{}); err != nil {
		return SignInResult{}, err
	}

	if err := tx.Commit(ctx); err != nil {
		return SignInResult{}, fmt.Errorf("commit device challenge consumption: %w", err)
	}
	return SignInResult{
		Status:  "signed_in",
		Session: &session,
	}, nil
}

func (s *Service) Approvals(ctx context.Context,
	auth AuthenticatedSession) ([]DeviceApprovalRequest, error) {
	if !auth.Device.Trusted || auth.Device.RevokedAt != nil {
		return nil, ErrSessionInvalid
	}
	now := s.clock.Now()

	rows, err := s.pool.Query(ctx, `
        SELECT id::text, 'device_signin', target_label, target_platform, expires_at
        FROM device_signin_challenges
        WHERE account_id = $1::uuid
          AND target_install_id <> $2::uuid
          AND state = 'pending'
          AND expires_at > $3
        UNION ALL
        SELECT id::text, 'trusted_recovery', target_label, target_platform, expires_at
        FROM trusted_recovery_challenges
        WHERE account_id = $1::uuid
          AND target_install_id <> $2::uuid
          AND state = 'pending'
          AND expires_at > $3
        ORDER BY expires_at
    `, auth.Account.ID, auth.Device.InstallID, now)
	if err != nil {
		return nil, fmt.Errorf("list approval requests: %w", err)
	}
	defer rows.Close()

	var requests []DeviceApprovalRequest
	for rows.Next() {
		var request DeviceApprovalRequest
		if err := rows.Scan(
			&request.ID,
			&request.Kind,
			&request.DeviceLabel,
			&request.Platform,
			&request.ExpiresAt); err != nil {
			return nil, fmt.Errorf("scan approval request: %w", err)
		}
		requests = append(requests, request)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("iterate approval requests: %w", err)
	}
	return requests, nil
}

func (s *Service) DecideApproval(ctx context.Context,
	auth AuthenticatedSession,
	kind,
	challengeID string,
	approve bool) error {
	if !auth.Device.Trusted || auth.Device.RevokedAt != nil {
		return ErrSessionInvalid
	}
	if !IsUUID(challengeID) {
		return ErrChallengeInvalid
	}

	table := ""
	switch kind {
	case "device_signin":
		table = "device_signin_challenges"
	case "trusted_recovery":
		table = "trusted_recovery_challenges"
	default:
		return ErrChallengeInvalid
	}

	state := "denied"
	event := kind + "_denied"
	if approve {
		state = "approved"
		event = kind + "_approved"
	}

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return fmt.Errorf("begin approval decision: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, err := loadAuthAccountByIDTx(ctx, tx, auth.Account.ID); err != nil {
		if errors.Is(err, ErrInvalidCredentials) {
			return ErrChallengeInvalid
		}
		return fmt.Errorf("lock account for approval decision: %w", err)
	}

	var targetInstallID string
	var challengeState string
	var challengeExpiresAt time.Time
	if err := tx.QueryRow(ctx, fmt.Sprintf(`
        SELECT target_install_id::text, state, expires_at
        FROM %s
        WHERE id = $1::uuid
          AND account_id = $2::uuid
        FOR UPDATE
    `, table), challengeID, auth.Account.ID).Scan(
		&targetInstallID,
		&challengeState,
		&challengeExpiresAt); errors.Is(err, pgx.ErrNoRows) {
		return ErrChallengeInvalid
	} else if err != nil {
		return fmt.Errorf("lock approval challenge: %w", err)
	}
	now := s.clock.Now()
	if challengeState != "pending" || targetInstallID == auth.Device.InstallID || !challengeExpiresAt.After(now) {
		return ErrChallengeInvalid
	}

	query := fmt.Sprintf(`
        UPDATE %s
        SET state = $4,
            decided_at = $5,
            decided_by_device_id = $2::uuid
        WHERE id = $1::uuid
          AND account_id = $3::uuid
          AND state = 'pending'
    `, table)

	command, err := tx.Exec(
		ctx,
		query,
		challengeID,
		auth.Device.ID,
		auth.Account.ID,
		state,
		now)
	if err != nil {
		return fmt.Errorf("decide approval: %w", err)
	}
	if command.RowsAffected() != 1 {
		return ErrChallengeInvalid
	}
	if err := validateAuthenticatedSessionTx(ctx, tx, auth, s.clock.Now); err != nil {
		return err
	}
	if kind == "trusted_recovery" && !approve {
		if _, err := tx.Exec(ctx, `
            UPDATE trusted_recovery_challenges
            SET new_password_hash = ''
            WHERE id = $1::uuid
        `, challengeID); err != nil {
			return fmt.Errorf("clear denied recovery verifier: %w", err)
		}
	}
	if err := recordSecurityEventTx(
		ctx,
		tx,
		auth.Account.ID,
		event,
		auth.Device.ID,
		now,
		map[string]any{"challenge_id": challengeID}); err != nil {
		return err
	}
	return tx.Commit(ctx)
}

func (s *Service) RecoverDeviceSignInWithKey(ctx context.Context,
	input ChallengeRecoveryInput) (ChallengeRecoveryResult, error) {
	if input.ChallengeToken == "" {
		return ChallengeRecoveryResult{}, ErrChallengeInvalid
	}
	var challengeAccountID string
	err := s.pool.QueryRow(ctx, `
        SELECT account_id::text
        FROM device_signin_challenges
        WHERE challenge_token_hash = $1
    `, TokenHash(input.ChallengeToken)).Scan(&challengeAccountID)
	if errors.Is(err, pgx.ErrNoRows) {
		return ChallengeRecoveryResult{}, ErrChallengeInvalid
	}
	if err != nil {
		return ChallengeRecoveryResult{}, fmt.Errorf("identify challenge recovery account: %w", err)
	}
	if err := s.rateLimiter.Allow(
		ctx,
		"device_recovery_key",
		[]string{normalizedSourceKey(input.SourceKey), challengeAccountID},
		recoveryWindow,
		recoveryLimit); err != nil {
		return ChallengeRecoveryResult{}, err
	}

	now := s.clock.Now()

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return ChallengeRecoveryResult{}, fmt.Errorf("begin challenge recovery: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var accountID string
	err = tx.QueryRow(ctx, `
        SELECT account_id::text
        FROM device_signin_challenges
        WHERE challenge_token_hash = $1
    `, TokenHash(input.ChallengeToken)).Scan(&accountID)
	if errors.Is(err, pgx.ErrNoRows) {
		return ChallengeRecoveryResult{}, ErrChallengeInvalid
	}
	if err != nil {
		return ChallengeRecoveryResult{}, fmt.Errorf("load challenge recovery account: %w", err)
	}
	accountRecord, err := loadAuthAccountByIDTx(ctx, tx, accountID)
	if errors.Is(err, ErrInvalidCredentials) {
		return ChallengeRecoveryResult{}, ErrChallengeInvalid
	}
	if err != nil {
		return ChallengeRecoveryResult{}, fmt.Errorf("lock account for challenge recovery: %w", err)
	}

	var challengeID string
	var installID string
	var label string
	var platform string
	var state string
	var expiresAt time.Time
	err = tx.QueryRow(ctx, `
		SELECT
            id::text,
            target_install_id::text,
            target_label,
            target_platform,
            state,
            expires_at
        FROM device_signin_challenges
        WHERE challenge_token_hash = $1
        FOR UPDATE
	`, TokenHash(input.ChallengeToken)).Scan(
		&challengeID,
		&installID,
		&label,
		&platform,
		&state,
		&expiresAt)
	if errors.Is(err, pgx.ErrNoRows) {
		return ChallengeRecoveryResult{}, ErrChallengeInvalid
	}
	if err != nil {
		return ChallengeRecoveryResult{}, fmt.Errorf("load challenge recovery: %w", err)
	}
	now = s.clock.Now()
	if state == "consumed" || state == "denied" {
		return ChallengeRecoveryResult{}, ErrChallengeInvalid
	}
	if now.After(expiresAt) {
		return ChallengeRecoveryResult{}, ErrChallengeExpired
	}

	if !s.recoveryVerifier.Verify(input.RecoveryKey, accountRecord.RecoveryVerifier) {
		return ChallengeRecoveryResult{}, ErrRecoveryKeyInvalid
	}

	newRecoveryKey, err := GenerateRecoveryKey()
	if err != nil {
		return ChallengeRecoveryResult{}, err
	}
	newVerifier, err := s.recoveryVerifier.Sum(newRecoveryKey)
	if err != nil {
		return ChallengeRecoveryResult{}, err
	}
	if _, err := tx.Exec(ctx, `
        UPDATE accounts
        SET recovery_key_verifier = $2,
            recovery_key_version = recovery_key_version + 1,
            updated_at = $3
        WHERE id = $1::uuid
    `, accountID, newVerifier, now); err != nil {
		return ChallengeRecoveryResult{}, fmt.Errorf("replace recovery key after device approval fallback: %w", err)
	}
	if err := invalidateAuthChallengesTx(
		ctx,
		tx,
		accountID,
		now,
		challengeID,
		""); err != nil {
		return ChallengeRecoveryResult{}, fmt.Errorf("invalidate sibling auth challenges after device recovery: %w", err)
	}

	device, err := upsertTrustedDeviceTx(
		ctx,
		tx,
		accountID,
		installID,
		label,
		platform,
		now)
	if err != nil {
		return ChallengeRecoveryResult{}, err
	}
	session, err := s.issueSessionTx(ctx, tx, accountRecord.Account, device, now)
	if err != nil {
		return ChallengeRecoveryResult{}, err
	}

	command, err := tx.Exec(ctx, `
        UPDATE device_signin_challenges
        SET state = 'consumed',
            consumed_at = $2
        WHERE id = $1::uuid
          AND state <> 'consumed'
    `, challengeID, now)
	if err != nil {
		return ChallengeRecoveryResult{}, fmt.Errorf("consume recovered device challenge: %w", err)
	}
	if command.RowsAffected() != 1 {
		return ChallengeRecoveryResult{}, ErrChallengeInvalid
	}

	if err := recordSecurityEventTx(
		ctx,
		tx,
		accountID,
		"new_device_recovery_key_used",
		device.ID,
		now,
		map[string]any{}); err != nil {
		return ChallengeRecoveryResult{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return ChallengeRecoveryResult{}, fmt.Errorf("commit device recovery fallback: %w", err)
	}
	return ChallengeRecoveryResult{
		Session:     session,
		RecoveryKey: newRecoveryKey,
	}, nil
}
