package database

import (
	"context"
	"fmt"
	"strings"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

func Open(ctx context.Context, databaseURL string, maxConnections int32) (*pgxpool.Pool, error) {
	cfg, err := pgxpool.ParseConfig(databaseURL)
	if err != nil {
		// Deliberately generic: the parse error embeds the full DATABASE_URL
		// (including its password), and callers log returned errors — see
		// TestOpenDoesNotReflectDatabaseURLSecret.
		return nil, fmt.Errorf("parse database configuration: invalid DSN")
	}
	cfg.MaxConns = maxConnections

	pool, err := pgxpool.NewWithConfig(ctx, cfg)
	if err != nil {
		return nil, fmt.Errorf("create database pool: database unavailable")
	}

	if err := pool.Ping(ctx); err != nil {
		pool.Close()
		return nil, fmt.Errorf("ping database: database unavailable")
	}
	return pool, nil
}

// OpenDirect opens one PostgreSQL connection for operator commands. It does
// not inspect provider-specific hostnames and therefore cannot prove that a
// URL is a provider's direct endpoint; config and the runbook require the
// operator to supply that direct URL explicitly. It intentionally does not
// use pgxpool, inherit runtime pool settings, or consume runtime connections.
func OpenDirect(ctx context.Context, databaseURL string) (*pgx.Conn, error) {
	databaseURL = strings.TrimSpace(databaseURL)
	if databaseURL == "" {
		return nil, fmt.Errorf("direct database URL is required")
	}
	cfg, err := pgx.ParseConfig(databaseURL)
	if err != nil {
		return nil, fmt.Errorf("parse direct database configuration: invalid DSN")
	}
	conn, err := pgx.ConnectConfig(ctx, cfg)
	if err != nil {
		return nil, fmt.Errorf("connect direct database: database unavailable")
	}
	if err := conn.Ping(ctx); err != nil {
		_ = conn.Close(ctx)
		return nil, fmt.Errorf("ping direct database: database unavailable")
	}
	return conn, nil
}
