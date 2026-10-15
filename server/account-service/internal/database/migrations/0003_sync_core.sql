CREATE TABLE account_sync_journal (
    server_seq bigserial PRIMARY KEY,
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    mutation_id uuid NOT NULL,
    device_id uuid NOT NULL,
    category text NOT NULL,
    record_key text NOT NULL,
    schema_version integer NOT NULL,
    hlc_physical_ms bigint NOT NULL,
    hlc_counter bigint NOT NULL,
    operation text NOT NULL,
    payload_ciphertext bytea,
    won boolean NOT NULL DEFAULT false,
    received_at timestamptz NOT NULL,
    CONSTRAINT account_sync_journal_mutation_uk UNIQUE(account_id, mutation_id),
    CONSTRAINT account_sync_journal_operation_ck CHECK(operation IN ('put', 'delete')),
    CONSTRAINT account_sync_journal_payload_ck CHECK(
        (operation = 'put' AND payload_ciphertext IS NOT NULL) OR
        (operation = 'delete' AND payload_ciphertext IS NULL)
    ),
    CONSTRAINT account_sync_journal_schema_ck CHECK(schema_version > 0),
    CONSTRAINT account_sync_journal_counter_ck CHECK(hlc_counter >= 0)
);

CREATE INDEX account_sync_journal_pull_idx
    ON account_sync_journal(account_id, server_seq);

CREATE TABLE account_sync_current (
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    category text NOT NULL,
    record_key text NOT NULL,
    mutation_id uuid NOT NULL,
    device_id uuid NOT NULL,
    schema_version integer NOT NULL,
    hlc_physical_ms bigint NOT NULL,
    hlc_counter bigint NOT NULL,
    operation text NOT NULL,
    payload_ciphertext bytea,
    server_seq bigint NOT NULL,
    updated_at timestamptz NOT NULL,
    PRIMARY KEY(account_id, category, record_key),
    CONSTRAINT account_sync_current_operation_ck CHECK(operation IN ('put', 'delete')),
    CONSTRAINT account_sync_current_payload_ck CHECK(
        (operation = 'put' AND payload_ciphertext IS NOT NULL) OR
        (operation = 'delete' AND payload_ciphertext IS NULL)
    )
);

CREATE TABLE account_sync_versions (
    id bigserial PRIMARY KEY,
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    category text NOT NULL,
    record_key text NOT NULL,
    mutation_id uuid NOT NULL,
    device_id uuid NOT NULL,
    schema_version integer NOT NULL,
    hlc_physical_ms bigint NOT NULL,
    hlc_counter bigint NOT NULL,
    operation text NOT NULL,
    payload_ciphertext bytea,
    server_seq bigint NOT NULL,
    replaced_at timestamptz NOT NULL,
    replacing_mutation_id uuid NOT NULL
);

CREATE INDEX account_sync_versions_recovery_idx
    ON account_sync_versions(account_id, replaced_at DESC);
