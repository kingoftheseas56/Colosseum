ALTER SEQUENCE account_sync_journal_server_seq_seq
    RENAME TO account_change_seq;

CREATE TABLE account_device_attachments (
    id uuid PRIMARY KEY,
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    device_id uuid NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    source_kind text NOT NULL,
    source_semantic_digest text NOT NULL,
    baseline_server_seq bigint NOT NULL,
    state text NOT NULL,
    created_at timestamptz NOT NULL,
    updated_at timestamptz NOT NULL,
    committed_at timestamptz,
    CONSTRAINT account_device_attachments_state_ck
        CHECK (state IN ('open', 'uploaded', 'committed', 'aborted')),
    CONSTRAINT account_device_attachments_baseline_ck
        CHECK (baseline_server_seq >= 0)
);

CREATE INDEX account_device_attachments_open_idx
    ON account_device_attachments(account_id, device_id, state);

ALTER TABLE account_sync_journal
    ADD COLUMN attachment_id uuid
    REFERENCES account_device_attachments(id) ON DELETE SET NULL;

CREATE INDEX account_sync_journal_attachment_idx
    ON account_sync_journal(account_id, attachment_id, server_seq)
    WHERE attachment_id IS NOT NULL;

CREATE TABLE account_activity_facts (
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    event_id uuid NOT NULL,
    mutation_id uuid NOT NULL,
    origin_device_id uuid NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    schema_version smallint NOT NULL,
    event_type text NOT NULL,
    payload_ciphertext bytea NOT NULL,
    server_seq bigint NOT NULL DEFAULT nextval('account_change_seq'),
    received_at timestamptz NOT NULL,
    PRIMARY KEY (account_id, event_id),
    CONSTRAINT account_activity_facts_mutation_uk
        UNIQUE(account_id, mutation_id),
    CONSTRAINT account_activity_facts_schema_ck
        CHECK(schema_version > 0),
    CONSTRAINT account_activity_facts_seq_uk
        UNIQUE(server_seq)
);

CREATE INDEX account_activity_facts_pull_idx
    ON account_activity_facts(account_id, server_seq);
