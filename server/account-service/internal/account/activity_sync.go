package account

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"strings"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
)

// The immutable server Activity fact path (Arc 36 N-10). Activity facts are
// append-only events, not mutable canonical records: they never enter
// account_sync_journal or account_sync_current, never merge by HLC, and are
// deduplicated by their normalized event identity instead of last-write-wins.
// The desktop counterpart is the N-09 ActivitySyncAdapter, whose wire shape
// this file mirrors: category "activity_fact", schema 1, record key
// "activity/<lowercase-eventId>", PUT-only, and a portable payload whose
// eventId may keep its original casing while its identity stays the
// normalized lowercase UUID.

const activityRecordKeyPrefix = "activity/"

var activityAllowedEventTypes = map[string]struct{}{
	"playback_delta":  {},
	"reading_delta":   {},
	"media_completed": {},
}

type parsedActivityFact struct {
	EventID   string
	EventType string
	Canonical []byte
}

func parseActivityFact(
	parsed parsedSyncMutation,
) (parsedActivityFact, string, string) {
	if parsed.Operation != "put" {
		return parsedActivityFact{},
			"invalid_operation",
			"Activity facts are immutable and accept PUT only."
	}

	suffix, hasPrefix := strings.CutPrefix(parsed.RecordKey, activityRecordKeyPrefix)
	if !hasPrefix ||
		!IsUUID(suffix) ||
		suffix != strings.ToLower(suffix) ||
		suffix != strings.TrimSpace(suffix) {
		return parsedActivityFact{},
			"invalid_record_key",
			"The Activity record key is invalid."
	}

	object, err := decodeActivityPayloadObject(parsed.Payload)
	if err != nil {
		return parsedActivityFact{},
			"payload_invalid",
			"The Activity fact payload must be a JSON object."
	}

	eventIDValue, ok := object["eventId"].(string)
	if !ok ||
		!IsUUID(eventIDValue) ||
		strings.ToLower(strings.TrimSpace(eventIDValue)) != suffix {
		return parsedActivityFact{},
			"activity_key_payload_mismatch",
			"The Activity payload identity does not match its record key."
	}

	syncable, ok := object["syncable"].(bool)
	if !ok || !syncable {
		return parsedActivityFact{},
			"activity_not_syncable",
			"Activity facts must be syncable."
	}

	eventType, ok := object["type"].(string)
	if !ok {
		return parsedActivityFact{},
			"activity_unsupported_type",
			"The Activity fact type is not accepted."
	}
	if _, allowed := activityAllowedEventTypes[eventType]; !allowed {
		return parsedActivityFact{},
			"activity_unsupported_type",
			"The Activity fact type is not accepted."
	}

	canonical, err := canonicalActivityJSON(object)
	if err != nil {
		return parsedActivityFact{},
			"payload_invalid",
			"The Activity fact payload could not be canonicalized."
	}

	return parsedActivityFact{
		EventID:   suffix,
		EventType: eventType,
		Canonical: canonical,
	}, "", ""
}

func decodeActivityPayloadObject(
	raw json.RawMessage,
) (map[string]any, error) {
	if len(raw) == 0 {
		return nil, fmt.Errorf("activity payload is empty")
	}
	decoder := json.NewDecoder(bytes.NewReader(raw))
	decoder.UseNumber()

	var value any
	if err := decoder.Decode(&value); err != nil {
		return nil, fmt.Errorf("activity payload is invalid JSON: %w", err)
	}
	var extra any
	if err := decoder.Decode(&extra); err != io.EOF {
		if err == nil {
			return nil, fmt.Errorf("activity payload contains trailing JSON")
		}
		return nil, fmt.Errorf("activity payload has invalid trailing data: %w", err)
	}
	object, ok := value.(map[string]any)
	if !ok || object == nil {
		return nil, fmt.Errorf("activity payload must be a JSON object")
	}
	return object, nil
}

// canonicalActivityJSON renders the portable fact deterministically — object
// keys sorted (Go map marshalling), numbers kept as their original literals
// via json.Number — so two payloads that differ only in key order or spacing
// compare equal. eventId casing is preserved, matching the desktop canonical
// portable projection.
func canonicalActivityJSON(object map[string]any) ([]byte, error) {
	encoded, err := json.Marshal(object)
	if err != nil {
		return nil, fmt.Errorf("encode canonical Activity payload: %w", err)
	}
	return encoded, nil
}

func (s *Service) pushOneActivityFact(
	ctx context.Context,
	auth AuthenticatedSession,
	parsed parsedSyncMutation,
	fact parsedActivityFact,
	now time.Time,
) (SyncPushResult, error) {
	result := SyncPushResult{
		MutationID: parsed.MutationID,
	}

	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return result, fmt.Errorf("begin activity fact: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if err := lockAccountSyncTx(ctx, tx, auth.Account.ID); err != nil {
		return result, err
	}

	existingRows, err := loadMutationRowsByIDTx(
		ctx, tx, auth.Account.ID, parsed.MutationID)
	if err != nil {
		return result, err
	}
	if existingRows.JournalFound ||
		(existingRows.ActivityFound && existingRows.AliasFound) {
		return syncMutationConflictResult(parsed.MutationID), nil
	}
	if existingRows.AliasFound {
		if !s.syncAliasMatches(parsed, fact.Canonical, existingRows.Alias) {
			return syncMutationConflictResult(parsed.MutationID), nil
		}
		if err := tx.Commit(ctx); err != nil {
			return result, fmt.Errorf("commit activity retry: %w", err)
		}
		result.Accepted = true
		result.ServerSeq = existingRows.Alias.ServerSeq
		result.Won = existingRows.Alias.Won
		return result, nil
	}
	if existingRows.ActivityFound {
		exact, err := s.syncStoredMutationMatches(
			ctx,
			auth.Account.ID,
			parsed,
			fact.Canonical,
			existingRows.Activity)
		if err != nil {
			return result, err
		}
		if !exact {
			return syncMutationConflictResult(parsed.MutationID), nil
		}
		if err := tx.Commit(ctx); err != nil {
			return result, fmt.Errorf("commit activity retry: %w", err)
		}
		result.Accepted = true
		result.ServerSeq = existingRows.Activity.ServerSeq
		result.Won = existingRows.Activity.Won
		return result, nil
	}

	// Serialize concurrent pushes of the same event identity so equal
	// duplicates converge on one row instead of racing the unique checks.
	lockKey :=
		auth.Account.ID + "\x1f" +
			parsed.Category + "\x1f" +
			fact.EventID
	if _, err := tx.Exec(ctx, `
        SELECT pg_advisory_xact_lock(hashtextextended($1, 0))
    `, lockKey); err != nil {
		return result, fmt.Errorf("lock activity event: %w", err)
	}

	storedCanonical, storedSeq, found, err := s.loadActivityCanonicalTx(
		ctx, tx, auth.Account.ID, parsed.Category, parsed.RecordKey, fact.EventID)
	if err != nil {
		return result, err
	}
	if found {
		if !bytes.Equal(storedCanonical, fact.Canonical) {
			result.Code = "activity_event_conflict"
			result.Message =
				"An Activity fact with this event id already exists with different content."
			return result, nil
		}

		if err := insertActivityMutationAliasTx(
			ctx,
			tx,
			auth.Account.ID,
			parsed,
			fact,
			storedSeq,
			true,
			now); err != nil {
			return result, err
		}
		if err := tx.Commit(ctx); err != nil {
			return result, fmt.Errorf("commit activity duplicate: %w", err)
		}
		// Semantic idempotency: same event, equal canonical portable content.
		// No new row, no sequence consumption, the original server_seq.
		result.Accepted = true
		result.ServerSeq = storedSeq
		result.Won = true
		return result, nil
	}

	ciphertext, err := s.syncCipher.Seal(
		auth.Account.ID,
		parsed.Category,
		parsed.RecordKey,
		fact.Canonical)
	if err != nil {
		return result, fmt.Errorf("encrypt activity payload: %w", err)
	}

	var serverSeq int64
	err = tx.QueryRow(ctx, `
        INSERT INTO account_activity_facts(
            account_id,
            event_id,
            mutation_id,
            origin_device_id,
            schema_version,
            event_type,
            payload_ciphertext,
            hlc_physical_ms,
            hlc_counter,
            received_at
        )
        VALUES(
            $1::uuid, $2::uuid, $3::uuid, $4::uuid,
            $5, $6, $7, $8, $9, $10
        )
        RETURNING server_seq
    `,
		auth.Account.ID,
		fact.EventID,
		parsed.MutationID,
		parsed.DeviceID,
		parsed.SchemaVersion,
		fact.EventType,
		ciphertext,
		parsed.HLCPhysicalMS,
		int64(parsed.HLCCounter),
		now).Scan(&serverSeq)

	if err != nil {
		var pgErr *pgconn.PgError
		if errors.As(err, &pgErr) && pgErr.Code == "23505" {
			// Every service writer takes the account lock, but this rollback also
			// makes recovery safe if an older process or an administrative repair
			// races the insert. The fresh lookup must not nest a pool acquisition
			// beneath this transaction.
			if rollbackErr := tx.Rollback(ctx); rollbackErr != nil {
				return result, fmt.Errorf("rollback activity unique violation: %w", rollbackErr)
			}
			return s.resolveActivityUniqueViolation(
				ctx, auth, parsed, fact)
		}
		return result, fmt.Errorf("insert activity fact: %w", err)
	}

	if err := tx.Commit(ctx); err != nil {
		return result, fmt.Errorf("commit activity fact: %w", err)
	}

	result.Accepted = true
	result.ServerSeq = uint64(serverSeq)
	result.Won = true
	return result, nil
}

// resolveActivityUniqueViolation finishes a raced insert by re-reading the
// committed winner on a fresh transaction and applying the same accepted /
// idempotent / conflict semantics as the uncontended path.
func (s *Service) resolveActivityUniqueViolation(
	ctx context.Context,
	auth AuthenticatedSession,
	parsed parsedSyncMutation,
	fact parsedActivityFact,
) (SyncPushResult, error) {
	result := SyncPushResult{
		MutationID: parsed.MutationID,
	}

	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return result, fmt.Errorf("begin activity race resolution: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if err := lockAccountSyncTx(ctx, tx, auth.Account.ID); err != nil {
		return result, err
	}

	existingRows, err := loadMutationRowsByIDTx(
		ctx, tx, auth.Account.ID, parsed.MutationID)
	if err != nil {
		return result, err
	}
	if existingRows.JournalFound ||
		(existingRows.ActivityFound && existingRows.AliasFound) {
		return syncMutationConflictResult(parsed.MutationID), nil
	}
	if existingRows.AliasFound {
		if !s.syncAliasMatches(parsed, fact.Canonical, existingRows.Alias) {
			return syncMutationConflictResult(parsed.MutationID), nil
		}
		if err := tx.Commit(ctx); err != nil {
			return result, fmt.Errorf("commit activity race resolution: %w", err)
		}
		result.Accepted = true
		result.ServerSeq = existingRows.Alias.ServerSeq
		result.Won = existingRows.Alias.Won
		return result, nil
	}
	if existingRows.ActivityFound {
		exact, err := s.syncStoredMutationMatches(
			ctx,
			auth.Account.ID,
			parsed,
			fact.Canonical,
			existingRows.Activity)
		if err != nil {
			return result, err
		}
		if !exact {
			return syncMutationConflictResult(parsed.MutationID), nil
		}
		if err := tx.Commit(ctx); err != nil {
			return result, fmt.Errorf("commit activity race resolution: %w", err)
		}
		result.Accepted = true
		result.ServerSeq = existingRows.Activity.ServerSeq
		result.Won = existingRows.Activity.Won
		return result, nil
	}

	storedCanonical, storedSeq, found, err := s.loadActivityCanonicalTx(
		ctx, tx, auth.Account.ID, parsed.Category, parsed.RecordKey, fact.EventID)
	if err != nil {
		return result, err
	}
	if found && bytes.Equal(storedCanonical, fact.Canonical) {
		if err := insertActivityMutationAliasTx(
			ctx,
			tx,
			auth.Account.ID,
			parsed,
			fact,
			storedSeq,
			true,
			s.clock.Now().UTC()); err != nil {
			return result, err
		}
		if err := tx.Commit(ctx); err != nil {
			return result, fmt.Errorf("commit activity race resolution: %w", err)
		}
		result.Accepted = true
		result.ServerSeq = storedSeq
		result.Won = true
		return result, nil
	}

	if found {
		result.Code = "activity_event_conflict"
		result.Message =
			"An Activity fact with this event id already exists with different content."
		return result, nil
	}
	return result, fmt.Errorf("activity unique violation did not resolve to an existing fact")
}

func loadActivityByMutationIDTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID,
	mutationID string,
) (syncStoredMutation, bool, error) {
	row := tx.QueryRow(ctx, `
        SELECT
            server_seq,
            mutation_id::text,
            origin_device_id::text,
            event_id::text,
            schema_version,
            hlc_physical_ms,
            hlc_counter,
            payload_ciphertext,
            received_at
        FROM account_activity_facts
        WHERE account_id = $1::uuid
          AND mutation_id = $2::uuid
    `, accountID, mutationID)

	var stored syncStoredMutation
	var eventID string
	var serverSeq, counter int64
	if err := row.Scan(
		&serverSeq,
		&stored.MutationID,
		&stored.DeviceID,
		&eventID,
		&stored.SchemaVersion,
		&stored.HLCPhysicalMS,
		&counter,
		&stored.PayloadCipher,
		&stored.ReceivedAt); err != nil {
		if err == pgx.ErrNoRows {
			return syncStoredMutation{}, false, nil
		}
		return syncStoredMutation{}, false, fmt.Errorf("load activity idempotency row: %w", err)
	}
	if serverSeq <= 0 {
		return syncStoredMutation{}, false, fmt.Errorf("activity idempotency row has invalid server_seq")
	}
	if counter < 0 {
		return syncStoredMutation{}, false, fmt.Errorf("activity idempotency row has invalid HLC counter")
	}
	stored.Category = "activity_fact"
	stored.RecordKey = activityRecordKeyPrefix + eventID
	stored.Operation = "put"
	stored.ServerSeq = uint64(serverSeq)
	stored.HLCCounter = uint64(counter)
	stored.Won = true
	return stored, true, nil
}

func loadActivityMutationAliasByIDTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID,
	mutationID string,
) (syncMutationAlias, bool, error) {
	row := tx.QueryRow(ctx, `
        SELECT
            mutation_id::text,
            device_id::text,
            category,
            record_key,
            schema_version,
            hlc_physical_ms,
            hlc_counter,
            operation,
            canonical_payload_hash,
            server_seq,
            won,
            activity_event_id::text,
            created_at
        FROM account_sync_mutation_aliases
        WHERE account_id = $1::uuid
          AND mutation_id = $2::uuid
    `, accountID, mutationID)

	var alias syncMutationAlias
	var serverSeq, counter int64
	if err := row.Scan(
		&alias.MutationID,
		&alias.DeviceID,
		&alias.Category,
		&alias.RecordKey,
		&alias.SchemaVersion,
		&alias.HLCPhysicalMS,
		&counter,
		&alias.Operation,
		&alias.PayloadHash,
		&serverSeq,
		&alias.Won,
		&alias.ActivityEventID,
		&alias.ReceivedAt); err != nil {
		if err == pgx.ErrNoRows {
			return syncMutationAlias{}, false, nil
		}
		return syncMutationAlias{}, false, fmt.Errorf("load activity mutation alias: %w", err)
	}
	if serverSeq <= 0 || counter < 0 {
		return syncMutationAlias{}, false, fmt.Errorf("activity mutation alias has invalid numeric state")
	}
	alias.ServerSeq = uint64(serverSeq)
	alias.HLCCounter = uint64(counter)
	return alias, true, nil
}

func insertActivityMutationAliasTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID string,
	parsed parsedSyncMutation,
	fact parsedActivityFact,
	serverSeq uint64,
	won bool,
	createdAt time.Time,
) error {
	if serverSeq == 0 {
		return fmt.Errorf("activity mutation alias requires a server sequence")
	}
	if _, err := tx.Exec(ctx, `
        INSERT INTO account_sync_mutation_aliases(
            account_id,
            mutation_id,
            category,
            record_key,
            device_id,
            schema_version,
            hlc_physical_ms,
            hlc_counter,
            operation,
            canonical_payload_hash,
            server_seq,
            won,
            activity_event_id,
            created_at
        )
        VALUES(
            $1::uuid, $2::uuid, $3, $4, $5::uuid, $6,
            $7, $8, $9, $10, $11, $12, $13::uuid, $14
        )
    `,
		accountID,
		parsed.MutationID,
		parsed.Category,
		parsed.RecordKey,
		parsed.DeviceID,
		parsed.SchemaVersion,
		parsed.HLCPhysicalMS,
		int64(parsed.HLCCounter),
		parsed.Operation,
		canonicalPayloadHash(fact.Canonical),
		int64(serverSeq),
		won,
		fact.EventID,
		createdAt.UTC()); err != nil {
		return fmt.Errorf("store activity mutation alias: %w", err)
	}
	return nil
}

func (s *Service) loadActivityCanonicalTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID,
	category,
	recordKey,
	eventID string,
) ([]byte, uint64, bool, error) {
	row := tx.QueryRow(ctx, `
        SELECT payload_ciphertext, server_seq
        FROM account_activity_facts
        WHERE account_id = $1::uuid
          AND event_id = $2::uuid
    `, accountID, eventID)

	var ciphertext []byte
	var serverSeq int64
	if err := row.Scan(&ciphertext, &serverSeq); err != nil {
		if err == pgx.ErrNoRows {
			return nil, 0, false, nil
		}
		return nil, 0, false, fmt.Errorf("load activity fact: %w", err)
	}
	if serverSeq <= 0 {
		return nil, 0, false, fmt.Errorf("activity fact has invalid server_seq")
	}

	plain, err := s.syncCipher.Open(accountID, category, recordKey, ciphertext)
	if err != nil {
		return nil, 0, false, fmt.Errorf("decrypt activity fact: %w", err)
	}
	if !json.Valid(plain) {
		return nil, 0, false, fmt.Errorf("decrypted activity fact is malformed")
	}
	return plain, uint64(serverSeq), true, nil
}
