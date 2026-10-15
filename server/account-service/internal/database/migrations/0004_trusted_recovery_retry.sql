ALTER TABLE trusted_recovery_challenges
    ADD COLUMN recovery_retry_ciphertext bytea,
    ADD COLUMN recovery_retry_expires_at timestamptz;

ALTER TABLE trusted_recovery_challenges
    ADD CONSTRAINT trusted_recovery_retry_material_ck
    CHECK (
        (recovery_retry_ciphertext IS NULL
         AND recovery_retry_expires_at IS NULL)
        OR
        (recovery_retry_ciphertext IS NOT NULL
         AND recovery_retry_expires_at IS NOT NULL)
    );
