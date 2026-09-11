package account

import (
	"encoding/json"
	"time"
)

type SyncMutationInput struct {
	MutationID    string          `json:"mutation_id"`
	DeviceID      string          `json:"device_id"`
	Category      string          `json:"category"`
	RecordKey     string          `json:"record_key"`
	SchemaVersion int             `json:"schema_version"`
	HLCPhysicalMS string          `json:"hlc_physical_ms"`
	HLCCounter    string          `json:"hlc_counter"`
	Operation     string          `json:"operation"`
	Payload       json.RawMessage `json:"payload,omitempty"`
}

type SyncCurrentMetadata struct {
	MutationID    string `json:"mutation_id"`
	DeviceID      string `json:"device_id"`
	SchemaVersion int    `json:"schema_version"`
	HLCPhysicalMS string `json:"hlc_physical_ms"`
	HLCCounter    string `json:"hlc_counter"`
	Operation     string `json:"operation"`
	ServerSeq     uint64 `json:"server_seq"`
}

type SyncPushResult struct {
	MutationID string               `json:"mutation_id"`
	Accepted   bool                 `json:"accepted"`
	ServerSeq  uint64               `json:"server_seq,omitempty"`
	Won        bool                 `json:"won,omitempty"`
	Code       string               `json:"code,omitempty"`
	Message    string               `json:"message,omitempty"`
	Current    *SyncCurrentMetadata `json:"current,omitempty"`
}

type SyncMutationView struct {
	MutationID    string          `json:"mutation_id"`
	DeviceID      string          `json:"device_id"`
	Category      string          `json:"category"`
	RecordKey     string          `json:"record_key"`
	SchemaVersion int             `json:"schema_version"`
	HLCPhysicalMS string          `json:"hlc_physical_ms"`
	HLCCounter    string          `json:"hlc_counter"`
	Operation     string          `json:"operation"`
	Payload       json.RawMessage `json:"payload,omitempty"`
	// HLC/device describe the original request identity. A semantic merge can
	// materialize a payload under the already accepted winner's ordering; that
	// ordering is carried separately so retries never compare against a
	// transformed request payload.
	MaterializedHLCPhysicalMS string `json:"materialized_hlc_physical_ms,omitempty"`
	MaterializedHLCCounter    string `json:"materialized_hlc_counter,omitempty"`
	MaterializedDeviceID      string `json:"materialized_device_id,omitempty"`
}

type SyncPullEntry struct {
	ServerSeq uint64           `json:"server_seq"`
	Won       bool             `json:"won"`
	Canonical bool             `json:"canonical,omitempty"`
	Mutation  SyncMutationView `json:"mutation"`
}

type SyncPushResponse struct {
	ServerTimeMS int64            `json:"server_time_ms"`
	Results      []SyncPushResult `json:"results"`
}

type SyncPullResponse struct {
	ServerTimeMS int64           `json:"server_time_ms"`
	Entries      []SyncPullEntry `json:"entries"`
	HasMore      bool            `json:"has_more"`
}

// SyncSnapshotResponse is the bounded, cursor-frozen canonical feed used by
// profile attachment verification. Cursor is the maximum committed
// server_seq captured for this snapshot; NextPageToken is opaque and bound to
// the authenticated account.
type SyncSnapshotResponse struct {
	ServerTimeMS  int64           `json:"server_time_ms"`
	Cursor        uint64          `json:"cursor"`
	Entries       []SyncPullEntry `json:"entries"`
	NextPageToken string          `json:"next_page_token,omitempty"`
	HasMore       bool            `json:"has_more"`
}

type syncStoredMutation struct {
	ServerSeq     uint64
	MutationID    string
	DeviceID      string
	Category      string
	RecordKey     string
	SchemaVersion int
	HLCPhysicalMS int64
	HLCCounter    uint64
	Operation     string
	PayloadCipher []byte
	// PayloadCipher is the original request identity. MaterializedPayloadCipher
	// is the canonical domain result sent to peers when a semantic merge (for
	// example History field convergence) transforms that request.
	MaterializedPayloadCipher []byte
	MaterializedHLCPhysicalMS int64
	MaterializedHLCCounter    uint64
	MaterializedDeviceID      string
	MaterializedHLCValid      bool
	AttachmentID              string
	Won                       bool
	ReceivedAt                time.Time
}
