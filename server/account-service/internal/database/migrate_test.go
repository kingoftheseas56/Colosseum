package database

import (
	"context"
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
