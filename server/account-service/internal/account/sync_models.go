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
}

type SyncPullEntry struct {
	ServerSeq uint64           `json:"server_seq"`
	Won       bool             `json:"won"`
	Canonical bool             `json:"canonical,omitempty"`
	Mutation  SyncMutationView `json:"mutation"`
}

type ProfileAttachment struct {
	ID                string `json:"attachment_id"`
	DeviceID          string `json:"device_id"`
	BaselineServerSeq uint64 `json:"baseline_server_seq"`
	State             string `json:"state"`
}

type BeginProfileAttachmentInput struct {
	AttachmentID         string `json:"attachment_id"`
	SourceKind           string `json:"source_kind"`
	SourceSemanticDigest string `json:"source_semantic_digest"`
}

type SyncPushEnvelope struct {
	AttachmentID string              `json:"attachment_id,omitempty"`
	Mutations    []SyncMutationInput `json:"mutations"`
}

type SyncSnapshotResponse struct {
	ServerTimeMS  int64           `json:"server_time_ms"`
	Cursor        uint64          `json:"cursor"`
	Entries       []SyncPullEntry `json:"entries"`
	NextPageToken string          `json:"next_page_token,omitempty"`
	HasMore       bool            `json:"has_more"`
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
	Won           bool
	ReceivedAt    time.Time
}
