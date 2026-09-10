package account

import (
	"context"
	"crypto/hmac"
	"crypto/sha256"
	"encoding/binary"
	"fmt"
	"strings"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgxpool"
)

type RateLimiter struct {
	pool  *pgxpool.Pool
	key   []byte
	clock Clock
}

type RateReservation struct {
	limiter       *RateLimiter
	eventID       int64
	committed     bool
	transactional bool
}

func NewRateLimiter(pool *pgxpool.Pool, key []byte, clock Clock) (*RateLimiter, error) {
	if pool == nil {
		return nil, fmt.Errorf("rate limiter requires a database pool")
	}
	if len(key) < 32 {
		return nil, fmt.Errorf("abuse HMAC key must contain at least 32 bytes")
	}
	if clock == nil {
		clock = SystemClock{}
	}
	return &RateLimiter{
		pool:  pool,
		key:   append([]byte(nil), key...),
		clock: clock,
	}, nil
}

func (l *RateLimiter) Allow(ctx context.Context,
	eventType string,
	keyParts []string,
	window time.Duration,
	limit int) error {
	reservation, err := l.reserve(ctx, eventType, keyParts, window, limit)
	if err != nil {
		return err
	}
	reservation.Commit()
	return nil
}

func (l *RateLimiter) Reserve(ctx context.Context,
	eventType string,
	keyParts []string,
	window time.Duration,
	limit int) (*RateReservation, error) {
	return l.reserve(ctx, eventType, keyParts, window, limit)
}

// ReserveTx records a rate-limit event in the caller's transaction. It is
// used by account creation, which already holds its account transaction and
// must not acquire a second pool connection. The event is committed or
// rolled back with tx.
func (l *RateLimiter) ReserveTx(
	ctx context.Context,
	tx pgx.Tx,
	eventType string,
	keyParts []string,
	window time.Duration,
	limit int,
) (*RateReservation, error) {
	if tx == nil {
		return nil, fmt.Errorf("rate limiter transaction is required")
	}
	return l.reserveInTx(ctx, tx, eventType, keyParts, window, limit, true)
}

func (r *RateReservation) Commit() {
	if r != nil {
		r.committed = true
	}
}

func (r *RateReservation) Release(ctx context.Context) {
	if r == nil || r.committed || r.transactional || r.eventID == 0 {
		return
	}
	_, _ = r.limiter.pool.Exec(ctx,
		"DELETE FROM auth_rate_events WHERE id = $1",
		r.eventID)
	r.eventID = 0
}

func (l *RateLimiter) reserve(ctx context.Context,
	eventType string,
	keyParts []string,
	window time.Duration,
	limit int) (*RateReservation, error) {
	tx, err := l.pool.Begin(ctx)
	if err != nil {
		return nil, fmt.Errorf("begin rate limit transaction: %w", err)
	}
	defer func() { _ = tx.Rollback(ctx) }()

	reservation, err := l.reserveInTx(
		ctx,
		tx,
		eventType,
		keyParts,
		window,
		limit,
		false)
	if err != nil {
		return nil, err
	}

	if err := tx.Commit(ctx); err != nil {
		return nil, fmt.Errorf("commit rate limit event: %w", err)
	}
	return reservation, nil
}

func (l *RateLimiter) reserveInTx(
	ctx context.Context,
	tx pgx.Tx,
	eventType string,
	keyParts []string,
	window time.Duration,
	limit int,
	transactional bool,
) (*RateReservation, error) {
	if strings.TrimSpace(eventType) == "" || window <= 0 || limit <= 0 {
		return nil, fmt.Errorf("invalid rate limit rule")
	}

	keyHash := l.keyHash(eventType, keyParts)
	lockKey := int64(binary.BigEndian.Uint64(keyHash[:8]))
	now := l.clock.Now()
	cutoff := now.Add(-window)

	if _, err := tx.Exec(ctx,
		"SELECT pg_advisory_xact_lock($1)",
		lockKey); err != nil {
		return nil, fmt.Errorf("lock rate limit bucket: %w", err)
	}

	var count int
	var oldest *time.Time
	if err := tx.QueryRow(ctx, `
        SELECT count(*)::int, min(occurred_at)
        FROM auth_rate_events
        WHERE event_type = $1
          AND key_hash = $2
          AND occurred_at > $3
    `, eventType, keyHash, cutoff).Scan(&count, &oldest); err != nil {
		return nil, fmt.Errorf("read rate limit bucket: %w", err)
	}

	if count >= limit {
		retryAfter := window
		if oldest != nil {
			retryAfter = oldest.Add(window).Sub(now)
			if retryAfter < time.Second {
				retryAfter = time.Second
			}
		}
		return nil, &RateLimitError{
			Scope:      eventType,
			RetryAfter: retryAfter,
		}
	}

	var eventID int64
	if err := tx.QueryRow(ctx, `
        INSERT INTO auth_rate_events(event_type, key_hash, occurred_at)
        VALUES($1, $2, $3)
        RETURNING id
    `, eventType, keyHash, now).Scan(&eventID); err != nil {
		return nil, fmt.Errorf("record rate limit event: %w", err)
	}
	return &RateReservation{
		limiter:       l,
		eventID:       eventID,
		transactional: transactional,
	}, nil
}

func (l *RateLimiter) Prune(ctx context.Context, before time.Time) error {
	if before.IsZero() {
		return fmt.Errorf("rate limit prune cutoff is required")
	}
	if _, err := l.pool.Exec(ctx, `
        DELETE FROM auth_rate_events
        WHERE occurred_at < $1
    `, before.UTC()); err != nil {
		return fmt.Errorf("prune rate limit events: %w", err)
	}
	return nil
}

func (l *RateLimiter) keyHash(eventType string, keyParts []string) []byte {
	mac := hmac.New(sha256.New, l.key)
	_, _ = mac.Write([]byte(eventType))
	for _, part := range keyParts {
		_, _ = mac.Write([]byte{0})
		_, _ = mac.Write([]byte(strings.TrimSpace(part)))
	}
	return mac.Sum(nil)
}
