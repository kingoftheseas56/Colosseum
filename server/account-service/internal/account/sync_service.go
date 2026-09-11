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

const syncJournalInsertQuery = `
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
            materialized_payload_ciphertext,
            materialized_hlc_physical_ms,
            materialized_hlc_counter,
            materialized_device_id,
            attachment_id,
            won,
            received_at
        )
        VALUES(
            $1::uuid, $2::uuid, $3::uuid, $4, $5, $6,
            $7, $8, $9, $10, NULL, NULL, NULL, NULL, $11::uuid, false, $12
        )
        ON CONFLICT(account_id, mutation_id) DO NOTHING
        RETURNING server_seq
    `

const syncJournalPullQuery = `
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
            materialized_payload_ciphertext,
            COALESCE(materialized_hlc_physical_ms, hlc_physical_ms),
            COALESCE(materialized_hlc_counter, hlc_counter),
            COALESCE(materialized_device_id::text, device_id::text),
            COALESCE(attachment_id::text, ''),
            won,
            received_at
        FROM account_sync_journal
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
            NULL::bytea,
            hlc_physical_ms,
            hlc_counter,
            origin_device_id::text,
            '',
            true,
            received_at
        FROM account_activity_facts
        WHERE account_id = $1::uuid
          AND suppressed = false
          AND server_seq > $2
        ORDER BY server_seq ASC
        LIMIT $3
    `

type parsedSyncMutation struct {
	MutationID       string
	DeviceID         string
	Category         string
	RecordKey        string
	SchemaVersion    int
	HLCPhysicalMS    int64
	HLCCounter       uint64
	Operation        string
	Payload          json.RawMessage
	CanonicalPayload []byte
}

func (s *Service) PushSync(
	ctx context.Context,
	auth AuthenticatedSession,
	inputs []SyncMutationInput,
) (SyncPushResponse, error) {
	return s.pushSyncMutations(ctx, auth, "", inputs)
}

// PushSyncWithAttachment binds every newly accepted mutation to the durable
// attachment receipt. Replays keep their original identity and are checked
// against the manifest before they can be accepted.
func (s *Service) PushSyncWithAttachment(
	ctx context.Context,
	auth AuthenticatedSession,
	attachmentID string,
	inputs []SyncMutationInput,
) (SyncPushResponse, error) {
	normalized, err := normalizeProfileAttachmentID(attachmentID)
	if err != nil {
		return SyncPushResponse{}, err
	}
	if err := s.loadOwnedActiveAttachment(ctx, auth, normalized); err != nil {
		return SyncPushResponse{}, err
	}
	return s.pushSyncMutations(ctx, auth, normalized, inputs)
}

func (s *Service) pushSyncMutations(
	ctx context.Context,
	auth AuthenticatedSession,
	attachmentID string,
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
		if attachmentID != "" {
			if err := s.validateAttachmentMutationForPush(
				ctx, auth, attachmentID, parsed); err != nil {
				response.Results = append(response.Results, SyncPushResult{
					MutationID: parsed.MutationID,
					Accepted:   false,
					Code:       attachmentErrorCode(err),
					Message:    err.Error(),
				})
				continue
			}
		}

		if parsed.Category == "activity_fact" {
			if parsed.RecordKey == "activity/reset" {
				_, resetCode, resetMessage := parseActivityReset(parsed)
				if resetCode != "" {
					response.Results = append(response.Results, SyncPushResult{
						MutationID: parsed.MutationID,
						Accepted:   false,
						Code:       resetCode,
						Message:    resetMessage,
					})
					continue
				}
				result, err := s.pushOneActivityReset(ctx, auth, parsed, attachmentID, now)
				if err != nil {
					return response, err
				}
				response.Results = append(response.Results, result)
				continue
			}
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
				attachmentID,
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
			attachmentID,
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
	attachmentID string,
	now time.Time,
) (SyncPushResult, error) {
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return SyncPushResult{}, fmt.Errorf("begin sync mutation: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	if err := lockAccountSyncTx(ctx, tx, auth.Account.ID); err != nil {
		return SyncPushResult{}, err
	}

	existingRows, err := loadMutationRowsByIDTx(
		ctx, tx, auth.Account.ID, parsed.MutationID)
	if err != nil {
		return SyncPushResult{}, err
	}
	if existingRows.JournalFound || existingRows.ActivityFound || existingRows.AliasFound {
		if !existingRows.JournalFound ||
			existingRows.ActivityFound ||
			existingRows.AliasFound {
			return syncMutationConflictResult(parsed.MutationID), nil
		}

		exact, err := s.syncStoredMutationMatches(
			ctx,
			auth.Account.ID,
			parsed,
			parsed.CanonicalPayload,
			existingRows.Journal)
		if err != nil {
			return SyncPushResult{}, err
		}
		if !exact {
			return syncMutationConflictResult(parsed.MutationID), nil
		}

		if err := tx.Commit(ctx); err != nil {
			return SyncPushResult{}, fmt.Errorf("commit sync idempotency lookup: %w", err)
		}
		return SyncPushResult{
			MutationID: parsed.MutationID,
			Accepted:   true,
			ServerSeq:  existingRows.Journal.ServerSeq,
			Won:        existingRows.Journal.Won,
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
	var attachmentParam any
	if attachmentID != "" {
		attachmentParam = attachmentID
	}

	var serverSeq int64
	err = tx.QueryRow(ctx, syncJournalInsertQuery,
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
		attachmentParam,
		now).Scan(&serverSeq)

	if err == pgx.ErrNoRows {
		existing, found, loadErr := loadJournalByMutationIDTx(
			ctx, tx, auth.Account.ID, parsed.MutationID)
		if loadErr != nil {
			return SyncPushResult{}, loadErr
		}
		if !found {
			return SyncPushResult{}, fmt.Errorf(
				"sync mutation conflict completed without an idempotency row")
		}
		exact, matchErr := s.syncStoredMutationMatches(
			ctx,
			auth.Account.ID,
			parsed,
			parsed.CanonicalPayload,
			existing)
		if matchErr != nil {
			return SyncPushResult{}, matchErr
		}
		if !exact {
			return syncMutationConflictResult(parsed.MutationID), nil
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
	if err := markAttachmentUploadedTx(ctx, tx, auth.Account.ID, attachmentID, now); err != nil {
		return SyncPushResult{}, err
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

	mergeCurrent := syncMergeCurrent{}
	if found {
		mergeCurrent = syncMergeCurrent{
			MutationID:    current.MutationID,
			DeviceID:      current.DeviceID,
			SchemaVersion: current.SchemaVersion,
			HLCPhysicalMS: current.HLCPhysicalMS,
			HLCCounter:    current.HLCCounter,
			Operation:     current.Operation,
		}
		if current.Operation == "put" {
			plain, openErr := s.syncCipher.Open(
				auth.Account.ID,
				current.Category,
				current.RecordKey,
				current.PayloadCipher)
			if openErr != nil {
				return SyncPushResult{}, fmt.Errorf("decrypt current sync payload: %w", openErr)
			}
			mergeCurrent.Payload = json.RawMessage(plain)
		}
	}
	blockedByHistoryReset, err := historyMutationBlockedByResetTx(
		ctx,
		tx,
		auth.Account.ID,
		parsed)
	if err != nil {
		return SyncPushResult{}, err
	}
	var resolution syncResolution
	if blockedByHistoryReset {
		if found {
			resolution = syncResolutionFromCurrent(
				mergeCurrent,
				cloneSyncMergePayload(mergeCurrent.Payload),
				false)
		} else {
			resolution = syncResolution{
				Changed:             false,
				Operation:           parsed.Operation,
				Payload:             cloneSyncMergePayload(parsed.CanonicalPayload),
				WinnerMutationID:    parsed.MutationID,
				WinnerDeviceID:      parsed.DeviceID,
				WinnerSchemaVersion: parsed.SchemaVersion,
				WinnerHLCPhysicalMS: parsed.HLCPhysicalMS,
				WinnerHLCCounter:    parsed.HLCCounter,
			}
		}
	} else {
		resolution, err = resolveMutableSync(mergeCurrent, found, parsed)
	}
	if err != nil {
		return SyncPushResult{}, fmt.Errorf("resolve sync mutation: %w", err)
	}
	won := resolution.Changed
	materializedCipher := []byte(nil)
	if resolution.Operation == "put" {
		if parsed.Category == "full_history" ||
			(parsed.Category == "activity_fact" && parsed.RecordKey == "activity/reset") {
			materializedCipher, err = s.syncCipher.Seal(
				auth.Account.ID,
				parsed.Category,
				parsed.RecordKey,
				resolution.Payload)
			if err != nil {
				return SyncPushResult{}, fmt.Errorf("encrypt materialized History payload: %w", err)
			}
		} else {
			materializedCipher = ciphertext
		}
	}

	if won {
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
				return SyncPushResult{}, fmt.Errorf("archive sync winner: %w", err)
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
			materializedCipher,
			serverSeq,
			now); err != nil {
			return SyncPushResult{}, fmt.Errorf("store sync winner: %w", err)
		}

		if parsed.Category == "full_history" &&
			parsed.RecordKey == "history/reset" {
			if err := s.storeHistoryResetStateTx(
				ctx,
				tx,
				auth.Account.ID,
				resolution); err != nil {
				return SyncPushResult{}, err
			}
			if _, err := tx.Exec(ctx, `
                DELETE FROM account_sync_current
                WHERE account_id = $1::uuid
                  AND category = 'full_history'
                  AND record_key <> 'history/reset'
            `, auth.Account.ID); err != nil {
				return SyncPushResult{}, fmt.Errorf("clear History current records at reset: %w", err)
			}
		}
		if parsed.Category == "activity_fact" &&
			parsed.RecordKey == "activity/reset" {
			if err := s.storeActivityResetStateTx(
				ctx,
				tx,
				auth.Account.ID,
				resolution); err != nil {
				return SyncPushResult{}, err
			}
		}
	}

	if _, err := tx.Exec(ctx, `
        UPDATE account_sync_journal
        SET materialized_payload_ciphertext = $2,
            materialized_hlc_physical_ms = $3,
            materialized_hlc_counter = $4,
            materialized_device_id = $5::uuid
        WHERE server_seq = $1
	`, serverSeq, materializedCipher,
		resolution.WinnerHLCPhysicalMS,
		int64(resolution.WinnerHLCCounter),
		resolution.WinnerDeviceID); err != nil {
		return SyncPushResult{}, fmt.Errorf("store materialized sync payload: %w", err)
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

func historyMutationBlockedByResetTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID string,
	parsed parsedSyncMutation,
) (bool, error) {
	if parsed.Category != "full_history" ||
		parsed.RecordKey == "history/reset" ||
		parsed.Operation != "put" {
		return false, nil
	}

	history, err := decodeSyncHistoryPayload(parsed.Payload)
	if err != nil {
		return false, fmt.Errorf("decode History reset candidate: %w", err)
	}

	loadBarrier := func(table string) (int64, int64, uint64, string, bool, error) {
		row := tx.QueryRow(ctx, `
            SELECT reset_at_ms, hlc_physical_ms, hlc_counter,
                   COALESCE(device_id::text, '')
            FROM `+table+`
            WHERE account_id = $1::uuid
        `, accountID)
		var resetAt, physical, counter int64
		var device string
		if err := row.Scan(&resetAt, &physical, &counter, &device); err != nil {
			if err == pgx.ErrNoRows {
				return 0, 0, 0, "", false, nil
			}
			return 0, 0, 0, "", false, err
		}
		if resetAt <= 0 || physical < 0 || counter < 0 || !IsUUID(device) {
			return 0, 0, 0, "", false, fmt.Errorf("invalid %s reset barrier", table)
		}
		return resetAt, physical, uint64(counter), device, true, nil
	}

	historyResetAt, historyPhysical, historyCounter, historyDevice, historyFound, err :=
		loadBarrier("account_history_reset_state")
	if err != nil {
		return false, fmt.Errorf("load History reset barrier: %w", err)
	}
	activityResetAt, activityPhysical, activityCounter, activityDevice, activityFound, err :=
		loadBarrier("account_activity_reset_state")
	if err != nil {
		return false, fmt.Errorf("load Activity reset barrier for History: %w", err)
	}
	if !historyFound && !activityFound {
		return false, nil
	}
	if historyFound && (history.lastActivityAt <= historyResetAt ||
		compareServerHLC(parsed.HLCPhysicalMS, parsed.HLCCounter, parsed.DeviceID,
			historyPhysical, historyCounter, historyDevice) <= 0) {
		return true, nil
	}
	if activityFound && (history.lastActivityAt <= activityResetAt ||
		compareServerHLC(parsed.HLCPhysicalMS, parsed.HLCCounter, parsed.DeviceID,
			activityPhysical, activityCounter, activityDevice) <= 0) {
		return true, nil
	}
	return false, nil
}

func (s *Service) storeHistoryResetStateTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID string,
	resolution syncResolution,
) error {
	reset, err := decodeActivityResetPayload(resolution.Payload)
	if err != nil {
		return fmt.Errorf("decode materialized History reset: %w", err)
	}
	var currentGeneration, currentAt int64
	var currentPhysical, currentCounter int64
	var currentDevice string
	err = tx.QueryRow(ctx, `
        SELECT reset_generation, reset_at_ms, hlc_physical_ms,
               hlc_counter, COALESCE(device_id::text, '')
        FROM account_history_reset_state
        WHERE account_id = $1::uuid
        FOR UPDATE
    `, accountID).Scan(
		&currentGeneration,
		&currentAt,
		&currentPhysical,
		&currentCounter,
		&currentDevice)
	if err != nil && err != pgx.ErrNoRows {
		return fmt.Errorf("load History reset state: %w", err)
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
		return fmt.Errorf("materialized History reset state is invalid")
	}
	_, err = tx.Exec(ctx, `
        INSERT INTO account_history_reset_state(
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
		effectiveDevice)
	if err != nil {
		return fmt.Errorf("store History reset state: %w", err)
	}
	return nil
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

	rows, err := s.pool.Query(
		ctx,
		syncJournalPullQuery,
		auth.Account.ID,
		int64(after),
		syncPullPageSize+1)
	if err != nil {
		return response, fmt.Errorf("query sync journal: %w", err)
	}
	defer rows.Close()

	for rows.Next() {
		var stored syncStoredMutation
		var serverSeq int64
		var counter int64
		var materializedCounter int64
		var attachmentID string
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
			&stored.MaterializedPayloadCipher,
			&stored.MaterializedHLCPhysicalMS,
			&materializedCounter,
			&stored.MaterializedDeviceID,
			&attachmentID,
			&stored.Won,
			&stored.ReceivedAt); err != nil {
			return response, fmt.Errorf("scan sync journal: %w", err)
		}
		if serverSeq <= 0 || counter < 0 || materializedCounter < 0 ||
			stored.MaterializedHLCPhysicalMS < 0 ||
			!IsUUID(stored.MaterializedDeviceID) {
			return response, fmt.Errorf("sync journal contains invalid numeric state")
		}
		if compareServerHLC(
			stored.MaterializedHLCPhysicalMS,
			uint64(materializedCounter),
			stored.MaterializedDeviceID,
			stored.HLCPhysicalMS,
			uint64(counter),
			stored.DeviceID) < 0 {
			return response, fmt.Errorf("sync journal materialized HLC precedes request HLC")
		}
		stored.ServerSeq = uint64(serverSeq)
		stored.HLCCounter = uint64(counter)
		stored.MaterializedHLCCounter = uint64(materializedCounter)
		stored.MaterializedHLCValid = true
		stored.AttachmentID = attachmentID

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
			Won:       stored.Won,
			Mutation:  view,
		})
	}
	if err := rows.Err(); err != nil {
		return response, fmt.Errorf("iterate sync journal: %w", err)
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
		if input.Category != "activity_fact" {
			if err := validateSyncRecordShape(
				input.Category,
				input.SchemaVersion,
				input.RecordKey,
				operation,
				input.Payload); err != nil {
				return parsedSyncMutation{}, err.Error(), "The sync record key or payload does not match the shipping category schema."
			}
		}
		canonicalPayload, err := canonicalSyncJSON(input.Payload)
		if err != nil {
			return parsedSyncMutation{}, "payload_invalid", "The sync payload contains invalid JSON."
		}
		return parsedSyncMutation{
			MutationID:       mutationID,
			DeviceID:         deviceID,
			Category:         input.Category,
			RecordKey:        input.RecordKey,
			SchemaVersion:    input.SchemaVersion,
			HLCPhysicalMS:    physical,
			HLCCounter:       counter,
			Operation:        operation,
			Payload:          input.Payload,
			CanonicalPayload: canonicalPayload,
		}, "", ""
	case "delete":
		if len(input.Payload) > 0 && string(input.Payload) != "null" {
			return parsedSyncMutation{}, "delete_payload_not_empty", "A delete mutation cannot contain a payload."
		}
		if err := validateSyncRecordShape(
			input.Category,
			input.SchemaVersion,
			input.RecordKey,
			operation,
			input.Payload); err != nil {
			return parsedSyncMutation{}, err.Error(), "The sync record key does not match the shipping category schema."
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
		ciphertext := stored.PayloadCipher
		if len(stored.MaterializedPayloadCipher) > 0 {
			ciphertext = stored.MaterializedPayloadCipher
		}
		plain, err := s.syncCipher.Open(
			accountID,
			stored.Category,
			stored.RecordKey,
			ciphertext)
		if err != nil {
			return SyncMutationView{}, fmt.Errorf("decrypt sync payload: %w", err)
		}
		if !json.Valid(plain) {
			return SyncMutationView{}, fmt.Errorf("decrypted sync payload is malformed")
		}
		payload = json.RawMessage(plain)
	}

	view := SyncMutationView{
		MutationID:    stored.MutationID,
		DeviceID:      stored.DeviceID,
		Category:      stored.Category,
		RecordKey:     stored.RecordKey,
		SchemaVersion: stored.SchemaVersion,
		HLCPhysicalMS: strconv.FormatInt(stored.HLCPhysicalMS, 10),
		HLCCounter:    strconv.FormatUint(stored.HLCCounter, 10),
		Operation:     stored.Operation,
		Payload:       payload,
	}
	if stored.MaterializedHLCValid {
		view.MaterializedHLCPhysicalMS = strconv.FormatInt(
			stored.MaterializedHLCPhysicalMS,
			10)
		view.MaterializedHLCCounter = strconv.FormatUint(
			stored.MaterializedHLCCounter,
			10)
		view.MaterializedDeviceID = stored.MaterializedDeviceID
	}
	return view, nil
}
