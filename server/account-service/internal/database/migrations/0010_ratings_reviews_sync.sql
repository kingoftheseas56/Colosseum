-- Ratings/reviews DELETEs carry their original semantic delete time separately
-- from HLC transport ordering. The field is valid only for that category's
-- DELETE operation and remains null everywhere else.
ALTER TABLE account_sync_journal
    ADD COLUMN deleted_at_ms bigint;
ALTER TABLE account_sync_current
    ADD COLUMN deleted_at_ms bigint;
ALTER TABLE account_sync_versions
    ADD COLUMN deleted_at_ms bigint;
ALTER TABLE account_device_attachment_manifest
    ADD COLUMN deleted_at_ms bigint;

ALTER TABLE account_sync_journal
    ADD CONSTRAINT account_sync_journal_deleted_at_ck
    CHECK (
        (category = 'ratings_reviews' AND operation = 'delete'
         AND deleted_at_ms IS NOT NULL AND deleted_at_ms > 0)
        OR
        ((category <> 'ratings_reviews' OR operation <> 'delete')
         AND deleted_at_ms IS NULL)
    );

ALTER TABLE account_sync_current
    ADD CONSTRAINT account_sync_current_deleted_at_ck
    CHECK (
        (category = 'ratings_reviews' AND operation = 'delete'
         AND deleted_at_ms IS NOT NULL AND deleted_at_ms > 0)
        OR
        ((category <> 'ratings_reviews' OR operation <> 'delete')
         AND deleted_at_ms IS NULL)
    );

ALTER TABLE account_sync_versions
    ADD CONSTRAINT account_sync_versions_deleted_at_ck
    CHECK (
        (category = 'ratings_reviews' AND operation = 'delete'
         AND deleted_at_ms IS NOT NULL AND deleted_at_ms > 0)
        OR
        ((category <> 'ratings_reviews' OR operation <> 'delete')
         AND deleted_at_ms IS NULL)
    );

ALTER TABLE account_device_attachment_manifest
    ADD CONSTRAINT account_device_attachment_manifest_deleted_at_ck
    CHECK (
        (category = 'ratings_reviews' AND operation = 'delete'
         AND deleted_at_ms IS NOT NULL AND deleted_at_ms > 0)
        OR
        ((category <> 'ratings_reviews' OR operation <> 'delete')
         AND deleted_at_ms IS NULL)
    );
