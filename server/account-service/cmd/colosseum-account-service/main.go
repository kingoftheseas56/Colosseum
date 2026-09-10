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
		operation, errorClass := serviceFailureDetails(err)
		logger.Error("service stopped",
			"operation", operation,
			"error_class", errorClass)
		os.Exit(1)
	}
}

type serviceFailure struct {
	operation  string
	errorClass string
	err        error
}

type maintenanceRunner interface {
	RunOnce(context.Context, account.MaintenanceOptions) error
}

func (e *serviceFailure) Error() string {
	if e == nil || e.err == nil {
		return "service failure"
	}
	return e.err.Error()
}

func (e *serviceFailure) Unwrap() error {
	if e == nil {
		return nil
	}
	return e.err
}

func failService(operation, errorClass string, err error) error {
	if err == nil {
		return nil
	}
	return &serviceFailure{
		operation:  operation,
		errorClass: errorClass,
		err:        err,
	}
}

func serviceFailureDetails(err error) (operation, errorClass string) {
	var failure *serviceFailure
	if errors.As(err, &failure) && failure != nil {
		return failure.operation, failure.errorClass
	}
	return "service", "service_failed"
}

func run(logger *slog.Logger) error {
	cfg, err := config.Load()
	if err != nil {
		return failService("configuration", "configuration_invalid", err)
	}

	rootCtx, stop := signal.NotifyContext(
		context.Background(),
		os.Interrupt,
		syscall.SIGTERM)
	defer stop()

	startupCtx, startupCancel := context.WithTimeout(rootCtx, 20*time.Second)
	defer startupCancel()
	pool, err := database.Open(
		startupCtx,
		cfg.DatabaseURL,
		cfg.DatabaseMaxConnections)
	if err != nil {
		return failService("database_connect", "database_unavailable", err)
	}
	defer pool.Close()

	if err := database.CheckSchema(startupCtx, pool); err != nil {
		return failService("schema_check", "schema_incompatible", err)
	}

	blocklist, err := account.LoadPasswordBlocklist(cfg.PasswordBlocklistPath)
	if err != nil {
		return failService("password_policy", "password_blocklist_unavailable", err)
	}
	passwordHasher, err := account.NewPasswordHasher(account.DefaultArgon2Params())
	if err != nil {
		return failService("password_policy", "password_hasher_unavailable", err)
	}
	recoveryVerifier, err := account.NewRecoveryKeyVerifier(cfg.RecoveryHMACKey)
	if err != nil {
		return failService("crypto", "recovery_key_unavailable", err)
	}
	sessionCipher, err := account.NewSessionCipher(cfg.SessionWrapKey)
	if err != nil {
		return failService("crypto", "session_cipher_unavailable", err)
	}
	syncCipher, err := account.NewSyncPayloadCipher(cfg.SyncDataKey)
	if err != nil {
		return failService("crypto", "sync_cipher_unavailable", err)
	}
	rateLimiter, err := account.NewRateLimiter(pool, cfg.AbuseHMACKey, account.SystemClock{})
	if err != nil {
		return failService("rate_limiter", "rate_limiter_unavailable", err)
	}

	var avatarStore avatar.Store = avatar.DisabledStore{}
	if strings.TrimSpace(cfg.AvatarBucketName) != "" {
		tigrisStore, err := avatar.NewTigrisStore(
			startupCtx,
			cfg.AvatarEndpoint,
			cfg.AvatarRegion,
			cfg.AvatarBucketName)
		if err != nil {
			return failService("avatar_storage", "avatar_storage_unavailable", err)
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
		return failService("account_service", "account_service_unavailable", err)
	}
	maintenance, err := account.NewMaintenance(account.MaintenanceDependencies{
		Pool:        pool,
		AvatarStore: avatarStore,
		Clock:       account.SystemClock{},
	})
	if err != nil {
		return failService("maintenance", "maintenance_unavailable", err)
	}

	cleanupDone := make(chan struct{})
	go func() {
		defer close(cleanupDone)
		runMaintenance(rootCtx, logger, maintenance)
	}()

	server := &http.Server{
		Addr: cfg.HTTPAddr,
		Handler: httpserver.New(
			pool,
			accounts,
			cfg.ReadinessTimeout,
			logger,
			database.PoolSchemaChecker{Pool: pool}),
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
			return failService("http_server", "listener_failed", err)
		}
		return nil
	}

	shutdownCtx, cancel := context.WithTimeout(context.Background(), cfg.ShutdownTimeout)
	defer cancel()

	if err := server.Shutdown(shutdownCtx); err != nil {
		return failService("http_shutdown", "shutdown_failed", err)
	}

	select {
	case err := <-listenErr:
		if err != nil && !errors.Is(err, http.ErrServerClosed) {
			return failService("http_server", "listener_failed", err)
		}
	default:
	}

	select {
	case <-cleanupDone:
	case <-shutdownCtx.Done():
		return failService("maintenance_shutdown", "shutdown_timeout", shutdownCtx.Err())
	}

	return nil
}

func runMaintenance(
	ctx context.Context,
	logger *slog.Logger,
	maintenance maintenanceRunner,
) {
	ticker := time.NewTicker(time.Minute)
	defer ticker.Stop()

	for {
		select {
		case <-ctx.Done():
			return
		case <-ticker.C:
			runMaintenancePass(ctx, logger, maintenance)
		}
	}
}

func runMaintenancePass(
	ctx context.Context,
	logger *slog.Logger,
	maintenance maintenanceRunner,
) {
	passCtx, cancel := context.WithTimeout(ctx, 20*time.Second)
	defer cancel()
	if err := maintenance.RunOnce(passCtx, account.MaintenanceOptions{}); err != nil {
		logger.Warn("maintenance pass failed",
			"operation", "maintenance",
			"error_class", "maintenance_failed")
	}
}
