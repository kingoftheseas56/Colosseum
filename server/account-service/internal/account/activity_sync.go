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

	// Transport idempotency: the exact (account, mutation) retry replays the
	// original server_seq without re-encrypting or consuming a sequence value.
	retrySeq, found, err := loadActivityServerSeqByMutationIDTx(
		ctx, tx, auth.Account.ID, parsed.MutationID)
	if err != nil {
		return result, err
	}
	if found {
		if err := tx.Commit(ctx); err != nil {
			return result, fmt.Errorf("commit activity retry: %w", err)
		}
		result.Accepted = true
		result.ServerSeq = retrySeq
		result.Won = true
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
			if err := tx.Rollback(ctx); err != nil {
				return result, fmt.Errorf("rollback activity conflict: %w", err)
			}
			result.Code = "activity_event_conflict"
			result.Message =
				"An Activity fact with this event id already exists with different content."
			return result, nil
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
			// A concurrent writer committed this mutation or event first
			// (the advisory lock only serializes one event identity). Replay
			// through the same idempotency lookups instead of failing.
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

	retrySeq, found, err := loadActivityServerSeqByMutationIDTx(
		ctx, tx, auth.Account.ID, parsed.MutationID)
	if err != nil {
		return result, err
	}
	if found {
		if err := tx.Commit(ctx); err != nil {
			return result, fmt.Errorf("commit activity race resolution: %w", err)
		}
		result.Accepted = true
		result.ServerSeq = retrySeq
		result.Won = true
		return result, nil
	}

	storedCanonical, storedSeq, found, err := s.loadActivityCanonicalTx(
		ctx, tx, auth.Account.ID, parsed.Category, parsed.RecordKey, fact.EventID)
	if err != nil {
		return result, err
	}
	if found && bytes.Equal(storedCanonical, fact.Canonical) {
		if err := tx.Commit(ctx); err != nil {
			return result, fmt.Errorf("commit activity race resolution: %w", err)
		}
		result.Accepted = true
		result.ServerSeq = storedSeq
		result.Won = true
		return result, nil
	}

	result.Code = "activity_event_conflict"
	result.Message =
		"An Activity fact with this event id already exists with different content."
	return result, nil
}

func loadActivityServerSeqByMutationIDTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID,
	mutationID string,
) (uint64, bool, error) {
	row := tx.QueryRow(ctx, `
        SELECT server_seq
        FROM account_activity_facts
        WHERE account_id = $1::uuid
          AND mutation_id = $2::uuid
    `, accountID, mutationID)

	var serverSeq int64
	if err := row.Scan(&serverSeq); err != nil {
		if err == pgx.ErrNoRows {
			return 0, false, nil
		}
		return 0, false, fmt.Errorf("load activity idempotency row: %w", err)
	}
	if serverSeq <= 0 {
		return 0, false, fmt.Errorf("activity idempotency row has invalid server_seq")
	}
	return uint64(serverSeq), true, nil
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
