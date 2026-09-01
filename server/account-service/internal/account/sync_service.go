package account

import (
	"context"
	"encoding/json"
	"fmt"
	"math"
	"strconv"
	"strings"
	"time"

	"github.com/jackc/pgx/v5"
)

const syncPullPageSize = 200

type parsedSyncMutation struct {
	MutationID    string
	DeviceID      string
	Category      string
	RecordKey     string
	SchemaVersion int
	HLCPhysicalMS int64
	HLCCounter    uint64
	Operation     string
	Payload       json.RawMessage
}

func (s *Service) PushSync(
	ctx context.Context,
	auth AuthenticatedSession,
	inputs []SyncMutationInput,
) (SyncPushResponse, error) {
	now := s.clock.Now().UTC()
	response := SyncPushResponse{
		ServerTimeMS: now.UnixMilli(),
		Results:      make([]SyncPushResult, 0, len(inputs)),
	}

	if len(inputs) == 0 || len(inputs) > 100 {
		return response, fmt.Errorf("sync push batch must contain 1..100 mutations")
	}

	for _, input := range inputs {
		parsed, code, message := s.validateSyncMutation(auth, input, now)
		if code != "" {
			result := SyncPushResult{
				MutationID: strings.ToLower(strings.TrimSpace(input.MutationID)),
				Accepted:   false,
				Code:       code,
				Message:    message,
			}

			if code == "clock_skew" {
				current, found, loadErr := s.loadCurrent(
					ctx,
					auth.Account.ID,
					input.Category,
					input.RecordKey)
				if loadErr != nil {
					return response, loadErr
				}
				if found {
					result.Current = syncCurrentMetadata(current)
				}
			}

			response.Results = append(response.Results, result)
			continue
		}

		if parsed.Category == "activity_fact" {
			fact, activityCode, activityMessage := parseActivityFact(parsed)
			if activityCode != "" {
				response.Results = append(response.Results, SyncPushResult{
					MutationID: parsed.MutationID,
					Accepted:   false,
					Code:       activityCode,
					Message:    activityMessage,
				})
				continue
			}
			result, err := s.pushOneActivityFact(
				ctx,
				auth,
				parsed,
				fact,
				now)
			if err != nil {
				return response, err
			}
			response.Results = append(response.Results, result)
			continue
		}

		result, err := s.pushOneSyncMutation(
			ctx,
			auth,
			parsed,
			now)
		if err != nil {
			return response, err
		}
		response.Results = append(response.Results, result)
	}

	return response, nil
}

func (s *Service) pushOneSyncMutation(
	ctx context.Context,
	auth AuthenticatedSession,
	parsed parsedSyncMutation,
	now time.Time,
) (SyncPushResult, error) {
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return SyncPushResult{}, fmt.Errorf("begin sync mutation: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	existing, found, err := loadJournalByMutationIDTx(
		ctx,
		tx,
		auth.Account.ID,
		parsed.MutationID)
	if err != nil {
		return SyncPushResult{}, err
	}
	if found {
		return SyncPushResult{
			MutationID: parsed.MutationID,
			Accepted:   true,
			ServerSeq:  existing.ServerSeq,
			Won:        existing.Won,
		}, nil
	}

	var ciphertext []byte
	if parsed.Operation == "put" {
		ciphertext, err = s.syncCipher.Seal(
			auth.Account.ID,
			parsed.Category,
			parsed.RecordKey,
			parsed.Payload)
		if err != nil {
			return SyncPushResult{}, fmt.Errorf("encrypt sync payload: %w", err)
		}
	}

	var serverSeq int64
	err = tx.QueryRow(ctx, `
        INSERT INTO account_sync_journal(
            account_id,
            mutation_id,
            device_id,
            category,
            record_key,
            schema_version,
            hlc_physical_ms,
            hlc_counter,
            operation,
            payload_ciphertext,
            won,
            received_at
        )
        VALUES(
            $1::uuid, $2::uuid, $3::uuid, $4, $5, $6,
            $7, $8, $9, $10, false, $11
        )
        ON CONFLICT(account_id, mutation_id) DO NOTHING
        RETURNING server_seq
    `,
		auth.Account.ID,
		parsed.MutationID,
		parsed.DeviceID,
		parsed.Category,
		parsed.RecordKey,
		parsed.SchemaVersion,
		parsed.HLCPhysicalMS,
		int64(parsed.HLCCounter),
		parsed.Operation,
		ciphertext,
		now).Scan(&serverSeq)

	if err == pgx.ErrNoRows {
		existing, found, loadErr := loadJournalByMutationIDTx(
			ctx,
			tx,
			auth.Account.ID,
			parsed.MutationID)
		if loadErr != nil {
			return SyncPushResult{}, loadErr
		}
		if !found {
			return SyncPushResult{}, fmt.Errorf(
				"sync mutation conflict completed without an idempotency row")
		}
		if err := tx.Commit(ctx); err != nil {
			return SyncPushResult{}, fmt.Errorf("commit sync idempotency lookup: %w", err)
		}
		return SyncPushResult{
			MutationID: parsed.MutationID,
			Accepted:   true,
			ServerSeq:  existing.ServerSeq,
			Won:        existing.Won,
		}, nil
	}
	if err != nil {
		return SyncPushResult{}, fmt.Errorf("insert sync journal: %w", err)
	}

	recordLockKey :=
		auth.Account.ID + "\x1f" +
			parsed.Category + "\x1f" +
			parsed.RecordKey
	if _, err := tx.Exec(ctx, `
        SELECT pg_advisory_xact_lock(hashtextextended($1, 0))
    `, recordLockKey); err != nil {
		return SyncPushResult{}, fmt.Errorf("lock sync record: %w", err)
	}

	current, found, err := loadCurrentForUpdateTx(
		ctx,
		tx,
		auth.Account.ID,
		parsed.Category,
		parsed.RecordKey)
	if err != nil {
		return SyncPushResult{}, err
	}

	won := !found || compareServerHLC(
		parsed.HLCPhysicalMS,
		parsed.HLCCounter,
		parsed.DeviceID,
		current.HLCPhysicalMS,
		current.HLCCounter,
		current.DeviceID) > 0

	var currentPlain json.RawMessage
	if found && current.Operation == "put" {
		plain, openErr := s.syncCipher.Open(
			auth.Account.ID,
			current.Category,
			current.RecordKey,
			current.PayloadCipher)
		if openErr != nil {
			return SyncPushResult{}, fmt.Errorf("decrypt current sync payload: %w", openErr)
		}
		if !json.Valid(plain) {
			return SyncPushResult{}, fmt.Errorf("decrypted current sync payload is malformed")
		}
		currentPlain = json.RawMessage(plain)
	}

	currentMerge := syncMergeCurrent{}
	if found {
		currentMerge = syncMergeCurrent{
			MutationID:    current.MutationID,
			DeviceID:      current.DeviceID,
			SchemaVersion: current.SchemaVersion,
			HLCPhysicalMS: current.HLCPhysicalMS,
			HLCCounter:    current.HLCCounter,
			Operation:     current.Operation,
			Payload:       currentPlain,
		}
	}
	resolution, resolveErr := resolveMutableSync(currentMerge, found, parsed)
	if resolveErr != nil {
		return SyncPushResult{}, fmt.Errorf("resolve mutable sync state: %w", resolveErr)
	}

	if resolution.Changed {
		if found {
			if _, err := tx.Exec(ctx, `
                INSERT INTO account_sync_versions(
                    account_id, category, record_key,
                    mutation_id, device_id, schema_version,
                    hlc_physical_ms, hlc_counter,
                    operation, payload_ciphertext, server_seq,
                    replaced_at, replacing_mutation_id
                )
                VALUES(
                    $1::uuid, $2, $3,
                    $4::uuid, $5::uuid, $6,
                    $7, $8, $9, $10, $11,
                    $12, $13::uuid
                )
            `,
				auth.Account.ID,
				current.Category,
				current.RecordKey,
				current.MutationID,
				current.DeviceID,
				current.SchemaVersion,
				current.HLCPhysicalMS,
				int64(current.HLCCounter),
				current.Operation,
				current.PayloadCipher,
				int64(current.ServerSeq),
				now,
				parsed.MutationID); err != nil {
				return SyncPushResult{}, fmt.Errorf("archive sync current state: %w", err)
			}
		}

		var resolvedCiphertext []byte
		if resolution.Operation == "put" {
			resolvedCiphertext, err = s.syncCipher.Seal(
				auth.Account.ID,
				parsed.Category,
				parsed.RecordKey,
				resolution.Payload)
			if err != nil {
				return SyncPushResult{}, fmt.Errorf("encrypt resolved sync payload: %w", err)
			}
		}

		if _, err := tx.Exec(ctx, `
            INSERT INTO account_sync_current(
                account_id, category, record_key,
                mutation_id, device_id, schema_version,
                hlc_physical_ms, hlc_counter,
                operation, payload_ciphertext, server_seq, updated_at
            )
            VALUES(
                $1::uuid, $2, $3,
                $4::uuid, $5::uuid, $6,
                $7, $8, $9, $10, $11, $12
            )
            ON CONFLICT(account_id, category, record_key)
            DO UPDATE SET
                mutation_id = EXCLUDED.mutation_id,
                device_id = EXCLUDED.device_id,
                schema_version = EXCLUDED.schema_version,
                hlc_physical_ms = EXCLUDED.hlc_physical_ms,
                hlc_counter = EXCLUDED.hlc_counter,
                operation = EXCLUDED.operation,
                payload_ciphertext = EXCLUDED.payload_ciphertext,
                server_seq = EXCLUDED.server_seq,
                updated_at = EXCLUDED.updated_at
        `,
			auth.Account.ID,
			parsed.Category,
			parsed.RecordKey,
			resolution.WinnerMutationID,
			resolution.WinnerDeviceID,
			resolution.WinnerSchemaVersion,
			resolution.WinnerHLCPhysicalMS,
			int64(resolution.WinnerHLCCounter),
			resolution.Operation,
			resolvedCiphertext,
			serverSeq,
			now); err != nil {
			return SyncPushResult{}, fmt.Errorf("store canonical sync state: %w", err)
		}
	}

	if _, err := tx.Exec(ctx, `
        UPDATE account_sync_journal
        SET won = $2
        WHERE server_seq = $1
    `, serverSeq, won); err != nil {
		return SyncPushResult{}, fmt.Errorf("mark sync journal winner: %w", err)
	}

	if err := tx.Commit(ctx); err != nil {
		return SyncPushResult{}, fmt.Errorf("commit sync mutation: %w", err)
	}

	return SyncPushResult{
		MutationID: parsed.MutationID,
		Accepted:   true,
		ServerSeq:  uint64(serverSeq),
		Won:        won,
	}, nil
}

func (s *Service) PullSync(
	ctx context.Context,
	auth AuthenticatedSession,
	after uint64,
) (SyncPullResponse, error) {
	now := s.clock.Now().UTC()
	response := SyncPullResponse{
		ServerTimeMS: now.UnixMilli(),
		Entries:      make([]SyncPullEntry, 0, syncPullPageSize),
	}

	if after > math.MaxInt64 {
		return response, fmt.Errorf("sync cursor is too large")
	}

	// Unified global server_seq stream: committed mutable canonical rows from
	// account_sync_current plus immutable Activity facts from
	// account_activity_facts, both drawing server_seq from the shared
	// account_change_seq. Activity rows materialize as canonical PUT records
	// keyed activity/<lowercase-eventId> with their stored metadata; the
	// decrypted canonical payload was sealed under exactly that
	// (account, category, recordKey) AAD, so decodeStoredMutation serves both
	// row kinds unchanged. Pulls never allocate sequence values; gaps from
	// superseded losers or semantic duplicates are legal and pagination
	// continues strictly by server_seq.
	rows, err := s.pool.Query(ctx, `
        SELECT
            server_seq,
            mutation_id::text,
            device_id::text,
            category,
            record_key,
            schema_version,
            hlc_physical_ms,
            hlc_counter,
            operation,
            payload_ciphertext,
            updated_at
        FROM account_sync_current
        WHERE account_id = $1::uuid
          AND server_seq > $2
        UNION ALL
        SELECT
            server_seq,
            mutation_id::text,
            origin_device_id::text,
            'activity_fact',
            'activity/' || event_id::text,
            schema_version,
            hlc_physical_ms,
            hlc_counter,
            'put',
            payload_ciphertext,
            received_at
        FROM account_activity_facts
        WHERE account_id = $1::uuid
          AND server_seq > $2
        ORDER BY server_seq ASC
        LIMIT $3
    `, auth.Account.ID, int64(after), syncPullPageSize+1)
	if err != nil {
		return response, fmt.Errorf("query unified sync state: %w", err)
	}
	defer rows.Close()

	for rows.Next() {
		var stored syncStoredMutation
		var serverSeq int64
		var counter int64
		if err := rows.Scan(
			&serverSeq,
			&stored.MutationID,
			&stored.DeviceID,
			&stored.Category,
			&stored.RecordKey,
			&stored.SchemaVersion,
			&stored.HLCPhysicalMS,
			&counter,
			&stored.Operation,
			&stored.PayloadCipher,
			&stored.ReceivedAt); err != nil {
			return response, fmt.Errorf("scan canonical sync state: %w", err)
		}
		if serverSeq <= 0 || counter < 0 {
			return response, fmt.Errorf("canonical sync state contains invalid numeric state")
		}
		stored.ServerSeq = uint64(serverSeq)
		stored.HLCCounter = uint64(counter)

		if len(response.Entries) == syncPullPageSize {
			response.HasMore = true
			break
		}

		view, err := s.decodeStoredMutation(
			auth.Account.ID,
			stored)
		if err != nil {
			return response, err
		}
		response.Entries = append(response.Entries, SyncPullEntry{
			ServerSeq: stored.ServerSeq,
			Won:       true,
			Canonical: true,
			Mutation:  view,
		})
	}
	if err := rows.Err(); err != nil {
		return response, fmt.Errorf("iterate canonical sync state: %w", err)
	}

	return response, nil
}

func (s *Service) PruneSyncVersions(
	ctx context.Context,
	before time.Time,
) error {
	_, err := s.pool.Exec(ctx, `
        DELETE FROM account_sync_versions
        WHERE replaced_at < $1
    `, before.UTC())
	if err != nil {
		return fmt.Errorf("prune sync versions: %w", err)
	}
	return nil
}

func (s *Service) validateSyncMutation(
	auth AuthenticatedSession,
	input SyncMutationInput,
	now time.Time,
) (parsedSyncMutation, string, string) {
	mutationID := strings.ToLower(strings.TrimSpace(input.MutationID))
	deviceID := strings.ToLower(strings.TrimSpace(input.DeviceID))
	if !IsUUID(mutationID) {
		return parsedSyncMutation{}, "invalid_mutation_id", "The mutation id is invalid."
	}
	if !IsUUID(deviceID) || deviceID != strings.ToLower(auth.Device.ID) {
		return parsedSyncMutation{}, "device_mismatch", "The mutation device does not match the authenticated session."
	}
	if err := validateSyncCategory(input.Category, input.SchemaVersion); err != nil {
		return parsedSyncMutation{}, err.Error(), "The sync category or schema is not accepted."
	}
	if err := validateSyncRecordKey(input.RecordKey); err != nil {
		return parsedSyncMutation{}, err.Error(), "The logical sync record key is invalid."
	}

	physical, err := strconv.ParseInt(input.HLCPhysicalMS, 10, 64)
	if err != nil || physical < 0 {
		return parsedSyncMutation{}, "invalid_hlc", "The mutation clock is invalid."
	}
	counter, err := strconv.ParseUint(input.HLCCounter, 10, 63)
	if err != nil {
		return parsedSyncMutation{}, "invalid_hlc", "The mutation clock is invalid."
	}

	if physical > now.Add(s.syncMaxFutureSkew).UnixMilli() {
		return parsedSyncMutation{}, "clock_skew", "The mutation clock is too far ahead of service time."
	}

	operation := strings.ToLower(strings.TrimSpace(input.Operation))
	switch operation {
	case "put":
		if err := validateSyncPayload(input.Payload); err != nil {
			return parsedSyncMutation{}, err.Error(), "The sync payload contains data that cannot be synced."
		}
	case "delete":
		if len(input.Payload) > 0 && string(input.Payload) != "null" {
			return parsedSyncMutation{}, "delete_payload_not_empty", "A delete mutation cannot contain a payload."
		}
	default:
		return parsedSyncMutation{}, "invalid_operation", "The sync mutation operation is invalid."
	}

	return parsedSyncMutation{
		MutationID:    mutationID,
		DeviceID:      deviceID,
		Category:      input.Category,
		RecordKey:     input.RecordKey,
		SchemaVersion: input.SchemaVersion,
		HLCPhysicalMS: physical,
		HLCCounter:    counter,
		Operation:     operation,
		Payload:       input.Payload,
	}, "", ""
}

func compareServerHLC(
	leftPhysical int64,
	leftCounter uint64,
	leftDevice string,
	rightPhysical int64,
	rightCounter uint64,
	rightDevice string,
) int {
	if leftPhysical < rightPhysical {
		return -1
	}
	if leftPhysical > rightPhysical {
		return 1
	}
	if leftCounter < rightCounter {
		return -1
	}
	if leftCounter > rightCounter {
		return 1
	}
	return strings.Compare(leftDevice, rightDevice)
}

func loadJournalByMutationIDTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID,
	mutationID string,
) (syncStoredMutation, bool, error) {
	row := tx.QueryRow(ctx, `
        SELECT server_seq, won
        FROM account_sync_journal
        WHERE account_id = $1::uuid
          AND mutation_id = $2::uuid
    `, accountID, mutationID)

	var serverSeq int64
	var won bool
	if err := row.Scan(&serverSeq, &won); err != nil {
		if err == pgx.ErrNoRows {
			return syncStoredMutation{}, false, nil
		}
		return syncStoredMutation{}, false, fmt.Errorf("load sync idempotency row: %w", err)
	}
	return syncStoredMutation{
		ServerSeq: uint64(serverSeq),
		Won:       won,
	}, true, nil
}

func (s *Service) loadCurrent(
	ctx context.Context,
	accountID,
	category,
	recordKey string,
) (syncStoredMutation, bool, error) {
	row := s.pool.QueryRow(ctx, `
        SELECT
            server_seq,
            mutation_id::text,
            device_id::text,
            category,
            record_key,
            schema_version,
            hlc_physical_ms,
            hlc_counter,
            operation,
            payload_ciphertext,
            updated_at
        FROM account_sync_current
        WHERE account_id = $1::uuid
          AND category = $2
          AND record_key = $3
    `, accountID, category, recordKey)

	var stored syncStoredMutation
	var serverSeq int64
	var counter int64
	if err := row.Scan(
		&serverSeq,
		&stored.MutationID,
		&stored.DeviceID,
		&stored.Category,
		&stored.RecordKey,
		&stored.SchemaVersion,
		&stored.HLCPhysicalMS,
		&counter,
		&stored.Operation,
		&stored.PayloadCipher,
		&stored.ReceivedAt); err != nil {
		if err == pgx.ErrNoRows {
			return syncStoredMutation{}, false, nil
		}
		return syncStoredMutation{}, false, fmt.Errorf("load current sync metadata: %w", err)
	}
	if serverSeq <= 0 || counter < 0 {
		return syncStoredMutation{}, false, fmt.Errorf("current sync record has invalid numeric state")
	}
	stored.ServerSeq = uint64(serverSeq)
	stored.HLCCounter = uint64(counter)
	return stored, true, nil
}

func syncCurrentMetadata(
	current syncStoredMutation,
) *SyncCurrentMetadata {
	return &SyncCurrentMetadata{
		MutationID:    current.MutationID,
		DeviceID:      current.DeviceID,
		SchemaVersion: current.SchemaVersion,
		HLCPhysicalMS: strconv.FormatInt(current.HLCPhysicalMS, 10),
		HLCCounter:    strconv.FormatUint(current.HLCCounter, 10),
		Operation:     current.Operation,
		ServerSeq:     current.ServerSeq,
	}
}

func loadCurrentForUpdateTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID,
	category,
	recordKey string,
) (syncStoredMutation, bool, error) {
	row := tx.QueryRow(ctx, `
        SELECT
            server_seq,
            mutation_id::text,
            device_id::text,
            category,
            record_key,
            schema_version,
            hlc_physical_ms,
            hlc_counter,
            operation,
            payload_ciphertext,
            updated_at
        FROM account_sync_current
        WHERE account_id = $1::uuid
          AND category = $2
          AND record_key = $3
        FOR UPDATE
    `, accountID, category, recordKey)

	var stored syncStoredMutation
	var serverSeq int64
	var counter int64
	if err := row.Scan(
		&serverSeq,
		&stored.MutationID,
		&stored.DeviceID,
		&stored.Category,
		&stored.RecordKey,
		&stored.SchemaVersion,
		&stored.HLCPhysicalMS,
		&counter,
		&stored.Operation,
		&stored.PayloadCipher,
		&stored.ReceivedAt); err != nil {
		if err == pgx.ErrNoRows {
			return syncStoredMutation{}, false, nil
		}
		return syncStoredMutation{}, false, fmt.Errorf("load current sync record: %w", err)
	}
	if serverSeq <= 0 || counter < 0 {
		return syncStoredMutation{}, false, fmt.Errorf("current sync record has invalid numeric state")
	}
	stored.ServerSeq = uint64(serverSeq)
	stored.HLCCounter = uint64(counter)
	return stored, true, nil
}

func (s *Service) decodeStoredMutation(
	accountID string,
	stored syncStoredMutation,
) (SyncMutationView, error) {
	var payload json.RawMessage
	if stored.Operation == "put" {
		plain, err := s.syncCipher.Open(
			accountID,
			stored.Category,
			stored.RecordKey,
			stored.PayloadCipher)
		if err != nil {
			return SyncMutationView{}, fmt.Errorf("decrypt sync payload: %w", err)
		}
		if !json.Valid(plain) {
			return SyncMutationView{}, fmt.Errorf("decrypted sync payload is malformed")
		}
		payload = json.RawMessage(plain)
	}

	return SyncMutationView{
		MutationID:    stored.MutationID,
		DeviceID:      stored.DeviceID,
		Category:      stored.Category,
		RecordKey:     stored.RecordKey,
		SchemaVersion: stored.SchemaVersion,
		HLCPhysicalMS: strconv.FormatInt(stored.HLCPhysicalMS, 10),
		HLCCounter:    strconv.FormatUint(stored.HLCCounter, 10),
		Operation:     stored.Operation,
		Payload:       payload,
	}, nil
}
