package main

import (
	"context"
	"errors"
	"log/slog"
	"net/http"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/avatar"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/config"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/database"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/httpserver"
)

func main() {
	logger := slog.New(slog.NewJSONHandler(os.Stdout, &slog.HandlerOptions{
		Level: slog.LevelInfo,
	}))
	if err := run(logger); err != nil {
		logger.Error("service stopped")
		os.Exit(1)
	}
}

func run(logger *slog.Logger) error {
	cfg, err := config.Load()
	if err != nil {
		return err
	}

	rootCtx, stop := signal.NotifyContext(
		context.Background(),
		os.Interrupt,
		syscall.SIGTERM)
	defer stop()

	pool, err := database.Open(
		rootCtx,
		cfg.DatabaseURL,
		cfg.DatabaseMaxConnections)
	if err != nil {
		return err
	}
	defer pool.Close()

	if err := database.RunMigrations(rootCtx, pool); err != nil {
		return err
	}

	blocklist, err := account.LoadPasswordBlocklist(cfg.PasswordBlocklistPath)
	if err != nil {
		return err
	}
	passwordHasher, err := account.NewPasswordHasher(account.DefaultArgon2Params())
	if err != nil {
		return err
	}
	recoveryVerifier, err := account.NewRecoveryKeyVerifier(cfg.RecoveryHMACKey)
	if err != nil {
		return err
	}
	sessionCipher, err := account.NewSessionCipher(cfg.SessionWrapKey)
	if err != nil {
		return err
	}
	syncCipher, err := account.NewSyncPayloadCipher(cfg.SyncDataKey)
	if err != nil {
		return err
	}
	rateLimiter, err := account.NewRateLimiter(pool, cfg.AbuseHMACKey, account.SystemClock{})
	if err != nil {
		return err
	}

	var avatarStore avatar.Store = avatar.DisabledStore{}
	if strings.TrimSpace(cfg.AvatarBucketName) != "" {
		tigrisStore, err := avatar.NewTigrisStore(
			rootCtx,
			cfg.AvatarEndpoint,
			cfg.AvatarRegion,
			cfg.AvatarBucketName)
		if err != nil {
			return err
		}
		avatarStore = tigrisStore
	}

	accounts, err := account.NewService(account.Dependencies{
		Pool:                    pool,
		PasswordPolicy:          account.PasswordPolicy{Blocklist: blocklist},
		PasswordHasher:          passwordHasher,
		RecoveryVerifier:        recoveryVerifier,
		SessionCipher:           sessionCipher,
		SyncCipher:              syncCipher,
		SyncMaxFutureSkew:       cfg.SyncMaxFutureSkew,
		RateLimiter:             rateLimiter,
		AvatarStore:             avatarStore,
		Clock:                   account.SystemClock{},
		RegistrationGlobalLimit: cfg.RegistrationGlobalLimit10m,
	})
	if err != nil {
		return err
	}

	cleanupDone := make(chan struct{})
	go func() {
		defer close(cleanupDone)
		runAvatarCleanup(rootCtx, logger, accounts)
	}()

	server := &http.Server{
		Addr:              cfg.HTTPAddr,
		Handler:           httpserver.New(pool, accounts, cfg.ReadinessTimeout, logger),
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       30 * time.Second,
		WriteTimeout:      35 * time.Second,
		IdleTimeout:       60 * time.Second,
	}

	listenErr := make(chan error, 1)
	go func() {
		logger.Info("account service listening",
			"environment", cfg.Environment,
			"addr", cfg.HTTPAddr)
		listenErr <- server.ListenAndServe()
	}()

	select {
	case <-rootCtx.Done():
	case err := <-listenErr:
		if err != nil && !errors.Is(err, http.ErrServerClosed) {
			return err
		}
		return nil
	}

	shutdownCtx, cancel := context.WithTimeout(context.Background(), cfg.ShutdownTimeout)
	defer cancel()

	if err := server.Shutdown(shutdownCtx); err != nil {
		return err
	}

	select {
	case err := <-listenErr:
		if err != nil && !errors.Is(err, http.ErrServerClosed) {
			return err
		}
	default:
	}

	select {
	case <-cleanupDone:
	case <-shutdownCtx.Done():
		return shutdownCtx.Err()
	}

	return nil
}

func runAvatarCleanup(
	ctx context.Context,
	logger *slog.Logger,
	accounts *account.Service,
) {
	ticker := time.NewTicker(time.Minute)
	defer ticker.Stop()

	lastRatePrune := time.Time{}
	for {
		select {
		case <-ctx.Done():
			return
		case now := <-ticker.C:
			cleanupCtx, cancel := context.WithTimeout(ctx, 20*time.Second)
			if err := accounts.RunAvatarCleanupOnce(cleanupCtx, 25); err != nil {
				logger.Warn("avatar cleanup pass failed")
			}
			cancel()

			if lastRatePrune.IsZero() || now.Sub(lastRatePrune) >= time.Hour {
				pruneCtx, pruneCancel := context.WithTimeout(ctx, 20*time.Second)
				rateErr := accounts.PruneAuthRateEvents(
					pruneCtx,
					now.UTC().Add(-48*time.Hour))
				securityErr := accounts.RunSecurityMaintenanceOnce(pruneCtx)
				syncVersionErr := accounts.PruneSyncVersions(
					pruneCtx,
					now.UTC().Add(-30*24*time.Hour))
				if rateErr != nil {
					logger.Warn("auth rate-event prune failed")
				}
				if securityErr != nil {
					logger.Warn("account security maintenance failed")
				}
				if syncVersionErr != nil {
					logger.Warn("sync-version prune failed")
				}
				if rateErr == nil && securityErr == nil && syncVersionErr == nil {
					lastRatePrune = now
				}
				pruneCancel()
			}
		}
	}
}
