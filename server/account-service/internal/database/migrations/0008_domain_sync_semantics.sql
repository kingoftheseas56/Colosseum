ALTER TABLE account_sync_journal
    ADD COLUMN IF NOT EXISTS materialized_payload_ciphertext bytea,
    ADD COLUMN IF NOT EXISTS materialized_hlc_physical_ms bigint,
    ADD COLUMN IF NOT EXISTS materialized_hlc_counter bigint,
    ADD COLUMN IF NOT EXISTS materialized_device_id uuid;

DO $$
BEGIN
    IF NOT EXISTS (
        SELECT 1
        FROM pg_constraint
        WHERE conname = 'account_sync_journal_materialized_hlc_ck'
    ) THEN
        ALTER TABLE account_sync_journal
            ADD CONSTRAINT account_sync_journal_materialized_hlc_ck
            CHECK (
                (materialized_hlc_physical_ms IS NULL
                 AND materialized_hlc_counter IS NULL
                 AND materialized_device_id IS NULL)
                OR
                (materialized_hlc_physical_ms IS NOT NULL
                 AND materialized_hlc_counter IS NOT NULL
                 AND materialized_device_id IS NOT NULL
                 AND materialized_hlc_physical_ms >= 0
                 AND materialized_hlc_counter >= 0)
            );
    END IF;
END
$$;

ALTER TABLE account_activity_facts
    ADD COLUMN IF NOT EXISTS suppressed boolean NOT NULL DEFAULT false;

CREATE TABLE IF NOT EXISTS account_activity_reset_state (
    account_id uuid PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
    reset_generation bigint NOT NULL,
    reset_at_ms bigint NOT NULL,
    hlc_physical_ms bigint NOT NULL,
    hlc_counter bigint NOT NULL,
    device_id uuid NOT NULL,
    CONSTRAINT account_activity_reset_generation_ck CHECK(reset_generation > 0),
    CONSTRAINT account_activity_reset_at_ck CHECK(reset_at_ms > 0),
    CONSTRAINT account_activity_reset_hlc_physical_ck CHECK(hlc_physical_ms >= 0),
    CONSTRAINT account_activity_reset_hlc_counter_ck CHECK(hlc_counter >= 0)
);

CREATE TABLE IF NOT EXISTS account_history_reset_state (
    account_id uuid PRIMARY KEY REFERENCES accounts(id) ON DELETE CASCADE,
    reset_generation bigint NOT NULL,
    reset_at_ms bigint NOT NULL,
    hlc_physical_ms bigint NOT NULL,
    hlc_counter bigint NOT NULL,
    device_id uuid NOT NULL,
    CONSTRAINT account_history_reset_generation_ck CHECK(reset_generation > 0),
    CONSTRAINT account_history_reset_at_ck CHECK(reset_at_ms > 0),
    CONSTRAINT account_history_reset_hlc_physical_ck CHECK(hlc_physical_ms >= 0),
    CONSTRAINT account_history_reset_hlc_counter_ck CHECK(hlc_counter >= 0)
);
