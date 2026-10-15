package account

import (
	"context"
	"errors"
	"fmt"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/avatar"
)

const (
	defaultMaintenanceAvatarLimit = 25
	defaultMaintenanceBatchSize   = 100
	defaultMaintenanceTimeout     = 20 * time.Second
	maintenanceOperationCount     = 5
	maintenanceOperationTimeout   = defaultMaintenanceTimeout / maintenanceOperationCount
	defaultRateEventRetention     = 48 * time.Hour
	defaultSyncVersionRetention   = 30 * 24 * time.Hour
)

// Maintenance owns work that must also be callable when the HTTP service has
// scaled to zero. It deliberately has no authentication or encryption-key
// dependency; runtime database permissions are sufficient for these bounded
// cleanup operations.
type Maintenance struct {
	pool        *pgxpool.Pool
	avatarStore avatar.Store
	clock       Clock
}

type MaintenanceDependencies struct {
	Pool        *pgxpool.Pool
	AvatarStore avatar.Store
	Clock       Clock
}

type MaintenanceOptions struct {
	AvatarCleanupLimit int
	BatchSize          int
	RateEventsBefore   time.Time
	SyncVersionsBefore time.Time
}

func NewMaintenance(dependencies MaintenanceDependencies) (*Maintenance, error) {
	if dependencies.Pool == nil {
		return nil, fmt.Errorf("maintenance requires a database pool")
	}
	if dependencies.AvatarStore == nil {
		dependencies.AvatarStore = avatar.DisabledStore{}
	}
	if dependencies.Clock == nil {
		dependencies.Clock = SystemClock{}
	}
	return &Maintenance{
		pool:        dependencies.Pool,
		avatarStore: dependencies.AvatarStore,
		clock:       dependencies.Clock,
	}, nil
}

// RunOnce performs one bounded operator pass. The caller supplies the
// overall context deadline; no operation starts an unbounded worker or
// ticker. All passes are attempted so one queue failure does not hide the
// security and retention work from the same invocation.
func (m *Maintenance) RunOnce(ctx context.Context, options MaintenanceOptions) error {
	if m == nil || m.pool == nil {
		return fmt.Errorf("maintenance is not configured")
	}
	runCtx, runCancel := context.WithTimeout(ctx, defaultMaintenanceTimeout)
	defer runCancel()
	if options.AvatarCleanupLimit <= 0 {
		options.AvatarCleanupLimit = defaultMaintenanceAvatarLimit
	}
	if options.BatchSize <= 0 {
		options.BatchSize = defaultMaintenanceBatchSize
	}
	now := m.clock.Now().UTC()
	if options.RateEventsBefore.IsZero() {
		options.RateEventsBefore = now.Add(-defaultRateEventRetention)
	}
	if options.SyncVersionsBefore.IsZero() {
		options.SyncVersionsBefore = now.Add(-defaultSyncVersionRetention)
	}

	// The avatar worker is intentionally reused so the externally callable path
	// and the in-process fallback share object-store semantics. Each operation
	// receives its own budget, so a slow provider cannot consume the whole pass.
	worker := &Service{
		pool:        m.pool,
		avatarStore: m.avatarStore,
		clock:       m.clock,
	}

	var errs []error
	if err := runMaintenanceOperation(runCtx, func(ctx context.Context) error {
		return worker.RunAvatarCleanupOnce(ctx, options.AvatarCleanupLimit)
	}); err != nil {
		errs = append(errs, fmt.Errorf("avatar cleanup: %w", err))
	}
	if err := runMaintenanceOperation(runCtx, func(ctx context.Context) error {
		return m.pruneAuthRateEvents(ctx, options.RateEventsBefore, options.BatchSize)
	}); err != nil {
		errs = append(errs, fmt.Errorf("auth rate-event prune: %w", err))
	}
	if err := runMaintenanceOperation(runCtx, func(ctx context.Context) error {
		return m.runSecurityMaintenance(ctx, now, options.BatchSize)
	}); err != nil {
		errs = append(errs, fmt.Errorf("security maintenance: %w", err))
	}
	if err := runMaintenanceOperation(runCtx, func(ctx context.Context) error {
		return m.pruneSyncVersions(ctx, options.SyncVersionsBefore, options.BatchSize)
	}); err != nil {
		errs = append(errs, fmt.Errorf("sync-version prune: %w", err))
	}
	if err := runMaintenanceOperation(runCtx, func(ctx context.Context) error {
		return worker.PruneLifecycleArtifacts(ctx, now, options.BatchSize)
	}); err != nil {
		errs = append(errs, fmt.Errorf("lifecycle artifact prune: %w", err))
	}
	return errors.Join(errs...)
}

func runMaintenanceOperation(parent context.Context, operation func(context.Context) error) error {
	ctx, cancel := context.WithTimeout(parent, maintenanceOperationTimeout)
	defer cancel()
	return operation(ctx)
}

func (m *Maintenance) pruneAuthRateEvents(ctx context.Context, before time.Time, limit int) error {
	if before.IsZero() {
		return fmt.Errorf("rate-event prune cutoff is required")
	}
	if limit <= 0 {
		return fmt.Errorf("rate-event prune batch size is required")
	}
	if _, err := m.pool.Exec(ctx, `
        DELETE FROM auth_rate_events
        WHERE id IN (
            SELECT id
            FROM auth_rate_events
            WHERE occurred_at < $1
            ORDER BY occurred_at, id
            LIMIT $2
        )
    `, before.UTC(), limit); err != nil {
		return fmt.Errorf("delete expired rate events: %w", err)
	}
	return nil
}

func (m *Maintenance) runSecurityMaintenance(
	ctx context.Context,
	now time.Time,
	limit int,
) error {
	if limit <= 0 {
		return fmt.Errorf("security maintenance batch size is required")
	}

	var errs []error
	if _, err := m.pool.Exec(ctx, `
        WITH due AS (
            SELECT id
            FROM trusted_recovery_challenges
            WHERE state IN ('pending', 'approved')
              AND expires_at <= $1
            ORDER BY expires_at, id
            LIMIT $2
        )
        UPDATE trusted_recovery_challenges AS challenge
        SET state = 'denied',
            decided_at = COALESCE(decided_at, $1),
            new_password_hash = ''
        FROM due
        WHERE challenge.id = due.id
    `, now, limit); err != nil {
		errs = append(errs, fmt.Errorf("expire trusted recovery challenges: %w", err))
	}
	if _, err := m.pool.Exec(ctx, `
        WITH due AS (
            SELECT id
            FROM device_signin_challenges
            WHERE state IN ('pending', 'approved')
              AND expires_at <= $1
            ORDER BY expires_at, id
            LIMIT $2
        )
        UPDATE device_signin_challenges AS challenge
        SET state = 'denied',
            decided_at = COALESCE(decided_at, $1)
        FROM due
        WHERE challenge.id = due.id
    `, now, limit); err != nil {
		errs = append(errs, fmt.Errorf("expire device sign-in challenges: %w", err))
	}
	if _, err := m.pool.Exec(ctx, `
        WITH due AS (
            SELECT id
            FROM trusted_recovery_challenges
            WHERE recovery_retry_expires_at IS NOT NULL
              AND recovery_retry_expires_at <= $1
            ORDER BY recovery_retry_expires_at, id
            LIMIT $2
        )
        UPDATE trusted_recovery_challenges AS challenge
        SET recovery_retry_ciphertext = NULL,
            recovery_retry_expires_at = NULL
        FROM due
        WHERE challenge.id = due.id
    `, now, limit); err != nil {
		errs = append(errs, fmt.Errorf("clear trusted recovery retry material: %w", err))
	}
	if _, err := m.pool.Exec(ctx, `
        WITH due AS (
            SELECT id
            FROM sessions
            WHERE previous_refresh_expires_at IS NOT NULL
              AND previous_refresh_expires_at <= $1
            ORDER BY previous_refresh_expires_at, id
            LIMIT $2
        )
        UPDATE sessions AS session
        SET previous_refresh_token_hash = NULL,
            previous_refresh_expires_at = NULL,
            refresh_retry_ciphertext = NULL
        FROM due
        WHERE session.id = due.id
    `, now, limit); err != nil {
		errs = append(errs, fmt.Errorf("clear refresh retry material: %w", err))
	}

	cutoff := now.Add(-24 * time.Hour)
	if _, err := m.pool.Exec(ctx, `
        DELETE FROM trusted_recovery_challenges
        WHERE id IN (
            SELECT id
            FROM trusted_recovery_challenges
            WHERE state IN ('denied', 'consumed')
              AND COALESCE(consumed_at, decided_at, expires_at) < $1
            ORDER BY COALESCE(consumed_at, decided_at, expires_at), id
            LIMIT $2
        )
    `, cutoff, limit); err != nil {
		errs = append(errs, fmt.Errorf("prune trusted recovery challenges: %w", err))
	}
	if _, err := m.pool.Exec(ctx, `
        DELETE FROM device_signin_challenges
        WHERE id IN (
            SELECT id
            FROM device_signin_challenges
            WHERE state IN ('denied', 'consumed')
              AND COALESCE(consumed_at, decided_at, expires_at) < $1
            ORDER BY COALESCE(consumed_at, decided_at, expires_at), id
            LIMIT $2
        )
    `, cutoff, limit); err != nil {
		errs = append(errs, fmt.Errorf("prune device sign-in challenges: %w", err))
	}
	return errors.Join(errs...)
}

func (m *Maintenance) pruneSyncVersions(
	ctx context.Context,
	before time.Time,
	limit int,
) error {
	if before.IsZero() {
		return fmt.Errorf("sync-version prune cutoff is required")
	}
	if limit <= 0 {
		return fmt.Errorf("sync-version prune batch size is required")
	}
	if _, err := m.pool.Exec(ctx, `
        DELETE FROM account_sync_versions
        WHERE id IN (
            SELECT id
            FROM account_sync_versions
            WHERE replaced_at < $1
            ORDER BY replaced_at, id
            LIMIT $2
        )
    `, before.UTC(), limit); err != nil {
		return fmt.Errorf("prune sync versions: %w", err)
	}
	return nil
}
