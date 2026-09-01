package main

import (
	"context"
	"log/slog"
	"os"

	"github.com/jackc/pgx/v5/pgxpool"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/config"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/database"
)

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, &slog.HandlerOptions{
		Level: slog.LevelInfo,
	}))
	if err := run(logger); err != nil {
		// Do not log the returned error: it can contain provider connection
		// details. The process status is the machine-readable failure signal.
		logger.Error("database migration failed")
		os.Exit(1)
	}
}

type migrationPool interface {
	Close()
}

type migrationOpenFunc func(context.Context, string) (migrationPool, error)
type migrationRunFunc func(context.Context, migrationPool) error

func run(logger *slog.Logger) error {
	cfg, err := config.LoadMigration()
	if err != nil {
		return err
	}

	ctx, cancel := context.WithTimeout(context.Background(), cfg.Timeout)
	defer cancel()

	if err := runMigration(
		ctx,
		cfg,
		func(ctx context.Context, databaseURL string) (migrationPool, error) {
			options := database.DefaultPoolOptions()
			options.MaxConnections = 1
			return database.OpenWithOptions(ctx, databaseURL, options)
		},
		func(ctx context.Context, pool migrationPool) error {
			return database.RunMigrations(ctx, pool.(*pgxpool.Pool))
		}); err != nil {
		return err
	}

	logger.Info("database migrations applied")
	return nil
}

func runMigration(
	ctx context.Context,
	cfg config.MigrationConfig,
	open migrationOpenFunc,
	migrate migrationRunFunc,
) error {
	pool, err := open(ctx, cfg.DatabaseURL)
	if err != nil {
		return err
	}
	if pool == nil {
		return os.ErrInvalid
	}
	defer pool.Close()
	return migrate(ctx, pool)
}
