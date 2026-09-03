CREATE TABLE account_activity_facts (
    account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
    event_id uuid NOT NULL,
    mutation_id uuid NOT NULL,
    origin_device_id uuid NOT NULL REFERENCES devices(id) ON DELETE CASCADE,
    schema_version integer NOT NULL,
    event_type text NOT NULL,
    payload_ciphertext bytea NOT NULL,
    hlc_physical_ms bigint NOT NULL,
    hlc_counter bigint NOT NULL,
    server_seq bigint NOT NULL DEFAULT nextval('account_sync_journal_server_seq_seq'),
    received_at timestamptz NOT NULL,
    PRIMARY KEY(account_id, event_id),
    CONSTRAINT account_activity_facts_mutation_uk UNIQUE(account_id, mutation_id),
    CONSTRAINT account_activity_facts_schema_ck CHECK(schema_version > 0),
    CONSTRAINT account_activity_facts_hlc_physical_ck CHECK(hlc_physical_ms >= 0),
    CONSTRAINT account_activity_facts_hlc_counter_ck CHECK(hlc_counter >= 0),
    CONSTRAINT account_activity_facts_seq_uk UNIQUE(server_seq)
);

CREATE INDEX account_activity_facts_pull_idx
    ON account_activity_facts(account_id, server_seq);
