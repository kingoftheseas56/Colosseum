-- Historical 1.3.0 rollout review only; NOT permission to execute.
-- Generated from tracked migration 0011; recheck all live targets/baselines first.
-- Canonical migration SHA256: f4276dd8012496a877b6ce65793f0c6c51c34a03ceddd575498e6567a8cf11eb
-- Run only through the reviewed, user-approved migration protocol.
BEGIN;
SET LOCAL lock_timeout = '10s';
SET LOCAL statement_timeout = '60s';
SET LOCAL search_path = public, pg_catalog;
SELECT pg_advisory_xact_lock(4850179227285407061);
DO $baseline_guard$
BEGIN
    IF current_database() <> 'neondb' THEN
        RAISE EXCEPTION 'Unexpected migration database';
    END IF;
    IF EXISTS (SELECT 1 FROM schema_migrations WHERE name = '0011_collection_publications.sql') THEN
        RAISE EXCEPTION 'Migration 0011 already recorded; inspect its receipt instead of replaying';
    END IF;
    IF EXISTS (
        WITH expected(name, checksum) AS (VALUES
        ('0001_bootstrap.sql','00cb4e49790795b2f89383fa0cf46ca4000e6fcf2cf708761c966a8c6ac91482'),
        ('0002_identity_security.sql','79c7da98a677410cf62e8bea50455fd6bfa9f99eead3618f6fe5a4b99cbc6bc7'),
        ('0003_sync_core.sql','e63cfc2d7d64a5f632720cb84b8384f57ce4c04a3ca9c02b019339764cafb510'),
        ('0004_trusted_recovery_retry.sql','d6127bee25a9f078ddff5ec8c1bdff15f674d692a66461983fa8fb2672c8d6ae'),
        ('0005_activity_fact_sync.sql','9c682592c0cbfc7bebde3be459408728f395f68e46eaf5533027ab399544d2a8'),
        ('0006_sync_mutation_aliases.sql','3c6d7c905aea5bf124b88876c5c4d50252090264e1ff3e1443a010cf310c474e'),
        ('0007_account_lifecycle.sql','2c1ed6731c6541e9caff045a56d5a51fe83bdb80ac14ca20eb472884ac99e4df'),
        ('0008_domain_sync_semantics.sql','b2f97d1f90b475a8c91ec44f0f9644857ba3d9b4999539f48d736ac60ded1bd7'),
        ('0009_profile_attachments.sql','3f4a2b59e0c4ae7d676571e0c17b89e5371d1f359a299fcbb40764c50f613b6c'),
        ('0010_ratings_reviews_sync.sql','b4cd7041a534459fe49c2c36c3cc1cac5fca9795849f2f9e5c21e7a5c0b3e8fe'))
        (SELECT name, checksum FROM schema_migrations EXCEPT SELECT name, checksum FROM expected)
        UNION ALL
        (SELECT name, checksum FROM expected EXCEPT SELECT name, checksum FROM schema_migrations)
    ) THEN
        RAISE EXCEPTION 'Migration baseline does not match reviewed 0001-0010';
    END IF;
    IF to_regclass('public.collection_publications') IS NOT NULL
       OR to_regclass('public.collection_publication_operations') IS NOT NULL THEN
        RAISE EXCEPTION 'Unexpected preexisting publication objects';
    END IF;
END
$baseline_guard$;

-- Explicit publications are independent from private collection sync. Account
-- deletion cascades; revoked links never restore. IDs contain 256 random bits.
CREATE TABLE collection_publications (
 share_id text PRIMARY KEY,
 account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 kind text NOT NULL CHECK (kind IN ('collection','gift')),
 revision bigint NOT NULL CHECK (revision > 0),
 snapshot jsonb NOT NULL,
 passphrase_hash text NOT NULL DEFAULT '',
 revoked boolean NOT NULL DEFAULT false,
 created_at timestamptz NOT NULL,
 updated_at timestamptz NOT NULL
);
CREATE INDEX collection_publications_owner_idx ON collection_publications(account_id,created_at,share_id);
CREATE TABLE collection_publication_operations (
 account_id uuid NOT NULL REFERENCES accounts(id) ON DELETE CASCADE,
 operation_id uuid NOT NULL,
 request_hash bytea NOT NULL,
 share_id text NOT NULL REFERENCES collection_publications(share_id) ON DELETE CASCADE,
 PRIMARY KEY(account_id,operation_id)
);

INSERT INTO schema_migrations(name, checksum)
VALUES ('0011_collection_publications.sql', 'f4276dd8012496a877b6ce65793f0c6c51c34a03ceddd575498e6567a8cf11eb');
COMMIT;
