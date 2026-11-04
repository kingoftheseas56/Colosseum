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
