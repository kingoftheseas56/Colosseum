-- Forward-only equivalence for the historical 0005-0007 lineage.
-- Keep the SAME sequence object and all values; only restore its canonical name.
ALTER SEQUENCE public.account_change_seq
    RENAME TO account_sync_journal_server_seq_seq;

-- PostgreSQL integer is a lossless widening of the historical smallint.
ALTER TABLE public.account_activity_facts
    ALTER COLUMN schema_version TYPE integer;
