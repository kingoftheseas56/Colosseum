package database

import (
	"context"
	"strings"
	"testing"
	"time"
)

func TestDefaultPoolOptionsUseApprovedRuntimeBounds(t *testing.T) {
	options := DefaultPoolOptions()

	if options.MaxConnections != 8 {
		t.Fatalf("MaxConnections = %d, want 8", options.MaxConnections)
	}
	if options.ConnectTimeout != 5*time.Second {
		t.Fatalf("ConnectTimeout = %v, want 5s", options.ConnectTimeout)
	}
	if options.StatementTimeout != 5*time.Second {
		t.Fatalf("StatementTimeout = %v, want 5s", options.StatementTimeout)
	}
	if options.LockTimeout != 2*time.Second {
		t.Fatalf("LockTimeout = %v, want 2s", options.LockTimeout)
	}
	if options.MaxConnLifetime != 30*time.Minute {
		t.Fatalf("MaxConnLifetime = %v, want 30m", options.MaxConnLifetime)
	}
	if options.MaxConnLifetimeJitter != 5*time.Minute {
		t.Fatalf("MaxConnLifetimeJitter = %v, want 5m", options.MaxConnLifetimeJitter)
	}
	if options.MaxConnIdleTime != 5*time.Minute {
		t.Fatalf("MaxConnIdleTime = %v, want 5m", options.MaxConnIdleTime)
	}
	if options.HealthCheckPeriod != time.Minute {
		t.Fatalf("HealthCheckPeriod = %v, want 1m", options.HealthCheckPeriod)
	}
}

func TestPoolConfigAppliesRuntimeOptionsAndSessionTimeouts(t *testing.T) {
	options := DefaultPoolOptions()
	cfg, err := poolConfig("postgres://user:password@example.invalid/colosseum", options)
	if err != nil {
		t.Fatalf("poolConfig() error = %v", err)
	}

	if cfg.MaxConns != options.MaxConnections {
		t.Fatalf("MaxConns = %d, want %d", cfg.MaxConns, options.MaxConnections)
	}
	if cfg.MinConns != 0 {
		t.Fatalf("MinConns = %d, want 0", cfg.MinConns)
	}
	if cfg.ConnConfig.ConnectTimeout != options.ConnectTimeout {
		t.Fatalf("ConnectTimeout = %v, want %v", cfg.ConnConfig.ConnectTimeout, options.ConnectTimeout)
	}
	if cfg.MaxConnLifetime != options.MaxConnLifetime {
		t.Fatalf("MaxConnLifetime = %v, want %v", cfg.MaxConnLifetime, options.MaxConnLifetime)
	}
	if cfg.MaxConnLifetimeJitter != options.MaxConnLifetimeJitter {
		t.Fatalf("MaxConnLifetimeJitter = %v, want %v", cfg.MaxConnLifetimeJitter, options.MaxConnLifetimeJitter)
	}
	if cfg.MaxConnIdleTime != options.MaxConnIdleTime {
		t.Fatalf("MaxConnIdleTime = %v, want %v", cfg.MaxConnIdleTime, options.MaxConnIdleTime)
	}
	if cfg.HealthCheckPeriod != options.HealthCheckPeriod {
		t.Fatalf("HealthCheckPeriod = %v, want %v", cfg.HealthCheckPeriod, options.HealthCheckPeriod)
	}
	if cfg.AfterConnect == nil {
		t.Fatal("AfterConnect is nil, want session timeout setup")
	}

	statements := postgresSessionSetupStatements(options)
	if len(statements) != 2 {
		t.Fatalf("session setup statement count = %d, want 2", len(statements))
	}
	if statements[0] != "SET statement_timeout = '5s'" {
		t.Fatalf("statement timeout SQL = %q", statements[0])
	}
	if statements[1] != "SET lock_timeout = '2s'" {
		t.Fatalf("lock timeout SQL = %q", statements[1])
	}
}

func TestOpenDoesNotReflectDatabaseURLSecret(t *testing.T) {
	const sentinel = "database-password-sentinel-84e1"
	_, err := Open(
		context.Background(),
		"postgres://user:"+sentinel+"@%zz/not-valid",
		1)
	if err == nil {
		t.Fatal("Open() accepted a malformed database URL")
	}
	if strings.Contains(err.Error(), sentinel) {
		t.Fatalf("Open() reflected DATABASE_URL secret in error: %q", err.Error())
	}
}
