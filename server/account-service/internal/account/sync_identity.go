package account

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/json"
	"fmt"
	"io"
	"strings"

	"github.com/jackc/pgx/v5"
)

const syncAccountLockPrefix = "colosseum-sync-account\x1f"

type syncMutationAlias struct {
	syncStoredMutation
	PayloadHash     []byte
	ActivityEventID string
}

type syncMutationRows struct {
	Journal       syncStoredMutation
	JournalFound  bool
	Activity      syncStoredMutation
	ActivityFound bool
	Alias         syncMutationAlias
	AliasFound    bool
}

func lockAccountSyncTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID string,
) error {
	lockKey := syncAccountLockPrefix + strings.ToLower(strings.TrimSpace(accountID))
	if _, err := tx.Exec(ctx, `
        SELECT pg_advisory_xact_lock(hashtextextended($1, 0))
    `, lockKey); err != nil {
		return fmt.Errorf("lock account sync feed: %w", err)
	}
	return nil
}

// canonicalSyncJSON produces the request identity used for retry comparison.
// UseNumber preserves the distinction between large integer literals and
// floating point values; object keys are sorted by encoding/json.
func canonicalSyncJSON(raw []byte) ([]byte, error) {
	if len(raw) == 0 {
		return nil, fmt.Errorf("payload is empty")
	}
	decoder := json.NewDecoder(bytes.NewReader(raw))
	decoder.UseNumber()

	var value any
	if err := decoder.Decode(&value); err != nil {
		return nil, fmt.Errorf("payload is invalid JSON: %w", err)
	}
	var extra any
	if err := decoder.Decode(&extra); err != io.EOF {
		if err == nil {
			return nil, fmt.Errorf("payload contains trailing JSON")
		}
		return nil, fmt.Errorf("payload has invalid trailing data: %w", err)
	}

	encoded, err := json.Marshal(value)
	if err != nil {
		return nil, fmt.Errorf("encode canonical payload: %w", err)
	}
	return encoded, nil
}

func canonicalPayloadHash(canonical []byte) []byte {
	digest := sha256.Sum256(canonical)
	return append([]byte(nil), digest[:]...)
}

func syncMutationConflictResult(mutationID string) SyncPushResult {
	return SyncPushResult{
		MutationID: mutationID,
		Accepted:   false,
		Code:       "mutation_id_conflict",
		Message:    "The mutation id is already bound to different sync content.",
	}
}

func loadMutationRowsByIDTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID,
	mutationID string,
) (syncMutationRows, error) {
	journal, journalFound, err := loadJournalByMutationIDTx(
		ctx, tx, accountID, mutationID)
	if err != nil {
		return syncMutationRows{}, err
	}
	activity, activityFound, err := loadActivityByMutationIDTx(
		ctx, tx, accountID, mutationID)
	if err != nil {
		return syncMutationRows{}, err
	}
	alias, aliasFound, err := loadActivityMutationAliasByIDTx(
		ctx, tx, accountID, mutationID)
	if err != nil {
		return syncMutationRows{}, err
	}
	return syncMutationRows{
		Journal:       journal,
		JournalFound:  journalFound,
		Activity:      activity,
		ActivityFound: activityFound,
		Alias:         alias,
		AliasFound:    aliasFound,
	}, nil
}

func loadJournalByMutationIDTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID,
	mutationID string,
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
            won,
            received_at
        FROM account_sync_journal
        WHERE account_id = $1::uuid
          AND mutation_id = $2::uuid
    `, accountID, mutationID)

	var stored syncStoredMutation
	var serverSeq, counter int64
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
		&stored.Won,
		&stored.ReceivedAt); err != nil {
		if err == pgx.ErrNoRows {
			return syncStoredMutation{}, false, nil
		}
		return syncStoredMutation{}, false, fmt.Errorf("load sync idempotency row: %w", err)
	}
	if serverSeq <= 0 || counter < 0 {
		return syncStoredMutation{}, false, fmt.Errorf("sync idempotency row has invalid numeric state")
	}
	stored.ServerSeq = uint64(serverSeq)
	stored.HLCCounter = uint64(counter)
	return stored, true, nil
}

func (s *Service) syncStoredMutationMatches(
	ctx context.Context,
	accountID string,
	parsed parsedSyncMutation,
	canonicalPayload []byte,
	stored syncStoredMutation,
) (bool, error) {
	if stored.DeviceID != parsed.DeviceID ||
		stored.Category != parsed.Category ||
		stored.RecordKey != parsed.RecordKey ||
		stored.SchemaVersion != parsed.SchemaVersion ||
		stored.HLCPhysicalMS != parsed.HLCPhysicalMS ||
		stored.HLCCounter != parsed.HLCCounter ||
		stored.Operation != parsed.Operation {
		return false, nil
	}
	if parsed.Operation == "delete" {
		return true, nil
	}

	plain, err := s.syncCipher.Open(
		accountID,
		stored.Category,
		stored.RecordKey,
		stored.PayloadCipher)
	if err != nil {
		return false, fmt.Errorf("decrypt stored sync retry identity: %w", err)
	}
	storedCanonical, err := canonicalSyncJSON(plain)
	if err != nil {
		return false, fmt.Errorf("canonicalize stored sync retry identity: %w", err)
	}
	return bytes.Equal(storedCanonical, canonicalPayload), nil
}

func (s *Service) syncAliasMatches(
	parsed parsedSyncMutation,
	canonicalPayload []byte,
	alias syncMutationAlias,
) bool {
	if alias.DeviceID != parsed.DeviceID ||
		alias.Category != parsed.Category ||
		alias.RecordKey != parsed.RecordKey ||
		alias.SchemaVersion != parsed.SchemaVersion ||
		alias.HLCPhysicalMS != parsed.HLCPhysicalMS ||
		alias.HLCCounter != parsed.HLCCounter ||
		alias.Operation != parsed.Operation {
		return false
	}
	return bytes.Equal(alias.PayloadHash, canonicalPayloadHash(canonicalPayload))
}
