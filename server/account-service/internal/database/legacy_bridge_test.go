package database

import (
	"context"
	"crypto/sha256"
	"encoding/hex"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/testsupport/testdb"
)

const (
	legacyAccount = "11111111-1111-4111-8111-111111111111"
	legacyDevice  = "22222222-2222-4222-8222-222222222222"
	legacySession = "33333333-3333-4333-8333-333333333333"
	legacyAttach  = "44444444-4444-4444-8444-444444444444"
	legacyMutate  = "55555555-5555-4555-8555-555555555555"
	legacyEvent   = "66666666-6666-4666-8666-666666666666"
	legacyFactMut = "77777777-7777-4777-8777-777777777777"
	legacyReplace = "88888888-8888-4888-8888-888888888888"
)

func createLegacyBridgeFixture(t *testing.T) (*pgxpool.Pool, context.Context) {
	t.Helper()
	pool := testdb.Open(t) // requires a *_test DATABASE and never creates one
	testdb.ResetPublicSchema(t, pool)
	ctx, cancel := context.WithTimeout(context.Background(), 90*time.Second)
	t.Cleanup(cancel)

	execFixtureSQL(t, ctx, pool, `
        CREATE TABLE schema_migrations (
            name text PRIMARY KEY,
            applied_at timestamptz NOT NULL DEFAULT now()
        );
    `)
	migrations, err := embeddedMigrations()
	if err != nil {
		t.Fatal(err)
	}
	// Exact identical Git blobs at versions 0001-0004.
	for _, m := range migrations[:4] {
		execFixtureSQL(t, ctx, pool, m.sql)
		mustFixtureExec(t, ctx, pool,
			"INSERT INTO schema_migrations(name) VALUES($1)", m.name)
	}
	// The three divergent historical Git objects are kept byte-for-byte in
	// testdata, rather than constructed from or renamed into canonical SQL.
	for _, name := range verifiedLegacyNames[4:] {
		raw, err := os.ReadFile(filepath.Join("testdata", "legacy", name))
		if err != nil {
			t.Fatal(err)
		}
		execFixtureSQL(t, ctx, pool, string(raw))
		mustFixtureExec(t, ctx, pool,
			"INSERT INTO schema_migrations(name) VALUES($1)", name)
	}
	return pool, ctx
}

func execFixtureSQL(t *testing.T, ctx context.Context, pool *pgxpool.Pool, sql string) {
	t.Helper()
	conn, err := pool.Acquire(ctx)
	if err != nil {
		t.Fatal(err)
	}
	defer conn.Release()
	if _, err := conn.Conn().PgConn().Exec(ctx, sql).ReadAll(); err != nil {
		t.Fatalf("fixture SQL execution failed: %v", err)
	}
}

func mustFixtureExec(t *testing.T, ctx context.Context, pool *pgxpool.Pool,
	query string, params ...any) {
	t.Helper()
	if _, err := pool.Exec(ctx, query, params...); err != nil {
		t.Fatal(err)
	}
}

func seedLegacySentinels(t *testing.T, ctx context.Context, pool *pgxpool.Pool) {
	t.Helper()
	// Every value below is synthetic. Ciphertext/token bytes are only
	// recognizable test markers, never credentials from a real account.
	execFixtureSQL(t, ctx, pool, `
        INSERT INTO username_reservations
            (canonical_username, reserved_at)
        VALUES ('bridge_sentinel', '2026-01-01T00:00:00Z');

        INSERT INTO accounts
            (id, canonical_username, display_username, password_hash,
             recovery_key_verifier, created_at, updated_at)
        VALUES
            ('11111111-1111-4111-8111-111111111111',
             'bridge_sentinel', 'Bridge Sentinel', 'synthetic-password-hash',
             decode('01020304','hex'), '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z');

        INSERT INTO devices
            (id, account_id, install_id, label, platform,
             created_at, last_seen_at)
        VALUES
            ('22222222-2222-4222-8222-222222222222',
             '11111111-1111-4111-8111-111111111111',
             'abababab-abab-4bab-8bab-abababababab', 'Synthetic device', 'windows',
             '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z');

        INSERT INTO sessions
            (id, account_id, device_id, access_token_hash,
             access_expires_at, refresh_token_hash, created_at, last_refreshed_at)
        VALUES
            ('33333333-3333-4333-8333-333333333333',
             '11111111-1111-4111-8111-111111111111',
             '22222222-2222-4222-8222-222222222222',
             decode('aabbcc','hex'), '2027-01-01T00:00:00Z',
             decode('ddeeff','hex'), '2026-01-01T00:00:00Z',
             '2026-01-01T00:00:00Z');

        INSERT INTO account_device_attachments
            (id, account_id, device_id, source_kind, source_semantic_digest,
             baseline_server_seq, state, created_at, updated_at, committed_at)
        VALUES
            ('44444444-4444-4444-8444-444444444444',
             '11111111-1111-4111-8111-111111111111',
             '22222222-2222-4222-8222-222222222222',
             'legacy_local', 'synthetic:semantic', 0, 'committed',
             '2026-01-01T00:00:00Z', '2026-01-01T00:00:00Z',
             '2026-01-01T00:00:00Z');

        INSERT INTO account_sync_journal
            (account_id, mutation_id, device_id, category, record_key,
             schema_version, hlc_physical_ms, hlc_counter, operation,
             payload_ciphertext, won, received_at, attachment_id)
        VALUES
            ('11111111-1111-4111-8111-111111111111',
             '55555555-5555-4555-8555-555555555555',
             '22222222-2222-4222-8222-222222222222',
             'history', 'synthetic/history', 1, 12345, 1, 'put',
             decode('a1b2c3','hex'), true, '2026-01-01T00:00:00Z',
             '44444444-4444-4444-8444-444444444444');

        INSERT INTO account_sync_current
            (account_id, category, record_key, mutation_id, device_id,
             schema_version, hlc_physical_ms, hlc_counter, operation,
             payload_ciphertext, server_seq, updated_at)
        VALUES
            ('11111111-1111-4111-8111-111111111111',
             'history', 'synthetic/history',
             '55555555-5555-4555-8555-555555555555',
             '22222222-2222-4222-8222-222222222222',
             1, 12345, 1, 'put', decode('a1b2c3','hex'), 1,
             '2026-01-01T00:00:00Z');

        INSERT INTO account_sync_versions
            (account_id, category, record_key, mutation_id, device_id,
             schema_version, hlc_physical_ms, hlc_counter, operation,
             payload_ciphertext, server_seq, replaced_at, replacing_mutation_id)
        VALUES
            ('11111111-1111-4111-8111-111111111111',
             'history', 'synthetic/old-history',
             '88888888-8888-4888-8888-888888888888',
             '22222222-2222-4222-8222-222222222222',
             1, 12340, 0, 'put', decode('ffee','hex'), 1,
             '2026-01-01T00:00:00Z',
             '55555555-5555-4555-8555-555555555555');

        INSERT INTO account_activity_facts
            (account_id, event_id, mutation_id, origin_device_id,
             schema_version, event_type, payload_ciphertext,
             hlc_physical_ms, hlc_counter, received_at, attachment_id)
        VALUES
            ('11111111-1111-4111-8111-111111111111',
             '66666666-6666-4666-8666-666666666666',
             '77777777-7777-4777-8777-777777777777',
             '22222222-2222-4222-8222-222222222222',
             1, 'synthetic-history-event', decode('cafeba','hex'),
             12346, 2, '2026-01-01T00:00:00Z',
             '44444444-4444-4444-8444-444444444444');
    `)
}

// Fixed-column snapshots deliberately exclude columns legitimately added by
// the bridge while covering every pre-existing data-bearing relation.
func snapshotSentinels(t *testing.T, ctx context.Context, pool *pgxpool.Pool) [8]string {
	t.Helper()
	var out [8]string
	err := pool.QueryRow(ctx, `
        SELECT
          COALESCE((SELECT jsonb_build_array(id, canonical_username, password_hash,
                 encode(recovery_key_verifier, 'hex'))::text FROM accounts
                 WHERE id='11111111-1111-4111-8111-111111111111'), ''),
          COALESCE((SELECT jsonb_build_array(id, account_id, install_id, trusted)::text
                 FROM devices WHERE id='22222222-2222-4222-8222-222222222222'), ''),
          COALESCE((SELECT jsonb_build_array(id, device_id, encode(access_token_hash,'hex'),
                 encode(refresh_token_hash,'hex'), revoked_at)::text FROM sessions
                 WHERE id='33333333-3333-4333-8333-333333333333'), ''),
          COALESCE((SELECT jsonb_build_array(server_seq, mutation_id, attachment_id,
                 encode(payload_ciphertext,'hex'))::text FROM account_sync_journal
                 WHERE mutation_id='55555555-5555-4555-8555-555555555555'), ''),
          COALESCE((SELECT jsonb_build_array(server_seq, mutation_id,
                 encode(payload_ciphertext,'hex'))::text FROM account_sync_current
                 WHERE mutation_id='55555555-5555-4555-8555-555555555555'), ''),
          COALESCE((SELECT jsonb_build_array(server_seq, mutation_id,
                 encode(payload_ciphertext,'hex'))::text FROM account_sync_versions
                 WHERE mutation_id='88888888-8888-4888-8888-888888888888'), ''),
          COALESCE((SELECT jsonb_build_array(server_seq, event_id, mutation_id,
                 schema_version, attachment_id, encode(payload_ciphertext,'hex'))::text
                 FROM account_activity_facts
                 WHERE event_id='66666666-6666-4666-8666-666666666666'), ''),
          COALESCE((SELECT jsonb_build_array(id, baseline_server_seq, state,
                 source_semantic_digest, committed_at)::text
                 FROM account_device_attachments
                 WHERE id='44444444-4444-4444-8444-444444444444'), '')
    `).Scan(&out[0], &out[1], &out[2], &out[3], &out[4],
		&out[5], &out[6], &out[7])
	if err != nil {
		t.Fatal(err)
	}
	for i, value := range out {
		if value == "" {
			t.Fatalf("synthetic sentinel %d disappeared", i)
		}
	}
	return out
}

func snapshotReceipt(t *testing.T, ctx context.Context,
	pool *pgxpool.Pool, relation string) string {
	t.Helper()
	if relation != "schema_migrations" &&
		relation != "account_legacy_migration_receipts" {
		t.Fatal("unreviewed receipt table")
	}
	var receipt string
	err := pool.QueryRow(ctx,
		"SELECT COALESCE(string_agg(name || '@' || applied_at::text, ',' ORDER BY name), '') FROM "+
			relation).Scan(&receipt)
	if err != nil {
		t.Fatal(err)
	}
	return receipt
}

func assertNoBridgeSideEffects(t *testing.T, ctx context.Context,
	pool *pgxpool.Pool, before [8]string, receipt string, expectedChecksum bool) {
	t.Helper()
	if after := snapshotSentinels(t, ctx, pool); after != before {
		t.Fatalf("legacy data changed on rejected migration: before=%v after=%v", before, after)
	}
	if after := snapshotReceipt(t, ctx, pool, "schema_migrations"); after != receipt {
		t.Fatal("legacy receipts changed on rejected migration")
	}
	var checksum, archive bool
	if err := pool.QueryRow(ctx, `
        SELECT EXISTS(
            SELECT 1 FROM information_schema.columns
            WHERE table_schema='public' AND table_name='schema_migrations'
              AND column_name='checksum'
        ), to_regclass('public.account_legacy_migration_receipts') IS NOT NULL
    `).Scan(&checksum, &archive); err != nil {
		t.Fatal(err)
	}
	if checksum != expectedChecksum || archive {
		t.Fatal("rejected bridge changed checksum metadata or left archive")
	}
}

func TestLegacyBridgeFrozenSourceAndHistoricalNames(t *testing.T) {
	migrations, err := embeddedMigrations()
	if err != nil {
		t.Fatal(err)
	}
	if len(migrations) != len(verifiedCanonicalNames) {
		t.Fatal("canonical embedded migration count changed")
	}
	for i, want := range verifiedCanonicalNames {
		if migrations[i].name != want {
			t.Fatalf("migration %d name changed", i)
		}
	}
	// Exact original Git object bytes, not reconstructed lineage names.
	historicalSHA256 := map[string]string{
		"0005_profile_portable_sync.sql":  "dcc07eb5d02a1bbd5388380ee252567d1b83cbee1c5dd697e58bef0e6d2d36f1",
		"0006_activity_fact_hlc.sql":      "15ef40ef1fbcb671fc6cf76750feba28e4ed323021c2f837621b039c13976185",
		"0007_activity_attachment_id.sql": "64c38e7b48cd1d1034fb1b4fa1f8485e4ab03aa7805c95678200a5f917fc38cf",
	}
	for _, name := range verifiedLegacyNames[4:] {
		raw, err := os.ReadFile(filepath.Join("testdata", "legacy", name))
		if err != nil || len(strings.TrimSpace(string(raw))) == 0 {
			t.Fatalf("missing original historical SQL fixture %s: %v", name, err)
		}
		checksum := sha256.Sum256(raw)
		if hex.EncodeToString(checksum[:]) != historicalSHA256[name] {
			t.Fatalf("historical SQL changed: %s", name)
		}
	}
}

func TestLegacyBridgeConvertsWithPreservationAndIdempotence(t *testing.T) {
	pool, ctx := createLegacyBridgeFixture(t)
	seedLegacySentinels(t, ctx, pool)
	before := snapshotSentinels(t, ctx, pool)
	receipt := snapshotReceipt(t, ctx, pool, "schema_migrations")
	var oldValue int64
	if err := pool.QueryRow(ctx, "SELECT last_value FROM account_change_seq").Scan(&oldValue); err != nil {
		t.Fatal(err)
	}
	if oldValue != 2 {
		t.Fatalf("historical shared sequence = %d, want 2", oldValue)
	}

	// First reproduce the actual blocker RED with the unchanged migrator.
	if err := RunMigrations(ctx, pool); err == nil {
		t.Fatal("unmodified migration path incorrectly accepted divergent legacy history")
	}
	assertNoBridgeSideEffects(t, ctx, pool, before, receipt, false)

	if err := RunLegacyBridge(ctx, pool); err != nil {
		t.Fatalf("bridge failed: %v", err)
	}
	if err := CheckSchema(ctx, pool); err != nil {
		t.Fatalf("runtime schema failed: %v", err)
	}
	if got := snapshotSentinels(t, ctx, pool); got != before {
		t.Fatalf("data changed during bridge: before=%v after=%v", before, got)
	}
	if got := snapshotReceipt(t, ctx, pool, "account_legacy_migration_receipts"); got != receipt {
		t.Fatal("historical receipt names/timestamps not preserved")
	}
	canonical := snapshotReceipt(t, ctx, pool, "schema_migrations")
	if err := RunMigrations(ctx, pool); err != nil {
		t.Fatalf("ordinary canonical migrator no longer idempotent: %v", err)
	}
	if err := RunLegacyBridge(ctx, pool); err != nil {
		t.Fatalf("second bridge: %v", err)
	}
	if got := snapshotReceipt(t, ctx, pool, "schema_migrations"); got != canonical {
		t.Fatal("idempotent bridge changed canonical receipt timestamps")
	}
	if got := snapshotSentinels(t, ctx, pool); got != before {
		t.Fatal("idempotent bridge changed sentinels")
	}
	var seq int64
	if err := pool.QueryRow(ctx,
		"SELECT last_value FROM account_sync_journal_server_seq_seq").Scan(&seq); err != nil {
		t.Fatal(err)
	}
	if seq != oldValue {
		t.Fatalf("sequence reseeded: before %d after %d", oldValue, seq)
	}
	var next int64
	if err := pool.QueryRow(ctx,
		"SELECT nextval('account_sync_journal_server_seq_seq')").Scan(&next); err != nil {
		t.Fatal(err)
	}
	if next != oldValue+1 {
		t.Fatalf("shared cursor advanced incorrectly: got %d", next)
	}
}

func TestLegacyBridgeRejectsUnknownHistoryAndPhysicalHybrid(t *testing.T) {
	for _, scenario := range []struct{ name, sql string }{
		{"future-ledger", "INSERT INTO schema_migrations(name) VALUES('0999_unknown.sql')"},
		{"hybrid-table", "ALTER TABLE accounts ADD COLUMN unsupported_future_field text"},
		{"name-only-checksum-hybrid", "ALTER TABLE schema_migrations ADD COLUMN checksum text"},
	} {
		t.Run(scenario.name, func(t *testing.T) {
			pool, ctx := createLegacyBridgeFixture(t)
			seedLegacySentinels(t, ctx, pool)
			mustFixtureExec(t, ctx, pool, scenario.sql)
			before := snapshotSentinels(t, ctx, pool)
			receipt := snapshotReceipt(t, ctx, pool, "schema_migrations")
			if err := RunLegacyBridge(ctx, pool); err == nil {
				t.Fatal("unknown/hybrid state admitted")
			}
			assertNoBridgeSideEffects(t, ctx, pool, before, receipt,
				scenario.name == "name-only-checksum-hybrid")
		})
	}
}

func TestLegacyBridgeDDLFailureRollsBackAllChanges(t *testing.T) {
	pool, ctx := createLegacyBridgeFixture(t)
	seedLegacySentinels(t, ctx, pool)
	// Old DB permits this row; canonical 0010 requires deleted_at_ms for
	// ratings_reviews deletes. Failure happens AFTER 005/006/007/008/009 DDL.
	mustFixtureExec(t, ctx, pool, `
        INSERT INTO account_sync_current
            (account_id,category,record_key,mutation_id,device_id,schema_version,
             hlc_physical_ms,hlc_counter,operation,payload_ciphertext,server_seq,updated_at)
        VALUES ('11111111-1111-4111-8111-111111111111',
                'ratings_reviews','synthetic/unmigratable',
                '99999999-9999-4999-8999-999999999999',
                '22222222-2222-4222-8222-222222222222',
                1,12347,1,'delete',NULL,1,now())
    `)
	before := snapshotSentinels(t, ctx, pool)
	receipt := snapshotReceipt(t, ctx, pool, "schema_migrations")
	if err := RunLegacyBridge(ctx, pool); err == nil {
		t.Fatal("incompatible old data did not roll back the full transition")
	}
	assertNoBridgeSideEffects(t, ctx, pool, before, receipt, false)
	var renamed bool
	if err := pool.QueryRow(ctx,
		"SELECT to_regclass('public.account_change_seq') IS NOT NULL").Scan(&renamed); err != nil {
		t.Fatal(err)
	}
	if !renamed {
		t.Fatal("failed bridge did not restore original sequence name")
	}
	var hasNewAlias bool
	if err := pool.QueryRow(ctx,
		"SELECT to_regclass('public.account_sync_mutation_aliases') IS NOT NULL").Scan(&hasNewAlias); err != nil {
		t.Fatal(err)
	}
	if hasNewAlias {
		t.Fatal("failed bridge left partially created canonical tables")
	}
}

func TestLegacyBridgeRejectsChecksumDriftOnRepeat(t *testing.T) {
	pool, ctx := createLegacyBridgeFixture(t)
	seedLegacySentinels(t, ctx, pool)
	if err := RunLegacyBridge(ctx, pool); err != nil {
		t.Fatal(err)
	}
	before := snapshotSentinels(t, ctx, pool)
	mustFixtureExec(t, ctx, pool,
		"UPDATE schema_migrations SET checksum='tampered' WHERE name='0009_profile_attachments.sql'")
	receipt := snapshotReceipt(t, ctx, pool, "schema_migrations")
	if err := RunLegacyBridge(ctx, pool); err == nil {
		t.Fatal("repeat bridge accepted checksum drift")
	}
	if got := snapshotReceipt(t, ctx, pool, "schema_migrations"); got != receipt {
		t.Fatal("rejection altered canonical receipt history")
	}
	if got := snapshotSentinels(t, ctx, pool); got != before {
		t.Fatal("checksum-drift rejection modified data")
	}
}
