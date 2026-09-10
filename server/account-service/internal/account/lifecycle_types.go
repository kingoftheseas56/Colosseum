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
