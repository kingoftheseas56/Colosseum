package account

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"strings"
	"time"

	"github.com/jackc/pgx/v5"
)

func (s *Service) createDeviceSignInChallenge(ctx context.Context,
	account Account,
	input SignInInput) (SignInResult, error) {
	token, err := GenerateToken()
	if err != nil {
		return SignInResult{}, err
	}
	now := s.clock.Now()
	expiresAt := now.Add(deviceChallengeLifetime)

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return SignInResult{}, fmt.Errorf("begin device challenge: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, err := tx.Exec(ctx, `
        UPDATE device_signin_challenges
        SET state = 'denied',
            decided_at = $3
        WHERE account_id = $1::uuid
          AND target_install_id = $2::uuid
          AND state IN ('pending', 'approved')
    `, account.ID, input.DeviceInstallID, now); err != nil {
		return SignInResult{}, fmt.Errorf("supersede device challenge: %w", err)
	}

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
		return SignInResult{}, fmt.Errorf("create device challenge: %w", err)
	}

	if err := recordSecurityEventTx(
		ctx,
		tx,
		account.ID,
		"new_device_challenge_created",
		"",
		now,
		map[string]any{"target_install_id": input.DeviceInstallID}); err != nil {
		return SignInResult{}, err
	}

	if err := tx.Commit(ctx); err != nil {
		return SignInResult{}, fmt.Errorf("commit device challenge: %w", err)
	}
	return SignInResult{
		Status:             "approval_required",
		ChallengeToken:     token,
		ChallengeExpiresAt: expiresAt,
	}, nil
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

	var challengeID string
	var accountID string
	var installID string
	var label string
	var platform string
	var state string
	var expiresAt time.Time
	err = tx.QueryRow(ctx, `
        SELECT
            id::text,
            account_id::text,
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
		&accountID,
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

	accountRecord, err := s.loadAuthAccountByID(ctx, accountID)
	if err != nil {
		return SignInResult{}, err
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
	now := s.clock.Now()

	query := fmt.Sprintf(`
        UPDATE %s
        SET state = $4,
            decided_at = $5,
            decided_by_device_id = $2::uuid
        WHERE id = $1::uuid
          AND account_id = $3::uuid
          AND target_install_id <> $6::uuid
          AND state = 'pending'
          AND expires_at > $5
    `, table)

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return fmt.Errorf("begin approval decision: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	command, err := tx.Exec(
		ctx,
		query,
		challengeID,
		auth.Device.ID,
		auth.Account.ID,
		state,
		now,
		auth.Device.InstallID)
	if err != nil {
		return fmt.Errorf("decide approval: %w", err)
	}
	if command.RowsAffected() != 1 {
		return ErrChallengeInvalid
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
	now := s.clock.Now()

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return ChallengeRecoveryResult{}, fmt.Errorf("begin challenge recovery: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var challengeID string
	var accountID string
	var installID string
	var label string
	var platform string
	var state string
	var expiresAt time.Time
	err = tx.QueryRow(ctx, `
        SELECT
            id::text,
            account_id::text,
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
		&accountID,
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
	if state == "consumed" || state == "denied" {
		return ChallengeRecoveryResult{}, ErrChallengeInvalid
	}
	if now.After(expiresAt) {
		return ChallengeRecoveryResult{}, ErrChallengeExpired
	}

	if err := s.rateLimiter.Allow(
		ctx,
		"device_recovery_key",
		[]string{normalizedSourceKey(input.SourceKey), accountID},
		recoveryWindow,
		recoveryLimit); err != nil {
		return ChallengeRecoveryResult{}, err
	}

	var accountRecord authAccount
	var changedAt sql.NullTime
	err = tx.QueryRow(ctx, accountSelect+`
        WHERE id = $1::uuid
        FOR UPDATE
    `, accountID).Scan(
		&accountRecord.ID,
		&accountRecord.CanonicalUsername,
		&accountRecord.DisplayUsername,
		&accountRecord.PasswordHash,
		&accountRecord.RecoveryVerifier,
		&accountRecord.RecoveryVersion,
		&accountRecord.ProtectNewDeviceSignins,
		&accountRecord.BuiltinAvatarID,
		&accountRecord.UploadedAvatarObjectKey,
		&changedAt,
		&accountRecord.CreatedAt,
		&accountRecord.UpdatedAt)
	if err != nil {
		return ChallengeRecoveryResult{}, fmt.Errorf("lock account for challenge recovery: %w", err)
	}
	if changedAt.Valid {
		changed := changedAt.Time.UTC()
		accountRecord.UsernameChangedAt = &changed
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
