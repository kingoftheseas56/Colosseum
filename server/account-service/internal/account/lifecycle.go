package account

import (
	"bytes"
	"context"
	"crypto/subtle"
	"database/sql"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"strings"
	"time"

	"github.com/jackc/pgx/v5"
	"golang.org/x/text/unicode/norm"
)

const (
	exportFormat                = "colosseum.account.export"
	exportSchemaVersion         = 1
	exportCursorCategory        = "account_export_cursor"
	exportSnapshotLifetime      = 15 * time.Minute
	exportPageDefaultLimit      = 50
	exportPageMaxLimit          = 100
	exportMaterializationBatch  = 32
	exportMaxSnapshotItems      = 10000
	exportMaxSnapshotBytes      = 64 * 1024 * 1024
	exportMaxPayloadBytes       = 256 * 1024
	deletionReceiptLifetime     = 30 * 24 * time.Hour
	deletionCapabilityByteCount = 32
	lifecyclePruneSnapshotLimit = 10
	lifecyclePruneItemLimit     = 10000
	lifecyclePruneReceiptLimit  = 100
)

// Export snapshots are materialized under the account sync advisory lock and
// a repeatable-read transaction so they see one transactionally consistent
// view of current sync winners, immutable Activity facts, and account
// metadata. Profile/avatar writers do not share the sync lock, so the
// transaction snapshot is required in addition to the lock. The rows are
// account-owned and expire after exportSnapshotLifetime; account deletion
// cascades them automatically.
func (s *Service) ExportAccount(
	ctx context.Context,
	auth AuthenticatedSession,
	input ExportAccountInput,
) (ExportPage, error) {
	limit := normalizeExportPageLimit(input.Limit)
	if input.Cursor == "" {
		return s.createExportSnapshot(ctx, auth, limit)
	}

	cursor, err := s.openExportCursor(auth.Account.ID, input.Cursor)
	if err != nil {
		return ExportPage{}, err
	}

	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return ExportPage{}, fmt.Errorf("begin export page: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if err := lockAccountSyncTx(ctx, tx, auth.Account.ID); err != nil {
		return ExportPage{}, err
	}
	if _, err := loadExportAccountTx(ctx, tx, auth.Account.ID); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return ExportPage{}, ErrSessionInvalid
		}
		return ExportPage{}, fmt.Errorf("load account for export page: %w", err)
	}
	if err := validateAuthenticatedSessionTx(ctx, tx, auth, s.clock.Now); err != nil {
		return ExportPage{}, err
	}

	snapshot, err := loadExportSnapshotTx(ctx, tx, cursor.SnapshotID)
	if err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return ExportPage{}, ErrExportCursorInvalid
		}
		return ExportPage{}, fmt.Errorf("load export snapshot: %w", err)
	}
	if snapshot.AccountID != auth.Account.ID {
		return ExportPage{}, ErrExportCursorInvalid
	}
	if !snapshot.ExpiresAt.After(s.clock.Now().UTC()) {
		return ExportPage{}, ErrExportSnapshotExpired
	}

	page, err := readExportPageTx(
		ctx,
		tx,
		s,
		auth.Account.ID,
		snapshot,
		cursor.ItemIndex,
		limit)
	if err != nil {
		return ExportPage{}, err
	}
	if err := s.setExportPageCursors(
		&page,
		auth.Account.ID,
		snapshot.ID,
		cursor.ItemIndex); err != nil {
		return ExportPage{}, err
	}

	if err := tx.Commit(ctx); err != nil {
		return ExportPage{}, fmt.Errorf("commit export page: %w", err)
	}
	return page, nil
}

func normalizeExportPageLimit(limit int) int {
	if limit <= 0 {
		return exportPageDefaultLimit
	}
	if limit > exportPageMaxLimit {
		return exportPageMaxLimit
	}
	return limit
}

type exportAccountMetadata struct {
	AccountID       string
	Username        string
	BuiltinAvatarID string
	CreatedAt       time.Time
	UpdatedAt       time.Time
}

func loadExportAccountTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID string,
) (exportAccountMetadata, error) {
	var value exportAccountMetadata
	err := tx.QueryRow(ctx, `
        SELECT
            id::text,
            display_username,
            COALESCE(builtin_avatar_id, ''),
            created_at,
            updated_at
        FROM accounts
        WHERE id = $1::uuid
        FOR SHARE
    `, accountID).Scan(
		&value.AccountID,
		&value.Username,
		&value.BuiltinAvatarID,
		&value.CreatedAt,
		&value.UpdatedAt)
	return value, err
}

type exportSnapshot struct {
	ID                 string
	AccountID          string
	HighWaterServerSeq uint64
	FormatVersion      int
	CreatedAt          time.Time
	ExpiresAt          time.Time
}

func loadExportSnapshotTx(
	ctx context.Context,
	tx pgx.Tx,
	snapshotID string,
) (exportSnapshot, error) {
	var value exportSnapshot
	var highwater int64
	err := tx.QueryRow(ctx, `
        SELECT
            id::text,
            account_id::text,
            highwater_server_seq,
            format_version,
            created_at,
            expires_at
        FROM account_export_snapshots
        WHERE id = $1::uuid
        FOR SHARE
    `, snapshotID).Scan(
		&value.ID,
		&value.AccountID,
		&highwater,
		&value.FormatVersion,
		&value.CreatedAt,
		&value.ExpiresAt)
	if err != nil {
		return exportSnapshot{}, err
	}
	if highwater < 0 {
		return exportSnapshot{}, fmt.Errorf("export snapshot has invalid high-water mark")
	}
	value.HighWaterServerSeq = uint64(highwater)
	return value, nil
}

func (s *Service) createExportSnapshot(
	ctx context.Context,
	auth AuthenticatedSession,
	limit int,
) (ExportPage, error) {
	tx, err := s.pool.BeginTx(ctx, pgx.TxOptions{
		IsoLevel: pgx.RepeatableRead,
	})
	if err != nil {
		return ExportPage{}, fmt.Errorf("begin export snapshot: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if err := lockAccountSyncTx(ctx, tx, auth.Account.ID); err != nil {
		return ExportPage{}, err
	}
	metadata, err := loadExportAccountTx(ctx, tx, auth.Account.ID)
	if errors.Is(err, pgx.ErrNoRows) {
		return ExportPage{}, ErrSessionInvalid
	}
	if err != nil {
		return ExportPage{}, fmt.Errorf("load account for export snapshot: %w", err)
	}
	if err := validateAuthenticatedSessionTx(ctx, tx, auth, s.clock.Now); err != nil {
		return ExportPage{}, err
	}

	createdAt := s.clock.Now().UTC()
	if snapshot, found, err := loadActiveExportSnapshotTx(
		ctx, tx, auth.Account.ID, createdAt); err != nil {
		return ExportPage{}, fmt.Errorf("load active export snapshot: %w", err)
	} else if found {
		page, err := readExportPageTx(ctx, tx, s, auth.Account.ID, snapshot, 0, limit)
		if err != nil {
			return ExportPage{}, err
		}
		if err := s.setExportPageCursors(&page, auth.Account.ID, snapshot.ID, 0); err != nil {
			return ExportPage{}, err
		}
		if err := tx.Commit(ctx); err != nil {
			return ExportPage{}, fmt.Errorf("commit active export page: %w", err)
		}
		return page, nil
	}

	expiresAt := createdAt.Add(exportSnapshotLifetime)
	var snapshotID string
	if err := tx.QueryRow(ctx, `
        INSERT INTO account_export_snapshots(
            id,
            account_id,
            highwater_server_seq,
            format_version,
            created_at,
            expires_at
        )
        VALUES(gen_random_uuid(), $1::uuid, 0, $2, $3, $4)
        RETURNING id::text
    `, auth.Account.ID, exportSchemaVersion, createdAt, expiresAt).Scan(&snapshotID); err != nil {
		return ExportPage{}, fmt.Errorf("create export snapshot: %w", err)
	}

	metadataPayload, err := json.Marshal(map[string]any{
		"account_id":        metadata.AccountID,
		"builtin_avatar_id": metadata.BuiltinAvatarID,
		"created_at":        metadata.CreatedAt.UTC().Format(time.RFC3339Nano),
		"updated_at":        metadata.UpdatedAt.UTC().Format(time.RFC3339Nano),
		"username":          metadata.Username,
	})
	if err != nil {
		return ExportPage{}, fmt.Errorf("encode export account metadata: %w", err)
	}
	metadataPayload, err = canonicalExportItemPayload(
		"account_metadata",
		"account_metadata",
		"profile",
		metadataPayload,
		auth.Account.ID)
	if err != nil {
		return ExportPage{}, err
	}
	metadataCiphertext, err := s.syncCipher.Seal(
		auth.Account.ID,
		"account_metadata",
		"profile",
		metadataPayload)
	if err != nil {
		return ExportPage{}, fmt.Errorf("encrypt export account metadata: %w", err)
	}
	if err := insertExportItemTx(
		ctx,
		tx,
		snapshotID,
		0,
		"account_metadata",
		"account_metadata",
		"profile",
		metadataCiphertext); err != nil {
		return ExportPage{}, err
	}

	itemIndex := int64(1)
	materializedBytes := int64(len(metadataCiphertext))
	if materializedBytes > exportMaxSnapshotBytes {
		return ExportPage{}, ErrExportTooLarge
	}
	if err := s.materializeExportSyncItemsTx(
		ctx,
		tx,
		auth.Account.ID,
		snapshotID,
		&itemIndex,
		&materializedBytes); err != nil {
		return ExportPage{}, err
	}
	if err := s.materializeExportActivityItemsTx(
		ctx,
		tx,
		auth.Account.ID,
		snapshotID,
		&itemIndex,
		&materializedBytes); err != nil {
		return ExportPage{}, err
	}

	highwater, err := loadExportHighWaterTx(ctx, tx, auth.Account.ID)
	if err != nil {
		return ExportPage{}, err
	}
	if _, err := tx.Exec(ctx, `
        UPDATE account_export_snapshots
        SET highwater_server_seq = $2
        WHERE id = $1::uuid
    `, snapshotID, int64(highwater)); err != nil {
		return ExportPage{}, fmt.Errorf("record export high-water mark: %w", err)
	}

	snapshot, err := loadExportSnapshotTx(ctx, tx, snapshotID)
	if err != nil {
		return ExportPage{}, fmt.Errorf("reload export snapshot: %w", err)
	}
	page, err := readExportPageTx(
		ctx,
		tx,
		s,
		auth.Account.ID,
		snapshot,
		0,
		limit)
	if err != nil {
		return ExportPage{}, err
	}
	if err := s.setExportPageCursors(&page, auth.Account.ID, snapshot.ID, 0); err != nil {
		return ExportPage{}, err
	}

	if err := tx.Commit(ctx); err != nil {
		return ExportPage{}, fmt.Errorf("commit export snapshot: %w", err)
	}
	return page, nil
}

type exportItemInput struct {
	kind      string
	category  string
	recordKey string
	canonical []byte
}

func loadActiveExportSnapshotTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID string,
	now time.Time,
) (exportSnapshot, bool, error) {
	var value exportSnapshot
	var highwater int64
	err := tx.QueryRow(ctx, `
        SELECT
            id::text,
            account_id::text,
            highwater_server_seq,
            format_version,
            created_at,
            expires_at
        FROM account_export_snapshots
        WHERE account_id = $1::uuid
          AND expires_at > $2
        ORDER BY created_at DESC, id DESC
        LIMIT 1
        FOR SHARE
    `, accountID, now.UTC()).Scan(
		&value.ID,
		&value.AccountID,
		&highwater,
		&value.FormatVersion,
		&value.CreatedAt,
		&value.ExpiresAt)
	if errors.Is(err, pgx.ErrNoRows) {
		return exportSnapshot{}, false, nil
	}
	if err != nil {
		return exportSnapshot{}, false, err
	}
	if highwater < 0 {
		return exportSnapshot{}, false, fmt.Errorf("active export snapshot has invalid high-water mark")
	}
	value.HighWaterServerSeq = uint64(highwater)
	return value, true, nil
}

func (s *Service) materializeExportSyncItemsTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID,
	snapshotID string,
	itemIndex,
	materializedBytes *int64,
) error {
	var lastCategory, lastRecordKey string
	hasLast := false
	for {
		query := `
            SELECT category, record_key, schema_version, payload_ciphertext
            FROM account_sync_current
            WHERE account_id = $1::uuid
              AND operation = 'put'
        `
		args := []any{accountID}
		if hasLast {
			query += `
              AND (category > $2 OR
                   (category = $2 AND record_key > $3))
            `
			args = append(args, lastCategory, lastRecordKey, exportMaterializationBatch)
			query += ` ORDER BY category, record_key LIMIT $4`
		} else {
			args = append(args, exportMaterializationBatch)
			query += ` ORDER BY category, record_key LIMIT $2`
		}

		rows, err := tx.Query(ctx, query, args...)
		if err != nil {
			return fmt.Errorf("list export sync state: %w", err)
		}
		batch := make([]exportItemInput, 0, exportMaterializationBatch)
		for rows.Next() {
			var category, recordKey string
			var schemaVersion int
			var ciphertext []byte
			if err := rows.Scan(&category, &recordKey, &schemaVersion, &ciphertext); err != nil {
				rows.Close()
				return fmt.Errorf("scan export sync state: %w", err)
			}
			if len(ciphertext) > exportMaxPayloadBytes+64 {
				rows.Close()
				return ErrExportIncomplete
			}
			if err := validateSyncCategory(category, schemaVersion); err != nil {
				rows.Close()
				return ErrExportIncomplete
			}
			if err := validateCategoryRecordKey(category, recordKey); err != nil {
				rows.Close()
				return ErrExportIncomplete
			}
			plain, err := s.syncCipher.Open(accountID, category, recordKey, ciphertext)
			if err != nil {
				rows.Close()
				return ErrExportIncomplete
			}
			canonical, err := canonicalExportItemPayload(
				"sync_record", category, recordKey, plain, accountID)
			if err != nil {
				rows.Close()
				return err
			}
			batch = append(batch, exportItemInput{
				kind:      "sync_record",
				category:  category,
				recordKey: recordKey,
				canonical: canonical,
			})
		}
		if err := rows.Err(); err != nil {
			rows.Close()
			return fmt.Errorf("iterate export sync state: %w", err)
		}
		rows.Close()

		for _, input := range batch {
			if *itemIndex >= exportMaxSnapshotItems {
				return ErrExportTooLarge
			}
			ciphertext, err := s.syncCipher.Seal(
				accountID, input.category, input.recordKey, input.canonical)
			if err != nil {
				return fmt.Errorf("encrypt export item: %w", err)
			}
			if err := reserveExportBytes(materializedBytes, len(ciphertext)); err != nil {
				return err
			}
			if err := insertExportItemTx(
				ctx,
				tx,
				snapshotID,
				*itemIndex,
				input.kind,
				input.category,
				input.recordKey,
				ciphertext); err != nil {
				return err
			}
			(*itemIndex)++
		}
		if len(batch) < exportMaterializationBatch {
			return nil
		}
		lastCategory = batch[len(batch)-1].category
		lastRecordKey = batch[len(batch)-1].recordKey
		hasLast = true
	}
}

func (s *Service) materializeExportActivityItemsTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID,
	snapshotID string,
	itemIndex,
	materializedBytes *int64,
) error {
	var lastEventID string
	hasLast := false
	for {
		query := `
            SELECT event_id::text, payload_ciphertext
            FROM account_activity_facts
            WHERE account_id = $1::uuid
              AND suppressed = false
        `
		args := []any{accountID}
		if hasLast {
			query += ` AND event_id > $2::uuid`
			args = append(args, lastEventID, exportMaterializationBatch)
			query += ` ORDER BY event_id LIMIT $3`
		} else {
			args = append(args, exportMaterializationBatch)
			query += ` ORDER BY event_id LIMIT $2`
		}

		rows, err := tx.Query(ctx, query, args...)
		if err != nil {
			return fmt.Errorf("list export Activity facts: %w", err)
		}
		batch := make([]exportItemInput, 0, exportMaterializationBatch)
		for rows.Next() {
			var eventID string
			var ciphertext []byte
			if err := rows.Scan(&eventID, &ciphertext); err != nil {
				rows.Close()
				return fmt.Errorf("scan export Activity fact: %w", err)
			}
			if !IsUUID(eventID) || len(ciphertext) > exportMaxPayloadBytes+64 {
				rows.Close()
				return ErrExportIncomplete
			}
			eventID = strings.ToLower(strings.TrimSpace(eventID))
			recordKey := activityRecordKeyPrefix + eventID
			plain, err := s.syncCipher.Open(accountID, "activity_fact", recordKey, ciphertext)
			if err != nil {
				rows.Close()
				return ErrExportIncomplete
			}
			canonical, err := canonicalExportItemPayload(
				"activity_fact", "activity_fact", recordKey, plain, accountID)
			if err != nil {
				rows.Close()
				return err
			}
			batch = append(batch, exportItemInput{
				kind:      "activity_fact",
				category:  "activity_fact",
				recordKey: recordKey,
				canonical: canonical,
			})
		}
		if err := rows.Err(); err != nil {
			rows.Close()
			return fmt.Errorf("iterate export Activity facts: %w", err)
		}
		rows.Close()

		for _, input := range batch {
			if *itemIndex >= exportMaxSnapshotItems {
				return ErrExportTooLarge
			}
			ciphertext, err := s.syncCipher.Seal(
				accountID, input.category, input.recordKey, input.canonical)
			if err != nil {
				return fmt.Errorf("encrypt export Activity fact: %w", err)
			}
			if err := reserveExportBytes(materializedBytes, len(ciphertext)); err != nil {
				return err
			}
			if err := insertExportItemTx(
				ctx,
				tx,
				snapshotID,
				*itemIndex,
				input.kind,
				input.category,
				input.recordKey,
				ciphertext); err != nil {
				return err
			}
			(*itemIndex)++
		}
		if len(batch) < exportMaterializationBatch {
			return nil
		}
		lastEventID = batch[len(batch)-1].recordKey[len(activityRecordKeyPrefix):]
		hasLast = true
	}
}

func reserveExportBytes(used *int64, additional int) error {
	if additional < 0 || *used > exportMaxSnapshotBytes-int64(additional) {
		return ErrExportTooLarge
	}
	*used += int64(additional)
	return nil
}

func loadExportHighWaterTx(ctx context.Context, tx pgx.Tx, accountID string) (uint64, error) {
	var highwater int64
	if err := tx.QueryRow(ctx, `
        SELECT GREATEST(
            COALESCE((SELECT MAX(server_seq)
                      FROM account_sync_journal
                      WHERE account_id = $1::uuid), 0),
            COALESCE((SELECT MAX(server_seq)
                      FROM account_activity_facts
                      WHERE account_id = $1::uuid), 0)
        )
    `, accountID).Scan(&highwater); err != nil {
		return 0, fmt.Errorf("load export feed high-water mark: %w", err)
	}
	if highwater < 0 {
		return 0, fmt.Errorf("export feed high-water mark is invalid")
	}
	return uint64(highwater), nil
}

func canonicalExportItemPayload(
	kind,
	category,
	recordKey string,
	plain []byte,
	accountID string,
) ([]byte, error) {
	if len(plain) == 0 || len(plain) > exportMaxPayloadBytes {
		return nil, ErrExportIncomplete
	}
	switch kind {
	case "account_metadata":
		if category != "account_metadata" || recordKey != "profile" {
			return nil, ErrExportIncomplete
		}
		if err := validateSyncPayload(plain); err != nil {
			return nil, ErrExportIncomplete
		}
		var object map[string]any
		if err := json.Unmarshal(plain, &object); err != nil || object == nil {
			return nil, ErrExportIncomplete
		}
		if len(object) != 5 {
			return nil, ErrExportIncomplete
		}
		if value, ok := object["account_id"].(string); !ok ||
			!IsUUID(value) || strings.ToLower(strings.TrimSpace(value)) != strings.ToLower(accountID) {
			return nil, ErrExportIncomplete
		}
		for _, field := range []string{"username", "builtin_avatar_id", "created_at", "updated_at"} {
			if _, ok := object[field].(string); !ok {
				return nil, ErrExportIncomplete
			}
		}
		if _, err := time.Parse(time.RFC3339Nano, object["created_at"].(string)); err != nil {
			return nil, ErrExportIncomplete
		}
		if _, err := time.Parse(time.RFC3339Nano, object["updated_at"].(string)); err != nil {
			return nil, ErrExportIncomplete
		}
		canonical, err := canonicalSyncJSON(plain)
		if err != nil {
			return nil, ErrExportIncomplete
		}
		return canonical, nil
	case "sync_record":
		schemaVersion, ok := syncAllowedCategories[category]
		if !ok || validateSyncRecordShape(
			category,
			schemaVersion,
			recordKey,
			"put",
			plain) != nil {
			return nil, ErrExportIncomplete
		}
		canonical, err := canonicalSyncJSON(plain)
		if err != nil {
			return nil, ErrExportIncomplete
		}
		return canonical, nil
	case "activity_fact":
		if category != "activity_fact" || validateCategoryRecordKey(category, recordKey) != nil {
			return nil, ErrExportIncomplete
		}
		fact, code, _ := parseActivityFact(parsedSyncMutation{
			Category:      category,
			RecordKey:     recordKey,
			SchemaVersion: 1,
			Operation:     "put",
			Payload:       plain,
		})
		if code != "" || fact.EventID == "" || len(fact.Canonical) == 0 {
			return nil, ErrExportIncomplete
		}
		return fact.Canonical, nil
	default:
		return nil, ErrExportIncomplete
	}
}

func insertExportItemTx(
	ctx context.Context,
	tx pgx.Tx,
	snapshotID string,
	itemIndex int64,
	kind,
	category,
	recordKey string,
	ciphertext []byte,
) error {
	if _, err := tx.Exec(ctx, `
        INSERT INTO account_export_items(
            snapshot_id,
            item_index,
            kind,
            category,
            record_key,
            payload_ciphertext
        )
        VALUES($1::uuid, $2, $3, $4, $5, $6)
    `, snapshotID, itemIndex, kind, category, recordKey, ciphertext); err != nil {
		return fmt.Errorf("store export item: %w", err)
	}
	return nil
}

func readExportPageTx(
	ctx context.Context,
	tx pgx.Tx,
	s *Service,
	accountID string,
	snapshot exportSnapshot,
	itemIndex int64,
	limit int,
) (ExportPage, error) {
	if itemIndex < 0 || limit <= 0 {
		return ExportPage{}, ErrExportCursorInvalid
	}

	rows, err := tx.Query(ctx, `
        SELECT item_index, kind, category, record_key, payload_ciphertext
        FROM account_export_items
        WHERE snapshot_id = $1::uuid
	      AND item_index >= $2
        ORDER BY item_index
        LIMIT $3
	`, snapshot.ID, itemIndex, limit+1)
	if err != nil {
		return ExportPage{}, fmt.Errorf("list export page: %w", err)
	}
	items := make([]ExportItem, 0, limit+1)
	for rows.Next() {
		var item ExportItem
		var storedIndex int64
		var ciphertext []byte
		if err := rows.Scan(
			&storedIndex,
			&item.Kind,
			&item.Category,
			&item.Key,
			&ciphertext); err != nil {
			rows.Close()
			return ExportPage{}, fmt.Errorf("scan export item: %w", err)
		}
		if storedIndex != itemIndex+int64(len(items)) {
			rows.Close()
			return ExportPage{}, ErrExportIncomplete
		}
		plain, err := s.syncCipher.Open(
			accountID,
			item.Category,
			item.Key,
			ciphertext)
		if err != nil {
			rows.Close()
			return ExportPage{}, fmt.Errorf("decrypt export item: %w", err)
		}
		canonical, err := canonicalExportItemPayload(
			item.Kind,
			item.Category,
			item.Key,
			plain,
			accountID)
		if err != nil {
			rows.Close()
			return ExportPage{}, err
		}
		item.Payload = json.RawMessage(canonical)
		items = append(items, item)
	}
	if err := rows.Err(); err != nil {
		rows.Close()
		return ExportPage{}, fmt.Errorf("iterate export page: %w", err)
	}
	rows.Close()
	hasMore := len(items) > limit
	if hasMore {
		items = items[:limit]
	}

	page := ExportPage{
		Format:             exportFormat,
		SchemaVersion:      snapshot.FormatVersion,
		SnapshotID:         snapshot.ID,
		GeneratedAt:        snapshot.CreatedAt.UTC(),
		ExpiresAt:          snapshot.ExpiresAt.UTC(),
		HighWaterServerSeq: snapshot.HighWaterServerSeq,
		HasMore:            hasMore,
		Items:              items,
	}
	return page, nil
}

func (s *Service) setExportPageCursors(
	page *ExportPage,
	accountID,
	snapshotID string,
	itemIndex int64,
) error {
	var err error
	page.Cursor, err = s.sealExportCursor(accountID, snapshotID, itemIndex)
	if err != nil {
		return err
	}
	if !page.HasMore {
		return nil
	}
	page.NextCursor, err = s.sealExportCursor(
		accountID,
		snapshotID,
		itemIndex+int64(len(page.Items)))
	return err
}

type exportCursor struct {
	SnapshotID string `json:"snapshot_id"`
	ItemIndex  int64  `json:"item_index"`
}

func (s *Service) sealExportCursor(
	accountID,
	snapshotID string,
	offset int64,
) (string, error) {
	if !IsUUID(snapshotID) || offset < 0 {
		return "", ErrExportCursorInvalid
	}
	canonicalSnapshotID := strings.ToLower(strings.TrimSpace(snapshotID))
	payload, err := json.Marshal(exportCursor{
		SnapshotID: canonicalSnapshotID,
		ItemIndex:  offset,
	})
	if err != nil {
		return "", fmt.Errorf("encode export cursor: %w", err)
	}
	sealed, err := s.syncCipher.Seal(
		accountID,
		exportCursorCategory,
		canonicalSnapshotID,
		payload)
	if err != nil {
		return "", fmt.Errorf("encrypt export cursor: %w", err)
	}
	return canonicalSnapshotID + "." + base64.RawURLEncoding.EncodeToString(sealed), nil
}

func (s *Service) openExportCursor(accountID, token string) (exportCursor, error) {
	snapshotID, encoded, found := strings.Cut(token, ".")
	if !found || snapshotID == "" || encoded == "" || strings.Contains(encoded, ".") ||
		!IsUUID(snapshotID) || strings.ToLower(strings.TrimSpace(snapshotID)) != snapshotID {
		return exportCursor{}, ErrExportCursorInvalid
	}
	sealed, err := base64.RawURLEncoding.DecodeString(encoded)
	if err != nil || base64.RawURLEncoding.EncodeToString(sealed) != encoded {
		return exportCursor{}, ErrExportCursorInvalid
	}
	plain, err := s.syncCipher.Open(
		accountID,
		exportCursorCategory,
		snapshotID,
		sealed)
	if err != nil {
		return exportCursor{}, ErrExportCursorInvalid
	}
	decoder := json.NewDecoder(bytes.NewReader(plain))
	decoder.DisallowUnknownFields()
	var cursor exportCursor
	if err := decoder.Decode(&cursor); err != nil {
		return exportCursor{}, ErrExportCursorInvalid
	}
	var extra any
	if err := decoder.Decode(&extra); err != io.EOF ||
		cursor.SnapshotID != snapshotID ||
		cursor.ItemIndex < 0 {
		return exportCursor{}, ErrExportCursorInvalid
	}
	return cursor, nil
}

func validateDeletionRequest(
	requestID,
	capability string,
) (string, []byte, error) {
	requestID = strings.TrimSpace(requestID)
	if !IsUUID(requestID) {
		return "", nil, ErrDeletionRetryInvalid
	}
	requestID = strings.ToLower(requestID)
	if capability == "" || strings.TrimSpace(capability) != capability {
		return "", nil, ErrDeletionRetryInvalid
	}
	raw, err := base64.RawURLEncoding.DecodeString(capability)
	if err != nil || len(raw) != deletionCapabilityByteCount ||
		base64.RawURLEncoding.EncodeToString(raw) != capability {
		return "", nil, ErrDeletionRetryInvalid
	}
	return requestID, TokenHash(capability), nil
}

type deletionReceipt struct {
	RequestID      string
	AccountID      string
	CapabilityHash []byte
	CreatedAt      time.Time
	ExpiresAt      time.Time
	CompletedAt    time.Time
}

func loadDeletionReceiptTx(
	ctx context.Context,
	tx pgx.Tx,
	requestID string,
) (deletionReceipt, error) {
	var value deletionReceipt
	err := tx.QueryRow(ctx, `
        SELECT
            request_id::text,
            account_id::text,
            capability_hash,
            created_at,
            expires_at,
            completed_at
        FROM account_deletion_receipts
        WHERE request_id = $1::uuid
        FOR UPDATE
    `, requestID).Scan(
		&value.RequestID,
		&value.AccountID,
		&value.CapabilityHash,
		&value.CreatedAt,
		&value.ExpiresAt,
		&value.CompletedAt)
	return value, err
}

type deletionAccountRecord struct {
	Account
	PasswordHash string
}

func loadDeletionAccountTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID string,
) (deletionAccountRecord, error) {
	var value deletionAccountRecord
	var usernameChangedAt sql.NullTime
	err := tx.QueryRow(ctx, `
        SELECT
            id::text,
            canonical_username,
            display_username,
            password_hash,
            protect_new_device_signins,
            COALESCE(builtin_avatar_id, ''),
            COALESCE(uploaded_avatar_object_key, ''),
            username_changed_at,
            created_at,
            updated_at
        FROM accounts
        WHERE id = $1::uuid
        FOR UPDATE
    `, accountID).Scan(
		&value.ID,
		&value.CanonicalUsername,
		&value.DisplayUsername,
		&value.PasswordHash,
		&value.ProtectNewDeviceSignins,
		&value.BuiltinAvatarID,
		&value.UploadedAvatarObjectKey,
		&usernameChangedAt,
		&value.CreatedAt,
		&value.UpdatedAt)
	if usernameChangedAt.Valid {
		changed := usernameChangedAt.Time.UTC()
		value.UsernameChangedAt = &changed
	}
	return value, err
}

// DeleteAccount performs the irreversible account deletion. The retry
// capability is never stored directly. Its verifier and the client request id
// are inserted in the same transaction that enqueues avatar cleanup and
// deletes the account, so a committed deletion remains discoverable after all
// ordinary sessions have cascaded away.
func (s *Service) DeleteAccount(
	ctx context.Context,
	auth AuthenticatedSession,
	input DeleteAccountInput,
) (DeleteAccountResult, error) {
	requestID, capabilityHash, err := validateDeletionRequest(
		input.RequestID,
		input.RetryCapability)
	if err != nil {
		return DeleteAccountResult{}, err
	}
	if err := s.rateLimiter.Allow(
		ctx,
		"account_delete_reauth",
		[]string{auth.Account.ID, auth.Device.ID},
		reauthWindow,
		reauthLimit); err != nil {
		return DeleteAccountResult{}, err
	}

	account, err := s.loadAuthAccountByID(ctx, auth.Account.ID)
	if errors.Is(err, ErrInvalidCredentials) {
		return DeleteAccountResult{}, ErrSessionInvalid
	}
	if err != nil {
		return DeleteAccountResult{}, err
	}
	valid, err := s.passwordVerify(
		account.PasswordHash,
		norm.NFC.String(input.CurrentPassword))
	if err != nil {
		return DeleteAccountResult{}, fmt.Errorf("verify deletion password: %w", err)
	}
	if !valid {
		return DeleteAccountResult{}, ErrInvalidCredentials
	}

	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return DeleteAccountResult{}, fmt.Errorf("begin account deletion: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	// Sync writers acquire this account advisory lock before any account-owned
	// insert/FK work. Deletion must use the same first lock to avoid a cycle
	// with a writer that is waiting to commit a child row.
	if err := lockAccountSyncTx(ctx, tx, auth.Account.ID); err != nil {
		return DeleteAccountResult{}, err
	}
	accountRecord, err := loadDeletionAccountTx(ctx, tx, auth.Account.ID)
	if errors.Is(err, pgx.ErrNoRows) {
		return DeleteAccountResult{}, ErrSessionInvalid
	}
	if err != nil {
		return DeleteAccountResult{}, fmt.Errorf("lock account for deletion: %w", err)
	}
	if !sameEncodedPasswordHash(accountRecord.PasswordHash, account.PasswordHash) {
		return DeleteAccountResult{}, ErrInvalidCredentials
	}
	if err := validateAuthenticatedSessionTx(ctx, tx, auth, s.clock.Now); err != nil {
		return DeleteAccountResult{}, err
	}

	existing, err := loadDeletionReceiptTx(ctx, tx, requestID)
	if err != nil && !errors.Is(err, pgx.ErrNoRows) {
		return DeleteAccountResult{}, fmt.Errorf("load deletion receipt: %w", err)
	}
	if err == nil {
		if existing.AccountID != auth.Account.ID ||
			subtle.ConstantTimeCompare(existing.CapabilityHash, capabilityHash) != 1 {
			return DeleteAccountResult{}, ErrDeletionRetryInvalid
		}
		if !existing.ExpiresAt.After(s.clock.Now().UTC()) {
			return DeleteAccountResult{}, ErrDeletionRetryInvalid
		}
		return DeleteAccountResult{
			Status:           "completed",
			Retried:          true,
			ReceiptExpiresAt: existing.ExpiresAt.UTC(),
		}, nil
	}
	now := s.clock.Now().UTC()
	expiresAt := now.Add(deletionReceiptLifetime)
	if _, err := tx.Exec(ctx, `
        INSERT INTO account_deletion_receipts(
            request_id,
            capability_hash,
            account_id,
            created_at,
            expires_at,
            completed_at
        )
        VALUES($1::uuid, $2, $3::uuid, $4, $5, $4)
    `, requestID, capabilityHash, auth.Account.ID, now, expiresAt); err != nil {
		if isUniqueViolation(err) {
			return DeleteAccountResult{}, ErrDeletionRetryInvalid
		}
		return DeleteAccountResult{}, fmt.Errorf("create deletion receipt: %w", err)
	}

	if accountRecord.UploadedAvatarObjectKey != "" {
		if err := enqueueAvatarCleanupTx(
			ctx,
			tx,
			accountRecord.UploadedAvatarObjectKey,
			now,
			"pending"); err != nil {
			return DeleteAccountResult{}, err
		}
	}

	// The canonical username remains reserved permanently to prevent
	// impersonation. Clearing the historical account marker keeps the
	// reservation independent of a deleted account while preserving the
	// existing anti-reuse policy.
	if _, err := tx.Exec(ctx, `
        UPDATE username_reservations
        SET reserved_account_id = NULL
        WHERE canonical_username = $1
    `, accountRecord.CanonicalUsername); err != nil {
		return DeleteAccountResult{}, fmt.Errorf("retain deleted username reservation: %w", err)
	}

	command, err := tx.Exec(ctx,
		"DELETE FROM accounts WHERE id = $1::uuid", auth.Account.ID)
	if err != nil {
		return DeleteAccountResult{}, fmt.Errorf("delete account data: %w", err)
	}
	if command.RowsAffected() != 1 {
		return DeleteAccountResult{}, ErrSessionInvalid
	}
	if err := tx.Commit(ctx); err != nil {
		return DeleteAccountResult{}, fmt.Errorf("commit account deletion: %w", err)
	}
	return DeleteAccountResult{
		Status:           "completed",
		ReceiptExpiresAt: expiresAt,
	}, nil
}

// RetryDeleteAccount is intentionally unauthenticated. It returns completion
// metadata only when both opaque client values match a retained receipt. Any
// malformed, unknown, expired or cross-account value maps to the same generic
// error, so it cannot be used to probe account existence.
func (s *Service) RetryDeleteAccount(
	ctx context.Context,
	input DeleteAccountRetryInput,
) (DeleteAccountResult, error) {
	requestID, capabilityHash, err := validateDeletionRequest(
		input.RequestID,
		input.RetryCapability)
	if err != nil {
		return DeleteAccountResult{}, err
	}

	var expiresAt, completedAt time.Time
	err = s.pool.QueryRow(ctx, `
        SELECT expires_at, completed_at
        FROM account_deletion_receipts
        WHERE request_id = $1::uuid
          AND capability_hash = $2
    `, requestID, capabilityHash).Scan(&expiresAt, &completedAt)
	if errors.Is(err, pgx.ErrNoRows) {
		return DeleteAccountResult{}, ErrDeletionRetryInvalid
	}
	if err != nil {
		return DeleteAccountResult{}, fmt.Errorf("lookup deletion receipt: %w", err)
	}
	if !expiresAt.After(s.clock.Now().UTC()) {
		return DeleteAccountResult{}, ErrDeletionRetryInvalid
	}
	return DeleteAccountResult{
		Status:           "completed",
		Retried:          true,
		ReceiptExpiresAt: expiresAt.UTC(),
	}, nil
}

// PruneExportSnapshots and PruneDeletionReceipts are bounded maintenance
// seams. Operations invokes them with an explicit cutoff; they do not run a
// hidden ticker in the request service.
func (s *Service) PruneExportSnapshots(
	ctx context.Context,
	before time.Time,
	batchSize int,
) error {
	if before.IsZero() {
		return fmt.Errorf("export snapshot prune cutoff is required")
	}
	if batchSize <= 0 {
		return fmt.Errorf("export snapshot prune batch size is required")
	}
	snapshotLimit := min(batchSize, lifecyclePruneSnapshotLimit)
	itemLimit := min(batchSize, lifecyclePruneItemLimit)
	for deletedSnapshots := 0; deletedSnapshots < snapshotLimit; deletedSnapshots++ {
		found, complete, err := s.pruneOneExpiredExportSnapshot(
			ctx, before.UTC(), itemLimit)
		if err != nil {
			return err
		}
		if !found || !complete {
			return nil
		}
	}
	return nil
}

func (s *Service) pruneOneExpiredExportSnapshot(
	ctx context.Context,
	before time.Time,
	itemLimit int,
) (found, complete bool, err error) {
	var snapshotID, accountID string
	err = s.pool.QueryRow(ctx, `
        SELECT id::text, account_id::text
        FROM account_export_snapshots
        WHERE expires_at <= $1
        ORDER BY expires_at, id
        LIMIT 1
    `, before).Scan(&snapshotID, &accountID)
	if errors.Is(err, pgx.ErrNoRows) {
		return false, true, nil
	}
	if err != nil {
		return false, false, fmt.Errorf("select export snapshot for prune: %w", err)
	}

	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return false, false, fmt.Errorf("begin export snapshot prune: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()
	if err := lockAccountSyncTx(ctx, tx, accountID); err != nil {
		return false, false, err
	}
	var lockedID string
	err = tx.QueryRow(ctx, `
        SELECT id::text
        FROM account_export_snapshots
        WHERE id = $1::uuid
          AND expires_at <= $2
        FOR UPDATE
    `, snapshotID, before).Scan(&lockedID)
	if errors.Is(err, pgx.ErrNoRows) {
		if err := tx.Commit(ctx); err != nil {
			return false, false, fmt.Errorf("commit skipped export snapshot prune: %w", err)
		}
		return false, true, nil
	}
	if err != nil {
		return false, false, fmt.Errorf("lock export snapshot for prune: %w", err)
	}
	if itemLimit <= 0 {
		itemLimit = lifecyclePruneItemLimit
	}
	if _, err := tx.Exec(ctx, `
        DELETE FROM account_export_items
        WHERE snapshot_id = $1::uuid
          AND item_index IN (
              SELECT item_index
              FROM account_export_items
              WHERE snapshot_id = $1::uuid
              ORDER BY item_index
              LIMIT $2
          )
    `, lockedID, itemLimit); err != nil {
		return false, false, fmt.Errorf("prune export snapshot items: %w", err)
	}
	var hasItems bool
	if err := tx.QueryRow(ctx, `
        SELECT EXISTS(
            SELECT 1 FROM account_export_items WHERE snapshot_id = $1::uuid
        )
    `, lockedID).Scan(&hasItems); err != nil {
		return false, false, fmt.Errorf("check export snapshot items: %w", err)
	}
	if hasItems {
		if err := tx.Commit(ctx); err != nil {
			return false, false, fmt.Errorf("commit bounded export item prune: %w", err)
		}
		return true, false, nil
	}
	if _, err := tx.Exec(ctx,
		"DELETE FROM account_export_snapshots WHERE id = $1::uuid",
		lockedID); err != nil {
		return false, false, fmt.Errorf("delete expired export snapshot: %w", err)
	}
	if err := tx.Commit(ctx); err != nil {
		return false, false, fmt.Errorf("commit export snapshot prune: %w", err)
	}
	return true, true, nil
}

func (s *Service) PruneDeletionReceipts(
	ctx context.Context,
	before time.Time,
	batchSize int,
) error {
	if before.IsZero() {
		return fmt.Errorf("deletion receipt prune cutoff is required")
	}
	if batchSize <= 0 {
		return fmt.Errorf("deletion receipt prune batch size is required")
	}
	receiptLimit := min(batchSize, lifecyclePruneReceiptLimit)
	if _, err := s.pool.Exec(ctx,
		`DELETE FROM account_deletion_receipts
         WHERE request_id IN (
             SELECT request_id
             FROM account_deletion_receipts
             WHERE expires_at <= $1
             ORDER BY expires_at, request_id
             LIMIT $2
         )`,
		before.UTC(), receiptLimit); err != nil {
		return fmt.Errorf("prune deletion receipts: %w", err)
	}
	return nil
}

func (s *Service) PruneLifecycleArtifacts(
	ctx context.Context,
	before time.Time,
	batchSize int,
) error {
	var errs []error
	if err := s.PruneExportSnapshots(ctx, before, batchSize); err != nil {
		errs = append(errs, err)
	}
	if err := s.PruneDeletionReceipts(ctx, before, batchSize); err != nil {
		errs = append(errs, err)
	}
	return errors.Join(errs...)
}
