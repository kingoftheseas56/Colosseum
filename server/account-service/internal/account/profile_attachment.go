package account

import (
	"bytes"
	"context"
	"crypto/sha256"
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"strconv"
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
	ErrAttachmentInvalid          = errors.New("profile attachment invalid")
	ErrAttachmentNotFound         = errors.New("profile attachment not found")
	ErrAttachmentConflict         = errors.New("profile attachment conflict")
	ErrAttachmentNotActive        = errors.New("profile attachment not active")
	ErrAttachmentManifestMissing  = errors.New("profile attachment manifest item missing")
	ErrAttachmentManifestMismatch = errors.New("profile attachment manifest item mismatch")
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
	SourceProfileID      string
	SourceSemanticDigest string
	SourceActivityDigest string
	ManifestDigest       string
	ManifestCount        int
	BaselineServerSeq    uint64
	State                string
}

const (
	profileAttachmentManifestLimit      = 100
	profileAttachmentManifestBytesLimit = 256 * 1024
)

func nullableUUIDArgument(raw string) any {
	if strings.TrimSpace(raw) == "" {
		return nil
	}
	return raw
}

func (stored storedProfileAttachment) view() ProfileAttachment {
	return ProfileAttachment{
		ID:                   stored.ID,
		DeviceID:             stored.DeviceID,
		BaselineServerSeq:    stored.BaselineServerSeq,
		State:                stored.State,
		SourceProfileID:      stored.SourceProfileID,
		SourceSemanticDigest: stored.SourceSemanticDigest,
		SourceActivityDigest: stored.SourceActivityDigest,
		ManifestDigest:       stored.ManifestDigest,
		ManifestCount:        stored.ManifestCount,
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
	manifest, manifestDigest, err := s.validateAttachmentManifest(
		auth,
		input.Manifest,
		input.ManifestDigest,
		s.clock.Now().UTC())
	if err != nil {
		return ProfileAttachment{}, err
	}
	sourceProfileID := strings.TrimSpace(input.SourceProfileID)
	if len([]rune(sourceProfileID)) > profileAttachmentMaxDigestRunes {
		return ProfileAttachment{}, ErrAttachmentInvalid
	}
	sourceActivityDigest := strings.TrimSpace(input.SourceActivityDigest)
	if len([]rune(sourceActivityDigest)) > profileAttachmentMaxDigestRunes {
		return ProfileAttachment{}, ErrAttachmentInvalid
	}

	now := s.clock.Now().UTC()
	tx, err := s.pool.Begin(ctx)
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
            id, account_id, device_id, source_kind, source_profile_id,
            source_semantic_digest, source_activity_digest,
            manifest_digest, manifest_count, baseline_server_seq,
            state, created_at, updated_at
        )
        VALUES(
            $1::uuid, $2::uuid, $3::uuid, $4, $5,
            $6, $7, $8, $9, $10,
            'open', $11, $11
        )
        ON CONFLICT (id) DO NOTHING
    `,
		attachmentID,
		auth.Account.ID,
		auth.Device.ID,
		sourceKind,
		sourceProfileID,
		digest,
		sourceActivityDigest,
		manifestDigest,
		len(manifest),
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
		stored.SourceSemanticDigest != digest ||
		stored.SourceProfileID != sourceProfileID ||
		stored.SourceActivityDigest != sourceActivityDigest ||
		stored.ManifestDigest != manifestDigest ||
		stored.ManifestCount != len(manifest) {
		return ProfileAttachment{}, ErrAttachmentConflict
	}
	if len(manifest) > 0 {
		if err := s.insertAttachmentManifestTx(ctx, tx, attachmentID, auth.Account.ID,
			auth.Device.ID, manifest, now); err != nil {
			return ProfileAttachment{}, err
		}
	}

	if err := tx.Commit(ctx); err != nil {
		return ProfileAttachment{}, fmt.Errorf("commit profile attachment begin: %w", err)
	}
	return stored.view(), nil
}

// validateAttachmentManifest runs the ordinary sync admission path for every
// item and derives one deterministic digest from the normalized request
// identity. A caller supplied digest is only an assertion; it is never used
// as proof by itself.
func (s *Service) validateAttachmentManifest(
	auth AuthenticatedSession,
	inputs []SyncMutationInput,
	suppliedDigest string,
	now time.Time,
) ([]SyncMutationInput, string, error) {
	if len(inputs) > profileAttachmentManifestLimit {
		return nil, "", ErrAttachmentInvalid
	}
	seen := make(map[string]struct{}, len(inputs))
	normalized := make([]SyncMutationInput, 0, len(inputs))
	for _, input := range inputs {
		parsed, code, _ := s.validateSyncMutation(auth, input, now)
		if code != "" {
			return nil, "", ErrAttachmentInvalid
		}
		if _, exists := seen[parsed.MutationID]; exists {
			return nil, "", ErrAttachmentInvalid
		}
		seen[parsed.MutationID] = struct{}{}

		normalizedInput := SyncMutationInput{
			MutationID:    parsed.MutationID,
			DeviceID:      parsed.DeviceID,
			Category:      parsed.Category,
			RecordKey:     parsed.RecordKey,
			SchemaVersion: parsed.SchemaVersion,
			HLCPhysicalMS: strconv.FormatInt(parsed.HLCPhysicalMS, 10),
			HLCCounter:    strconv.FormatUint(parsed.HLCCounter, 10),
			Operation:     parsed.Operation,
			Payload:       parsed.CanonicalPayload,
		}
		if parsed.Operation == "delete" {
			normalizedInput.Payload = nil
		}
		normalized = append(normalized, normalizedInput)
	}
	canonical, err := canonicalAttachmentManifest(normalized)
	if err != nil || len(canonical) > profileAttachmentManifestBytesLimit {
		return nil, "", ErrAttachmentInvalid
	}
	digest := "sha256:" + fmt.Sprintf("%x", sha256.Sum256(canonical))
	if supplied := strings.TrimSpace(suppliedDigest); supplied != "" && supplied != digest {
		return nil, "", ErrAttachmentConflict
	}
	if len(normalized) == 0 {
		digest = ""
	}
	return normalized, digest, nil
}

func canonicalAttachmentManifest(inputs []SyncMutationInput) ([]byte, error) {
	value, err := json.Marshal(inputs)
	if err != nil {
		return nil, err
	}
	return canonicalSyncJSON(value)
}

func (s *Service) insertAttachmentManifestTx(
	ctx context.Context,
	tx pgx.Tx,
	attachmentID, accountID, deviceID string,
	manifest []SyncMutationInput,
	now time.Time,
) error {
	for ordinal, input := range manifest {
		parsed, code, message := s.validateSyncMutation(
			AuthenticatedSession{
				Account: Account{ID: accountID},
				Device:  Device{ID: deviceID},
			},
			input,
			now)
		if code != "" {
			return fmt.Errorf("attachment manifest item %d rejected: %s", ordinal, message)
		}
		ciphertext := []byte(nil)
		if parsed.Operation == "put" {
			var err error
			ciphertext, err = s.syncCipher.Seal(
				accountID, parsed.Category, parsed.RecordKey, parsed.CanonicalPayload)
			if err != nil {
				return fmt.Errorf("encrypt attachment manifest item: %w", err)
			}
		}
		if _, err := tx.Exec(ctx, `
            INSERT INTO account_device_attachment_manifest(
                attachment_id, ordinal, mutation_id, device_id,
                category, record_key, schema_version,
                hlc_physical_ms, hlc_counter, operation,
                payload_ciphertext, canonical_payload_hash, created_at)
            VALUES($1::uuid, $2, $3::uuid, $4::uuid, $5, $6, $7,
                   $8, $9, $10, $11, $12, $13)
            ON CONFLICT(attachment_id, mutation_id) DO NOTHING
        `, attachmentID, ordinal, parsed.MutationID, parsed.DeviceID,
			parsed.Category, parsed.RecordKey, parsed.SchemaVersion,
			parsed.HLCPhysicalMS, int64(parsed.HLCCounter), parsed.Operation,
			ciphertext, canonicalPayloadHash(parsed.CanonicalPayload), now); err != nil {
			return fmt.Errorf("store attachment manifest item: %w", err)
		}
	}
	return nil
}

// validateStoredAttachmentManifestTx proves that every item in the receipt
// was accepted through this attachment.  The query checks the immutable
// original request fields and canonical payload hash, while attachment_id
// prevents an older ordinary push with the same mutation id from satisfying
// a later attachment.
func (s *Service) validateStoredAttachmentManifestTx(
	ctx context.Context,
	tx pgx.Tx,
	accountID string,
	attachment storedProfileAttachment,
) error {
	if attachment.ManifestCount == 0 {
		return nil
	}
	rows, err := tx.Query(ctx, `
        SELECT ordinal, mutation_id::text, device_id::text, category,
               record_key, schema_version, hlc_physical_ms, hlc_counter,
               operation, canonical_payload_hash
        FROM account_device_attachment_manifest
        WHERE attachment_id = $1::uuid
        ORDER BY ordinal ASC
    `, attachment.ID)
	if err != nil {
		return fmt.Errorf("load attachment manifest: %w", err)
	}
	type manifestItem struct {
		ordinal                                   int
		mutationID, deviceID, category, recordKey string
		schemaVersion                             int
		physical, counter                         int64
		operation                                 string
		payloadHash                               []byte
	}
	items := make([]manifestItem, 0, attachment.ManifestCount)
	for rows.Next() {
		var item manifestItem
		if err := rows.Scan(&item.ordinal, &item.mutationID, &item.deviceID,
			&item.category, &item.recordKey, &item.schemaVersion, &item.physical,
			&item.counter, &item.operation, &item.payloadHash); err != nil {
			rows.Close()
			return fmt.Errorf("scan attachment manifest: %w", err)
		}
		items = append(items, item)
	}
	if err := rows.Err(); err != nil {
		rows.Close()
		return fmt.Errorf("iterate attachment manifest: %w", err)
	}
	rows.Close()
	if len(items) != attachment.ManifestCount {
		return ErrAttachmentNotActive
	}

	for seen, item := range items {
		if item.ordinal != seen || !IsUUID(item.mutationID) || !IsUUID(item.deviceID) ||
			item.deviceID != attachment.DeviceID || item.physical < 0 || item.counter < 0 ||
			len(item.payloadHash) != sha256.Size {
			return ErrAttachmentInvalid
		}
		var (
			foundCategory, foundKey, foundDevice, foundOperation string
			foundSchema                                          int
			foundPhysical, foundCounter                          int64
			foundPayload, foundHash                              []byte
			foundAttachment                                      *string
			foundAlias                                           bool
		)
		lookupErr := tx.QueryRow(ctx, `
            SELECT category, record_key, device_id::text, schema_version,
                   hlc_physical_ms, hlc_counter, operation,
                   payload_ciphertext, NULL::bytea, attachment_id::text, false
            FROM account_sync_journal
            WHERE account_id = $1::uuid AND mutation_id = $2::uuid
            UNION ALL
            SELECT 'activity_fact', 'activity/' || event_id::text,
                   origin_device_id::text, schema_version, hlc_physical_ms,
                   hlc_counter, 'put', payload_ciphertext, NULL::bytea,
                   attachment_id::text, false
            FROM account_activity_facts
            WHERE account_id = $1::uuid AND mutation_id = $2::uuid
            UNION ALL
            SELECT category, record_key, device_id::text, schema_version,
                   hlc_physical_ms, hlc_counter, operation,
                   NULL::bytea, canonical_payload_hash, attachment_id::text, true
            FROM account_sync_mutation_aliases
            WHERE account_id = $1::uuid AND mutation_id = $2::uuid
            LIMIT 1
		`, accountID, item.mutationID).Scan(&foundCategory, &foundKey, &foundDevice,
			&foundSchema, &foundPhysical, &foundCounter, &foundOperation,
			&foundPayload, &foundHash, &foundAttachment, &foundAlias)
		if lookupErr != nil {
			if errors.Is(lookupErr, pgx.ErrNoRows) {
				return ErrAttachmentNotActive
			}
			return fmt.Errorf("load accepted attachment mutation: %w", lookupErr)
		}
		if !foundAlias && foundOperation == "put" {
			plain, openErr := s.syncCipher.Open(accountID, foundCategory, foundKey, foundPayload)
			if openErr != nil {
				return fmt.Errorf("decrypt accepted attachment mutation: %w", openErr)
			}
			canonical, canonicalErr := canonicalSyncJSON(plain)
			if canonicalErr != nil {
				return fmt.Errorf("canonicalize accepted attachment mutation: %w", canonicalErr)
			}
			foundHash = canonicalPayloadHash(canonical)
		} else if !foundAlias {
			foundHash = canonicalPayloadHash(nil)
		}
		if foundCategory != item.category || foundKey != item.recordKey || foundDevice != item.deviceID ||
			foundSchema != item.schemaVersion || foundPhysical != item.physical || foundCounter != item.counter ||
			foundOperation != item.operation || !bytes.Equal(foundHash, item.payloadHash) ||
			foundAttachment == nil || *foundAttachment != attachment.ID {
			return ErrAttachmentConflict
		}
	}
	return nil
}

// attachmentHistoryAbsorbs applies the same field semantics as the server's
// History resolver while keeping the current canonical object as the winner.
// The source may contribute an earlier first/completion or a later last
// timestamp; every other canonical field remains owned by the current
// materialization.
func attachmentHistoryAbsorbs(
	source,
	actual []byte,
) (bool, error) {
	sourceHistory, err := decodeSyncHistoryPayload(source)
	if err != nil {
		return false, err
	}
	actualHistory, err := decodeSyncHistoryPayload(actual)
	if err != nil {
		return false, err
	}
	merged := syncHistoryPayload{
		object:          cloneSyncHistoryObject(actualHistory.object),
		firstActivityAt: minPositiveSyncTimestamp(actualHistory.firstActivityAt, sourceHistory.firstActivityAt),
		lastActivityAt:  maxSyncTimestamp(actualHistory.lastActivityAt, sourceHistory.lastActivityAt),
		completedAt:     minPositiveSyncTimestamp(actualHistory.completedAt, sourceHistory.completedAt),
	}
	mergedPayload, err := canonicalSyncHistoryPayload(merged)
	if err != nil {
		return false, err
	}
	actualPayload, err := canonicalSyncHistoryPayload(actualHistory)
	if err != nil {
		return false, err
	}
	return bytes.Equal(mergedPayload, actualPayload), nil
}

// attachmentDispositionsTx is called while the account sync lock is held.
// It recomputes every outcome from the stored original manifest and the
// current canonical winner; no client done flag or payload claim is trusted.
func (s *Service) attachmentDispositionsTx(
	ctx context.Context,
	tx pgx.Tx,
	auth AuthenticatedSession,
	attachment storedProfileAttachment,
) ([]ProfileAttachmentDisposition, error) {
	if attachment.ManifestCount == 0 {
		return nil, nil
	}
	rows, err := tx.Query(ctx, `
        SELECT mutation_id::text, device_id::text, category, record_key,
               schema_version, operation, payload_ciphertext,
               canonical_payload_hash
        FROM account_device_attachment_manifest
        WHERE attachment_id = $1::uuid
        ORDER BY ordinal ASC
    `, attachment.ID)
	if err != nil {
		return nil, fmt.Errorf("load attachment dispositions: %w", err)
	}
	type dispositionSource struct {
		mutationID, deviceID, category, recordKey, operation string
		schemaVersion                                        int
		ciphertext, sourceHash                               []byte
	}
	sources := make([]dispositionSource, 0, attachment.ManifestCount)
	for rows.Next() {
		var source dispositionSource
		if err := rows.Scan(&source.mutationID, &source.deviceID, &source.category,
			&source.recordKey, &source.schemaVersion, &source.operation,
			&source.ciphertext, &source.sourceHash); err != nil {
			rows.Close()
			return nil, fmt.Errorf("scan attachment disposition: %w", err)
		}
		sources = append(sources, source)
	}
	if err := rows.Err(); err != nil {
		rows.Close()
		return nil, fmt.Errorf("iterate attachment dispositions: %w", err)
	}
	rows.Close()
	if len(sources) != attachment.ManifestCount {
		return nil, ErrAttachmentNotActive
	}

	result := make([]ProfileAttachmentDisposition, 0, attachment.ManifestCount)
	for _, source := range sources {
		mutationID, deviceID := source.mutationID, source.deviceID
		category, recordKey, operation := source.category, source.recordKey, source.operation
		ciphertext, sourceHash := source.ciphertext, source.sourceHash
		if deviceID != attachment.DeviceID || !IsUUID(mutationID) ||
			len(sourceHash) != sha256.Size {
			return nil, ErrAttachmentInvalid
		}

		var sourceCanonical []byte
		if operation == "put" {
			plain, err := s.syncCipher.Open(auth.Account.ID, category, recordKey, ciphertext)
			if err != nil {
				return nil, fmt.Errorf("decrypt attachment source: %w", err)
			}
			sourceCanonical, err = canonicalSyncJSON(plain)
			if err != nil {
				return nil, fmt.Errorf("canonicalize attachment source: %w", err)
			}
			if !bytes.Equal(canonicalPayloadHash(sourceCanonical), sourceHash) {
				return nil, ErrAttachmentConflict
			}
		}

		actualHash := canonicalPayloadHash(nil)
		disposition := "materialized"
		if category == "activity_fact" {
			if operation != "put" || !strings.HasPrefix(recordKey, activityRecordKeyPrefix) {
				return nil, ErrAttachmentInvalid
			}
			eventID := strings.TrimPrefix(recordKey, activityRecordKeyPrefix)
			var eventMutationID, eventDevice string
			var eventSchema int
			var eventCipher []byte
			var suppressed bool
			err := tx.QueryRow(ctx, `
                SELECT mutation_id::text, origin_device_id::text,
                       schema_version, payload_ciphertext, suppressed
                FROM account_activity_facts
                WHERE account_id = $1::uuid AND event_id = $2::uuid
            `, auth.Account.ID, eventID).Scan(
				&eventMutationID, &eventDevice, &eventSchema, &eventCipher, &suppressed)
			if errors.Is(err, pgx.ErrNoRows) || suppressed {
				return nil, ErrAttachmentNotActive
			}
			if err != nil {
				return nil, fmt.Errorf("load attachment Activity fact: %w", err)
			}
			plain, err := s.syncCipher.Open(auth.Account.ID, category, recordKey, eventCipher)
			if err != nil {
				return nil, fmt.Errorf("decrypt attachment Activity fact: %w", err)
			}
			canonical, err := canonicalExportItemPayload(
				"activity_fact", category, recordKey, plain, auth.Account.ID)
			if err != nil {
				return nil, err
			}
			actualHash = canonicalPayloadHash(canonical)
			if !bytes.Equal(actualHash, sourceHash) {
				return nil, ErrAttachmentConflict
			}
			_ = eventMutationID
			_ = eventDevice
			_ = eventSchema
		} else {
			current, found, err := loadCurrentForUpdateTx(
				ctx, tx, auth.Account.ID, category, recordKey)
			if err != nil {
				return nil, err
			}
			if !found {
				if operation == "delete" {
					actualHash = canonicalPayloadHash(nil)
				} else {
					return nil, ErrAttachmentNotActive
				}
			} else if current.Operation == "delete" {
				actualHash = canonicalPayloadHash(nil)
				if operation != "delete" {
					disposition = "superseded"
				}
			} else {
				plain, err := s.syncCipher.Open(
					auth.Account.ID, category, recordKey, current.PayloadCipher)
				if err != nil {
					return nil, fmt.Errorf("decrypt attachment current winner: %w", err)
				}
				canonical, err := canonicalSyncJSON(plain)
				if err != nil {
					return nil, fmt.Errorf("canonicalize attachment current winner: %w", err)
				}
				actualHash = canonicalPayloadHash(canonical)
				if category == "full_history" {
					absorbed, mergeErr := attachmentHistoryAbsorbs(sourceCanonical, canonical)
					if mergeErr != nil {
						return nil, mergeErr
					}
					if !absorbed {
						return nil, ErrAttachmentConflict
					}
				} else if current.MutationID == mutationID {
					if !bytes.Equal(actualHash, sourceHash) {
						return nil, ErrAttachmentConflict
					}
				} else {
					disposition = "superseded"
				}
			}
		}

		result = append(result, ProfileAttachmentDisposition{
			MutationID:              mutationID,
			Category:                category,
			RecordKey:               recordKey,
			Operation:               operation,
			Disposition:             disposition,
			MaterializedPayloadHash: fmt.Sprintf("%x", actualHash),
		})
	}
	return result, nil
}

// validateAttachmentMutationForPush admits an attached push only when the
// mutation is one of the exact, bounded identities recorded by Begin. The
// manifest is the durable source of truth; request payloads are compared by
// their canonical hash and are never reconstructed from client claims.
func (s *Service) validateAttachmentMutationForPush(
	ctx context.Context,
	auth AuthenticatedSession,
	attachmentID string,
	parsed parsedSyncMutation,
) error {
	var (
		manifestDevice, category, recordKey, operation string
		schemaVersion                                  int
		physical, counter                              int64
		payloadHash                                    []byte
	)
	err := s.pool.QueryRow(ctx, `
        SELECT m.device_id::text, m.category, m.record_key,
               m.schema_version, m.hlc_physical_ms, m.hlc_counter,
               m.operation, m.canonical_payload_hash
        FROM account_device_attachment_manifest m
        JOIN account_device_attachments a ON a.id = m.attachment_id
        WHERE m.attachment_id = $1::uuid
          AND m.mutation_id = $2::uuid
          AND a.account_id = $3::uuid
          AND a.device_id = $4::uuid
          AND a.state IN ('open', 'uploaded')
    `, attachmentID, parsed.MutationID, auth.Account.ID, auth.Device.ID).Scan(
		&manifestDevice, &category, &recordKey, &schemaVersion,
		&physical, &counter, &operation, &payloadHash)
	if errors.Is(err, pgx.ErrNoRows) {
		return ErrAttachmentManifestMissing
	}
	if err != nil {
		return fmt.Errorf("load attachment manifest item: %w", err)
	}
	if !IsUUID(manifestDevice) || manifestDevice != parsed.DeviceID ||
		category != parsed.Category || recordKey != parsed.RecordKey ||
		schemaVersion != parsed.SchemaVersion || physical != parsed.HLCPhysicalMS ||
		counter < 0 || uint64(counter) != parsed.HLCCounter ||
		operation != parsed.Operation ||
		!bytes.Equal(payloadHash, canonicalPayloadHash(parsed.CanonicalPayload)) {
		return ErrAttachmentManifestMismatch
	}
	return nil
}

func attachmentErrorCode(err error) string {
	switch {
	case errors.Is(err, ErrAttachmentManifestMissing):
		return "attachment_manifest_missing"
	case errors.Is(err, ErrAttachmentManifestMismatch):
		return "attachment_manifest_mismatch"
	case errors.Is(err, ErrAttachmentNotActive):
		return "attachment_not_active"
	case errors.Is(err, ErrAttachmentNotFound):
		return "attachment_not_found"
	default:
		return "attachment_invalid"
	}
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
        SELECT device_id::text, source_profile_id, source_semantic_digest,
               source_activity_digest, manifest_digest, manifest_count,
               baseline_server_seq, state
        FROM account_device_attachments
        WHERE id = $1::uuid
          AND account_id = $2::uuid
          AND device_id = $3::uuid
    `, attachmentID, auth.Account.ID, auth.Device.ID)

	var deviceID, sourceProfileID, sourceDigest, activityDigest, manifestDigest, state string
	var manifestCount int
	var baseline int64
	if err := row.Scan(&deviceID, &sourceProfileID, &sourceDigest,
		&activityDigest, &manifestDigest, &manifestCount, &baseline, &state); err != nil {
		if errors.Is(err, pgx.ErrNoRows) {
			return ProfileAttachment{}, ErrAttachmentNotFound
		}
		return ProfileAttachment{}, fmt.Errorf("load profile attachment: %w", err)
	}
	if baseline < 0 {
		return ProfileAttachment{}, fmt.Errorf("profile attachment has invalid baseline")
	}
	return ProfileAttachment{
		ID:                   attachmentID,
		DeviceID:             deviceID,
		SourceProfileID:      sourceProfileID,
		SourceSemanticDigest: sourceDigest,
		SourceActivityDigest: activityDigest,
		ManifestDigest:       manifestDigest,
		ManifestCount:        manifestCount,
		BaselineServerSeq:    uint64(baseline),
		State:                state,
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
	tx, err := s.pool.Begin(ctx)
	if err != nil {
		return ProfileAttachment{}, fmt.Errorf("begin profile attachment commit: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	var state string
	var baseline int64
	// Commit is the cloud proof boundary. It holds the account sync lock while
	// validating every immutable manifest item and materializing a fresh,
	// sealed canonical export, so a concurrent writer cannot slip between proof
	// and the state transition.
	if err := lockAccountSyncTx(ctx, tx, auth.Account.ID); err != nil {
		return ProfileAttachment{}, err
	}
	stored, found, err := loadProfileAttachmentByIDTx(ctx, tx, attachmentID)
	if err != nil {
		return ProfileAttachment{}, err
	}
	if !found || stored.AccountID != auth.Account.ID || stored.DeviceID != auth.Device.ID {
		return ProfileAttachment{}, ErrAttachmentNotFound
	}
	if stored.State == "aborted" {
		return ProfileAttachment{}, ErrAttachmentNotActive
	}
	if stored.State == "committed" {
		// An already committed receipt is idempotent. Recompute the locked
		// dispositions and mint a fresh export so a lost response can be
		// retried without trusting a client-side done flag.
		if err := s.validateStoredAttachmentManifestTx(ctx, tx, auth.Account.ID, stored); err != nil {
			return ProfileAttachment{}, err
		}
		dispositions, dispositionErr := s.attachmentDispositionsTx(ctx, tx, auth, stored)
		if dispositionErr != nil {
			return ProfileAttachment{}, dispositionErr
		}
		page, exportErr := s.createExportSnapshotTx(ctx, tx, auth, exportPageDefaultLimit, true)
		if exportErr != nil {
			return ProfileAttachment{}, exportErr
		}
		if err := tx.Commit(ctx); err != nil {
			return ProfileAttachment{}, fmt.Errorf("commit idempotent lookup: %w", err)
		}
		result := stored.view()
		result.FreshExportSnapshotID = page.SnapshotID
		result.FreshExportCursor = page.Cursor
		result.FreshExportHighWaterSeq = page.HighWaterServerSeq
		result.Dispositions = dispositions
		return result, nil
	}
	if err := s.validateStoredAttachmentManifestTx(ctx, tx, auth.Account.ID, stored); err != nil {
		return ProfileAttachment{}, err
	}

	var committedStored storedProfileAttachment
	err = tx.QueryRow(ctx, `
		UPDATE account_device_attachments
		SET state = 'committed',
		    committed_at = $4,
		    updated_at = $4
		WHERE id = $1::uuid
		  AND account_id = $2::uuid
		  AND device_id = $3::uuid
		  AND state IN ('open', 'uploaded')
		RETURNING id::text, account_id::text, device_id::text,
		          source_kind, source_profile_id, source_semantic_digest,
		          source_activity_digest, manifest_digest, manifest_count,
		          baseline_server_seq, state
	`,
		attachmentID, auth.Account.ID, auth.Device.ID, now).Scan(
		&committedStored.ID, &committedStored.AccountID, &committedStored.DeviceID,
		&committedStored.SourceKind, &committedStored.SourceProfileID,
		&committedStored.SourceSemanticDigest, &committedStored.SourceActivityDigest,
		&committedStored.ManifestDigest, &committedStored.ManifestCount,
		&baseline, &state)
	if err == nil {
		committedStored.BaselineServerSeq = uint64(baseline)
		committedStored.State = state
		dispositions, dispositionErr := s.attachmentDispositionsTx(
			ctx, tx, auth, committedStored)
		if dispositionErr != nil {
			return ProfileAttachment{}, dispositionErr
		}
		// A forced fresh export is created in this same locked transaction. The
		// helper is defined in lifecycle.go and never reuses the caller's cursor.
		page, exportErr := s.createExportSnapshotTx(ctx, tx, auth, exportPageDefaultLimit, true)
		if exportErr != nil {
			return ProfileAttachment{}, exportErr
		}
		if err := tx.Commit(ctx); err != nil {
			return ProfileAttachment{}, fmt.Errorf("commit profile attachment: %w", err)
		}
		result := committedStored.view()
		result.FreshExportSnapshotID = page.SnapshotID
		result.FreshExportCursor = page.Cursor
		result.FreshExportHighWaterSeq = page.HighWaterServerSeq
		result.Dispositions = dispositions
		return result, nil
	}
	if !errors.Is(err, pgx.ErrNoRows) {
		return ProfileAttachment{}, fmt.Errorf("commit profile attachment state: %w", err)
	}
	return ProfileAttachment{}, ErrAttachmentNotActive
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
		       source_kind, source_profile_id, source_semantic_digest,
		       source_activity_digest, manifest_digest, manifest_count,
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
		&stored.SourceProfileID,
		&stored.SourceSemanticDigest,
		&stored.SourceActivityDigest,
		&stored.ManifestDigest,
		&stored.ManifestCount,
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
