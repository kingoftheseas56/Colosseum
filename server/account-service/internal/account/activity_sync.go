package account

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"math/big"
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

var activityCommonFields = map[string]struct{}{
	"v": {}, "type": {}, "eventId": {}, "sessionId": {}, "world": {},
	"kind": {}, "titleKey": {}, "itemKey": {}, "title": {}, "itemLabel": {},
	"cover": {}, "utcOffsetMinutes": {}, "syncable": {}, "source": {},
}

var activityTypeFields = map[string]map[string]struct{}{
	"playback_delta": {
		"startAtMs": {}, "endAtMs": {}, "activeMs": {}, "rateMilli": {},
	},
	"reading_delta": {
		"atMs": {}, "readingForm": {}, "pageKeys": {}, "progressMicros": {},
	},
	"media_completed": {
		"atMs": {}, "reason": {},
	},
}

// ActivityProjector uses the JavaScript Date-compatible millisecond window.
// Its calendar projection also shifts values by utcOffsetMinutes, so the
// same shifted range is enforced before a fact can enter the sync ledger.
const activityTimeClipLimitMs int64 = 8640000000000000

type parsedActivityFact struct {
	EventID   string
	EventType string
	Canonical []byte
}

type parsedActivityReset struct {
	Generation uint64
	ResetAtMS  int64
	Canonical  []byte
}

func activityFactAt(canonical []byte) (int64, bool) {
	object, err := decodeActivityPayloadObject(canonical)
	if err != nil {
		return 0, false
	}
	field := "atMs"
	if object["type"] == "playback_delta" {
		field = "startAtMs"
	}
	return activityInteger(object[field])
}

func parseActivityReset(parsed parsedSyncMutation) (parsedActivityReset, string, string) {
	if parsed.RecordKey != "activity/reset" || parsed.Operation != "put" {
		return parsedActivityReset{}, "invalid_operation", "Activity reset barriers accept PUT only."
	}
	object, err := decodeActivityPayloadObject(parsed.Payload)
	if err != nil || validateActivityResetPayload(object) != nil {
		return parsedActivityReset{}, "payload_invalid", "The Activity reset payload is invalid."
	}
	generation, ok := syncIntegerNumber(object["resetGeneration"])
	resetAt, resetAtOK := syncIntegerNumber(object["resetAtMs"])
	if !ok || !resetAtOK || generation <= 0 || resetAt <= 0 {
		return parsedActivityReset{}, "payload_invalid", "The Activity reset payload is invalid."
	}
	canonical, err := canonicalActivityJSON(object)
	if err != nil {
		return parsedActivityReset{}, "payload_invalid", "The Activity reset payload could not be canonicalized."
	}
	return parsedActivityReset{Generation: uint64(generation), ResetAtMS: resetAt, Canonical: canonical}, "", ""
}

func (s *Service) pushOneActivityReset(
	ctx context.Context,
	auth AuthenticatedSession,
	parsed parsedSyncMutation,
	now time.Time,
) (SyncPushResult, error) {
	return s.pushOneSyncMutation(ctx, auth, parsed, now)
}

func (s *Service) storeActivityResetStateTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID string,
	resolution syncResolution,
) error {
	reset, err := decodeActivityResetPayload(resolution.Payload)
	if err != nil {
		return fmt.Errorf("decode materialized Activity reset: %w", err)
	}
	var currentGeneration, currentAt int64
	var currentPhysical, currentCounter int64
	var currentDevice string
	err = tx.QueryRow(ctx, `
        SELECT reset_generation, reset_at_ms, hlc_physical_ms,
               hlc_counter, COALESCE(device_id::text, '')
        FROM account_activity_reset_state
        WHERE account_id = $1::uuid
        FOR UPDATE
    `, accountID).Scan(
		&currentGeneration,
		&currentAt,
		&currentPhysical,
		&currentCounter,
		&currentDevice)
	if err != nil && err != pgx.ErrNoRows {
		return fmt.Errorf("load Activity reset state: %w", err)
	}
	effectiveGeneration := reset.generation
	effectiveAt := reset.resetAt
	effectivePhysical := resolution.WinnerHLCPhysicalMS
	effectiveCounter := resolution.WinnerHLCCounter
	effectiveDevice := resolution.WinnerDeviceID
	if err == nil {
		if currentGeneration > effectiveGeneration {
			effectiveGeneration = currentGeneration
		}
		if currentAt > effectiveAt {
			effectiveAt = currentAt
		}
		if currentGeneration == effectiveGeneration &&
			compareServerHLC(
				effectivePhysical,
				effectiveCounter,
				effectiveDevice,
				currentPhysical,
				uint64(currentCounter),
				currentDevice) < 0 {
			effectivePhysical = currentPhysical
			effectiveCounter = uint64(currentCounter)
			effectiveDevice = currentDevice
		}
	}
	if effectiveGeneration <= 0 || effectiveAt <= 0 ||
		!IsUUID(effectiveDevice) {
		return fmt.Errorf("materialized Activity reset state is invalid")
	}
	if _, err := tx.Exec(ctx, `
        INSERT INTO account_activity_reset_state(
            account_id, reset_generation, reset_at_ms,
            hlc_physical_ms, hlc_counter, device_id)
        VALUES($1::uuid, $2, $3, $4, $5, $6::uuid)
        ON CONFLICT(account_id) DO UPDATE SET
            reset_generation = EXCLUDED.reset_generation,
            reset_at_ms = EXCLUDED.reset_at_ms,
            hlc_physical_ms = EXCLUDED.hlc_physical_ms,
            hlc_counter = EXCLUDED.hlc_counter,
            device_id = EXCLUDED.device_id
    `,
		accountID,
		effectiveGeneration,
		effectiveAt,
		effectivePhysical,
		int64(effectiveCounter),
		effectiveDevice); err != nil {
		return fmt.Errorf("store Activity reset state: %w", err)
	}
	if _, err := tx.Exec(ctx, `
        UPDATE account_activity_facts
        SET suppressed = true
        WHERE account_id = $1::uuid
    `, accountID); err != nil {
		return fmt.Errorf("suppress pre-reset Activity facts: %w", err)
	}
	if _, err := tx.Exec(ctx, `
        DELETE FROM account_sync_current
        WHERE account_id = $1::uuid
          AND category = 'full_history'
    `, accountID); err != nil {
		return fmt.Errorf("clear History current records at Activity reset: %w", err)
	}
	return nil
}

func nullableString(value *string) string {
	if value == nil {
		return ""
	}
	return *value
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

	if err := validateActivityPayloadObject(object); err != nil {
		return parsedActivityFact{},
			"activity_schema_invalid",
			"The Activity fact payload does not match the shipping Activity schema."
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

func activityInteger(value any) (int64, bool) {
	return syncIntegerNumber(value)
}

func activityTimestamp(value any, offsetMs int64) (int64, bool) {
	parsed, ok := activityInteger(value)
	if !ok {
		return 0, false
	}
	return parsed,
		parsed >= -activityTimeClipLimitMs-offsetMs &&
			parsed <= activityTimeClipLimitMs-offsetMs
}

func activityRequiredString(
	object map[string]any,
	field string,
	allowEmpty bool,
) (string, bool) {
	value, ok := object[field].(string)
	if !ok || (!allowEmpty && value == "") {
		return "", false
	}
	return value, true
}

func activityValidWorldKind(world, kind string) bool {
	switch world {
	case "theatre":
		return kind == "movie" || kind == "episode"
	case "tankoban":
		return kind == "manga_chapter" || kind == "comic_issue" || kind == "tankoban_volume"
	case "biblio":
		return kind == "book" || kind == "audiobook"
	default:
		return false
	}
}

func validateActivityPayloadObject(object map[string]any) error {
	v, ok := activityInteger(object["v"])
	if !ok || v != 1 {
		return fmt.Errorf("activity_schema_invalid")
	}
	eventType, ok := activityRequiredString(object, "type", false)
	if !ok {
		return fmt.Errorf("activity_schema_invalid")
	}
	if _, allowed := activityAllowedEventTypes[eventType]; !allowed {
		return fmt.Errorf("activity_schema_invalid")
	}
	if _, ok := activityRequiredString(object, "eventId", false); !ok {
		return fmt.Errorf("activity_schema_invalid")
	}
	if _, ok := activityRequiredString(object, "sessionId", false); !ok {
		return fmt.Errorf("activity_schema_invalid")
	}
	world, ok := activityRequiredString(object, "world", false)
	if !ok {
		return fmt.Errorf("activity_schema_invalid")
	}
	kind, ok := activityRequiredString(object, "kind", false)
	if !ok || !activityValidWorldKind(world, kind) {
		return fmt.Errorf("activity_schema_invalid")
	}
	for _, field := range []string{"titleKey", "itemKey", "title"} {
		if _, ok := activityRequiredString(object, field, false); !ok {
			return fmt.Errorf("activity_schema_invalid")
		}
	}
	for _, field := range []string{"itemLabel", "cover", "source"} {
		if _, ok := activityRequiredString(object, field, true); !ok {
			return fmt.Errorf("activity_schema_invalid")
		}
	}
	allowedTypeFields, ok := activityTypeFields[eventType]
	if !ok {
		return fmt.Errorf("activity_schema_invalid")
	}
	for field := range object {
		if _, common := activityCommonFields[field]; common {
			continue
		}
		if _, typeSpecific := allowedTypeFields[field]; !typeSpecific {
			return fmt.Errorf("activity_schema_invalid")
		}
	}
	utcOffset, ok := activityInteger(object["utcOffsetMinutes"])
	if !ok || utcOffset < -840 || utcOffset > 840 {
		return fmt.Errorf("activity_schema_invalid")
	}
	offsetMs := utcOffset * 60000
	syncable, ok := object["syncable"].(bool)
	if !ok || !syncable {
		return fmt.Errorf("activity_schema_invalid")
	}

	switch eventType {
	case "playback_delta":
		if kind != "movie" && kind != "episode" && kind != "audiobook" {
			return fmt.Errorf("activity_schema_invalid")
		}
		start, startOK := activityTimestamp(object["startAtMs"], offsetMs)
		end, endOK := activityTimestamp(object["endAtMs"], offsetMs)
		active, activeOK := activityInteger(object["activeMs"])
		rate, rateOK := activityInteger(object["rateMilli"])
		if !startOK || !endOK || !activeOK || !rateOK || end <= start ||
			active <= 0 || active > 30000 || rate <= 0 {
			return fmt.Errorf("activity_schema_invalid")
		}
		difference := new(big.Int).Sub(big.NewInt(end), big.NewInt(start))
		if difference.Cmp(big.NewInt(active)) != 0 {
			return fmt.Errorf("activity_schema_invalid")
		}
	case "reading_delta":
		if kind != "manga_chapter" && kind != "comic_issue" && kind != "tankoban_volume" && kind != "book" {
			return fmt.Errorf("activity_schema_invalid")
		}
		if _, ok := activityTimestamp(object["atMs"], offsetMs); !ok {
			return fmt.Errorf("activity_schema_invalid")
		}
		readingForm, ok := activityRequiredString(object, "readingForm", false)
		if !ok || (readingForm != "fixed" && readingForm != "reflowable") {
			return fmt.Errorf("activity_schema_invalid")
		}
		pageValues, ok := object["pageKeys"].([]any)
		if !ok {
			return fmt.Errorf("activity_schema_invalid")
		}
		seen := make(map[string]struct{}, len(pageValues))
		for _, value := range pageValues {
			page, ok := value.(string)
			if !ok || page == "" {
				return fmt.Errorf("activity_schema_invalid")
			}
			if _, duplicate := seen[page]; duplicate {
				return fmt.Errorf("activity_schema_invalid")
			}
			seen[page] = struct{}{}
		}
		progress, ok := activityInteger(object["progressMicros"])
		if !ok || progress < 0 || (readingForm == "reflowable" && len(pageValues) != 0) ||
			(len(pageValues) == 0 && progress == 0) {
			return fmt.Errorf("activity_schema_invalid")
		}
	case "media_completed":
		if _, ok := activityTimestamp(object["atMs"], offsetMs); !ok {
			return fmt.Errorf("activity_schema_invalid")
		}
		reason, ok := activityRequiredString(object, "reason", false)
		if !ok || (reason != "guarded_90_percent" && reason != "eof" &&
			reason != "full_page_coverage" && reason != "sequential_book_end") {
			return fmt.Errorf("activity_schema_invalid")
		}
	default:
		return fmt.Errorf("activity_schema_invalid")
	}
	return nil
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

	var resetAt, resetPhysical, resetCounter int64
	var resetDevice string
	resetErr := tx.QueryRow(ctx, `
        SELECT reset_at_ms, hlc_physical_ms, hlc_counter, COALESCE(device_id::text, '')
        FROM account_activity_reset_state
        WHERE account_id = $1::uuid
    `, auth.Account.ID).Scan(&resetAt, &resetPhysical, &resetCounter, &resetDevice)
	if resetErr != nil && resetErr != pgx.ErrNoRows {
		return result, fmt.Errorf("load Activity reset barrier: %w", resetErr)
	}
	suppressed := false
	if resetErr == nil {
		if factAt, ok := activityFactAt(fact.Canonical); ok && factAt <= resetAt {
			suppressed = true
		}
		if !suppressed && compareServerHLC(parsed.HLCPhysicalMS, parsed.HLCCounter,
			parsed.DeviceID, resetPhysical, uint64(resetCounter), resetDevice) <= 0 {
			suppressed = true
		}
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
            suppressed,
            received_at
        )
        VALUES(
            $1::uuid, $2::uuid, $3::uuid, $4::uuid,
            $5, $6, $7, $8, $9, $10, $11
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
		suppressed,
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
