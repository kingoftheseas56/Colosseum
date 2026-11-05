package httpserver

import (
	"context"
	"errors"
	"sync"
	"sync/atomic"
	"testing"
	"time"
)

type countingChecker struct {
	calls atomic.Int32
	err   atomic.Value // stores errBox
	delay time.Duration
}

type errBox struct{ err error }

func (c *countingChecker) setErr(err error) { c.err.Store(errBox{err}) }

func (c *countingChecker) CheckSchema(context.Context) error {
	c.calls.Add(1)
	if c.delay > 0 {
		time.Sleep(c.delay)
	}
	if v, ok := c.err.Load().(errBox); ok {
		return v.err
	}
	return nil
}

func newTestCache(inner SchemaChecker, clock *time.Time) *CachedSchemaChecker {
	c := NewCachedSchemaChecker(inner, 10*time.Minute, 30*time.Second)
	c.now = func() time.Time { return *clock }
	return c
}

func TestCachedSchemaCheckerReusesSuccessUntilTTL(t *testing.T) {
	inner := &countingChecker{}
	clock := time.Unix(1000, 0)
	c := newTestCache(inner, &clock)

	for i := 0; i < 3; i++ {
		if err := c.CheckSchema(context.Background()); err != nil {
			t.Fatalf("call %d: %v", i, err)
		}
	}
	if got := inner.calls.Load(); got != 1 {
		t.Fatalf("calls inside TTL = %d, want 1", got)
	}

	clock = clock.Add(9 * time.Minute)
	_ = c.CheckSchema(context.Background())
	if got := inner.calls.Load(); got != 1 {
		t.Fatalf("calls before expiry = %d, want 1", got)
	}

	clock = clock.Add(2 * time.Minute)
	_ = c.CheckSchema(context.Background())
	if got := inner.calls.Load(); got != 2 {
		t.Fatalf("calls after expiry = %d, want 2", got)
	}
}

func TestCachedSchemaCheckerCachesFailureBriefly(t *testing.T) {
	inner := &countingChecker{}
	broken := errors.New("schema drift")
	inner.setErr(broken)
	clock := time.Unix(1000, 0)
	c := newTestCache(inner, &clock)

	for i := 0; i < 3; i++ {
		if err := c.CheckSchema(context.Background()); !errors.Is(err, broken) {
			t.Fatalf("call %d: err = %v, want %v", i, err, broken)
		}
	}
	if got := inner.calls.Load(); got != 1 {
		t.Fatalf("calls inside failure TTL = %d, want 1", got)
	}

	inner.setErr(nil)
	clock = clock.Add(31 * time.Second)
	if err := c.CheckSchema(context.Background()); err != nil {
		t.Fatalf("after recovery: %v", err)
	}
	if got := inner.calls.Load(); got != 2 {
		t.Fatalf("calls after failure TTL = %d, want 2", got)
	}
	// Recovered success is now cached for the long TTL.
	clock = clock.Add(5 * time.Minute)
	_ = c.CheckSchema(context.Background())
	if got := inner.calls.Load(); got != 2 {
		t.Fatalf("calls = %d, want 2", got)
	}
}

func TestCachedSchemaCheckerConcurrentCallsRunOnce(t *testing.T) {
	inner := &countingChecker{delay: 50 * time.Millisecond}
	clock := time.Unix(1000, 0)
	c := newTestCache(inner, &clock)

	var wg sync.WaitGroup
	for i := 0; i < 20; i++ {
		wg.Add(1)
		go func() {
			defer wg.Done()
			if err := c.CheckSchema(context.Background()); err != nil {
				t.Errorf("unexpected error: %v", err)
			}
		}()
	}
	wg.Wait()
	if got := inner.calls.Load(); got != 1 {
		t.Fatalf("underlying calls = %d, want 1", got)
	}
}

func TestCachedSchemaCheckerWaiterHonorsContext(t *testing.T) {
	inner := &countingChecker{delay: 200 * time.Millisecond}
	clock := time.Unix(1000, 0)
	c := newTestCache(inner, &clock)

	go func() { _ = c.CheckSchema(context.Background()) }()
	time.Sleep(20 * time.Millisecond)

	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Millisecond)
	defer cancel()
	if err := c.CheckSchema(ctx); !errors.Is(err, context.DeadlineExceeded) {
		t.Fatalf("err = %v, want deadline exceeded", err)
	}
}
