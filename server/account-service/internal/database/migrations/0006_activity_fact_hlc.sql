ALTER TABLE account_activity_facts
    ADD COLUMN hlc_physical_ms bigint NOT NULL,
    ADD COLUMN hlc_counter bigint NOT NULL,
    ADD CONSTRAINT account_activity_facts_hlc_physical_ck
        CHECK (hlc_physical_ms >= 0),
    ADD CONSTRAINT account_activity_facts_hlc_counter_ck
        CHECK (hlc_counter >= 0);
