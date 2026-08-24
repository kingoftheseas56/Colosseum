package testdb

import (
	"context"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"
)

const defaultTimeout = 20 * time.Second

func Open(t testing.TB) *pgxpool.Pool {
	t.Helper()

	databaseURL := strings.TrimSpace(os.Getenv("TEST_DATABASE_URL"))
	if databaseURL == "" {
		t.Skip("TEST_DATABASE_URL is not set")
	}

	cfg, err := pgxpool.ParseConfig(databaseURL)
	if err != nil {
		t.Fatal("invalid TEST_DATABASE_URL")
	}
	if !safeDatabaseName(cfg.ConnConfig.Database) {
		t.Fatalf("refusing to use non-test database %q", cfg.ConnConfig.Database)
	}

	ctx, cancel := context.WithTimeout(context.Background(), defaultTimeout)
	defer cancel()

	pool, err := pgxpool.NewWithConfig(ctx, cfg)
	if err != nil {
		t.Fatal("could not open test database")
	}
	if err := pool.Ping(ctx); err != nil {
		pool.Close()
		t.Fatal("could not ping test database")
	}

	t.Cleanup(pool.Close)
	return pool
}

func ResetPublicSchema(t testing.TB, pool *pgxpool.Pool) {
	t.Helper()

	// Destructive-by-design guard, independent of Open(): this function drops
	// the ENTIRE public schema, so it must never run against a pool whose
	// database is not explicitly _test-suffixed, no matter who built the pool.
	if pool == nil || !safeDatabaseName(pool.Config().ConnConfig.Database) {
		t.Fatalf("refusing to reset non-test database %q", poolDatabaseName(pool))
	}

	ctx, cancel := context.WithTimeout(context.Background(), defaultTimeout)
	defer cancel()

	if _, err := pool.Exec(ctx, "DROP SCHEMA public CASCADE"); err != nil {
		t.Fatalf("drop public test schema: %v", err)
	}
	if _, err := pool.Exec(ctx, "CREATE SCHEMA public"); err != nil {
		t.Fatalf("create public test schema: %v", err)
	}
}

func poolDatabaseName(pool *pgxpool.Pool) string {
	if pool == nil || pool.Config() == nil || pool.Config().ConnConfig == nil {
		return ""
	}
	return pool.Config().ConnConfig.Database
}

func safeDatabaseName(name string) bool {
	return strings.HasSuffix(strings.ToLower(strings.TrimSpace(name)), "_test")
}
