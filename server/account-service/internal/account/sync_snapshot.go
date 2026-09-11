package account

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"fmt"
	"math"
	"strings"
)

// The fixed-cursor canonical snapshot (Arc 36 N-12). A snapshot walks the
// account's committed canonical state — mutable current rows plus immutable
// Activity facts — in logical (category, record_key) order under ONE cursor
// frozen at the maximum committed canonical server_seq. Later pages reuse the
// frozen cursor exactly, so rows that change after cursor capture disappear
// from later pages only when their new server_seq exceeds the cursor, and an
// ordinary pull after the cursor recovers them. Pulls and snapshots never
// allocate sequence values and Activity facts are never deleted.

var ErrInvalidPageToken = errors.New("invalid snapshot page token")

const syncSnapshotTokenVersion = 1

type syncSnapshotPageState struct {
	Cursor    uint64
	Category  string
	RecordKey string
}

// syncSnapshotPageToken is the internal payload of the opaque next-page
// token. The account binding makes a token replayed against another account
// invalid instead of silently reordering that account's rows.
type syncSnapshotPageToken struct {
	Version   int    `json:"v"`
	AccountID string `json:"account_id"`
	Cursor    uint64 `json:"cursor"`
	Category  string `json:"category"`
	RecordKey string `json:"record_key"`
}

func encodeSyncSnapshotPageToken(
	auth AuthenticatedSession,
	state syncSnapshotPageState,
) (string, error) {
	payload, err := json.Marshal(syncSnapshotPageToken{
		Version:   syncSnapshotTokenVersion,
		AccountID: auth.Account.ID,
		Cursor:    state.Cursor,
		Category:  state.Category,
		RecordKey: state.RecordKey,
	})
	if err != nil {
		return "", fmt.Errorf("encode snapshot page token: %w", err)
	}
	return base64.RawURLEncoding.EncodeToString(payload), nil
}

func decodeSyncSnapshotPageToken(
	raw string,
	auth AuthenticatedSession,
) (syncSnapshotPageState, error) {
	payload, err := base64.RawURLEncoding.DecodeString(strings.TrimSpace(raw))
	if err != nil {
		return syncSnapshotPageState{}, ErrInvalidPageToken
	}
	var token syncSnapshotPageToken
	if err := json.Unmarshal(payload, &token); err != nil {
		return syncSnapshotPageState{}, ErrInvalidPageToken
	}
	if token.Version != syncSnapshotTokenVersion ||
		token.AccountID != auth.Account.ID ||
		token.Cursor == 0 ||
		token.Cursor > math.MaxInt64 ||
		strings.TrimSpace(token.Category) == "" ||
		len(token.Category) > 256 ||
		strings.TrimSpace(token.RecordKey) == "" ||
		len(token.RecordKey) > 512 {
		return syncSnapshotPageState{}, ErrInvalidPageToken
	}
	return syncSnapshotPageState{
		Cursor:    token.Cursor,
		Category:  token.Category,
		RecordKey: token.RecordKey,
	}, nil
}

// SnapshotSync returns one page of the account's frozen canonical snapshot.
// An empty pageToken starts a new snapshot at the current committed maximum;
// otherwise the token's cursor and last logical key position the page.
func (s *Service) SnapshotSync(
	ctx context.Context,
	auth AuthenticatedSession,
	pageToken string,
) (SyncSnapshotResponse, error) {
	now := s.clock.Now().UTC()
	response := SyncSnapshotResponse{
		ServerTimeMS: now.UnixMilli(),
		Entries:      make([]SyncPullEntry, 0, syncPullPageSize),
	}

	var start syncSnapshotPageState
	if strings.TrimSpace(pageToken) == "" {
		cursor, err := maxCommittedServerSeq(ctx, s.pool, auth.Account.ID)
		if err != nil {
			return response, err
		}
		start = syncSnapshotPageState{Cursor: cursor}
	} else {
		state, err := decodeSyncSnapshotPageToken(pageToken, auth)
		if err != nil {
			return response, err
		}
		start = state
	}
	response.Cursor = start.Cursor

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
            received_at
        FROM (
            SELECT
                server_seq,
                mutation_id,
                device_id,
                category,
                record_key,
                schema_version,
                hlc_physical_ms,
                hlc_counter,
                operation,
                payload_ciphertext,
                updated_at AS received_at
            FROM account_sync_current
            WHERE account_id = $1::uuid
              AND server_seq <= $2
              AND (category > $3 OR (category = $3 AND record_key > $4))
            UNION ALL
            SELECT
                server_seq,
                mutation_id,
                origin_device_id,
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
              AND server_seq <= $2
              AND ('activity_fact' > $3 OR
                   ('activity_fact' = $3 AND 'activity/' || event_id::text > $4))
        ) snapshot_rows
        ORDER BY category ASC, record_key ASC, server_seq ASC
        LIMIT $5
    `,
		auth.Account.ID,
		int64(start.Cursor),
		start.Category,
		start.RecordKey,
		syncPullPageSize+1)
	if err != nil {
		return response, fmt.Errorf("query snapshot rows: %w", err)
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
			return response, fmt.Errorf("scan snapshot row: %w", err)
		}
		if serverSeq <= 0 || counter < 0 {
			return response, fmt.Errorf("snapshot row contains invalid numeric state")
		}
		stored.ServerSeq = uint64(serverSeq)
		stored.HLCCounter = uint64(counter)

		if len(response.Entries) == syncPullPageSize {
			response.HasMore = true
			break
		}

		view, err := s.decodeStoredMutation(auth.Account.ID, stored)
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
		return response, fmt.Errorf("iterate snapshot rows: %w", err)
	}

	if response.HasMore && len(response.Entries) > 0 {
		last := response.Entries[len(response.Entries)-1].Mutation
		token, err := encodeSyncSnapshotPageToken(auth, syncSnapshotPageState{
			Cursor:    start.Cursor,
			Category:  last.Category,
			RecordKey: last.RecordKey,
		})
		if err != nil {
			return response, err
		}
		response.NextPageToken = token
	}

	return response, nil
}
