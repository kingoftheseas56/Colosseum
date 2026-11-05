package httpserver

import (
	"context"
	"time"
)

const (
	DefaultSchemaSuccessTTL = 10 * time.Minute
	DefaultSchemaFailureTTL = 30 * time.Second
)

// CachedSchemaChecker memoises a SchemaChecker's result. Readiness probes hit
// /readyz every few seconds, and the full catalog validation behind
// CheckSchema transfers tens of kilobytes from the database per run, which
// adds up against the hosted database's data-transfer allowance. The cheap
// connection ping still runs on every probe; only the schema check is cached.
// Successes are reused for successTTL and failures for the shorter failureTTL
// so a fixed schema is noticed quickly. Only one underlying check runs at a
// time; concurrent callers wait for it (until their context ends) and reuse
// its result.
type CachedSchemaChecker struct {
	inner      SchemaChecker
	successTTL time.Duration
	failureTTL time.Duration
	now        func() time.Time

	lock      chan struct{} // capacity 1; acquired with ctx awareness
	checked   bool
	err       error
	expiresAt time.Time
}

func NewCachedSchemaChecker(inner SchemaChecker, successTTL, failureTTL time.Duration) *CachedSchemaChecker {
	return &CachedSchemaChecker{
		inner:      inner,
		successTTL: successTTL,
		failureTTL: failureTTL,
		now:        time.Now,
		lock:       make(chan struct{}, 1),
	}
}

func (c *CachedSchemaChecker) CheckSchema(ctx context.Context) error {
	select {
	case c.lock <- struct{}{}:
	case <-ctx.Done():
		return ctx.Err()
	}
	defer func() { <-c.lock }()

	if c.checked && c.now().Before(c.expiresAt) {
		return c.err
	}

	err := c.inner.CheckSchema(ctx)
	if ctx.Err() != nil && err != nil {
		// The caller's deadline caused this failure; do not cache it.
		return err
	}
	ttl := c.successTTL
	if err != nil {
		ttl = c.failureTTL
	}
	c.checked = true
	c.err = err
	c.expiresAt = c.now().Add(ttl)
	return err
}
