package account

import (
	"context"
	"errors"
	"fmt"
	"math"
	"strings"
	"time"

	"github.com/jackc/pgx/v5"
)

// The server-side profile attachment lifecycle (Arc 36 N-12). An attachment
// binds one authenticated device's local-profile push to a stable
// server-side identity: begins are idempotent per attachment UUID, reads are
// account/device scoped, pushes tag accepted rows with the attachment, and
// commit is a pure state transition that never moves bulk data because the
// pushes already persisted every mutation.

var (
	ErrAttachmentInvalid   = errors.New("profile attachment invalid")
	ErrAttachmentNotFound  = errors.New("profile attachment not found")
	ErrAttachmentConflict  = errors.New("profile attachment conflict")
	ErrAttachmentNotActive = errors.New("profile attachment not active")
)

var profileAttachmentSourceKinds = map[string]struct{}{
	"legacy_local": {},
	"local_only":   {},
}

const profileAttachmentMaxDigestRunes = 256

type storedProfileAttachment struct {
	ID                   string
	AccountID            string
	DeviceID             string
	SourceKind           string
	SourceSemanticDigest string
	BaselineServerSeq    uint64
	State                string
}

func (stored storedProfileAttachment) view() ProfileAttachment {
	return ProfileAttachment{
		ID:                stored.ID,
		DeviceID:          stored.DeviceID,
		BaselineServerSeq: stored.BaselineServerSeq,
		State:             stored.State,
	}
}

func normalizeProfileAttachmentID(raw string) (string, error) {
	id := strings.ToLower(strings.TrimSpace(raw))
	if !IsUUID(id) {
		return "", ErrAttachmentInvalid
	}
	return id, nil
}

// BeginProfileAttachment opens (or idempotently returns) the attachment for
// one authenticated device. The baseline freezes the account's maximum
// committed canonical server_seq across mutable current state and Activity
// facts at begin time; the sequence's last_value is never consulted.
func (s *Service) BeginProfileAttachment(
	ctx context.Context,
	auth AuthenticatedSession,
	input BeginProfileAttachmentInput,
) (ProfileAttachment, error) {
	attachmentID, err := normalizeProfileAttachmentID(input.AttachmentID)
	if err != nil {
		return ProfileAttachment{}, err
	}
	sourceKind := strings.TrimSpace(input.SourceKind)
	if _, allowed := profileAttachmentSourceKinds[sourceKind]; !allowed {
		return ProfileAttachment{}, ErrAttachmentInvalid
	}
	digest := strings.TrimSpace(input.SourceSemanticDigest)
	if digest == "" || len([]rune(digest)) > profileAttachmentMaxDigestRunes {
		return ProfileAttachment{}, ErrAttachmentInvalid
	}

	now := s.clock.Now().UTC()
	conn, err := s.acquireDatabaseConnection(ctx)
	if err != nil {
		return ProfileAttachment{}, fmt.Errorf("begin profile attachment tx: %w", err)
	}
	defer conn.Release()

	tx, err := conn.Begin(ctx)
	if err != nil {
		return ProfileAttachment{}, fmt.Errorf("begin profile attachment tx: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	baseline, err := maxCommittedServerSeq(ctx, tx, auth.Account.ID)
	if err != nil {
		return ProfileAttachment{}, err
	}

	if _, err := tx.Exec(ctx, `
        INSERT INTO account_device_attachments(
            id, account_id, device_id, source_kind, source_semantic_digest,
            baseline_server_seq, state, created_at, updated_at
        )
        VALUES(
            $1::uuid, $2::uuid, $3::uuid, $4, $5,
            $6, 'open', $7, $7
        )
        ON CONFLICT (id) DO NOTHING
    `,
		attachmentID,
		auth.Account.ID,
		auth.Device.ID,
		sourceKind,
		digest,
		int64(baseline),
		now); err != nil {
		return ProfileAttachment{}, fmt.Errorf("insert profile attachment: %w", err)
	}

	stored, found, err := loadProfileAttachmentByIDTx(ctx, tx, attachmentID)
	if err != nil {
		return ProfileAttachment{}, err
	}
	if !found {
		return ProfileAttachment{}, fmt.Errorf(
			"profile attachment insert completed without a readable row")
	}
	if stored.AccountID != auth.Account.ID ||
		stored.DeviceID != auth.Device.ID ||
		stored.SourceKind != sourceKind ||
		stored.SourceSemanticDigest != digest {
		return ProfileAttachment{}, ErrAttachmentConflict
	}

	if err := tx.Commit(ctx); err != nil {
		return ProfileAttachment{}, fmt.Errorf("commit profile attachment begin: %w", err)
	}
	return stored.view(), nil
}

// GetProfileAttachment returns the attachment for the authenticated
// account/device pair only; every other reader fails closed as not found.
func (s *Service) GetProfileAttachment(
	ctx context.Context,
	auth AuthenticatedSession,
	rawAttachmentID string,
) (ProfileAttachment, error) {
	attachmentID, err := normalizeProfileAttachmentID(rawAttachmentID)
	if err != nil {
		return ProfileAttachment{}, err
	}

	row := s.pool.QueryRow(ctx, `
        SELECT device_id::text, baseline_server_seq, state
        FROM account_device_attachments
        WHERE id = $1::uuid
          AND account_id = $2::uuid
          AND device_id = $3::uuid
    `, attachmentID, auth.Account.ID, auth.Device.ID)

	var deviceID, state string
	var baseline int64
	if err := row.Scan(&deviceID, &baseline, &state); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return ProfileAttachment{}, ErrAttachmentNotFound
		}
		return ProfileAttachment{}, fmt.Errorf("load profile attachment: %w", err)
	}
	if baseline < 0 {
		return ProfileAttachment{}, fmt.Errorf("profile attachment has invalid baseline")
	}
	return ProfileAttachment{
		ID:                attachmentID,
		DeviceID:          deviceID,
		BaselineServerSeq: uint64(baseline),
		State:             state,
	}, nil
}

// CommitProfileAttachment moves an open or uploaded attachment to committed.
// Committing an already-committed attachment succeeds without duplicating
// work; aborted attachments are terminal and stay uncommittable. Commit only
// transitions state — pushes already persisted all mutations.
func (s *Service) CommitProfileAttachment(
	ctx context.Context,
	auth AuthenticatedSession,
	rawAttachmentID string,
) (ProfileAttachment, error) {
	attachmentID, err := normalizeProfileAttachmentID(rawAttachmentID)
	if err != nil {
		return ProfileAttachment{}, err
	}

	now := s.clock.Now().UTC()
	conn, err := s.acquireDatabaseConnection(ctx)
	if err != nil {
		return ProfileAttachment{}, fmt.Errorf("begin profile attachment commit: %w", err)
	}
	defer conn.Release()

	tx, err := conn.Begin(ctx)
	if err != nil {
		return ProfileAttachment{}, fmt.Errorf("begin profile attachment commit: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var deviceID, state string
	var baseline int64
	err = tx.QueryRow(ctx, `
        UPDATE account_device_attachments
        SET state = 'committed',
            committed_at = $4,
            updated_at = $4
        WHERE id = $1::uuid
          AND account_id = $2::uuid
          AND device_id = $3::uuid
          AND state IN ('open', 'uploaded')
        RETURNING device_id::text, baseline_server_seq, state
    `,
		attachmentID,
		auth.Account.ID,
		auth.Device.ID,
		now).Scan(&deviceID, &baseline, &state)
	if err == nil {
		if err := tx.Commit(ctx); err != nil {
			return ProfileAttachment{}, fmt.Errorf("commit profile attachment: %w", err)
		}
		return ProfileAttachment{
			ID:                attachmentID,
			DeviceID:          deviceID,
			BaselineServerSeq: uint64(baseline),
			State:             state,
		}, nil
	}
	if !errors.Is(err, pgx.ErrNoRows) {
		return ProfileAttachment{}, fmt.Errorf("commit profile attachment state: %w", err)
	}

	stored, found, err := loadProfileAttachmentByIDTx(ctx, tx, attachmentID)
	if err != nil {
		return ProfileAttachment{}, err
	}
	if !found ||
		stored.AccountID != auth.Account.ID ||
		stored.DeviceID != auth.Device.ID {
		return ProfileAttachment{}, ErrAttachmentNotFound
	}
	if stored.State != "committed" {
		// Terminal aborted attachments never re-open.
		return ProfileAttachment{}, ErrAttachmentNotActive
	}
	if err := tx.Commit(ctx); err != nil {
		return ProfileAttachment{}, fmt.Errorf("commit idempotent lookup: %w", err)
	}
	return stored.view(), nil
}

// loadOwnedActiveAttachment validates that the attachment belongs to the
// authenticated account/device and is still accepting pushes.
func (s *Service) loadOwnedActiveAttachment(
	ctx context.Context,
	auth AuthenticatedSession,
	attachmentID string,
) error {
	row := s.pool.QueryRow(ctx, `
        SELECT state
        FROM account_device_attachments
        WHERE id = $1::uuid
          AND account_id = $2::uuid
          AND device_id = $3::uuid
    `, attachmentID, auth.Account.ID, auth.Device.ID)

	var state string
	if err := row.Scan(&state); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return ErrAttachmentNotFound
		}
		return fmt.Errorf("load attachment for push: %w", err)
	}
	if state != "open" && state != "uploaded" {
		return ErrAttachmentNotActive
	}
	return nil
}

func loadProfileAttachmentByIDTx(
	ctx context.Context,
	tx pgx.Tx,
	attachmentID string,
) (storedProfileAttachment, bool, error) {
	row := tx.QueryRow(ctx, `
        SELECT id::text, account_id::text, device_id::text,
               source_kind, source_semantic_digest,
               baseline_server_seq, state
        FROM account_device_attachments
        WHERE id = $1::uuid
    `, attachmentID)

	var stored storedProfileAttachment
	var baseline int64
	if err := row.Scan(
		&stored.ID,
		&stored.AccountID,
		&stored.DeviceID,
		&stored.SourceKind,
		&stored.SourceSemanticDigest,
		&baseline,
		&stored.State); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return storedProfileAttachment{}, false, nil
		}
		return storedProfileAttachment{}, false, fmt.Errorf(
			"load profile attachment row: %w", err)
	}
	if baseline < 0 {
		return storedProfileAttachment{}, false, fmt.Errorf(
			"profile attachment row has invalid baseline")
	}
	stored.BaselineServerSeq = uint64(baseline)
	return stored, true, nil
}

// syncDB queries either a pool or a transaction; both satisfy QueryRow.
type syncDB interface {
	QueryRow(ctx context.Context, sql string, args ...any) pgx.Row
}

// maxCommittedServerSeq reports the maximum committed canonical server_seq
// for an account across mutable current-state rows and immutable Activity
// facts. It deliberately never reads the account_change_seq last_value:
// loser journal rows and deduplicated duplicates allocate sequence values
// that are not part of committed canonical state.
func maxCommittedServerSeq(
	ctx context.Context,
	db syncDB,
	accountID string,
) (uint64, error) {
	var baseline int64
	if err := db.QueryRow(ctx, `
        SELECT GREATEST(
            COALESCE((
                SELECT max(server_seq) FROM account_sync_current
                WHERE account_id = $1::uuid
            ), 0),
            COALESCE((
                SELECT max(server_seq) FROM account_activity_facts
                WHERE account_id = $1::uuid
            ), 0)
        )
    `, accountID).Scan(&baseline); err != nil {
		return 0, fmt.Errorf("compute committed baseline: %w", err)
	}
	if baseline < 0 || baseline > math.MaxInt64 {
		return 0, fmt.Errorf("committed baseline is out of range")
	}
	return uint64(baseline), nil
}

// markAttachmentUploadedTx transitions an attachment from open to uploaded
// inside the caller's mutation transaction so the first accepted attached
// mutation records the state change atomically with its row.
func markAttachmentUploadedTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID,
	attachmentID string,
	now time.Time,
) error {
	if attachmentID == "" {
		return nil
	}
	if _, err := tx.Exec(ctx, `
        UPDATE account_device_attachments
        SET state = 'uploaded', updated_at = $3
        WHERE id = $1::uuid
          AND account_id = $2::uuid
          AND state = 'open'
    `, attachmentID, accountID, now); err != nil {
		return fmt.Errorf("advance attachment to uploaded: %w", err)
	}
	return nil
}
