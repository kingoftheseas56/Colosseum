CREATE TABLE account_sync_mutation_aliases (
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    mutation_id uuid NOT NULL,
    category text NOT NULL,
    record_key text NOT NULL,
    device_id uuid NOT NULL,
    schema_version integer NOT NULL,
    hlc_physical_ms bigint NOT NULL,
    hlc_counter bigint NOT NULL,
    operation text NOT NULL,
    canonical_payload_hash bytea NOT NULL,
    server_seq bigint NOT NULL,
    won boolean NOT NULL,
    activity_event_id uuid NOT NULL,
    created_at timestamptz NOT NULL,
    PRIMARY KEY(account_id, mutation_id),
    CONSTRAINT account_sync_mutation_aliases_category_ck CHECK(category = 'activity_fact'),
    CONSTRAINT account_sync_mutation_aliases_record_key_ck CHECK(record_key LIKE 'activity/%'),
    CONSTRAINT account_sync_mutation_aliases_schema_ck CHECK(schema_version > 0),
    CONSTRAINT account_sync_mutation_aliases_hlc_physical_ck CHECK(hlc_physical_ms >= 0),
    CONSTRAINT account_sync_mutation_aliases_hlc_counter_ck CHECK(hlc_counter >= 0),
    CONSTRAINT account_sync_mutation_aliases_operation_ck CHECK(operation = 'put'),
    CONSTRAINT account_sync_mutation_aliases_payload_hash_ck CHECK(octet_length(canonical_payload_hash) = 32),
    CONSTRAINT account_sync_mutation_aliases_server_seq_ck CHECK(server_seq > 0)
);

CREATE INDEX account_sync_mutation_aliases_event_idx
    ON account_sync_mutation_aliases(account_id, activity_event_id);
