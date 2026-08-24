package account

import (
	"context"
	"database/sql"
	"errors"
	"fmt"
	"strings"

	"github.com/jackc/pgx/v5"
	"golang.org/x/text/unicode/norm"
)

func (s *Service) CreateAccount(ctx context.Context, input CreateAccountInput) (CreateAccountResult, error) {
	// Rate-limit BEFORE validation so unauthenticated callers cannot probe
	// usernames (or burn CPU on hashing) without consuming limiter budget —
	// mirrors SignIn's ordering. TestCreateAttemptRateLimitStopsEleventhAttempt
	// depends on this: invalid-username attempts must still count.
	sourceKey := normalizedSourceKey(input.SourceKey)
	if err := s.rateLimiter.Allow(
		ctx,
		"create_attempt_source",
		[]string{sourceKey},
		createAttemptWindow,
		createAttemptLimit); err != nil {
		return CreateAccountResult{}, err
	}

	displayUsername, canonicalUsername, err := NormalizeUsername(input.Username)
	if err != nil {
		return CreateAccountResult{}, err
	}
	if err := ValidateDeviceIdentity(
		input.DeviceInstallID,
		input.DeviceLabel,
		input.Platform); err != nil {
		return CreateAccountResult{}, err
	}

	if err := s.rateLimiter.Allow(
		ctx,
		"create_attempt_username",
		[]string{sourceKey, canonicalUsername},
		createUsernameWindow,
		createUsernameLimit); err != nil {
		return CreateAccountResult{}, err
	}

	password, err := s.passwordPolicy.Validate(input.Password, canonicalUsername)
	if err != nil {
		return CreateAccountResult{}, err
	}
	passwordHash, err := s.passwordHasher.Hash(password)
	if err != nil {
		return CreateAccountResult{}, fmt.Errorf("hash password: %w", err)
	}

	recoveryKey, err := GenerateRecoveryKey()
	if err != nil {
		return CreateAccountResult{}, err
	}
	recoveryVerifier, err := s.recoveryVerifier.Sum(recoveryKey)
	if err != nil {
		return CreateAccountResult{}, err
	}

	now := s.clock.Now()
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return CreateAccountResult{}, fmt.Errorf("begin account creation: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if _, err := tx.Exec(ctx, `
        INSERT INTO username_reservations(
            canonical_username,
            reserved_account_id,
            reserved_at
        )
        VALUES($1, NULL, $2)
    `, canonicalUsername, now); err != nil {
		if isUniqueViolation(err) {
			return CreateAccountResult{}, ErrUsernameUnavailable
		}
		return CreateAccountResult{}, fmt.Errorf("reserve username: %w", err)
	}

	sourceSlot, err := s.rateLimiter.Reserve(
		ctx,
		"create_success_source",
		[]string{sourceKey},
		createSuccessWindow,
		createSuccessLimit)
	if err != nil {
		return CreateAccountResult{}, err
	}
	defer sourceSlot.Release(context.Background())

	globalSlot, err := s.rateLimiter.Reserve(
		ctx,
		"create_success_global",
		[]string{"global"},
		createGlobalWindow,
		s.registrationGlobalLimit)
	if err != nil {
		return CreateAccountResult{}, err
	}
	defer globalSlot.Release(context.Background())

	var account authAccount
	var usernameChangedAt sql.NullTime
	err = tx.QueryRow(ctx, `
        INSERT INTO accounts(
            canonical_username,
            display_username,
            password_hash,
            recovery_key_verifier,
            recovery_key_version,
            protect_new_device_signins,
            builtin_avatar_id,
            uploaded_avatar_object_key,
            username_changed_at,
            created_at,
            updated_at
        )
        VALUES($1, $2, $3, $4, 1, false, NULL, NULL, NULL, $5, $5)
        RETURNING
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
    `,
		canonicalUsername,
		displayUsername,
		passwordHash,
		recoveryVerifier,
		now).Scan(
		&account.ID,
		&account.CanonicalUsername,
		&account.DisplayUsername,
		&account.PasswordHash,
		&account.RecoveryVerifier,
		&account.RecoveryVersion,
		&account.ProtectNewDeviceSignins,
		&account.BuiltinAvatarID,
		&account.UploadedAvatarObjectKey,
		&usernameChangedAt,
		&account.CreatedAt,
		&account.UpdatedAt)
	if err != nil {
		return CreateAccountResult{}, fmt.Errorf("create account: %w", err)
	}
	if usernameChangedAt.Valid {
		changed := usernameChangedAt.Time.UTC()
		account.UsernameChangedAt = &changed
	}

	if _, err := tx.Exec(ctx, `
        UPDATE username_reservations
        SET reserved_account_id = $2::uuid
        WHERE canonical_username = $1
    `, canonicalUsername, account.ID); err != nil {
		return CreateAccountResult{}, fmt.Errorf("bind username reservation: %w", err)
	}

	device, err := upsertTrustedDeviceTx(
		ctx,
		tx,
		account.ID,
		input.DeviceInstallID,
		strings.TrimSpace(input.DeviceLabel),
		strings.TrimSpace(input.Platform),
		now)
	if err != nil {
		return CreateAccountResult{}, err
	}

	session, err := s.issueSessionTx(ctx, tx, account.Account, device, now)
	if err != nil {
		return CreateAccountResult{}, err
	}

	if err := recordSecurityEventTx(
		ctx,
		tx,
		account.ID,
		"account_created",
		device.ID,
		now,
		map[string]any{}); err != nil {
		return CreateAccountResult{}, err
	}

	if err := tx.Commit(ctx); err != nil {
		return CreateAccountResult{}, fmt.Errorf("commit account creation: %w", err)
	}

	sourceSlot.Commit()
	globalSlot.Commit()
	return CreateAccountResult{
		Session:     session,
		RecoveryKey: recoveryKey,
	}, nil
}

func (s *Service) SignIn(ctx context.Context, input SignInInput) (SignInResult, error) {
	sourceKey := normalizedSourceKey(input.SourceKey)
	if err := s.rateLimiter.Allow(
		ctx,
		"signin_source",
		[]string{sourceKey},
		signInWindow,
		signInLimit); err != nil {
		return SignInResult{}, err
	}

	_, canonicalUsername, err := NormalizeUsername(input.Username)
	if err != nil {
		return SignInResult{}, ErrInvalidCredentials
	}
	if err := s.rateLimiter.Allow(
		ctx,
		"signin_identity",
		[]string{sourceKey, canonicalUsername},
		signInWindow,
		signInLimit); err != nil {
		return SignInResult{}, err
	}
	if err := ValidateDeviceIdentity(
		input.DeviceInstallID,
		input.DeviceLabel,
		input.Platform); err != nil {
		return SignInResult{}, ErrInvalidCredentials
	}

	account, err := s.loadAuthAccountByCanonical(ctx, canonicalUsername)
	if err != nil {
		if errors.Is(err, ErrInvalidCredentials) {
			return SignInResult{}, ErrInvalidCredentials
		}
		return SignInResult{}, err
	}

	password := norm.NFC.String(input.Password)
	valid, err := s.passwordHasher.Verify(account.PasswordHash, password)
	if err != nil {
		return SignInResult{}, fmt.Errorf("verify password hash: %w", err)
	}
	if !valid {
		return SignInResult{}, ErrInvalidCredentials
	}

	existingDevice, exists, err := s.loadDeviceByInstall(
		ctx,
		account.ID,
		input.DeviceInstallID)
	if err != nil {
		return SignInResult{}, fmt.Errorf("load sign-in device: %w", err)
	}

	if account.ProtectNewDeviceSignins &&
		(!exists || !existingDevice.Trusted || existingDevice.RevokedAt != nil) {
		return s.createDeviceSignInChallenge(ctx, account.Account, input)
	}

	now := s.clock.Now()
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return SignInResult{}, fmt.Errorf("begin sign in: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	device, err := upsertTrustedDeviceTx(
		ctx,
		tx,
		account.ID,
		input.DeviceInstallID,
		strings.TrimSpace(input.DeviceLabel),
		strings.TrimSpace(input.Platform),
		now)
	if err != nil {
		return SignInResult{}, err
	}

	session, err := s.issueSessionTx(ctx, tx, account.Account, device, now)
	if err != nil {
		return SignInResult{}, err
	}
	if err := recordSecurityEventTx(
		ctx,
		tx,
		account.ID,
		"signin",
		device.ID,
		now,
		map[string]any{}); err != nil {
		return SignInResult{}, err
	}

	if err := tx.Commit(ctx); err != nil {
		return SignInResult{}, fmt.Errorf("commit sign in: %w", err)
	}
	return SignInResult{
		Status:  "signed_in",
		Session: &session,
	}, nil
}

func (s *Service) ChangePassword(ctx context.Context,
	auth AuthenticatedSession,
	input ChangePasswordInput) error {
	if err := s.rateLimiter.Allow(
		ctx,
		"password_change_reauth",
		[]string{auth.Account.ID, auth.Device.ID},
		reauthWindow,
		reauthLimit); err != nil {
		return err
	}

	account, err := s.loadAuthAccountByID(ctx, auth.Account.ID)
	if err != nil {
		return err
	}

	current := norm.NFC.String(input.CurrentPassword)
	valid, err := s.passwordHasher.Verify(account.PasswordHash, current)
	if err != nil {
		return fmt.Errorf("verify current password: %w", err)
	}
	if !valid {
		return ErrInvalidCredentials
	}

	newPassword, err := s.passwordPolicy.Validate(
		input.NewPassword,
		account.CanonicalUsername)
	if err != nil {
		return err
	}
	newHash, err := s.passwordHasher.Hash(newPassword)
	if err != nil {
		return fmt.Errorf("hash new password: %w", err)
	}

	now := s.clock.Now()
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return fmt.Errorf("begin password change: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var currentHash string
	if err := tx.QueryRow(ctx, `
        SELECT password_hash
        FROM accounts
        WHERE id = $1::uuid
        FOR UPDATE
    `, auth.Account.ID).Scan(&currentHash); err != nil {
		return fmt.Errorf("lock account for password change: %w", err)
	}
	valid, err = s.passwordHasher.Verify(currentHash, current)
	if err != nil {
		return fmt.Errorf("verify locked password: %w", err)
	}
	if !valid {
		return ErrInvalidCredentials
	}

	if _, err := tx.Exec(ctx, `
        UPDATE accounts
        SET password_hash = $2,
            updated_at = $3
        WHERE id = $1::uuid
    `, auth.Account.ID, newHash, now); err != nil {
		return fmt.Errorf("update password: %w", err)
	}

	if _, err := tx.Exec(ctx, `
        UPDATE sessions
        SET revoked_at = $3
        WHERE account_id = $1::uuid
          AND id <> $2::uuid
          AND revoked_at IS NULL
    `, auth.Account.ID, auth.SessionID, now); err != nil {
		return fmt.Errorf("revoke other sessions after password change: %w", err)
	}

	if err := recordSecurityEventTx(
		ctx,
		tx,
		auth.Account.ID,
		"password_changed",
		auth.Device.ID,
		now,
		map[string]any{}); err != nil {
		return err
	}

	if err := tx.Commit(ctx); err != nil {
		return fmt.Errorf("commit password change: %w", err)
	}
	return nil
}

func (s *Service) RenameUsername(ctx context.Context,
	auth AuthenticatedSession,
	input RenameUsernameInput) (Account, error) {
	display, canonical, err := NormalizeUsername(input.NewUsername)
	if err != nil {
		return Account{}, err
	}
	now := s.clock.Now()

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return Account{}, fmt.Errorf("begin username rename: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var currentCanonical string
	var changedAt sql.NullTime
	if err := tx.QueryRow(ctx, `
        SELECT canonical_username, username_changed_at
        FROM accounts
        WHERE id = $1::uuid
        FOR UPDATE
    `, auth.Account.ID).Scan(&currentCanonical, &changedAt); err != nil {
		return Account{}, fmt.Errorf("lock account for username rename: %w", err)
	}
	if canonical == currentCanonical {
		return auth.Account, nil
	}
	if changedAt.Valid && now.Before(changedAt.Time.Add(usernameRenameCooldown)) {
		return Account{}, ErrRenameCooldown
	}

	if _, err := tx.Exec(ctx, `
        INSERT INTO username_reservations(
            canonical_username,
            reserved_account_id,
            reserved_at
        )
        VALUES($1, $2::uuid, $3)
    `, canonical, auth.Account.ID, now); err != nil {
		if isUniqueViolation(err) {
			return Account{}, ErrUsernameUnavailable
		}
		return Account{}, fmt.Errorf("reserve renamed username: %w", err)
	}

	if _, err := tx.Exec(ctx, `
        UPDATE accounts
        SET canonical_username = $2,
            display_username = $3,
            username_changed_at = $4,
            updated_at = $4
        WHERE id = $1::uuid
    `, auth.Account.ID, canonical, display, now); err != nil {
		return Account{}, fmt.Errorf("update username: %w", err)
	}

	if err := recordSecurityEventTx(
		ctx,
		tx,
		auth.Account.ID,
		"username_changed",
		auth.Device.ID,
		now,
		map[string]any{"previous_username": currentCanonical}); err != nil {
		return Account{}, err
	}

	if err := tx.Commit(ctx); err != nil {
		return Account{}, fmt.Errorf("commit username rename: %w", err)
	}

	updated, err := s.loadAuthAccountByID(ctx, auth.Account.ID)
	if err != nil {
		return Account{}, err
	}
	return updated.Account, nil
}

func normalizedSourceKey(source string) string {
	source = strings.TrimSpace(source)
	if source == "" {
		return "unknown"
	}
	return source
}
