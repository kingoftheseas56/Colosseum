package database

import (
	"context"
	"strings"
	"testing"
	"time"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/testsupport/testdb"
)

func TestRunMigrationsFromEmptyDatabase(t *testing.T) {
	pool := testdb.Open(t)
	testdb.ResetPublicSchema(t, pool)

	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()

	if err := RunMigrations(ctx, pool); err != nil {
		t.Fatalf("RunMigrations() first pass error = %v", err)
	}
	if err := RunMigrations(ctx, pool); err != nil {
		t.Fatalf("RunMigrations() second pass error = %v", err)
	}

	embedded, err := embeddedMigrations()
	if err != nil {
		t.Fatalf("embeddedMigrations() error = %v", err)
	}

	var migrationCount int
	if err := pool.QueryRow(ctx,
		"SELECT count(*) FROM schema_migrations").Scan(&migrationCount); err != nil {
		t.Fatalf("count schema_migrations: %v", err)
	}
	if migrationCount != len(embedded) {
		t.Fatalf("schema migration count = %d, want %d (one row per embedded migration, unchanged by the second idempotent pass)",
			migrationCount, len(embedded))
	}

	var metadataTableExists bool
	if err := pool.QueryRow(ctx, `
        SELECT to_regclass('public.service_metadata') IS NOT NULL
    `).Scan(&metadataTableExists); err != nil {
		t.Fatalf("check service_metadata: %v", err)
	}
	if !metadataTableExists {
		t.Fatal("service_metadata table was not created")
	}

	var bootstrapSchema string
	if err := pool.QueryRow(ctx,
		"SELECT value FROM service_metadata WHERE key = 'bootstrap_schema'").Scan(&bootstrapSchema); err != nil {
		t.Fatalf("read bootstrap schema metadata: %v", err)
	}
	if bootstrapSchema != "1" {
		t.Fatalf("bootstrap schema value = %q, want 1", bootstrapSchema)
	}

	for _, table := range []string{
		"username_reservations",
		"accounts",
		"devices",
		"sessions",
		"device_signin_challenges",
		"trusted_recovery_challenges",
		"auth_rate_events",
		"account_security_events",
		"avatar_cleanup_queue",
	} {
		var exists bool
		if err := pool.QueryRow(ctx,
			"SELECT to_regclass('public.' || $1) IS NOT NULL",
			table).Scan(&exists); err != nil {
			t.Fatalf("check table %s: %v", table, err)
		}
		if !exists {
			t.Fatalf("identity/security table %s was not created", table)
		}
	}
}

func TestMigration0007ActivityAttachmentID(t *testing.T) {
	pool := testdb.Open(t)
	testdb.ResetPublicSchema(t, pool)

	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()

	if err := RunMigrations(ctx, pool); err != nil {
		t.Fatalf("RunMigrations() first pass error = %v", err)
	}
	if err := RunMigrations(ctx, pool); err != nil {
		t.Fatalf("RunMigrations() second pass error = %v", err)
	}

	// Migration 0007 applies exactly once and never double-applies on replay.
	var appliedCount int
	if err := pool.QueryRow(ctx, `
        SELECT count(*)
        FROM schema_migrations
        WHERE name = '0007_activity_attachment_id.sql'
    `).Scan(&appliedCount); err != nil {
		t.Fatalf("count migration 0007 applications: %v", err)
	}
	if appliedCount != 1 {
		t.Fatalf("migration 0007 application count = %d, want exactly 1", appliedCount)
	}

	// The sealed 0005/0006 history stays unchanged: one row each and the
	// total still matches the embedded set.
	var migrationCount int
	if err := pool.QueryRow(ctx,
		"SELECT count(*) FROM schema_migrations").Scan(&migrationCount); err != nil {
		t.Fatalf("count schema_migrations: %v", err)
	}
	embedded, err := embeddedMigrations()
	if err != nil {
		t.Fatalf("embeddedMigrations() error = %v", err)
	}
	if migrationCount != len(embedded) {
		t.Fatalf("schema migration count = %d, want %d", migrationCount, len(embedded))
	}
	for _, sealed := range []string{
		"0005_profile_portable_sync.sql",
		"0006_activity_fact_hlc.sql",
	} {
		var sealedCount int
		if err := pool.QueryRow(ctx, `
            SELECT count(*) FROM schema_migrations WHERE name = $1
        `, sealed).Scan(&sealedCount); err != nil {
			t.Fatalf("count migration %s: %v", sealed, err)
		}
		if sealedCount != 1 {
			t.Fatalf("sealed migration %s count = %d, want 1", sealed, sealedCount)
		}
	}

	// Nullable uuid column on account_activity_facts.
	var dataType, nullable string
	if err := pool.QueryRow(ctx, `
        SELECT data_type, is_nullable
        FROM information_schema.columns
        WHERE table_schema = 'public'
          AND table_name = 'account_activity_facts'
          AND column_name = 'attachment_id'
    `).Scan(&dataType, &nullable); err != nil {
		t.Fatalf("read account_activity_facts.attachment_id: %v", err)
	}
	if dataType != "uuid" || nullable != "YES" {
		t.Fatalf("account_activity_facts.attachment_id = (%s, nullable=%s), want nullable uuid",
			dataType, nullable)
	}

	// FK to account_device_attachments with ON DELETE SET NULL.
	var fkExists bool
	if err := pool.QueryRow(ctx, `
        SELECT EXISTS (
            SELECT 1
            FROM pg_constraint
            WHERE conrelid = 'public.account_activity_facts'::regclass
              AND confrelid = 'public.account_device_attachments'::regclass
              AND contype = 'f'
              AND confdeltype::text = 'n'
        )
    `).Scan(&fkExists); err != nil {
		t.Fatalf("check activity attachment FK: %v", err)
	}
	if !fkExists {
		t.Fatal("required FK account_activity_facts -> account_device_attachments (SET NULL) missing")
	}

	// Partial attachment lookup index.
	var indexDefinition string
	if err := pool.QueryRow(ctx, `
        SELECT indexdef
        FROM pg_indexes
        WHERE schemaname = 'public'
          AND tablename = 'account_activity_facts'
          AND indexname = 'account_activity_facts_attachment_idx'
    `).Scan(&indexDefinition); err != nil {
		t.Fatalf("read account_activity_facts_attachment_idx: %v", err)
	}
	for _, snippet := range []string{
		"account_id, attachment_id, server_seq",
		"attachment_id IS NOT NULL",
	} {
		if !strings.Contains(indexDefinition, snippet) {
			t.Fatalf("index definition = %q, want fragment %q", indexDefinition, snippet)
		}
	}

	var accountID, deviceID, attachmentID string
	if _, err := pool.Exec(ctx, `
        INSERT INTO username_reservations(canonical_username, reserved_at)
        VALUES ('migration-0007-user', now())
    `); err != nil {
		t.Fatalf("insert username reservation: %v", err)
	}
	if err := pool.QueryRow(ctx, `
        INSERT INTO accounts(
            canonical_username, display_username, password_hash,
            recovery_key_verifier, created_at, updated_at
        ) VALUES ($1, $2, $3, decode('01', 'hex'), now(), now())
        RETURNING id::text
    `, "migration-0007-user", "Migration 0007", "test-hash").Scan(&accountID); err != nil {
		t.Fatalf("insert account: %v", err)
	}
	if err := pool.QueryRow(ctx, `
        INSERT INTO devices(
            account_id, install_id, label, platform, created_at, last_seen_at
        ) VALUES ($1::uuid, gen_random_uuid(), 'Device 000007', 'windows', now(), now())
        RETURNING id::text
    `, accountID).Scan(&deviceID); err != nil {
		t.Fatalf("insert device: %v", err)
	}
	if err := pool.QueryRow(ctx, `
        INSERT INTO account_device_attachments(
            id, account_id, device_id, source_kind, source_semantic_digest,
            baseline_server_seq, state, created_at, updated_at
        ) VALUES (
            gen_random_uuid(), $1::uuid, $2::uuid, 'local_only', 'digest-0007',
            0, 'open', now(), now()
        )
        RETURNING id::text
    `, accountID, deviceID).Scan(&attachmentID); err != nil {
		t.Fatalf("insert attachment: %v", err)
	}

	insertFact := func(attachmentParam any) error {
		_, err := pool.Exec(ctx, `
            INSERT INTO account_activity_facts(
                account_id, event_id, mutation_id, origin_device_id,
                schema_version, event_type, payload_ciphertext,
                hlc_physical_ms, hlc_counter, received_at, attachment_id
            ) VALUES (
                $1::uuid, gen_random_uuid(), gen_random_uuid(), $2::uuid,
                1, 'playback_delta', decode('03', 'hex'), 1, 0, now(), $3::uuid
            )
        `, accountID, deviceID, attachmentParam)
		return err
	}

	var eventID string
	if err := pool.QueryRow(ctx, `
        INSERT INTO account_activity_facts(
            account_id, event_id, mutation_id, origin_device_id,
            schema_version, event_type, payload_ciphertext,
            hlc_physical_ms, hlc_counter, received_at, attachment_id
        ) VALUES (
            $1::uuid, gen_random_uuid(), gen_random_uuid(), $2::uuid,
            1, 'playback_delta', decode('03', 'hex'), 1, 0, now(), $3::uuid
        )
        RETURNING event_id::text
    `, accountID, deviceID, attachmentID).Scan(&eventID); err != nil {
		t.Fatalf("insert attached activity fact: %v", err)
	}

	if err := insertFact("00000000-0000-4000-8000-0000000000ff"); err == nil {
		t.Fatal("activity fact accepted an unknown attachment id")
	}

	if _, err := pool.Exec(ctx,
		"DELETE FROM account_device_attachments WHERE id = $1::uuid", attachmentID); err != nil {
		t.Fatalf("delete attachment: %v", err)
	}
	var factAttachmentIsNull bool
	if err := pool.QueryRow(ctx, `
        SELECT attachment_id IS NULL
        FROM account_activity_facts
        WHERE account_id = $1::uuid AND event_id = $2::uuid
    `, accountID, eventID).Scan(&factAttachmentIsNull); err != nil {
		t.Fatalf("read fact after attachment delete: %v", err)
	}
	if !factAttachmentIsNull {
		t.Fatal("deleting an attachment did not preserve the activity fact with attachment_id NULL")
	}
}

func TestMigration0006ActivityFactHLC(t *testing.T) {
	pool := testdb.Open(t)
	testdb.ResetPublicSchema(t, pool)

	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()

	if err := RunMigrations(ctx, pool); err != nil {
		t.Fatalf("RunMigrations() first pass error = %v", err)
	}
	if err := RunMigrations(ctx, pool); err != nil {
		t.Fatalf("RunMigrations() second pass error = %v", err)
	}

	var appliedCount int
	if err := pool.QueryRow(ctx, `
        SELECT count(*)
        FROM schema_migrations
        WHERE name = '0006_activity_fact_hlc.sql'
    `).Scan(&appliedCount); err != nil {
		t.Fatalf("count migration 0006 applications: %v", err)
	}
	if appliedCount != 1 {
		t.Fatalf("migration 0006 application count = %d, want exactly 1", appliedCount)
	}

	for _, column := range []string{"hlc_physical_ms", "hlc_counter"} {
		var dataType, nullable string
		if err := pool.QueryRow(ctx, `
            SELECT data_type, is_nullable
            FROM information_schema.columns
            WHERE table_schema = 'public'
              AND table_name = 'account_activity_facts'
              AND column_name = $1
        `, column).Scan(&dataType, &nullable); err != nil {
			t.Fatalf("read account_activity_facts.%s: %v", column, err)
		}
		if dataType != "bigint" || nullable != "NO" {
			t.Fatalf("account_activity_facts.%s = (%s, nullable=%s), want bigint NOT NULL", column, dataType, nullable)
		}
	}

	for _, constraint := range []string{
		"account_activity_facts_hlc_physical_ck",
		"account_activity_facts_hlc_counter_ck",
	} {
		var exists bool
		if err := pool.QueryRow(ctx, `
            SELECT EXISTS (
                SELECT 1
                FROM pg_constraint
                WHERE conrelid = 'public.account_activity_facts'::regclass
                  AND conname = $1
                  AND contype = 'c'
            )
        `, constraint).Scan(&exists); err != nil {
			t.Fatalf("check constraint %s: %v", constraint, err)
		}
		if !exists {
			t.Fatalf("required constraint %s missing", constraint)
		}
	}

	var accountID, deviceID string
	if _, err := pool.Exec(ctx, `
        INSERT INTO username_reservations(canonical_username, reserved_at)
        VALUES ('migration-0006-user', now())
    `); err != nil {
		t.Fatalf("insert username reservation: %v", err)
	}
	if err := pool.QueryRow(ctx, `
        INSERT INTO accounts(
            canonical_username, display_username, password_hash,
            recovery_key_verifier, created_at, updated_at
        ) VALUES ($1, $2, $3, decode('01', 'hex'), now(), now())
        RETURNING id::text
    `, "migration-0006-user", "Migration 0006", "test-hash").Scan(&accountID); err != nil {
		t.Fatalf("insert account: %v", err)
	}
	if err := pool.QueryRow(ctx, `
        INSERT INTO devices(
            account_id, install_id, label, platform, created_at, last_seen_at
        ) VALUES ($1::uuid, gen_random_uuid(), 'Device 000006', 'windows', now(), now())
        RETURNING id::text
    `, accountID).Scan(&deviceID); err != nil {
		t.Fatalf("insert device: %v", err)
	}

	insertFact := func(physical, counter int64) error {
		_, err := pool.Exec(ctx, `
            INSERT INTO account_activity_facts(
                account_id, event_id, mutation_id, origin_device_id,
                schema_version, event_type, payload_ciphertext,
                hlc_physical_ms, hlc_counter, received_at
            ) VALUES (
                $1::uuid, gen_random_uuid(), gen_random_uuid(), $2::uuid,
                1, 'playback_delta', decode('03', 'hex'), $3, $4, now()
            )
        `, accountID, deviceID, physical, counter)
		return err
	}

	if err := insertFact(123, 4); err != nil {
		t.Fatalf("insert valid activity HLC: %v", err)
	}
	if err := insertFact(-1, 0); err == nil {
		t.Fatal("negative hlc_physical_ms was accepted")
	}
	if err := insertFact(0, -1); err == nil {
		t.Fatal("negative hlc_counter was accepted")
	}
}

func TestMigration0005PortableProfileSync(t *testing.T) {
	pool := testdb.Open(t)
	testdb.ResetPublicSchema(t, pool)

	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()

	if err := RunMigrations(ctx, pool); err != nil {
		t.Fatalf("RunMigrations() first pass error = %v", err)
	}
	if err := RunMigrations(ctx, pool); err != nil {
		t.Fatalf("RunMigrations() second pass error = %v", err)
	}

	var appliedCount int
	if err := pool.QueryRow(ctx, `
        SELECT count(*)
        FROM schema_migrations
        WHERE name = '0005_profile_portable_sync.sql'
    `).Scan(&appliedCount); err != nil {
		t.Fatalf("count migration 0005 applications: %v", err)
	}
	if appliedCount != 1 {
		t.Fatalf("migration 0005 application count = %d, want exactly 1", appliedCount)
	}

	for _, table := range []string{
		"account_device_attachments",
		"account_activity_facts",
	} {
		var exists bool
		if err := pool.QueryRow(ctx,
			"SELECT to_regclass('public.' || $1) IS NOT NULL", table).Scan(&exists); err != nil {
			t.Fatalf("check table %s: %v", table, err)
		}
		if !exists {
			t.Fatalf("migration 0005 table %s was not created", table)
		}
	}

	var attachmentColumnExists bool
	if err := pool.QueryRow(ctx, `
        SELECT EXISTS (
            SELECT 1
            FROM information_schema.columns
            WHERE table_schema = 'public'
              AND table_name = 'account_sync_journal'
              AND column_name = 'attachment_id'
        )
    `).Scan(&attachmentColumnExists); err != nil {
		t.Fatalf("check account_sync_journal.attachment_id: %v", err)
	}
	if !attachmentColumnExists {
		t.Fatal("account_sync_journal.attachment_id was not created")
	}

	var sharedSequenceExists, oldSequenceGone bool
	if err := pool.QueryRow(ctx, `
        SELECT to_regclass('public.account_change_seq') IS NOT NULL,
               to_regclass('public.account_sync_journal_server_seq_seq') IS NULL
    `).Scan(&sharedSequenceExists, &oldSequenceGone); err != nil {
		t.Fatalf("check shared change sequence: %v", err)
	}
	if !sharedSequenceExists {
		t.Fatal("account_change_seq was not created by renaming the journal sequence")
	}
	if !oldSequenceGone {
		t.Fatal("old account_sync_journal_server_seq_seq remains authoritative")
	}

	constraintChecks := []struct {
		table string
		name  string
		kind  string
	}{
		{"account_device_attachments", "account_device_attachments_state_ck", "c"},
		{"account_device_attachments", "account_device_attachments_baseline_ck", "c"},
		{"account_activity_facts", "account_activity_facts_pkey", "p"},
		{"account_activity_facts", "account_activity_facts_mutation_uk", "u"},
		{"account_activity_facts", "account_activity_facts_schema_ck", "c"},
		{"account_activity_facts", "account_activity_facts_seq_uk", "u"},
	}
	for _, check := range constraintChecks {
		var exists bool
		if err := pool.QueryRow(ctx, `
            SELECT EXISTS (
                SELECT 1
                FROM pg_constraint
                WHERE conrelid = ('public.' || $1)::regclass
                  AND conname = $2
                  AND contype::text = $3
            )
        `, check.table, check.name, check.kind).Scan(&exists); err != nil {
			t.Fatalf("check constraint %s: %v", check.name, err)
		}
		if !exists {
			t.Fatalf("required constraint %s (%s) missing from %s", check.name, check.kind, check.table)
		}
	}

	indexChecks := []struct {
		table    string
		name     string
		snippets []string
	}{
		{"account_device_attachments", "account_device_attachments_open_idx", []string{"account_id, device_id, state"}},
		{"account_sync_journal", "account_sync_journal_attachment_idx", []string{"account_id, attachment_id, server_seq", "attachment_id IS NOT NULL"}},
		{"account_activity_facts", "account_activity_facts_pull_idx", []string{"account_id, server_seq"}},
	}
	for _, check := range indexChecks {
		var definition string
		if err := pool.QueryRow(ctx, `
            SELECT indexdef
            FROM pg_indexes
            WHERE schemaname = 'public'
              AND tablename = $1
              AND indexname = $2
        `, check.table, check.name).Scan(&definition); err != nil {
			t.Fatalf("read index %s: %v", check.name, err)
		}
		for _, snippet := range check.snippets {
			if !strings.Contains(definition, snippet) {
				t.Fatalf("index %s definition = %q, want fragment %q", check.name, definition, snippet)
			}
		}
	}

	var accountID, deviceID string
	if _, err := pool.Exec(ctx, `
        INSERT INTO username_reservations(canonical_username, reserved_at)
        VALUES ('migration-0005-user', now())
    `); err != nil {
		t.Fatalf("insert username reservation: %v", err)
	}

	if err := pool.QueryRow(ctx, `
        INSERT INTO accounts(
            canonical_username, display_username, password_hash,
            recovery_key_verifier, created_at, updated_at
        ) VALUES ($1, $2, $3, decode('01', 'hex'), now(), now())
        RETURNING id::text
    `, "migration-0005-user", "Migration 0005", "test-hash").Scan(&accountID); err != nil {
		t.Fatalf("insert account: %v", err)
	}
	if err := pool.QueryRow(ctx, `
        INSERT INTO devices(
            account_id, install_id, label, platform, created_at, last_seen_at
        ) VALUES ($1::uuid, gen_random_uuid(), 'Device 000005', 'windows', now(), now())
        RETURNING id::text
    `, accountID).Scan(&deviceID); err != nil {
		t.Fatalf("insert device: %v", err)
	}

	var attachmentID string
	if err := pool.QueryRow(ctx, `
        INSERT INTO account_device_attachments(
            id, account_id, device_id, source_kind, source_semantic_digest,
            baseline_server_seq, state, created_at, updated_at
        ) VALUES (
            gen_random_uuid(), $1::uuid, $2::uuid, 'local_only', 'digest-a',
            0, 'open', now(), now()
        )
        RETURNING id::text
    `, accountID, deviceID).Scan(&attachmentID); err != nil {
		t.Fatalf("insert attachment: %v", err)
	}

	var journalMutationID string
	if err := pool.QueryRow(ctx, `
        INSERT INTO account_sync_journal(
            account_id, mutation_id, device_id, category, record_key,
            schema_version, hlc_physical_ms, hlc_counter, operation,
            payload_ciphertext, won, received_at, attachment_id
        ) VALUES (
            $1::uuid, gen_random_uuid(), $2::uuid, 'collection', 'item-1',
            1, 1, 0, 'put', decode('02', 'hex'), true, now(), $3::uuid
        )
        RETURNING mutation_id::text
    `, accountID, deviceID, attachmentID).Scan(&journalMutationID); err != nil {
		t.Fatalf("insert attached journal row: %v", err)
	}

	if _, err := pool.Exec(ctx,
		"DELETE FROM account_device_attachments WHERE id = $1::uuid", attachmentID); err != nil {
		t.Fatalf("delete attachment: %v", err)
	}
	var journalAttachmentIsNull bool
	if err := pool.QueryRow(ctx, `
        SELECT attachment_id IS NULL
        FROM account_sync_journal
        WHERE account_id = $1::uuid AND mutation_id = $2::uuid
    `, accountID, journalMutationID).Scan(&journalAttachmentIsNull); err != nil {
		t.Fatalf("read journal after attachment delete: %v", err)
	}
	if !journalAttachmentIsNull {
		t.Fatal("deleting an attachment did not preserve journal history with attachment_id set to NULL")
	}

	fkChecks := []struct {
		table        string
		referenced   string
		deleteAction string
	}{
		{"account_device_attachments", "accounts", "c"},
		{"account_device_attachments", "devices", "c"},
		{"account_activity_facts", "accounts", "c"},
		{"account_activity_facts", "devices", "c"},
		{"account_sync_journal", "account_device_attachments", "n"},
	}
	for _, check := range fkChecks {
		var exists bool
		if err := pool.QueryRow(ctx, `
            SELECT EXISTS (
                SELECT 1
                FROM pg_constraint
                WHERE conrelid = ('public.' || $1)::regclass
                  AND confrelid = ('public.' || $2)::regclass
                  AND contype = 'f'
                  AND confdeltype::text = $3
            )
        `, check.table, check.referenced, check.deleteAction).Scan(&exists); err != nil {
			t.Fatalf("check FK %s -> %s: %v", check.table, check.referenced, err)
		}
		if !exists {
			t.Fatalf("required FK delete action %s -> %s (%s) missing", check.table, check.referenced, check.deleteAction)
		}
	}

	if _, err := pool.Exec(ctx, `
        INSERT INTO account_device_attachments(
            id, account_id, device_id, source_kind, source_semantic_digest,
            baseline_server_seq, state, created_at, updated_at
        ) VALUES (
            gen_random_uuid(), $1::uuid, $2::uuid, 'legacy_local', 'digest-b',
            0, 'uploaded', now(), now()
        )
    `, accountID, deviceID); err != nil {
		t.Fatalf("insert cascade attachment: %v", err)
	}

	if _, err := pool.Exec(ctx, `
        INSERT INTO account_activity_facts(
            account_id, event_id, mutation_id, origin_device_id,
            schema_version, event_type, payload_ciphertext,
            hlc_physical_ms, hlc_counter, received_at
        ) VALUES (
            $1::uuid, gen_random_uuid(), gen_random_uuid(), $2::uuid,
            1, 'playback_delta', decode('03', 'hex'), 1, 0, now()
        )
    `, accountID, deviceID); err != nil {
		t.Fatalf("insert activity fact: %v", err)
	}

	if _, err := pool.Exec(ctx, "DELETE FROM accounts WHERE id = $1::uuid", accountID); err != nil {
		t.Fatalf("delete account: %v", err)
	}

	for _, table := range []string{"account_device_attachments", "account_activity_facts"} {
		var count int
		if err := pool.QueryRow(ctx,
			"SELECT count(*) FROM "+table+" WHERE account_id = $1::uuid", accountID).Scan(&count); err != nil {
			t.Fatalf("count %s after account delete: %v", table, err)
		}
		if count != 0 {
			t.Fatalf("%s rows after account delete = %d, want 0", table, count)
		}
	}
}
