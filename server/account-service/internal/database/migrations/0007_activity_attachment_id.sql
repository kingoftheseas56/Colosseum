ALTER TABLE account_activity_facts
    ADD COLUMN attachment_id uuid
    REFERENCES account_device_attachments(id) ON DELETE SET NULL;

CREATE INDEX account_activity_facts_attachment_idx
    ON account_activity_facts(account_id, attachment_id, server_seq)
    WHERE attachment_id IS NOT NULL;
