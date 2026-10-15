CREATE TABLE account_export_snapshots (
    id uuid PRIMARY KEY DEFAULT gen_random_uuid(),
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    highwater_server_seq bigint NOT NULL DEFAULT 0,
    format_version integer NOT NULL DEFAULT 1,
    created_at timestamptz NOT NULL DEFAULT now(),
    expires_at timestamptz NOT NULL,
    CONSTRAINT account_export_snapshots_highwater_ck
        CHECK(highwater_server_seq >= 0),
    CONSTRAINT account_export_snapshots_format_ck
        CHECK(format_version > 0),
    CONSTRAINT account_export_snapshots_expiry_ck
        CHECK(expires_at > created_at)
);

CREATE INDEX account_export_snapshots_account_expiry_idx
    ON account_export_snapshots(account_id, expires_at);

CREATE TABLE account_export_items (
    snapshot_id uuid NOT NULL
        REFERENCES account_export_snapshots(id) ON DELETE CASCADE,
    item_index bigint NOT NULL,
    kind text NOT NULL,
    category text NOT NULL,
    record_key text NOT NULL,
    payload_ciphertext bytea NOT NULL,
    PRIMARY KEY(snapshot_id, item_index),
    CONSTRAINT account_export_items_index_ck CHECK(item_index >= 0),
    CONSTRAINT account_export_items_kind_ck CHECK(
        kind IN ('account_metadata', 'sync_record', 'activity_fact')
    ),
    CONSTRAINT account_export_items_payload_ck CHECK(
        octet_length(payload_ciphertext) > 0
    ),
    CONSTRAINT account_export_items_identity_uk
        UNIQUE(snapshot_id, kind, category, record_key)
);

CREATE TABLE account_deletion_receipts (
    request_id uuid PRIMARY KEY,
    capability_hash bytea NOT NULL UNIQUE,
    account_id uuid NOT NULL,
    created_at timestamptz NOT NULL,
    expires_at timestamptz NOT NULL,
    completed_at timestamptz NOT NULL,
    CONSTRAINT account_deletion_receipts_capability_ck
        CHECK(octet_length(capability_hash) = 32),
    CONSTRAINT account_deletion_receipts_expiry_ck
        CHECK(expires_at > created_at),
    CONSTRAINT account_deletion_receipts_completed_ck
        CHECK(completed_at >= created_at)
);

CREATE INDEX account_deletion_receipts_expiry_idx
    ON account_deletion_receipts(expires_at);
