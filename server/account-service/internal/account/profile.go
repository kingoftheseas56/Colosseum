package account

import (
	"context"
	"errors"
	"fmt"
	"regexp"
	"strings"
	"time"

	"github.com/jackc/pgx/v5"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/avatar"
)

var builtinAvatarIDPattern = regexp.MustCompile(`^[a-z0-9][a-z0-9_-]{0,63}$`)

func (s *Service) GetProfile(ctx context.Context,
	auth AuthenticatedSession) (Profile, error) {
	record, err := s.loadAuthAccountByID(ctx, auth.Account.ID)
	if err != nil {
		return Profile{}, err
	}
	profile := Profile{Account: record.Account}
	if record.UploadedAvatarObjectKey != "" {
		url, err := s.avatarStore.PresignGet(
			ctx,
			record.UploadedAvatarObjectKey,
			avatar.DefaultURLTTL)
		if err != nil {
			return Profile{}, fmt.Errorf("read avatar URL: %w", err)
		}
		profile.AvatarURL = url
	}
	return profile, nil
}

func (s *Service) SetBuiltinAvatar(ctx context.Context,
	auth AuthenticatedSession,
	avatarID string) (AvatarUpdateResult, error) {
	avatarID = strings.TrimSpace(avatarID)
	if !builtinAvatarIDPattern.MatchString(avatarID) {
		return AvatarUpdateResult{}, ErrAvatarInvalid
	}
	now := s.clock.Now()

	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		return AvatarUpdateResult{}, fmt.Errorf("begin built-in avatar update: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var oldObjectKey string
	if err := tx.QueryRow(ctx, `
        SELECT COALESCE(uploaded_avatar_object_key, '')
        FROM accounts
        WHERE id = $1::uuid
        FOR UPDATE
    `, auth.Account.ID).Scan(&oldObjectKey); err != nil {
		return AvatarUpdateResult{}, fmt.Errorf("lock avatar profile: %w", err)
	}

	if _, err := tx.Exec(ctx, `
        UPDATE accounts
        SET builtin_avatar_id = $2,
            uploaded_avatar_object_key = NULL,
            updated_at = $3
        WHERE id = $1::uuid
    `, auth.Account.ID, avatarID, now); err != nil {
		return AvatarUpdateResult{}, fmt.Errorf("set built-in avatar: %w", err)
	}

	if err := recordSecurityEventTx(
		ctx,
		tx,
		auth.Account.ID,
		"avatar_builtin_selected",
		auth.Device.ID,
		now,
		map[string]any{"avatar_id": avatarID}); err != nil {
		return AvatarUpdateResult{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		return AvatarUpdateResult{}, fmt.Errorf("commit built-in avatar update: %w", err)
	}

	s.deleteAvatarOrQueue(oldObjectKey)

	profile, err := s.GetProfile(ctx, auth)
	if err != nil {
		return AvatarUpdateResult{}, err
	}
	return AvatarUpdateResult{Profile: profile}, nil
}

func (s *Service) UploadAvatar(ctx context.Context,
	auth AuthenticatedSession,
	data []byte) (AvatarUpdateResult, error) {
	if _, _, err := avatar.Validate(data); err != nil {
		return AvatarUpdateResult{}, ErrAvatarInvalid
	}

	newObjectKey, err := s.avatarStore.Put(ctx, auth.Account.ID, data)
	if errors.Is(err, avatar.ErrDisabled) {
		return AvatarUpdateResult{}, ErrAvatarStorageDisabled
	}
	if err != nil {
		return AvatarUpdateResult{}, err
	}

	now := s.clock.Now()
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		s.deleteAvatarOrQueue(newObjectKey)
		return AvatarUpdateResult{}, fmt.Errorf("begin avatar upload commit: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var oldObjectKey string
	if err := tx.QueryRow(ctx, `
        SELECT COALESCE(uploaded_avatar_object_key, '')
        FROM accounts
        WHERE id = $1::uuid
        FOR UPDATE
    `, auth.Account.ID).Scan(&oldObjectKey); err != nil {
		s.deleteAvatarOrQueue(newObjectKey)
		return AvatarUpdateResult{}, fmt.Errorf("lock uploaded avatar profile: %w", err)
	}

	if _, err := tx.Exec(ctx, `
        UPDATE accounts
        SET builtin_avatar_id = NULL,
            uploaded_avatar_object_key = $2,
            updated_at = $3
        WHERE id = $1::uuid
    `, auth.Account.ID, newObjectKey, now); err != nil {
		s.deleteAvatarOrQueue(newObjectKey)
		return AvatarUpdateResult{}, fmt.Errorf("commit uploaded avatar reference: %w", err)
	}

	if err := recordSecurityEventTx(
		ctx,
		tx,
		auth.Account.ID,
		"avatar_uploaded",
		auth.Device.ID,
		now,
		map[string]any{}); err != nil {
		s.deleteAvatarOrQueue(newObjectKey)
		return AvatarUpdateResult{}, err
	}
	if err := tx.Commit(ctx); err != nil {
		s.deleteAvatarOrQueue(newObjectKey)
		return AvatarUpdateResult{}, fmt.Errorf("commit uploaded avatar: %w", err)
	}

	if oldObjectKey != "" && oldObjectKey != newObjectKey {
		s.deleteAvatarOrQueue(oldObjectKey)
	}

	profile, err := s.GetProfile(ctx, auth)
	if err != nil {
		return AvatarUpdateResult{}, err
	}
	return AvatarUpdateResult{Profile: profile}, nil
}

func (s *Service) deleteAvatarOrQueue(objectKey string) {
	objectKey = strings.TrimSpace(objectKey)
	if objectKey == "" {
		return
	}

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()

	if err := s.avatarStore.Delete(ctx, objectKey); err == nil {
		return
	} else {
		now := s.clock.Now()
		_, _ = s.pool.Exec(ctx, `
            INSERT INTO avatar_cleanup_queue(
                object_key,
                enqueued_at,
                attempts,
                last_error,
                next_attempt_at
            )
            VALUES($1, $2, 0, $3, $2)
            ON CONFLICT(object_key)
            DO UPDATE SET
                last_error = EXCLUDED.last_error,
                next_attempt_at = LEAST(
                    avatar_cleanup_queue.next_attempt_at,
                    EXCLUDED.next_attempt_at
                )
        `, objectKey, now, avatarCleanupFailureCode(err))
	}
}

func (s *Service) RunAvatarCleanupOnce(ctx context.Context, limit int) error {
	if limit <= 0 {
		limit = 25
	}
	now := s.clock.Now()
	rows, err := s.pool.Query(ctx, `
        SELECT id, object_key, attempts
        FROM avatar_cleanup_queue
        WHERE next_attempt_at <= $1
        ORDER BY next_attempt_at, id
        LIMIT $2
    `, now, limit)
	if err != nil {
		return fmt.Errorf("list avatar cleanup work: %w", err)
	}

	type cleanupItem struct {
		id       int64
		key      string
		attempts int
	}
	var items []cleanupItem
	for rows.Next() {
		var item cleanupItem
		if err := rows.Scan(&item.id, &item.key, &item.attempts); err != nil {
			rows.Close()
			return fmt.Errorf("scan avatar cleanup work: %w", err)
		}
		items = append(items, item)
	}
	if err := rows.Err(); err != nil {
		rows.Close()
		return fmt.Errorf("iterate avatar cleanup work: %w", err)
	}
	rows.Close()

	for _, item := range items {
		err := s.avatarStore.Delete(ctx, item.key)
		if err == nil {
			if _, err := s.pool.Exec(ctx,
				"DELETE FROM avatar_cleanup_queue WHERE id = $1",
				item.id); err != nil {
				return fmt.Errorf("complete avatar cleanup: %w", err)
			}
			continue
		}

		attempts := item.attempts + 1
		backoff := time.Minute * time.Duration(1<<min(attempts, 6))
		if _, updateErr := s.pool.Exec(ctx, `
            UPDATE avatar_cleanup_queue
            SET attempts = $2,
                last_error = $3,
                next_attempt_at = $4
            WHERE id = $1
        `,
			item.id,
			attempts,
			avatarCleanupFailureCode(err),
			now.Add(backoff)); updateErr != nil {
			return fmt.Errorf("reschedule avatar cleanup: %w", updateErr)
		}
	}
	return nil
}

func avatarCleanupFailureCode(err error) string {
	if err == nil {
		return ""
	}
	if errors.Is(err, avatar.ErrDisabled) {
		return "storage_disabled"
	}
	return "delete_failed"
}
