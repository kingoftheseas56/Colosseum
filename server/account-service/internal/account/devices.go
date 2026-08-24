package account

import (
	"context"
	"database/sql"
	"fmt"

	"github.com/jackc/pgx/v5"
)

func (s *Service) ListDevices(ctx context.Context,
	auth AuthenticatedSession) ([]DeviceView, error) {
	rows, err := s.pool.Query(ctx, `
        SELECT
            d.id::text,
            d.account_id::text,
            d.install_id::text,
            d.label,
            d.platform,
            d.trusted,
            d.revoked_at,
            d.created_at,
            d.last_seen_at,
            EXISTS(
                SELECT 1
                FROM sessions s
                WHERE s.device_id = d.id
                  AND s.revoked_at IS NULL
            ) AS active
        FROM devices d
        WHERE d.account_id = $1::uuid
          AND d.revoked_at IS NULL
        ORDER BY
            CASE WHEN d.id = $2::uuid THEN 0 ELSE 1 END,
            d.last_seen_at DESC
    `, auth.Account.ID, auth.Device.ID)
	if err != nil {
		return nil, fmt.Errorf("list devices: %w", err)
	}
	defer rows.Close()

	var devices []DeviceView
	for rows.Next() {
		var view DeviceView
		var revoked sql.NullTime
		if err := rows.Scan(
			&view.ID,
			&view.AccountID,
			&view.InstallID,
			&view.Label,
			&view.Platform,
			&view.Trusted,
			&revoked,
			&view.CreatedAt,
			&view.LastSeenAt,
			&view.Active); err != nil {
			return nil, fmt.Errorf("scan device: %w", err)
		}
		view.Current = view.ID == auth.Device.ID
		devices = append(devices, view)
	}
	if err := rows.Err(); err != nil {
		return nil, fmt.Errorf("iterate devices: %w", err)
	}
	return devices, nil
}

func (s *Service) RevokeDevice(ctx context.Context,
	auth AuthenticatedSession,
	deviceID string) error {
	if !IsUUID(deviceID) {
		return ErrDeviceNotFound
	}
	now := s.clock.Now()
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return fmt.Errorf("begin device revoke: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	command, err := tx.Exec(ctx, `
        UPDATE devices
        SET trusted = false,
            revoked_at = $3,
            last_seen_at = $3
        WHERE id = $1::uuid
          AND account_id = $2::uuid
          AND revoked_at IS NULL
    `, deviceID, auth.Account.ID, now)
	if err != nil {
		return fmt.Errorf("revoke device: %w", err)
	}
	if command.RowsAffected() != 1 {
		return ErrDeviceNotFound
	}

	if _, err := tx.Exec(ctx, `
        UPDATE sessions
        SET revoked_at = $2
        WHERE device_id = $1::uuid
          AND revoked_at IS NULL
    `, deviceID, now); err != nil {
		return fmt.Errorf("revoke device sessions: %w", err)
	}

	if err := recordSecurityEventTx(
		ctx,
		tx,
		auth.Account.ID,
		"device_revoked",
		auth.Device.ID,
		now,
		map[string]any{"revoked_device_id": deviceID}); err != nil {
		return err
	}
	if err := tx.Commit(ctx); err != nil {
		return fmt.Errorf("commit device revoke: %w", err)
	}
	return nil
}

func (s *Service) SetNewDeviceProtection(ctx context.Context,
	auth AuthenticatedSession,
	enabled bool) (Account, error) {
	now := s.clock.Now()
	command, err := s.pool.Exec(ctx, `
        UPDATE accounts
        SET protect_new_device_signins = $2,
            updated_at = $3
        WHERE id = $1::uuid
    `, auth.Account.ID, enabled, now)
	if err != nil {
		return Account{}, fmt.Errorf("update new-device protection: %w", err)
	}
	if command.RowsAffected() != 1 {
		return Account{}, ErrSessionInvalid
	}
	updated, err := s.loadAuthAccountByID(ctx, auth.Account.ID)
	if err != nil {
		return Account{}, err
	}
	return updated.Account, nil
}

func (s *Service) DeviceStillActive(ctx context.Context,
	accountID,
	deviceID string) (bool, error) {
	var active bool
	err := s.pool.QueryRow(ctx, `
        SELECT EXISTS(
            SELECT 1
            FROM devices d
            WHERE d.id = $1::uuid
              AND d.account_id = $2::uuid
              AND d.revoked_at IS NULL
        )
    `, deviceID, accountID).Scan(&active)
	if err != nil {
		return false, fmt.Errorf("check device active state: %w", err)
	}
	return active, nil
}
