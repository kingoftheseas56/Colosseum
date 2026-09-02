package database

import (
	"context"
	"fmt"

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
		return nil, fmt.Errorf("create database pool: %w", err)
	}

	if err := pool.Ping(ctx); err != nil {
		pool.Close()
		return nil, fmt.Errorf("ping database: %w", err)
	}
	return pool, nil
}
