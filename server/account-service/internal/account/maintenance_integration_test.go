package account

import (
	"context"
	"testing"
	"time"
)

func TestMaintenanceRunOnceCommitsBoundedRetentionBatches(t *testing.T) {
	fixture := newServiceFixture(t)
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	created := createFixtureAccount(t, fixture, "MaintenanceBatchOwner")
	now := fixture.clock.Now().UTC()
	rateCutoff := now.Add(-48 * time.Hour)
	versionCutoff := now.Add(-30 * 24 * time.Hour)

	for index := 0; index < 3; index++ {
		if _, err := fixture.pool.Exec(ctx, `
            INSERT INTO auth_rate_events(event_type, key_hash, occurred_at)
            VALUES($1, $2, $3)
        `, "maintenance-batch-test", []byte{byte(index + 1)}, rateCutoff.Add(-time.Hour)); err != nil {
			t.Fatalf("insert expired rate event %d: %v", index, err)
		}
		if _, err := fixture.pool.Exec(ctx, `
            INSERT INTO account_sync_versions(
                account_id, category, record_key, mutation_id, device_id,
                schema_version, hlc_physical_ms, hlc_counter, operation,
                payload_ciphertext, server_seq, replaced_at, replacing_mutation_id
            )
            VALUES($1, 'history', $2, gen_random_uuid(), $3, 1, 0, 0,
                'delete', NULL, $4, $5, gen_random_uuid())
        `, created.Session.Account.ID, "maintenance/"+string(rune('a'+index)),
			created.Session.Device.ID, int64(100+index), versionCutoff.Add(-time.Hour)); err != nil {
			t.Fatalf("insert expired sync version %d: %v", index, err)
		}
	}

	maintenance, err := NewMaintenance(MaintenanceDependencies{
		Pool:        fixture.pool,
		AvatarStore: fixture.avatars,
		Clock:       fixture.clock,
	})
	if err != nil {
		t.Fatalf("NewMaintenance() error = %v", err)
	}
	options := MaintenanceOptions{
		AvatarCleanupLimit: 1,
		BatchSize:          2,
		RateEventsBefore:   rateCutoff,
		SyncVersionsBefore: versionCutoff,
	}
	if err := maintenance.RunOnce(ctx, options); err != nil {
		t.Fatalf("RunOnce() error = %v", err)
	}

	var remainingRate, remainingVersions int
	if err := fixture.pool.QueryRow(ctx, `
        SELECT count(*) FROM auth_rate_events
        WHERE event_type = 'maintenance-batch-test'
    `).Scan(&remainingRate); err != nil {
		t.Fatalf("count remaining rate events: %v", err)
	}
	if err := fixture.pool.QueryRow(ctx, `
        SELECT count(*) FROM account_sync_versions
        WHERE account_id = $1::uuid AND category = 'history'
          AND record_key LIKE 'maintenance/%'
    `, created.Session.Account.ID).Scan(&remainingVersions); err != nil {
		t.Fatalf("count remaining sync versions: %v", err)
	}
	if remainingRate != 1 {
		t.Fatalf("remaining rate events = %d, want 1 after one batch of 2", remainingRate)
	}
	if remainingVersions != 1 {
		t.Fatalf("remaining sync versions = %d, want 1 after one batch of 2", remainingVersions)
	}

	if err := maintenance.RunOnce(ctx, options); err != nil {
		t.Fatalf("RunOnce() second pass error = %v", err)
	}
	if err := fixture.pool.QueryRow(ctx, `
        SELECT count(*) FROM auth_rate_events
        WHERE event_type = 'maintenance-batch-test'
    `).Scan(&remainingRate); err != nil {
		t.Fatalf("count rate events after second pass: %v", err)
	}
	if err := fixture.pool.QueryRow(ctx, `
        SELECT count(*) FROM account_sync_versions
        WHERE account_id = $1::uuid AND category = 'history'
          AND record_key LIKE 'maintenance/%'
    `, created.Session.Account.ID).Scan(&remainingVersions); err != nil {
		t.Fatalf("count sync versions after second pass: %v", err)
	}
	if remainingRate != 0 || remainingVersions != 0 {
		t.Fatalf("second maintenance pass left rate events=%d sync versions=%d", remainingRate, remainingVersions)
	}
}

func TestMaintenanceRunOnceBoundsSecurityPruneBatch(t *testing.T) {
	fixture := newServiceFixture(t)
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	created := createFixtureAccount(t, fixture, "MaintenanceSecurityOwner")
	now := fixture.clock.Now().UTC()
	old := now.Add(-48 * time.Hour)

	for index := 0; index < 3; index++ {
		if _, err := fixture.pool.Exec(ctx, `
            INSERT INTO trusted_recovery_challenges(
                account_id, target_install_id, target_label, target_platform,
                challenge_token_hash, new_password_hash, state, expires_at,
                created_at, decided_at, consumed_at,
                recovery_retry_ciphertext, recovery_retry_expires_at
            )
            VALUES($1, gen_random_uuid(), 'maintenance', 'test', $2,
                '', 'denied', $3, $3, $3, NULL, NULL, NULL)
        `, created.Session.Account.ID, []byte{byte(index + 11)}, old); err != nil {
			t.Fatalf("insert expired recovery challenge %d: %v", index, err)
		}
	}

	maintenance, err := NewMaintenance(MaintenanceDependencies{
		Pool:        fixture.pool,
		AvatarStore: fixture.avatars,
		Clock:       fixture.clock,
	})
	if err != nil {
		t.Fatalf("NewMaintenance() error = %v", err)
	}
	if err := maintenance.RunOnce(ctx, MaintenanceOptions{BatchSize: 2}); err != nil {
		t.Fatalf("RunOnce() error = %v", err)
	}

	var remaining int
	if err := fixture.pool.QueryRow(ctx, `
        SELECT count(*) FROM trusted_recovery_challenges
        WHERE account_id = $1::uuid AND state = 'denied'
    `, created.Session.Account.ID).Scan(&remaining); err != nil {
		t.Fatalf("count remaining recovery challenges: %v", err)
	}
	if remaining != 1 {
		t.Fatalf("remaining recovery challenges = %d, want 1 after one batch of 2", remaining)
	}
}

func TestMaintenanceRunOnceBoundsLifecycleCascadePruning(t *testing.T) {
	fixture := newServiceFixture(t)
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	created := createFixtureAccount(t, fixture, "MaintLifecycle")
	now := fixture.clock.Now().UTC()
	createdAt := now.Add(-2 * time.Hour)
	expiresAt := now.Add(-time.Hour)

	var snapshotID string
	if err := fixture.pool.QueryRow(ctx, `
        INSERT INTO account_export_snapshots(
            account_id, highwater_server_seq, format_version, created_at, expires_at
        )
        VALUES($1::uuid, 0, 1, $2, $3)
        RETURNING id::text
    `, created.Session.Account.ID, createdAt, expiresAt).Scan(&snapshotID); err != nil {
		t.Fatalf("insert expired export snapshot: %v", err)
	}
	if _, err := fixture.pool.Exec(ctx, `
        INSERT INTO account_export_items(
            snapshot_id, item_index, kind, category, record_key, payload_ciphertext
        )
        SELECT $1::uuid, item_index, 'account_metadata', 'profile',
               'maintenance/' || item_index::text, decode('01', 'hex')
        FROM generate_series(0, 2) AS item_index
    `, snapshotID); err != nil {
		t.Fatalf("insert expired export items: %v", err)
	}
	if _, err := fixture.pool.Exec(ctx, `
        INSERT INTO account_deletion_receipts(
            request_id, capability_hash, account_id, created_at, expires_at, completed_at
        )
        SELECT gen_random_uuid(), decode(lpad(to_hex(receipt_no), 64, '0'), 'hex'),
               $1::uuid, $2, $3, $2
        FROM generate_series(1, 3) AS receipt_no
    `, created.Session.Account.ID, createdAt, expiresAt); err != nil {
		t.Fatalf("insert expired deletion receipts: %v", err)
	}

	maintenance, err := NewMaintenance(MaintenanceDependencies{
		Pool:  fixture.pool,
		Clock: fixture.clock,
	})
	if err != nil {
		t.Fatalf("NewMaintenance() error = %v", err)
	}
	options := MaintenanceOptions{BatchSize: 1}
	if err := maintenance.RunOnce(ctx, options); err != nil {
		t.Fatalf("RunOnce() first lifecycle pass error = %v", err)
	}

	var remainingSnapshots, remainingItems, remainingReceipts int
	if err := fixture.pool.QueryRow(ctx, `
        SELECT
            (SELECT count(*) FROM account_export_snapshots WHERE id = $1::uuid),
            (SELECT count(*) FROM account_export_items WHERE snapshot_id = $1::uuid),
            (SELECT count(*) FROM account_deletion_receipts WHERE account_id = $2::uuid)
    `, snapshotID, created.Session.Account.ID).Scan(
		&remainingSnapshots, &remainingItems, &remainingReceipts); err != nil {
		t.Fatalf("count lifecycle backlog after first pass: %v", err)
	}
	if remainingSnapshots != 1 || remainingItems != 2 || remainingReceipts != 2 {
		t.Fatalf("first lifecycle pass left snapshots=%d items=%d receipts=%d, want 1/2/2",
			remainingSnapshots, remainingItems, remainingReceipts)
	}

	if err := maintenance.RunOnce(ctx, options); err != nil {
		t.Fatalf("RunOnce() second lifecycle pass error = %v", err)
	}
	if err := fixture.pool.QueryRow(ctx, `
        SELECT
            (SELECT count(*) FROM account_export_snapshots WHERE id = $1::uuid),
            (SELECT count(*) FROM account_export_items WHERE snapshot_id = $1::uuid),
            (SELECT count(*) FROM account_deletion_receipts WHERE account_id = $2::uuid)
    `, snapshotID, created.Session.Account.ID).Scan(
		&remainingSnapshots, &remainingItems, &remainingReceipts); err != nil {
		t.Fatalf("count lifecycle backlog after second pass: %v", err)
	}
	if remainingSnapshots != 1 || remainingItems != 1 || remainingReceipts != 1 {
		t.Fatalf("second lifecycle pass left snapshots=%d items=%d receipts=%d, want 1/1/1",
			remainingSnapshots, remainingItems, remainingReceipts)
	}

	if err := maintenance.RunOnce(ctx, options); err != nil {
		t.Fatalf("RunOnce() final lifecycle pass error = %v", err)
	}
	if err := fixture.pool.QueryRow(ctx, `
        SELECT
            (SELECT count(*) FROM account_export_snapshots WHERE id = $1::uuid),
            (SELECT count(*) FROM account_export_items WHERE snapshot_id = $1::uuid),
            (SELECT count(*) FROM account_deletion_receipts WHERE account_id = $2::uuid)
    `, snapshotID, created.Session.Account.ID).Scan(
		&remainingSnapshots, &remainingItems, &remainingReceipts); err != nil {
		t.Fatalf("count lifecycle backlog after final pass: %v", err)
	}
	if remainingSnapshots != 0 || remainingItems != 0 || remainingReceipts != 0 {
		t.Fatalf("final lifecycle pass left snapshots=%d items=%d receipts=%d, want 0/0/0",
			remainingSnapshots, remainingItems, remainingReceipts)
	}
}
