package main

import (
	"context"
	"log/slog"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/config"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/database"
)

const migrationTimeout = 5 * time.Minute

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, &slog.HandlerOptions{
		Level: slog.LevelInfo,
	}))
	if err := run(logger); err != nil {
		// Migration errors intentionally stay out of logs: PostgreSQL drivers can
		// include operator-controlled details, and the command's stable class is
		// sufficient for provider diagnostics.
		logger.Error("migration command failed",
			"operation", "migration",
			"error_class", "migration_failed")
		os.Exit(1)
	}
	logger.Info("migration command completed", "operation", "migration")
}

func run(logger *slog.Logger) error {
	cfg, err := config.LoadMigration()
	if err != nil {
		return err
	}

	rootCtx, stop := signal.NotifyContext(
		context.Background(),
		os.Interrupt,
		syscall.SIGTERM)
	defer stop()
	ctx, cancel := context.WithTimeout(rootCtx, migrationTimeout)
	defer cancel()

	conn, err := database.OpenDirect(ctx, cfg.DatabaseURL)
	if err != nil {
		return err
	}
	defer func() { _ = conn.Close(context.Background()) }()

	return database.RunMigrationsOnConn(ctx, conn)
}
