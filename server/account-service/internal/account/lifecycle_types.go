package account

import (
	"encoding/json"
	"time"
)

// ExportAccountInput selects one bounded page of the canonical account
// export. An empty Cursor starts a new expiring snapshot. The Cursor returned
// with every page can be replayed to restart that page without observing later
// account writes.
type ExportAccountInput struct {
	Cursor string `json:"cursor,omitempty"`
	Limit  int    `json:"limit,omitempty"`
}

// ExportPage is a portable page projection. It deliberately contains no
// journal mutation ids, device ids, HLC values, ciphertext, local paths,
// access tokens, refresh tokens, password material or recovery material.
type ExportPage struct {
	Format             string       `json:"format"`
	SchemaVersion      int          `json:"schema_version"`
	SnapshotID         string       `json:"snapshot_id"`
	GeneratedAt        time.Time    `json:"generated_at"`
	ExpiresAt          time.Time    `json:"expires_at"`
	HighWaterServerSeq uint64       `json:"high_water_server_seq"`
	Cursor             string       `json:"cursor"`
	NextCursor         string       `json:"next_cursor,omitempty"`
	HasMore            bool         `json:"has_more"`
	Items              []ExportItem `json:"items"`
}

type ExportItem struct {
	Kind     string          `json:"kind"`
	Category string          `json:"category"`
	Key      string          `json:"key"`
	Payload  json.RawMessage `json:"payload"`
}

// BeginProfileAttachmentInput records the immutable source contribution that
// an attachment is allowed to upload.  The manifest is deliberately the same
// wire shape as SyncMutationInput so the service can validate it with the
// normal sync admission rules before it ever accepts an attached push.
type BeginProfileAttachmentInput struct {
	AttachmentID         string              `json:"attachment_id"`
	SourceKind           string              `json:"source_kind"`
	SourceProfileID      string              `json:"source_profile_id,omitempty"`
	SourceSemanticDigest string              `json:"source_semantic_digest"`
	SourceActivityDigest string              `json:"source_activity_digest,omitempty"`
	ManifestDigest       string              `json:"manifest_digest,omitempty"`
	Manifest             []SyncMutationInput `json:"manifest,omitempty"`
}

// ProfileAttachmentDisposition is the server's locked, per-manifest proof
// of how the exact source identity was absorbed into canonical state.
type ProfileAttachmentDisposition struct {
	MutationID              string `json:"mutation_id"`
	Category                string `json:"category"`
	RecordKey               string `json:"record_key"`
	Operation               string `json:"operation"`
	Disposition             string `json:"disposition"`
	MaterializedPayloadHash string `json:"materialized_payload_hash"`
}

// ProfileAttachment is the account/device-scoped server receipt.  The fresh
// export fields are sealed to the commit transaction: clients must read that
// cursor before treating the source as retired.
type ProfileAttachment struct {
	ID                      string                         `json:"attachment_id"`
	DeviceID                string                         `json:"device_id"`
	BaselineServerSeq       uint64                         `json:"baseline_server_seq"`
	State                   string                         `json:"state"`
	SourceProfileID         string                         `json:"source_profile_id,omitempty"`
	SourceSemanticDigest    string                         `json:"source_semantic_digest,omitempty"`
	SourceActivityDigest    string                         `json:"source_activity_digest,omitempty"`
	ManifestDigest          string                         `json:"manifest_digest,omitempty"`
	ManifestCount           int                            `json:"manifest_count,omitempty"`
	FreshExportSnapshotID   string                         `json:"fresh_export_snapshot_id,omitempty"`
	FreshExportCursor       string                         `json:"fresh_export_cursor,omitempty"`
	FreshExportHighWaterSeq uint64                         `json:"fresh_export_high_water_server_seq,omitempty"`
	Dispositions            []ProfileAttachmentDisposition `json:"dispositions,omitempty"`
}

type DeleteAccountInput struct {
	RequestID       string
	RetryCapability string
	CurrentPassword string
}

// DeleteAccountRetryInput is accepted by the unauthenticated retry seam after
// the original session has been revoked. Both values are opaque client-held
// values; the service stores only a cryptographic verifier for the capability.
type DeleteAccountRetryInput struct {
	RequestID       string
	RetryCapability string
}

type DeleteAccountResult struct {
	Status           string    `json:"status"`
	Retried          bool      `json:"retried,omitempty"`
	ReceiptExpiresAt time.Time `json:"receipt_expires_at"`
}
