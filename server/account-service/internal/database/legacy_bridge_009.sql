-- Preserve historical attachment IDs, states, journal/activity associations
-- and high-water cursors. Missing metadata has no historical source: initialize
-- neutral values rather than inventing a manifest or modifying old ciphertext.
ALTER TABLE public.account_device_attachments
    ADD COLUMN source_profile_id text NOT NULL DEFAULT '',
    ADD COLUMN source_activity_digest text NOT NULL DEFAULT '',
    ADD COLUMN manifest_digest text NOT NULL DEFAULT '',
    ADD COLUMN manifest_count integer NOT NULL DEFAULT 0,
    ADD CONSTRAINT account_device_attachments_manifest_count_ck
        CHECK (manifest_count >= 0 AND manifest_count <= 100),
    ADD CONSTRAINT account_device_attachments_digest_ck
        CHECK (length(source_semantic_digest) > 0 AND length(source_semantic_digest) <= 256),
    ADD CONSTRAINT account_device_attachments_manifest_digest_ck
        CHECK (manifest_count = 0 OR length(manifest_digest) > 0);

ALTER TABLE public.account_sync_mutation_aliases
    ADD COLUMN attachment_id uuid
        REFERENCES account_device_attachments(id) ON DELETE SET NULL;

CREATE INDEX account_sync_mutation_aliases_attachment_idx
    ON account_sync_mutation_aliases(account_id, attachment_id, server_seq)
    WHERE attachment_id IS NOT NULL;

CREATE TABLE account_device_attachment_manifest (
    attachment_id uuid NOT NULL
        REFERENCES account_device_attachments(id) ON DELETE CASCADE,
    ordinal integer NOT NULL,
    mutation_id uuid NOT NULL,
    device_id uuid NOT NULL,
    category text NOT NULL,
    record_key text NOT NULL,
    schema_version integer NOT NULL,
    hlc_physical_ms bigint NOT NULL,
    hlc_counter bigint NOT NULL,
    operation text NOT NULL,
    payload_ciphertext bytea,
    canonical_payload_hash bytea NOT NULL,
    created_at timestamptz NOT NULL,
    PRIMARY KEY (attachment_id, ordinal),
    UNIQUE (attachment_id, mutation_id),
    CONSTRAINT account_device_attachment_manifest_ordinal_ck
        CHECK (ordinal >= 0 AND ordinal < 100),
    CONSTRAINT account_device_attachment_manifest_schema_ck
        CHECK (schema_version > 0),
    CONSTRAINT account_device_attachment_manifest_hlc_physical_ck
        CHECK (hlc_physical_ms >= 0),
    CONSTRAINT account_device_attachment_manifest_hlc_counter_ck
        CHECK (hlc_counter >= 0),
    CONSTRAINT account_device_attachment_manifest_operation_ck
        CHECK (operation IN ('put', 'delete')),
    CONSTRAINT account_device_attachment_manifest_payload_ck
        CHECK ((operation = 'put' AND payload_ciphertext IS NOT NULL)
               OR (operation = 'delete' AND payload_ciphertext IS NULL)),
    CONSTRAINT account_device_attachment_manifest_hash_ck
        CHECK (octet_length(canonical_payload_hash) = 32)
);

CREATE INDEX account_device_attachment_manifest_mutation_idx
    ON account_device_attachment_manifest(attachment_id, mutation_id);
