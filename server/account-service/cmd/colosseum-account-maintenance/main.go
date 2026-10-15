package main

import (
	"context"
	"flag"
	"log/slog"
	"os"
	"os/signal"
	"syscall"
	"time"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/avatar"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/config"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/database"
)

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, &slog.HandlerOptions{
		Level: slog.LevelInfo,
	}))
	avatarLimit := flag.Int("avatar-limit", 25, "maximum avatar cleanup rows to inspect")
	batchSize := flag.Int("batch-size", 100, "maximum rows per database retention operation")
	timeout := flag.Duration("timeout", 20*time.Second, "overall maintenance deadline")
	flag.Parse()
	if *avatarLimit <= 0 || *batchSize <= 0 || *timeout <= 0 {
		logger.Error("maintenance command failed",
			"operation", "maintenance",
			"error_class", "invalid_arguments")
		os.Exit(2)
	}

	if err := run(logger, *avatarLimit, *batchSize, *timeout); err != nil {
		// Keep driver and object-storage errors out of logs. Maintenance has a
		// stable operation/error class and the provider retains command status.
		logger.Error("maintenance command failed",
			"operation", "maintenance",
			"error_class", "maintenance_failed")
		os.Exit(1)
	}
	logger.Info("maintenance command completed", "operation", "maintenance")
}

func run(logger *slog.Logger, avatarLimit, batchSize int, timeout time.Duration) error {
	cfg, err := config.LoadMaintenance()
	if err != nil {
		return err
	}

	rootCtx, stop := signal.NotifyContext(
		context.Background(),
		os.Interrupt,
		syscall.SIGTERM)
	defer stop()
	ctx, cancel := context.WithTimeout(rootCtx, timeout)
	defer cancel()

	pool, err := database.Open(ctx, cfg.DatabaseURL, cfg.DatabaseMaxConnections)
	if err != nil {
		return err
	}
	defer pool.Close()
	if err := database.CheckSchema(ctx, pool); err != nil {
		return err
	}

	var avatarStore avatar.Store = avatar.DisabledStore{}
	if cfg.AvatarBucketName != "" {
		avatarStore, err = avatar.NewTigrisStore(
			ctx,
			cfg.AvatarEndpoint,
			cfg.AvatarRegion,
			cfg.AvatarBucketName)
		if err != nil {
			return err
		}
	}

	maintenance, err := account.NewMaintenance(account.MaintenanceDependencies{
		Pool:        pool,
		AvatarStore: avatarStore,
		Clock:       account.SystemClock{},
	})
	if err != nil {
		return err
	}
	return maintenance.RunOnce(ctx, account.MaintenanceOptions{
		AvatarCleanupLimit: avatarLimit,
		BatchSize:          batchSize,
	})
}
