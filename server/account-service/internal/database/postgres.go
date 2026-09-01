package database

import (
	"context"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

type PoolOptions struct {
	MaxConnections        int32
	ConnectTimeout        time.Duration
	StatementTimeout      time.Duration
	LockTimeout           time.Duration
	MaxConnLifetime       time.Duration
	MaxConnLifetimeJitter time.Duration
	MaxConnIdleTime       time.Duration
	HealthCheckPeriod     time.Duration
}

func DefaultPoolOptions() PoolOptions {
	return PoolOptions{
		MaxConnections:        8,
		ConnectTimeout:        5 * time.Second,
		StatementTimeout:      5 * time.Second,
		LockTimeout:           2 * time.Second,
		MaxConnLifetime:       30 * time.Minute,
		MaxConnLifetimeJitter: 5 * time.Minute,
		MaxConnIdleTime:       5 * time.Minute,
		HealthCheckPeriod:     time.Minute,
	}
}

func Open(ctx context.Context, databaseURL string, maxConnections int32) (*pgxpool.Pool, error) {
	options := DefaultPoolOptions()
	options.MaxConnections = maxConnections
	return OpenWithOptions(ctx, databaseURL, options)
}

func OpenWithOptions(
	ctx context.Context,
	databaseURL string,
	options PoolOptions,
) (*pgxpool.Pool, error) {
	cfg, err := poolConfig(databaseURL, options)
	if err != nil {
		return nil, err
	}

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

func poolConfig(databaseURL string, options PoolOptions) (*pgxpool.Config, error) {
	if options.MaxConnections <= 0 {
		return nil, fmt.Errorf("database pool requires a positive max connection count")
	}
	if options.ConnectTimeout <= 0 ||
		options.StatementTimeout <= 0 ||
		options.LockTimeout <= 0 ||
		options.MaxConnLifetime <= 0 ||
		options.MaxConnLifetimeJitter < 0 ||
		options.MaxConnIdleTime <= 0 ||
		options.HealthCheckPeriod <= 0 {
		return nil, fmt.Errorf("database pool requires positive timeout and lifetime options")
	}

	cfg, err := pgxpool.ParseConfig(databaseURL)
	if err != nil {
		// Deliberately generic: the parse error embeds the full DATABASE_URL
		// (including its password), and callers log returned errors — see
		// TestOpenDoesNotReflectDatabaseURLSecret.
		return nil, fmt.Errorf("parse database configuration: invalid DSN")
	}

	cfg.MaxConns = options.MaxConnections
	cfg.MinConns = 0
	cfg.ConnConfig.ConnectTimeout = options.ConnectTimeout
	cfg.MaxConnLifetime = options.MaxConnLifetime
	cfg.MaxConnLifetimeJitter = options.MaxConnLifetimeJitter
	cfg.MaxConnIdleTime = options.MaxConnIdleTime
	cfg.HealthCheckPeriod = options.HealthCheckPeriod
	cfg.AfterConnect = func(ctx context.Context, conn *pgx.Conn) error {
		for _, statement := range postgresSessionSetupStatements(options) {
			if _, err := conn.Exec(ctx, statement); err != nil {
				return fmt.Errorf("configure postgres session: %w", err)
			}
		}
		return nil
	}
	return cfg, nil
}

func postgresSessionSetupStatements(options PoolOptions) []string {
	return []string{
		fmt.Sprintf("SET statement_timeout = '%s'", options.StatementTimeout),
		fmt.Sprintf("SET lock_timeout = '%s'", options.LockTimeout),
	}
}
