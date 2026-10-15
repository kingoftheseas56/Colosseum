package account

import (
	"context"
	"crypto/subtle"
	"database/sql"
	"errors"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5"
)

func (s *Service) AuthenticateAccessToken(ctx context.Context, token string) (AuthenticatedSession, error) {
	if token == "" {
		return AuthenticatedSession{}, ErrSessionInvalid
	}
	now := s.clock.Now()
	hash := TokenHash(token)

	row := s.pool.QueryRow(ctx, `
        SELECT
            s.id::text,
            a.id::text,
            a.canonical_username,
            a.display_username,
            a.protect_new_device_signins,
            COALESCE(a.builtin_avatar_id, ''),
            COALESCE(a.uploaded_avatar_object_key, ''),
            a.username_changed_at,
            a.created_at,
            a.updated_at,
            d.id::text,
            d.account_id::text,
            d.install_id::text,
            d.label,
            d.platform,
            d.trusted,
            d.revoked_at,
            d.created_at,
            d.last_seen_at
        FROM sessions s
        JOIN accounts a ON a.id = s.account_id
        JOIN devices d ON d.id = s.device_id
        WHERE s.access_token_hash = $1
          AND s.access_expires_at > $2
          AND s.revoked_at IS NULL
          AND d.revoked_at IS NULL
    `, hash, now)

	var auth AuthenticatedSession
	var usernameChanged sql.NullTime
	var deviceRevoked sql.NullTime
	err := row.Scan(
		&auth.SessionID,
		&auth.Account.ID,
		&auth.Account.CanonicalUsername,
		&auth.Account.DisplayUsername,
		&auth.Account.ProtectNewDeviceSignins,
		&auth.Account.BuiltinAvatarID,
		&auth.Account.UploadedAvatarObjectKey,
		&usernameChanged,
		&auth.Account.CreatedAt,
		&auth.Account.UpdatedAt,
		&auth.Device.ID,
		&auth.Device.AccountID,
		&auth.Device.InstallID,
		&auth.Device.Label,
		&auth.Device.Platform,
		&auth.Device.Trusted,
		&deviceRevoked,
		&auth.Device.CreatedAt,
		&auth.Device.LastSeenAt)
	if errors.Is(err, pgx.ErrNoRows) {
		return AuthenticatedSession{}, ErrSessionInvalid
	}
	if err != nil {
		return AuthenticatedSession{}, fmt.Errorf("authenticate access token: %w", err)
	}
	if usernameChanged.Valid {
		changed := usernameChanged.Time.UTC()
		auth.Account.UsernameChangedAt = &changed
	}
	if deviceRevoked.Valid {
		revoked := deviceRevoked.Time.UTC()
		auth.Device.RevokedAt = &revoked
	}
	return auth, nil
}

func (s *Service) RefreshSession(ctx context.Context, refreshToken string) (RefreshResult, error) {
	if refreshToken == "" {
		return RefreshResult{}, ErrSessionInvalid
	}
	hash := TokenHash(refreshToken)

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return RefreshResult{}, fmt.Errorf("begin refresh: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var accountID string
	err = tx.QueryRow(ctx, `
        SELECT account_id::text
        FROM sessions
        WHERE refresh_token_hash = $1
           OR previous_refresh_token_hash = $1
        ORDER BY last_refreshed_at DESC
        LIMIT 1
    `, hash).Scan(&accountID)
	if errors.Is(err, pgx.ErrNoRows) {
		return RefreshResult{}, ErrSessionInvalid
	}
	if err != nil {
		return RefreshResult{}, fmt.Errorf("identify refresh account: %w", err)
	}

	accountRecord, err := loadAuthAccountByIDTx(ctx, tx, accountID)
	if errors.Is(err, ErrInvalidCredentials) {
		return RefreshResult{}, ErrSessionInvalid
	}
	if err != nil {
		return RefreshResult{}, fmt.Errorf("lock refresh account: %w", err)
	}

	var sessionID string
	var deviceID string
	err = tx.QueryRow(ctx, `
        SELECT id::text, device_id::text
        FROM sessions
        WHERE account_id = $1::uuid
          AND (refresh_token_hash = $2 OR previous_refresh_token_hash = $2)
        ORDER BY last_refreshed_at DESC
        LIMIT 1
    `, accountID, hash).Scan(&sessionID, &deviceID)
	if errors.Is(err, pgx.ErrNoRows) {
		return RefreshResult{}, ErrSessionInvalid
	}
	if err != nil {
		return RefreshResult{}, fmt.Errorf("load refresh session identity: %w", err)
	}

	device, err := loadDeviceByIDTx(ctx, tx, accountID, deviceID)
	if errors.Is(err, ErrDeviceNotFound) {
		return RefreshResult{}, ErrSessionInvalid
	}
	if err != nil {
		return RefreshResult{}, fmt.Errorf("lock refresh device: %w", err)
	}

	var currentHash []byte
	var previousHash []byte
	var previousExpires sql.NullTime
	var retryCiphertext []byte
	var sessionRevoked sql.NullTime
	var lockedDeviceID string
	err = tx.QueryRow(ctx, `
        SELECT
            device_id::text,
            refresh_token_hash,
            previous_refresh_token_hash,
            previous_refresh_expires_at,
            refresh_retry_ciphertext,
            revoked_at
        FROM sessions
        WHERE id = $1::uuid
          AND account_id = $2::uuid
        FOR UPDATE
    `, sessionID, accountID).Scan(
		&lockedDeviceID,
		&currentHash,
		&previousHash,
		&previousExpires,
		&retryCiphertext,
		&sessionRevoked)
	if errors.Is(err, pgx.ErrNoRows) {
		return RefreshResult{}, ErrSessionInvalid
	}
	if err != nil {
		return RefreshResult{}, fmt.Errorf("load refresh session: %w", err)
	}
	if lockedDeviceID != device.ID {
		return RefreshResult{}, ErrSessionInvalid
	}
	device.AccountID = accountRecord.ID
	if sessionRevoked.Valid || device.RevokedAt != nil {
		return RefreshResult{}, ErrSessionRevoked
	}

	matchesCurrent := subtle.ConstantTimeCompare(hash, currentHash) == 1
	matchesPrevious := len(previousHash) > 0 &&
		subtle.ConstantTimeCompare(hash, previousHash) == 1

	if !matchesCurrent && !matchesPrevious {
		return RefreshResult{}, ErrSessionInvalid
	}
	now := s.clock.Now()

	var accessToken string
	var accessExpiresAt time.Time
	var returnedRefreshToken string

	if matchesPrevious {
		if !previousExpires.Valid || now.After(previousExpires.Time) {
			if _, err := tx.Exec(ctx, `
                UPDATE sessions
                SET revoked_at = $2
                WHERE id = $1::uuid
            `, sessionID, now); err != nil {
				return RefreshResult{}, fmt.Errorf("revoke replayed refresh session: %w", err)
			}
			if err := tx.Commit(ctx); err != nil {
				return RefreshResult{}, fmt.Errorf("commit replay revocation: %w", err)
			}
			return RefreshResult{}, ErrSessionRevoked
		}

		returnedRefreshToken, err = s.sessionCipher.Open(retryCiphertext)
		if err != nil {
			return RefreshResult{}, fmt.Errorf("open refresh retry token: %w", err)
		}
		if subtle.ConstantTimeCompare(
			TokenHash(returnedRefreshToken),
			currentHash) != 1 {
			return RefreshResult{}, ErrSessionInvalid
		}

		accessToken, err = GenerateToken()
		if err != nil {
			return RefreshResult{}, err
		}
		accessExpiresAt = now.Add(accessTokenLifetime)
		if _, err := tx.Exec(ctx, `
            UPDATE sessions
            SET access_token_hash = $2,
                access_expires_at = $3,
                last_refreshed_at = $4
            WHERE id = $1::uuid
        `, sessionID, TokenHash(accessToken), accessExpiresAt, now); err != nil {
			return RefreshResult{}, fmt.Errorf("refresh access token after retry: %w", err)
		}
	} else {
		returnedRefreshToken, err = GenerateToken()
		if err != nil {
			return RefreshResult{}, err
		}
		retryCiphertext, err = s.sessionCipher.Seal(returnedRefreshToken)
		if err != nil {
			return RefreshResult{}, err
		}
		accessToken, err = GenerateToken()
		if err != nil {
			return RefreshResult{}, err
		}
		accessExpiresAt = now.Add(accessTokenLifetime)
		if _, err := tx.Exec(ctx, `
            UPDATE sessions
            SET access_token_hash = $2,
                access_expires_at = $3,
                previous_refresh_token_hash = refresh_token_hash,
                previous_refresh_expires_at = $4,
                refresh_token_hash = $5,
                refresh_retry_ciphertext = $6,
                last_refreshed_at = $7
            WHERE id = $1::uuid
        `,
			sessionID,
			TokenHash(accessToken),
			accessExpiresAt,
			now.Add(refreshRetryGrace),
			TokenHash(returnedRefreshToken),
			retryCiphertext,
			now); err != nil {
			return RefreshResult{}, fmt.Errorf("rotate refresh token: %w", err)
		}
	}

	if _, err := tx.Exec(ctx, `
        UPDATE devices
        SET last_seen_at = $2
        WHERE id = $1::uuid
    `, device.ID, now); err != nil {
		return RefreshResult{}, fmt.Errorf("touch refreshed device: %w", err)
	}
	device.LastSeenAt = now

	if err := tx.Commit(ctx); err != nil {
		return RefreshResult{}, fmt.Errorf("commit session refresh: %w", err)
	}

	return RefreshResult{
		Session: IssuedSession{
			Account:         accountRecord.Account,
			Device:          device,
			AccessToken:     accessToken,
			AccessExpiresAt: accessExpiresAt,
			RefreshToken:    returnedRefreshToken,
		},
	}, nil
}

func (s *Service) RevokeRefreshToken(ctx context.Context, refreshToken string) error {
	if refreshToken == "" {
		return nil
	}
	hash := TokenHash(refreshToken)
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return fmt.Errorf("begin refresh-token revocation: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var accountID string
	err = tx.QueryRow(ctx, `
        SELECT account_id::text
        FROM sessions
        WHERE refresh_token_hash = $1
           OR previous_refresh_token_hash = $1
        ORDER BY last_refreshed_at DESC
        LIMIT 1
    `, hash).Scan(&accountID)
	if errors.Is(err, pgx.ErrNoRows) {
		return nil
	}
	if err != nil {
		return fmt.Errorf("identify refresh-token account: %w", err)
	}
	if _, err := loadAuthAccountByIDTx(ctx, tx, accountID); err != nil {
		if errors.Is(err, ErrInvalidCredentials) {
			return nil
		}
		return fmt.Errorf("lock refresh-token account: %w", err)
	}
	now := s.clock.Now()
	if _, err := tx.Exec(ctx, `
        UPDATE sessions
        SET revoked_at = COALESCE(revoked_at, $2)
        WHERE account_id = $3::uuid
          AND (refresh_token_hash = $1 OR previous_refresh_token_hash = $1)
    `, hash, now, accountID); err != nil {
		return fmt.Errorf("revoke refresh token: %w", err)
	}
	if err := tx.Commit(ctx); err != nil {
		return fmt.Errorf("commit refresh-token revocation: %w", err)
	}
	return nil
}

func (s *Service) LogoutCurrent(ctx context.Context, auth AuthenticatedSession) error {
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return fmt.Errorf("begin current-session logout: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()
	if _, err := loadAuthAccountByIDTx(ctx, tx, auth.Account.ID); err != nil {
		if errors.Is(err, ErrInvalidCredentials) {
			return ErrSessionInvalid
		}
		return fmt.Errorf("lock current-session account: %w", err)
	}
	now := s.clock.Now()
	if _, err := tx.Exec(ctx, `
        UPDATE sessions
        SET revoked_at = $3
        WHERE id = $1::uuid
          AND account_id = $2::uuid
          AND revoked_at IS NULL
    `, auth.SessionID, auth.Account.ID, now); err != nil {
		return fmt.Errorf("logout current session: %w", err)
	}
	if err := tx.Commit(ctx); err != nil {
		return fmt.Errorf("commit current-session logout: %w", err)
	}
	return nil
}

func (s *Service) LogoutEverywhere(ctx context.Context, auth AuthenticatedSession) error {
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return fmt.Errorf("begin logout everywhere: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()
	if _, err := loadAuthAccountByIDTx(ctx, tx, auth.Account.ID); err != nil {
		if errors.Is(err, ErrInvalidCredentials) {
			return ErrSessionInvalid
		}
		return fmt.Errorf("lock logout-everywhere account: %w", err)
	}
	if err := validateAuthenticatedSessionTx(ctx, tx, auth, s.clock.Now); err != nil {
		return err
	}
	now := s.clock.Now()

	if _, err := tx.Exec(ctx, `
        UPDATE sessions
        SET revoked_at = $2
        WHERE account_id = $1::uuid
          AND revoked_at IS NULL
    `, auth.Account.ID, now); err != nil {
		return fmt.Errorf("revoke all sessions: %w", err)
	}
	if err := recordSecurityEventTx(
		ctx,
		tx,
		auth.Account.ID,
		"logout_everywhere",
		auth.Device.ID,
		now,
		map[string]any{}); err != nil {
		return err
	}
	if err := tx.Commit(ctx); err != nil {
		return fmt.Errorf("commit logout everywhere: %w", err)
	}
	return nil
}
