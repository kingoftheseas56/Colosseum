package account

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"errors"
	"strings"
	"sync"
	"testing"
	"time"
)

func lifecycleCapability(seed byte) string {
	raw := make([]byte, 32)
	for index := range raw {
		raw[index] = seed + byte(index)
	}
	return base64.RawURLEncoding.EncodeToString(raw)
}

func lifecycleRequestID(seed string) string {
	return seed
}

func TestExportAccountIsStableAcrossPagesAndLiveMutations(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "LifecycleExport")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	for index, value := range []string{"one", "two", "three"} {
		mutation := fixtureSyncMutation(
			"a"+strings.Repeat("0", 7)+"-0000-4000-8000-00000000000"+string(rune('1'+index)),
			auth.Device.ID,
			"theatre/title-"+string(rune('a'+index)),
			value,
			fixture.clock.Now().UnixMilli()+int64(index),
			0)
		if _, err := fixture.service.PushSync(context.Background(), auth, []SyncMutationInput{mutation}); err != nil {
			t.Fatalf("PushSync(%d) error = %v", index, err)
		}
	}

	first, err := fixture.service.ExportAccount(context.Background(), auth, ExportAccountInput{Limit: 2})
	if err != nil {
		t.Fatalf("ExportAccount(first) error = %v", err)
	}
	if first.SnapshotID == "" || first.Cursor == "" || !first.HasMore {
		t.Fatalf("first export page = %+v, want stable snapshot cursor and more pages", first)
	}
	if len(first.Items) != 2 {
		t.Fatalf("first export page items = %d, want 2", len(first.Items))
	}
	restarted, err := fixture.service.ExportAccount(context.Background(), auth, ExportAccountInput{Limit: 2})
	if err != nil {
		t.Fatalf("ExportAccount(restart) error = %v", err)
	}
	if restarted.SnapshotID != first.SnapshotID ||
		string(mustJSON(t, restarted.Items)) != string(mustJSON(t, first.Items)) {
		t.Fatalf("restart page = %+v, want active snapshot reuse %+v", restarted, first)
	}
	var snapshotCount int
	if err := fixture.pool.QueryRow(context.Background(),
		"SELECT count(*) FROM account_export_snapshots WHERE account_id = $1::uuid",
		created.Session.Account.ID).Scan(&snapshotCount); err != nil {
		t.Fatalf("count active export snapshots: %v", err)
	}
	if snapshotCount != 1 {
		t.Fatalf("active export snapshots = %d, want 1", snapshotCount)
	}
	for _, item := range first.Items {
		if strings.Contains(strings.ToLower(string(item.Payload)), "password") ||
			strings.Contains(strings.ToLower(string(item.Payload)), "token") ||
			strings.Contains(strings.ToLower(string(item.Payload)), "ciphertext") {
			t.Fatalf("portable export item contains forbidden credential material: %+v", item)
		}
	}

	newMutation := fixtureSyncMutation(
		"b"+strings.Repeat("0", 7)+"-0000-4000-8000-000000000001",
		auth.Device.ID,
		"theatre/title-z",
		"after-snapshot",
		fixture.clock.Now().UnixMilli()+100,
		0)
	if _, err := fixture.service.PushSync(context.Background(), auth, []SyncMutationInput{newMutation}); err != nil {
		t.Fatalf("PushSync(after snapshot) error = %v", err)
	}

	retryFirst, err := fixture.service.ExportAccount(context.Background(), auth, ExportAccountInput{
		Cursor: first.Cursor,
		Limit:  2,
	})
	if err != nil {
		t.Fatalf("ExportAccount(retry first page) error = %v", err)
	}
	if retryFirst.SnapshotID != first.SnapshotID ||
		string(mustJSON(t, retryFirst.Items)) != string(mustJSON(t, first.Items)) {
		t.Fatalf("retry page = %+v, want identical first page %+v", retryFirst, first)
	}

	seen := append([]ExportItem(nil), first.Items...)
	cursor := first.NextCursor
	for cursor != "" {
		page, err := fixture.service.ExportAccount(context.Background(), auth, ExportAccountInput{
			Cursor: cursor,
			Limit:  2,
		})
		if err != nil {
			t.Fatalf("ExportAccount(next page) error = %v", err)
		}
		seen = append(seen, page.Items...)
		cursor = page.NextCursor
	}
	if len(seen) != 4 {
		t.Fatalf("export item count = %d, want metadata plus three records", len(seen))
	}

	snapshotPart, sealedPart, found := strings.Cut(first.Cursor, ".")
	if !found {
		t.Fatal("export cursor has no snapshot prefix")
	}
	replacement := "0"
	if snapshotPart[0] == '0' {
		replacement = "1"
	}
	tampered := replacement + snapshotPart[1:] + "." + sealedPart
	if _, err := fixture.service.ExportAccount(context.Background(), auth, ExportAccountInput{Cursor: tampered, Limit: 2}); !errors.Is(err, ErrExportCursorInvalid) {
		t.Fatalf("tampered export cursor error = %v, want ErrExportCursorInvalid", err)
	}

	fixture.clock.Advance(2 * time.Minute)
	if _, err := fixture.pool.Exec(context.Background(),
		"UPDATE account_export_snapshots SET expires_at = $2 WHERE id = $1::uuid",
		first.SnapshotID, fixture.clock.Now().Add(-time.Minute)); err != nil {
		t.Fatalf("expire export snapshot: %v", err)
	}
	if _, err := fixture.service.ExportAccount(context.Background(), auth, ExportAccountInput{Cursor: first.Cursor, Limit: 2}); !errors.Is(err, ErrExportSnapshotExpired) {
		t.Fatalf("expired export cursor error = %v, want ErrExportSnapshotExpired", err)
	}
}

func TestExportCursorCannotCrossAccount(t *testing.T) {
	fixture := newServiceFixture(t)
	a := createFixtureAccount(t, fixture, "LifecycleExportA")
	b := createFixtureAccount(t, fixture, "LifecycleExportB")
	authA := authenticateFixtureSession(t, fixture, a.Session)
	authB := authenticateFixtureSession(t, fixture, b.Session)

	page, err := fixture.service.ExportAccount(context.Background(), authA, ExportAccountInput{Limit: 10})
	if err != nil {
		t.Fatalf("ExportAccount(A) error = %v", err)
	}
	if _, err := fixture.service.ExportAccount(context.Background(), authB, ExportAccountInput{Cursor: page.Cursor, Limit: 10}); !errors.Is(err, ErrExportCursorInvalid) {
		t.Fatalf("cross-account export cursor error = %v, want generic invalid cursor", err)
	}
}

func TestExportFailsClosedOnInvalidCurrentPayload(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "LifecycleExportCorrupt")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	if _, err := fixture.pool.Exec(context.Background(), `
        INSERT INTO account_sync_current(
            account_id, category, record_key, mutation_id, device_id,
            schema_version, hlc_physical_ms, hlc_counter, operation,
            payload_ciphertext, server_seq, updated_at
        )
        VALUES(
            $1::uuid, 'collection', $2,
            'cccccccc-cccc-4ccc-8ccc-cccccccccccc'::uuid, $3::uuid,
            1, 1, 0, 'put', decode('deadbeef', 'hex'), 77, $4
        )`,
		created.Session.Account.ID,
		fixtureCollectionRecordKey("theatre/corrupt"),
		created.Session.Device.ID,
		fixture.clock.Now()); err != nil {
		t.Fatalf("seed corrupt current payload: %v", err)
	}
	if _, err := fixture.service.ExportAccount(context.Background(), auth, ExportAccountInput{Limit: 10}); !errors.Is(err, ErrExportIncomplete) {
		t.Fatalf("corrupt export error = %v, want ErrExportIncomplete", err)
	}
}

func TestExportFailsClosedOnOwnerInvalidCurrentPayload(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "OwnerShape")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	recordKey := fixtureCollectionRecordKey("theatre/owner-shape")
	ciphertext, err := fixture.service.syncCipher.Seal(
		auth.Account.ID,
		"collection",
		recordKey,
		[]byte(`{"world":"theatre","id":"different","value":"valid-json"}`))
	if err != nil {
		t.Fatalf("encrypt owner-invalid payload: %v", err)
	}
	if _, err := fixture.pool.Exec(context.Background(), `
        INSERT INTO account_sync_current(
            account_id, category, record_key, mutation_id, device_id,
            schema_version, hlc_physical_ms, hlc_counter, operation,
            payload_ciphertext, server_seq, updated_at
        )
        VALUES(
            $1::uuid, 'collection', $2,
            '12121212-1212-4121-8121-121212121212'::uuid, $3::uuid,
            1, 1, 0, 'put', $4, 77, $5
        )`,
		created.Session.Account.ID,
		recordKey,
		created.Session.Device.ID,
		ciphertext,
		fixture.clock.Now()); err != nil {
		t.Fatalf("seed owner-invalid current payload: %v", err)
	}
	if _, err := fixture.service.ExportAccount(context.Background(), auth, ExportAccountInput{Limit: 10}); !errors.Is(err, ErrExportIncomplete) {
		t.Fatalf("owner-invalid export error = %v, want ErrExportIncomplete", err)
	}
}

func TestExportHighWaterIncludesCommittedTombstone(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "LifecycleExportTombstone")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	key := "theatre/tombstone"
	put := fixtureSyncMutation(
		"dddddddd-dddd-4ddd-8ddd-dddddddddddd",
		auth.Device.ID,
		key,
		"present",
		fixture.clock.Now().UnixMilli(),
		0)
	if response, err := fixture.service.PushSync(context.Background(), auth, []SyncMutationInput{put}); err != nil ||
		len(response.Results) != 1 || !response.Results[0].Accepted {
		t.Fatalf("PushSync(put) response=%+v error=%v", response.Results, err)
	}
	deleteMutation := fixtureSyncMutation(
		"eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee",
		auth.Device.ID,
		key,
		"",
		fixture.clock.Now().UnixMilli()+1,
		0)
	deleteResponse, err := fixture.service.PushSync(context.Background(), auth, []SyncMutationInput{deleteMutation})
	if err != nil || len(deleteResponse.Results) != 1 || !deleteResponse.Results[0].Accepted {
		t.Fatalf("PushSync(delete) response=%+v error=%v", deleteResponse.Results, err)
	}
	page, err := fixture.service.ExportAccount(context.Background(), auth, ExportAccountInput{Limit: 10})
	if err != nil {
		t.Fatalf("ExportAccount(tombstone) error = %v", err)
	}
	if page.HighWaterServerSeq < deleteResponse.Results[0].ServerSeq {
		t.Fatalf("export high-water = %d, want tombstone server seq >= %d", page.HighWaterServerSeq, deleteResponse.Results[0].ServerSeq)
	}
	for _, item := range page.Items {
		if item.Category == "collection" && item.Key == deleteMutation.RecordKey {
			t.Fatalf("tombstoned collection key leaked into canonical current export: %+v", item)
		}
	}
}

func TestExportOmitsHistoryClearedByActivityReset(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "LifecycleExportActivityReset")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	now := fixture.clock.Now().UnixMilli()

	history := historySemanticMutation(
		"f1000000-0000-4000-8000-000000000001",
		auth.Device.ID,
		now,
		0,
		`{"kind":"episode","id":"show-1/e1","firstActivityAt":1000,"lastActivityAt":2000}`)
	if result := pushOneSyncMutation(t, fixture, auth, history); !result.Accepted || !result.Won {
		t.Fatalf("History push = %+v", result)
	}
	reset := activityResetSemanticMutation(
		"f1000000-0000-4000-8000-000000000002",
		auth.Device.ID,
		1,
		now,
		now+1,
		0)
	if result := pushOneActivity(t, fixture, auth, reset); !result.Accepted || !result.Won {
		t.Fatalf("Activity reset push = %+v", result)
	}

	page, err := fixture.service.ExportAccount(
		context.Background(),
		auth,
		ExportAccountInput{Limit: 100})
	if err != nil {
		t.Fatalf("ExportAccount after Activity reset = %v", err)
	}
	for _, item := range page.Items {
		if item.Category == "full_history" {
			t.Fatalf("cleared History leaked into canonical export: %+v", item)
		}
	}
}

func TestExportSnapshotKeepsMetadataStableDuringConcurrentProfileChange(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "LifecycleExportProfile")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	if _, err := fixture.service.SetBuiltinAvatar(
		context.Background(), auth, "portrait"); err != nil {
		t.Fatalf("seed profile metadata: %v", err)
	}

	// Pause the exporter after it has read account metadata and reached the
	// snapshot insert. The profile update must remain pending while the export
	// transaction owns its account share lock, then commit after the snapshot is
	// complete. This trigger is test-only and lives in the disposable database
	// reset by each fixture.
	const pauseLockKey int64 = 7046029254386353131
	lockConn, err := fixture.pool.Acquire(context.Background())
	if err != nil {
		t.Fatalf("acquire export pause connection: %v", err)
	}
	defer lockConn.Release()
	pauseCtx, pauseCancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer pauseCancel()
	if _, err := lockConn.Exec(pauseCtx, `
        CREATE OR REPLACE FUNCTION lifecycle_test_pause_export()
        RETURNS trigger
        LANGUAGE plpgsql
        AS $$
        BEGIN
            PERFORM pg_advisory_xact_lock(7046029254386353131);
            RETURN NEW;
        END
        $$`); err != nil {
		t.Fatalf("create export pause trigger function: %v", err)
	}
	if _, err := lockConn.Exec(pauseCtx, `
        DROP TRIGGER IF EXISTS lifecycle_test_pause_export_trigger
            ON account_export_snapshots;
        CREATE TRIGGER lifecycle_test_pause_export_trigger
        BEFORE INSERT ON account_export_snapshots
        FOR EACH ROW EXECUTE FUNCTION lifecycle_test_pause_export()`); err != nil {
		t.Fatalf("create export pause trigger: %v", err)
	}
	if _, err := lockConn.Exec(pauseCtx,
		"SELECT pg_advisory_lock($1)", pauseLockKey); err != nil {
		t.Fatalf("hold export pause lock: %v", err)
	}
	defer func() {
		_, _ = lockConn.Exec(context.Background(),
			"SELECT pg_advisory_unlock($1)", pauseLockKey)
	}()

	exportDone := make(chan struct {
		page ExportPage
		err  error
	}, 1)
	go func() {
		page, err := fixture.service.ExportAccount(
			context.Background(), auth, ExportAccountInput{Limit: 10})
		exportDone <- struct {
			page ExportPage
			err  error
		}{page: page, err: err}
	}()
	paused := false
	deadline := time.Now().Add(5 * time.Second)
	for !paused && time.Now().Before(deadline) {
		var waiting bool
		if err := fixture.pool.QueryRow(context.Background(), `
            SELECT EXISTS(
                SELECT 1
                FROM pg_stat_activity
                WHERE wait_event_type = 'Lock'
                  AND wait_event = 'advisory'
                  AND query ILIKE '%account_export_snapshots%'
            )`).Scan(&waiting); err != nil {
			_, _ = lockConn.Exec(context.Background(),
				"SELECT pg_advisory_unlock($1)", pauseLockKey)
			t.Fatalf("observe export pause: %v", err)
		}
		paused = waiting
		if paused {
			break
		}
		select {
		case result := <-exportDone:
			_, _ = lockConn.Exec(context.Background(),
				"SELECT pg_advisory_unlock($1)", pauseLockKey)
			t.Fatalf("export finished before pause: %v", result.err)
		case <-time.After(10 * time.Millisecond):
		}
	}
	if !paused {
		_, _ = lockConn.Exec(context.Background(),
			"SELECT pg_advisory_unlock($1)", pauseLockKey)
		t.Fatal("export did not reach the paused materialization statement")
	}

	profileDone := make(chan error, 1)
	go func() {
		_, err := fixture.service.SetBuiltinAvatar(
			context.Background(), auth, "landscape")
		profileDone <- err
	}()
	select {
	case err := <-profileDone:
		if err != nil {
			t.Fatalf("concurrent profile change: %v", err)
		}
		t.Fatal("profile change completed while export snapshot was paused")
	case <-time.After(100 * time.Millisecond):
	}

	if _, err := lockConn.Exec(pauseCtx,
		"SELECT pg_advisory_unlock($1)", pauseLockKey); err != nil {
		t.Fatalf("release export pause lock: %v", err)
	}
	var result struct {
		page ExportPage
		err  error
	}
	select {
	case result = <-exportDone:
	case <-time.After(5 * time.Second):
		t.Fatal("export did not finish after profile change")
	}
	if result.err != nil {
		t.Fatalf("ExportAccount() error = %v", result.err)
	}
	select {
	case err := <-profileDone:
		if err != nil {
			t.Fatalf("concurrent profile change after export: %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("profile change did not finish after export snapshot commit")
	}

	var metadata map[string]any
	for _, item := range result.page.Items {
		if item.Kind == "account_metadata" {
			if err := json.Unmarshal(item.Payload, &metadata); err != nil {
				t.Fatalf("decode exported account metadata: %v", err)
			}
			break
		}
	}
	if metadata == nil {
		t.Fatal("export omitted account metadata")
	}
	if metadata["builtin_avatar_id"] != "portrait" {
		t.Fatalf("export metadata = %v, want pre-change portrait metadata", metadata)
	}
}

func TestDeleteAccountRequiresCurrentPassword(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "LifecycleDeletePassword")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	input := DeleteAccountInput{
		RequestID:       "ffffffff-ffff-4fff-8fff-ffffffffffff",
		RetryCapability: lifecycleCapability(0x70),
		CurrentPassword: "wrong password",
	}
	if _, err := fixture.service.DeleteAccount(context.Background(), auth, input); !errors.Is(err, ErrInvalidCredentials) {
		t.Fatalf("wrong deletion password error = %v, want ErrInvalidCredentials", err)
	}
	var accountCount int
	if err := fixture.pool.QueryRow(context.Background(),
		"SELECT count(*) FROM accounts WHERE id = $1::uuid", created.Session.Account.ID).Scan(&accountCount); err != nil {
		t.Fatalf("read account after rejected deletion: %v", err)
	}
	if accountCount != 1 {
		t.Fatal("wrong deletion password removed the account")
	}
	input.CurrentPassword = testPassword
	if _, err := fixture.service.DeleteAccount(context.Background(), auth, input); err != nil {
		t.Fatalf("correct deletion password error = %v", err)
	}
}

func TestDeleteAccountUsesCapabilityForResponseLossRetryAndCascade(t *testing.T) {
	fixture := newServiceFixture(t)
	createdA := createFixtureAccount(t, fixture, "LifecycleDeleteA")
	createdB := createFixtureAccount(t, fixture, "LifecycleDeleteB")
	authA := authenticateFixtureSession(t, fixture, createdA.Session)
	authB := authenticateFixtureSession(t, fixture, createdB.Session)

	capability := lifecycleCapability(0x40)
	requestID := lifecycleRequestID("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa")
	if _, err := fixture.service.DeleteAccount(context.Background(), authA, DeleteAccountInput{
		RequestID:       requestID,
		RetryCapability: "too-short",
	}); !errors.Is(err, ErrDeletionRetryInvalid) {
		t.Fatalf("invalid capability error = %v, want ErrDeletionRetryInvalid", err)
	}

	const objectKey = "avatars/11111111-1111-4111-8111-111111111111/deleted.png"
	fixture.avatars.objects[objectKey] = []byte("fixture")
	if _, err := fixture.pool.Exec(context.Background(),
		"UPDATE accounts SET uploaded_avatar_object_key = $2 WHERE id = $1::uuid",
		createdA.Session.Account.ID, objectKey); err != nil {
		t.Fatalf("seed avatar reference: %v", err)
	}

	deleted, err := fixture.service.DeleteAccount(context.Background(), authA, DeleteAccountInput{
		RequestID:       requestID,
		RetryCapability: capability,
		CurrentPassword: testPassword,
	})
	if err != nil {
		t.Fatalf("DeleteAccount() error = %v", err)
	}
	if deleted.Status != "completed" || deleted.ReceiptExpiresAt.IsZero() {
		t.Fatalf("DeleteAccount() result = %+v, want completed receipt", deleted)
	}

	var accountCount, deviceCount, sessionCount, queueCount, reservationCount int
	if err := fixture.pool.QueryRow(context.Background(),
		`SELECT
			(SELECT count(*) FROM accounts WHERE id = $1::uuid),
			(SELECT count(*) FROM devices WHERE account_id = $1::uuid),
			(SELECT count(*) FROM sessions WHERE account_id = $1::uuid),
			(SELECT count(*) FROM avatar_cleanup_queue WHERE object_key = $2),
			(SELECT count(*) FROM username_reservations WHERE canonical_username = $3)`,
		createdA.Session.Account.ID, objectKey, "lifecycledeletea").Scan(
		&accountCount, &deviceCount, &sessionCount, &queueCount, &reservationCount); err != nil {
		t.Fatalf("read deleted account cascade: %v", err)
	}
	if accountCount != 0 || deviceCount != 0 || sessionCount != 0 || queueCount != 1 || reservationCount != 1 {
		t.Fatalf("deleted account state = account %d device %d session %d queue %d reservation %d", accountCount, deviceCount, sessionCount, queueCount, reservationCount)
	}
	var reservedAccountID *string
	if err := fixture.pool.QueryRow(context.Background(),
		"SELECT reserved_account_id::text FROM username_reservations WHERE canonical_username = $1",
		"lifecycledeletea").Scan(&reservedAccountID); err != nil {
		t.Fatalf("read username reservation: %v", err)
	}
	if reservedAccountID != nil {
		t.Fatalf("reserved account id = %q, want cleared historical reservation marker", *reservedAccountID)
	}

	retried, err := fixture.service.RetryDeleteAccount(context.Background(), DeleteAccountRetryInput{
		RequestID:       requestID,
		RetryCapability: capability,
	})
	if err != nil {
		t.Fatalf("RetryDeleteAccount() error = %v, want idempotent completion", err)
	}
	if retried.Status != "completed" || !retried.Retried {
		t.Fatalf("RetryDeleteAccount() result = %+v", retried)
	}

	if _, err := fixture.service.RetryDeleteAccount(context.Background(), DeleteAccountRetryInput{
		RequestID:       requestID,
		RetryCapability: lifecycleCapability(0x41),
	}); !errors.Is(err, ErrDeletionRetryInvalid) {
		t.Fatalf("wrong capability retry error = %v, want generic invalid retry", err)
	}
	if _, err := fixture.service.DeleteAccount(context.Background(), authB, DeleteAccountInput{
		RequestID:       requestID,
		RetryCapability: capability,
		CurrentPassword: testPassword,
	}); !errors.Is(err, ErrDeletionRetryInvalid) {
		t.Fatalf("cross-account request reuse error = %v, want generic invalid retry", err)
	}
	if _, err := fixture.service.GetProfile(context.Background(), authB); err != nil {
		t.Fatalf("unrelated account was affected by deletion: %v", err)
	}
}

func TestDeleteAccountWaitsForSyncAccountLockBeforeAccountRow(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "LifecycleLock")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	lockConn, err := fixture.pool.Acquire(context.Background())
	if err != nil {
		t.Fatalf("acquire lock connection: %v", err)
	}
	lockTx, err := lockConn.Begin(context.Background())
	if err != nil {
		lockConn.Release()
		t.Fatalf("begin lock transaction: %v", err)
	}
	if err := lockAccountSyncTx(context.Background(), lockTx, auth.Account.ID); err != nil {
		_ = lockTx.Rollback(context.Background())
		lockConn.Release()
		t.Fatalf("hold account sync lock: %v", err)
	}

	done := make(chan error, 1)
	go func() {
		_, err := fixture.service.DeleteAccount(context.Background(), auth, DeleteAccountInput{
			RequestID:       "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
			RetryCapability: lifecycleCapability(0x50),
			CurrentPassword: testPassword,
		})
		done <- err
	}()

	select {
	case err := <-done:
		t.Fatalf("DeleteAccount() completed while sync writer lock held: %v", err)
	case <-time.After(100 * time.Millisecond):
	}
	if err := lockTx.Commit(context.Background()); err != nil {
		lockConn.Release()
		t.Fatalf("release account sync lock: %v", err)
	}
	lockConn.Release()
	select {
	case err := <-done:
		if err != nil {
			t.Fatalf("DeleteAccount() after sync lock release error = %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("DeleteAccount() did not complete after sync lock release")
	}
}

func TestDeleteAccountRechecksPasswordAfterConcurrentChange(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "LifecycleReauth")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	lockConn, err := fixture.pool.Acquire(context.Background())
	if err != nil {
		t.Fatalf("acquire lock connection: %v", err)
	}
	lockTx, err := lockConn.Begin(context.Background())
	if err != nil {
		lockConn.Release()
		t.Fatalf("begin lock transaction: %v", err)
	}
	if err := lockAccountSyncTx(context.Background(), lockTx, auth.Account.ID); err != nil {
		_ = lockTx.Rollback(context.Background())
		lockConn.Release()
		t.Fatalf("hold account sync lock: %v", err)
	}

	originalVerify := fixture.service.passwordVerify
	verified := make(chan struct{})
	var signalOnce sync.Once
	fixture.service.passwordVerify = func(encoded, password string) (bool, error) {
		valid, err := originalVerify(encoded, password)
		if password == testPassword {
			signalOnce.Do(func() { close(verified) })
		}
		return valid, err
	}

	deletionDone := make(chan error, 1)
	go func() {
		_, err := fixture.service.DeleteAccount(context.Background(), auth, DeleteAccountInput{
			RequestID:       "56565656-5656-4565-8565-565656565656",
			RetryCapability: lifecycleCapability(0x80),
			CurrentPassword: testPassword,
		})
		deletionDone <- err
	}()
	select {
	case <-verified:
	case <-time.After(5 * time.Second):
		_ = lockTx.Rollback(context.Background())
		lockConn.Release()
		t.Fatal("deletion did not verify the current password")
	}

	if err := fixture.service.ChangePassword(context.Background(), auth, ChangePasswordInput{
		CurrentPassword: testPassword,
		NewPassword:     secondPassword,
	}); err != nil {
		_ = lockTx.Rollback(context.Background())
		lockConn.Release()
		t.Fatalf("concurrent ChangePassword() error = %v", err)
	}
	if err := lockTx.Commit(context.Background()); err != nil {
		lockConn.Release()
		t.Fatalf("release account sync lock: %v", err)
	}
	lockConn.Release()

	select {
	case err := <-deletionDone:
		if !errors.Is(err, ErrInvalidCredentials) {
			t.Fatalf("stale-password deletion error = %v, want ErrInvalidCredentials", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("deletion did not finish after sync lock release")
	}
	var accountCount int
	if err := fixture.pool.QueryRow(context.Background(),
		"SELECT count(*) FROM accounts WHERE id = $1::uuid", created.Session.Account.ID).Scan(&accountCount); err != nil {
		t.Fatalf("read account after stale-password rejection: %v", err)
	}
	if accountCount != 1 {
		t.Fatal("stale password deletion removed the account")
	}
}

func TestDeleteRetryExpiresThenPrunesReceipt(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "LifecycleRetryExpiry")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	requestID := "57575757-5757-4575-8575-575757575757"
	capability := lifecycleCapability(0x90)
	if _, err := fixture.service.DeleteAccount(context.Background(), auth, DeleteAccountInput{
		RequestID:       requestID,
		RetryCapability: capability,
		CurrentPassword: testPassword,
	}); err != nil {
		t.Fatalf("DeleteAccount() error = %v", err)
	}
	fixture.clock.Advance(deletionReceiptLifetime + time.Minute)
	if _, err := fixture.service.RetryDeleteAccount(context.Background(), DeleteAccountRetryInput{
		RequestID:       requestID,
		RetryCapability: capability,
	}); !errors.Is(err, ErrDeletionRetryInvalid) {
		t.Fatalf("expired deletion retry error = %v, want ErrDeletionRetryInvalid", err)
	}
	var receiptCount int
	if err := fixture.pool.QueryRow(context.Background(),
		"SELECT count(*) FROM account_deletion_receipts WHERE request_id = $1::uuid", requestID).Scan(&receiptCount); err != nil {
		t.Fatalf("read expired deletion receipt: %v", err)
	}
	if receiptCount != 1 {
		t.Fatal("expired deletion receipt was removed before retention pruning")
	}
	if err := fixture.service.PruneDeletionReceipts(
		context.Background(), fixture.clock.Now(), 1); err != nil {
		t.Fatalf("PruneDeletionReceipts() error = %v", err)
	}
	if _, err := fixture.service.RetryDeleteAccount(context.Background(), DeleteAccountRetryInput{
		RequestID:       requestID,
		RetryCapability: capability,
	}); !errors.Is(err, ErrDeletionRetryInvalid) {
		t.Fatalf("pruned deletion retry error = %v, want ErrDeletionRetryInvalid", err)
	}
}

func TestAvatarCleanupDoesNotDeleteReferencedObject(t *testing.T) {
	fixture := newServiceFixture(t)
	a := createFixtureAccount(t, fixture, "LifecycleAvatarA")
	b := createFixtureAccount(t, fixture, "LifecycleAvatarB")
	const objectKey = "avatars/shared/reference.png"
	fixture.avatars.objects[objectKey] = []byte("fixture")
	if _, err := fixture.pool.Exec(context.Background(), `
		UPDATE accounts SET uploaded_avatar_object_key = $2
		WHERE id IN ($1::uuid, $3::uuid)`, a.Session.Account.ID, objectKey, b.Session.Account.ID); err != nil {
		t.Fatalf("seed shared avatar references: %v", err)
	}

	if err := fixture.service.deleteAvatarOrQueue(objectKey); err != nil {
		t.Fatalf("deleteAvatarOrQueue(referenced) error = %v", err)
	}
	if _, ok := fixture.avatars.objects[objectKey]; !ok {
		t.Fatal("referenced avatar object was deleted")
	}
}

func TestAvatarReplacementPersistsCleanupIntentBeforeReferenceRemoval(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "LifecycleAvatarReplace")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	const oldObjectKey = "avatars/replacement/old.png"
	fixture.avatars.objects[oldObjectKey] = []byte("old")
	fixture.service.avatarStore = failedAvatarDeleteStore{fixture.avatars}
	if _, err := fixture.pool.Exec(context.Background(),
		"UPDATE accounts SET uploaded_avatar_object_key = $2 WHERE id = $1::uuid",
		created.Session.Account.ID, oldObjectKey); err != nil {
		t.Fatalf("seed replacement avatar: %v", err)
	}

	_, err := fixture.service.SetBuiltinAvatar(context.Background(), auth, "portrait")
	if !errors.Is(err, errAvatarCleanupQueueUnavailable) && err != nil {
		// A failed object-store delete is expected to surface only when the
		// cleanup queue itself is unavailable; a durable queue is success.
		t.Fatalf("SetBuiltinAvatar() error = %v", err)
	}
	var count int
	if err := fixture.pool.QueryRow(context.Background(),
		"SELECT count(*) FROM avatar_cleanup_queue WHERE object_key = $1", oldObjectKey).Scan(&count); err != nil {
		t.Fatalf("read replacement cleanup intent: %v", err)
	}
	if count != 1 {
		t.Fatalf("replacement cleanup intents = %d, want 1", count)
	}
	if err := fixture.pool.QueryRow(context.Background(),
		"SELECT count(*) FROM accounts WHERE id = $1::uuid AND uploaded_avatar_object_key IS NULL",
		created.Session.Account.ID).Scan(&count); err != nil {
		t.Fatalf("read replaced avatar reference: %v", err)
	}
	if count != 1 {
		t.Fatal("avatar reference was not removed after durable cleanup intent")
	}
}

func mustJSON(t *testing.T, value any) []byte {
	t.Helper()
	encoded, err := json.Marshal(value)
	if err != nil {
		t.Fatalf("marshal JSON fixture: %v", err)
	}
	return encoded
}
