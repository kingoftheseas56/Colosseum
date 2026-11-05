package database

import (
	"context"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/testsupport/testdb"
)

func TestCheckSchemaAcceptsMigratedDatabase(t *testing.T) {
	pool := testdb.Open(t)
	testdb.ResetPublicSchema(t, pool)
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()

	if err := RunMigrations(ctx, pool); err != nil {
		t.Fatalf("RunMigrations() error = %v", err)
	}
	if err := CheckSchema(ctx, pool); err != nil {
		t.Fatalf("CheckSchema() error = %v", err)
	}

	var checksumCount int
	if err := pool.QueryRow(ctx,
		"SELECT count(*) FROM schema_migrations WHERE checksum IS NOT NULL AND length(checksum) = 64").Scan(&checksumCount); err != nil {
		t.Fatalf("count migration checksums: %v", err)
	}
	embedded, err := embeddedMigrations()
	if err != nil {
		t.Fatalf("embeddedMigrations() error = %v", err)
	}
	if checksumCount != len(embedded) {
		t.Fatalf("checksum count = %d, want %d", checksumCount, len(embedded))
	}
}

func TestCheckSchemaRejectsMissingFutureAndDriftedReceipts(t *testing.T) {
	pool := testdb.Open(t)
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()

	for _, test := range []struct {
		name string
		edit func(context.Context) error
	}{
		{
			name: "missing",
			edit: func(ctx context.Context) error {
				_, err := pool.Exec(ctx,
					"DELETE FROM schema_migrations WHERE name = '0006_sync_mutation_aliases.sql'")
				return err
			},
		},
		{
			name: "future",
			edit: func(ctx context.Context) error {
				_, err := pool.Exec(ctx,
					"INSERT INTO schema_migrations(name, checksum) VALUES('9999_future.sql', repeat('f', 64))")
				return err
			},
		},
		{
			name: "drift",
			edit: func(ctx context.Context) error {
				_, err := pool.Exec(ctx,
					"UPDATE schema_migrations SET checksum = repeat('d', 64) WHERE name = '0001_bootstrap.sql'")
				return err
			},
		},
		{
			name: "empty checksum",
			edit: func(ctx context.Context) error {
				_, err := pool.Exec(ctx,
					"UPDATE schema_migrations SET checksum = '' WHERE name = '0001_bootstrap.sql'")
				return err
			},
		},
		{
			name: "legacy checksum",
			edit: func(ctx context.Context) error {
				_, err := pool.Exec(ctx,
					"UPDATE schema_migrations SET checksum = NULL")
				return err
			},
		},
	} {
		t.Run(test.name, func(t *testing.T) {
			testdb.ResetPublicSchema(t, pool)
			if err := RunMigrations(ctx, pool); err != nil {
				t.Fatalf("RunMigrations() error = %v", err)
			}
			if err := test.edit(ctx); err != nil {
				t.Fatalf("edit schema receipt: %v", err)
			}
			if err := CheckSchema(ctx, pool); err == nil {
				t.Fatalf("CheckSchema() accepted %s schema", test.name)
			}

			if test.name == "legacy checksum" {
				if err := RunMigrations(ctx, pool); err != nil {
					t.Fatalf("RunMigrations() legacy baseline error = %v", err)
				}
				if err := CheckSchema(ctx, pool); err != nil {
					t.Fatalf("CheckSchema() after legacy baseline error = %v", err)
				}
			}
		})
	}
}

func TestCheckSchemaRejectsBrokenPhysicalContract(t *testing.T) {
	pool := testdb.Open(t)
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()

	for _, test := range []struct {
		name string
		edit string
	}{
		{name: "column", edit: "ALTER TABLE accounts DROP COLUMN password_hash"},
		{name: "unique", edit: "ALTER TABLE account_sync_journal DROP CONSTRAINT account_sync_journal_mutation_uk"},
		{name: "foreign key", edit: "ALTER TABLE accounts DROP CONSTRAINT accounts_username_reservation_fk"},
		{name: "foreign key action", edit: `
            ALTER TABLE accounts DROP CONSTRAINT accounts_username_reservation_fk;
            ALTER TABLE accounts
                ADD CONSTRAINT accounts_username_reservation_fk
                FOREIGN KEY (canonical_username)
                REFERENCES username_reservations(canonical_username)
                ON DELETE CASCADE
        `},
		{name: "check", edit: "ALTER TABLE accounts DROP CONSTRAINT accounts_avatar_choice_ck"},
		{name: "same-name weaker check", edit: `
            ALTER TABLE accounts DROP CONSTRAINT accounts_avatar_choice_ck;
            ALTER TABLE accounts
                ADD CONSTRAINT accounts_avatar_choice_ck CHECK (true)
        `},
		{name: "lifecycle column", edit: "ALTER TABLE account_export_items DROP COLUMN payload_ciphertext"},
		{name: "lifecycle unique", edit: "ALTER TABLE account_export_items DROP CONSTRAINT account_export_items_identity_uk"},
		{name: "lifecycle foreign key", edit: "ALTER TABLE account_export_items DROP CONSTRAINT account_export_items_snapshot_id_fkey"},
		{name: "lifecycle index", edit: "DROP INDEX account_export_snapshots_account_expiry_idx"},
		{name: "same-name wrong index columns", edit: `
            DROP INDEX account_export_snapshots_account_expiry_idx;
            CREATE INDEX account_export_snapshots_account_expiry_idx
                ON account_export_snapshots(expires_at)
        `},
		{name: "lifecycle id default", edit: "ALTER TABLE account_export_snapshots ALTER COLUMN id DROP DEFAULT"},
		{name: "lifecycle receipt foreign key", edit: `
            ALTER TABLE account_deletion_receipts
                ADD CONSTRAINT account_deletion_receipts_account_fk
                FOREIGN KEY (account_id) REFERENCES accounts(id) ON DELETE CASCADE
        `},
	} {
		t.Run(test.name, func(t *testing.T) {
			testdb.ResetPublicSchema(t, pool)
			if err := RunMigrations(ctx, pool); err != nil {
				t.Fatalf("RunMigrations() error = %v", err)
			}
			if _, err := pool.Exec(ctx, test.edit); err != nil {
				t.Fatalf("break schema contract: %v", err)
			}
			if err := CheckSchema(ctx, pool); err == nil {
				t.Fatalf("CheckSchema() accepted broken %s contract", test.name)
			}
		})
	}
}

func TestLegacyBaselineRejectsSameNameSemanticDrift(t *testing.T) {
	pool := testdb.Open(t)
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()

	for _, test := range []struct {
		name string
		edit string
	}{
		{name: "same-name weaker check", edit: `
            ALTER TABLE accounts DROP CONSTRAINT accounts_avatar_choice_ck;
            ALTER TABLE accounts
                ADD CONSTRAINT accounts_avatar_choice_ck CHECK (true)
        `},
		{name: "same-name wrong index columns", edit: `
            DROP INDEX account_export_snapshots_account_expiry_idx;
            CREATE INDEX account_export_snapshots_account_expiry_idx
                ON account_export_snapshots(expires_at)
        `},
	} {
		t.Run(test.name, func(t *testing.T) {
			testdb.ResetPublicSchema(t, pool)
			if err := RunMigrations(ctx, pool); err != nil {
				t.Fatalf("RunMigrations() error = %v", err)
			}
			if _, err := pool.Exec(ctx, "UPDATE schema_migrations SET checksum = NULL"); err != nil {
				t.Fatalf("clear migration checksums: %v", err)
			}
			if _, err := pool.Exec(ctx, test.edit); err != nil {
				t.Fatalf("introduce semantic schema drift: %v", err)
			}
			if err := RunMigrations(ctx, pool); err == nil {
				t.Fatal("RunMigrations() accepted same-name semantic drift while baselining")
			}

			var checksumCount int
			if err := pool.QueryRow(ctx,
				"SELECT count(*) FROM schema_migrations WHERE checksum IS NOT NULL").Scan(&checksumCount); err != nil {
				t.Fatalf("count baseline checksums after rejection: %v", err)
			}
			if checksumCount != 0 {
				t.Fatalf("rejected semantic baseline recorded %d checksums", checksumCount)
			}
		})
	}
}

func TestRunMigrationsFailedTransactionLeavesNoReceipt(t *testing.T) {
	pool := testdb.Open(t)
	testdb.ResetPublicSchema(t, pool)
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()

	if _, err := pool.Exec(ctx, "CREATE TABLE service_metadata (wrong_column text)"); err != nil {
		t.Fatalf("create incompatible table: %v", err)
	}
	if err := RunMigrations(ctx, pool); err == nil {
		t.Fatal("RunMigrations() accepted incompatible schema")
	}
	var migrationsTableExists bool
	if err := pool.QueryRow(ctx,
		"SELECT to_regclass('public.schema_migrations') IS NOT NULL").Scan(&migrationsTableExists); err != nil {
		t.Fatalf("check rolled-back metadata table: %v", err)
	}
	if migrationsTableExists {
		t.Fatal("failed migration left schema_migrations behind")
	}
}

func TestLegacyBaselineRequiresPhysicalSchemaContract(t *testing.T) {
	pool := testdb.Open(t)
	testdb.ResetPublicSchema(t, pool)
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()

	if err := RunMigrations(ctx, pool); err != nil {
		t.Fatalf("RunMigrations() error = %v", err)
	}
	if _, err := pool.Exec(ctx, "UPDATE schema_migrations SET checksum = NULL"); err != nil {
		t.Fatalf("clear migration checksums: %v", err)
	}
	if _, err := pool.Exec(ctx, "ALTER TABLE accounts DROP COLUMN password_hash"); err != nil {
		t.Fatalf("break legacy schema contract: %v", err)
	}
	if err := RunMigrations(ctx, pool); err == nil {
		t.Fatal("RunMigrations() baselined a physically incompatible legacy schema")
	}

	var checksumCount int
	if err := pool.QueryRow(ctx,
		"SELECT count(*) FROM schema_migrations WHERE checksum IS NOT NULL").Scan(&checksumCount); err != nil {
		t.Fatalf("count legacy checksums after rejected baseline: %v", err)
	}
	if checksumCount != 0 {
		t.Fatalf("rejected legacy baseline recorded %d checksums", checksumCount)
	}
}

func TestRunMigrationsHonorsTransactionalAdvisoryLock(t *testing.T) {
	pool := testdb.Open(t)
	testdb.ResetPublicSchema(t, pool)
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()

	lockConn, err := pool.Acquire(ctx)
	if err != nil {
		t.Fatalf("acquire lock connection: %v", err)
	}
	lockTx, err := lockConn.Begin(ctx)
	if err != nil {
		lockConn.Release()
		t.Fatalf("begin lock transaction: %v", err)
	}
	if _, err := lockTx.Exec(ctx, "SELECT pg_advisory_xact_lock($1)", migrationLockKey); err != nil {
		_ = lockTx.Rollback(ctx)
		lockConn.Release()
		t.Fatalf("hold migration lock: %v", err)
	}

	done := make(chan error, 1)
	go func() { done <- RunMigrations(ctx, pool) }()
	select {
	case err := <-done:
		_ = lockTx.Rollback(ctx)
		lockConn.Release()
		t.Fatalf("RunMigrations() completed while lock was held: %v", err)
	case <-time.After(100 * time.Millisecond):
	}

	if err := lockTx.Commit(ctx); err != nil {
		lockConn.Release()
		t.Fatalf("release migration lock: %v", err)
	}
	lockConn.Release()
	if err := <-done; err != nil {
		t.Fatalf("RunMigrations() after lock release error = %v", err)
	}
}

func TestRuntimeSchemaGateWorksWithoutDDLPrivilege(t *testing.T) {
	adminPool := testdb.Open(t)
	testdb.ResetPublicSchema(t, adminPool)
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	if err := RunMigrations(ctx, adminPool); err != nil {
		t.Fatalf("RunMigrations() error = %v", err)
	}

	const role = "colosseum_runtime_test"
	const password = "runtime-test-password"
	_, _ = adminPool.Exec(ctx, "DROP ROLE IF EXISTS "+role)
	if _, err := adminPool.Exec(ctx, "CREATE ROLE "+role+" LOGIN PASSWORD '"+password+"'"); err != nil {
		t.Fatalf("create runtime role: %v", err)
	}
	t.Cleanup(func() {
		_, _ = adminPool.Exec(context.Background(), "DROP ROLE IF EXISTS "+role)
	})
	if _, err := adminPool.Exec(ctx, "GRANT USAGE ON SCHEMA public TO "+role); err != nil {
		t.Fatalf("grant schema usage: %v", err)
	}
	if _, err := adminPool.Exec(ctx, "REVOKE CREATE ON SCHEMA public FROM "+role); err != nil {
		t.Fatalf("revoke schema create: %v", err)
	}
	if _, err := adminPool.Exec(ctx, "GRANT SELECT ON ALL TABLES IN SCHEMA public TO "+role); err != nil {
		t.Fatalf("grant schema reads: %v", err)
	}

	poolConfig, err := pgxpool.ParseConfig(os.Getenv("TEST_DATABASE_URL"))
	if err != nil {
		t.Fatalf("parse test database URL: %v", err)
	}
	poolConfig.ConnConfig.User = role
	poolConfig.ConnConfig.Password = password
	poolConfig.MaxConns = 1
	runtimePool, err := pgxpool.NewWithConfig(ctx, poolConfig)
	if err != nil {
		t.Fatalf("open runtime role pool: %v", err)
	}
	defer runtimePool.Close()
	if err := runtimePool.Ping(ctx); err != nil {
		t.Fatalf("ping runtime role pool: %v", err)
	}
	if err := CheckSchema(ctx, runtimePool); err != nil {
		t.Fatalf("runtime role CheckSchema() error = %v", err)
	}
	if err := RunMigrations(ctx, runtimePool); err == nil {
		t.Fatal("runtime role unexpectedly acquired migration DDL privilege")
	} else if strings.Contains(err.Error(), password) {
		t.Fatalf("migration error reflected runtime password: %q", err)
	}
}
