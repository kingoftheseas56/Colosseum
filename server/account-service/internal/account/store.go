package account

import (
	"context"
	"database/sql"
	"encoding/json"
	"errors"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
)

type authAccount struct {
	Account
	PasswordHash     string
	RecoveryVerifier []byte
	RecoveryVersion  int
}

func (s *Service) loadAuthAccountByCanonical(ctx context.Context, canonical string) (authAccount, error) {
	return scanAuthAccount(s.pool.QueryRow(ctx, accountSelect+`
        WHERE canonical_username = $1
    `, canonical))
}

func (s *Service) loadAuthAccountByID(ctx context.Context, accountID string) (authAccount, error) {
	return scanAuthAccount(s.pool.QueryRow(ctx, accountSelect+`
        WHERE id = $1::uuid
    `, accountID))
}

const accountSelect = `
    SELECT
        id::text,
        canonical_username,
        display_username,
        password_hash,
        recovery_key_verifier,
        recovery_key_version,
        protect_new_device_signins,
        COALESCE(builtin_avatar_id, ''),
        COALESCE(uploaded_avatar_object_key, ''),
        username_changed_at,
        created_at,
        updated_at
    FROM accounts
`

func scanAuthAccount(row pgx.Row) (authAccount, error) {
	var value authAccount
	var usernameChanged sql.NullTime
	err := row.Scan(
		&value.ID,
		&value.CanonicalUsername,
		&value.DisplayUsername,
		&value.PasswordHash,
		&value.RecoveryVerifier,
		&value.RecoveryVersion,
		&value.ProtectNewDeviceSignins,
		&value.BuiltinAvatarID,
		&value.UploadedAvatarObjectKey,
		&usernameChanged,
		&value.CreatedAt,
		&value.UpdatedAt)
	if errors.Is(err, pgx.ErrNoRows) {
		return authAccount{}, ErrInvalidCredentials
	}
	if err != nil {
		return authAccount{}, fmt.Errorf("load account: %w", err)
	}
	if usernameChanged.Valid {
		changed := usernameChanged.Time.UTC()
		value.UsernameChangedAt = &changed
	}
	return value, nil
}

func (s *Service) loadDeviceByInstall(ctx context.Context,
	accountID,
	installID string) (Device, bool, error) {
	row := s.pool.QueryRow(ctx, `
        SELECT
            id::text,
            account_id::text,
            install_id::text,
            label,
            platform,
            trusted,
            revoked_at,
            created_at,
            last_seen_at
        FROM devices
        WHERE account_id = $1::uuid
          AND install_id = $2::uuid
    `, accountID, installID)

	device, err := scanDevice(row)
	if errors.Is(err, pgx.ErrNoRows) {
		return Device{}, false, nil
	}
	if err != nil {
		return Device{}, false, err
	}
	return device, true, nil
}

func scanDevice(row pgx.Row) (Device, error) {
	return scanDeviceFields(row)
}

func scanDeviceFields(row pgx.Row) (Device, error) {
	var value Device
	var revoked sql.NullTime
	if err := row.Scan(
		&value.ID,
		&value.AccountID,
		&value.InstallID,
		&value.Label,
		&value.Platform,
		&value.Trusted,
		&revoked,
		&value.CreatedAt,
		&value.LastSeenAt); err != nil {
		return Device{}, err
	}
	if revoked.Valid {
		revokedAt := revoked.Time.UTC()
		value.RevokedAt = &revokedAt
	}
	return value, nil
}

func upsertTrustedDeviceTx(ctx context.Context,
	tx pgx.Tx,
	accountID,
	installID,
	label,
	platform string,
	now time.Time) (Device, error) {
	row := tx.QueryRow(ctx, `
        INSERT INTO devices(
            account_id,
            install_id,
            label,
            platform,
            trusted,
            revoked_at,
            created_at,
            last_seen_at
        )
        VALUES($1::uuid, $2::uuid, $3, $4, true, NULL, $5, $5)
        ON CONFLICT(account_id, install_id)
        DO UPDATE SET
            label = EXCLUDED.label,
            platform = EXCLUDED.platform,
            trusted = true,
            revoked_at = NULL,
            last_seen_at = EXCLUDED.last_seen_at
        RETURNING
            id::text,
            account_id::text,
            install_id::text,
            label,
            platform,
            trusted,
            revoked_at,
            created_at,
            last_seen_at
    `, accountID, installID, label, platform, now)
	return scanDeviceFields(row)
}

func (s *Service) issueSessionTx(ctx context.Context,
	tx pgx.Tx,
	account Account,
	device Device,
	now time.Time) (IssuedSession, error) {
	accessToken, err := GenerateToken()
	if err != nil {
		return IssuedSession{}, err
	}
	refreshToken, err := GenerateToken()
	if err != nil {
		return IssuedSession{}, err
	}

	if _, err := tx.Exec(ctx, `
        UPDATE sessions
        SET revoked_at = $2
        WHERE device_id = $1::uuid
          AND revoked_at IS NULL
    `, device.ID, now); err != nil {
		return IssuedSession{}, fmt.Errorf("revoke previous device session: %w", err)
	}

	accessExpiresAt := now.Add(accessTokenLifetime)
	if _, err := tx.Exec(ctx, `
        INSERT INTO sessions(
            account_id,
            device_id,
            access_token_hash,
            access_expires_at,
            refresh_token_hash,
            previous_refresh_token_hash,
            previous_refresh_expires_at,
            refresh_retry_ciphertext,
            revoked_at,
            created_at,
            last_refreshed_at
        )
        VALUES(
            $1::uuid,
            $2::uuid,
            $3,
            $4,
            $5,
            NULL,
            NULL,
            NULL,
            NULL,
            $6,
            $6
        )
    `,
		account.ID,
		device.ID,
		TokenHash(accessToken),
		accessExpiresAt,
		TokenHash(refreshToken),
		now); err != nil {
		return IssuedSession{}, fmt.Errorf("create session: %w", err)
	}

	return IssuedSession{
		Account:         account,
		Device:          device,
		AccessToken:     accessToken,
		AccessExpiresAt: accessExpiresAt,
		RefreshToken:    refreshToken,
	}, nil
}

func recordSecurityEventTx(ctx context.Context,
	tx pgx.Tx,
	accountID,
	eventType,
	deviceID string,
	now time.Time,
	metadata map[string]any) error {
	payload, err := json.Marshal(metadata)
	if err != nil {
		return fmt.Errorf("encode security event metadata: %w", err)
	}
	var nullableDevice any
	if deviceID != "" {
		nullableDevice = deviceID
	}
	if _, err := tx.Exec(ctx, `
        INSERT INTO account_security_events(
            account_id,
            event_type,
            device_id,
            occurred_at,
            metadata
        )
        VALUES($1::uuid, $2, $3::uuid, $4, $5::jsonb)
    `, accountID, eventType, nullableDevice, now, payload); err != nil {
		return fmt.Errorf("record security event: %w", err)
	}
	return nil
}

func isUniqueViolation(err error) bool {
	var pgError *pgconn.PgError
	return errors.As(err, &pgError) && pgError.Code == "23505"
}
