package account

import (
	"context"
	"errors"
	"os"
	"strings"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"
)

type timeoutAvatarDeleteStore struct {
	*memoryAvatarStore
}

func (s timeoutAvatarDeleteStore) Delete(ctx context.Context, _ string) error {
	<-ctx.Done()
	return ctx.Err()
}

type failedAvatarDeleteStore struct {
	*memoryAvatarStore
}

func (s failedAvatarDeleteStore) Delete(context.Context, string) error {
	return errors.New("synthetic avatar delete failure")
}

func newMaxConnsServiceFixture(t *testing.T, maxConns int32) serviceFixture {
	t.Helper()

	databaseURL := strings.TrimSpace(os.Getenv("TEST_DATABASE_URL"))
	if databaseURL == "" {
		t.Skip("TEST_DATABASE_URL is not set")
	}
	cfg, err := pgxpool.ParseConfig(databaseURL)
	if err != nil {
		t.Fatal("invalid TEST_DATABASE_URL")
	}
	if !strings.HasSuffix(
		strings.ToLower(strings.TrimSpace(cfg.ConnConfig.Database)),
		"_test") {
		t.Fatalf("refusing to use non-test database %q", cfg.ConnConfig.Database)
	}
	cfg.MaxConns = maxConns

	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	pool, err := pgxpool.NewWithConfig(ctx, cfg)
	if err != nil {
		t.Fatal("could not open test database")
	}
	if err := pool.Ping(ctx); err != nil {
		pool.Close()
		t.Fatal("could not ping test database")
	}
	t.Cleanup(pool.Close)
	return newServiceFixtureWithPool(t, pool)
}

func TestDeleteAvatarOrQueuePersistsCleanupAfterDeleteDeadline(t *testing.T) {
	fixture := newServiceFixture(t)
	fixture.service.avatarStore = timeoutAvatarDeleteStore{fixture.avatars}
	const objectKey = "avatars/cleanup/orphan.png"

	// The synthetic store waits for the delete deadline. The cleanup intent must
	// still be persisted using a fresh database context after that deadline.
	if err := fixture.service.deleteAvatarOrQueue(objectKey); err != nil {
		t.Fatalf("deleteAvatarOrQueue() error = %v, want queued cleanup", err)
	}

	var count int
	var attempts int
	var lastError string
	if err := fixture.pool.QueryRow(
		context.Background(),
		"SELECT count(*), COALESCE(max(attempts), 0), COALESCE(max(last_error), '') FROM avatar_cleanup_queue WHERE object_key = $1",
		objectKey,
	).Scan(&count, &attempts, &lastError); err != nil {
		t.Fatalf("read avatar cleanup queue: %v", err)
	}
	if count != 1 {
		t.Fatalf("cleanup queue rows = %d, want 1", count)
	}
	if attempts != 0 {
		t.Fatalf("cleanup attempts = %d, want 0", attempts)
	}
	if lastError != "delete_failed" {
		t.Fatalf("cleanup last_error = %q, want delete_failed", lastError)
	}

	fixture.service.avatarStore = fixture.avatars
	if err := fixture.service.RunAvatarCleanupOnce(context.Background(), 25); err != nil {
		t.Fatalf("RunAvatarCleanupOnce() error = %v", err)
	}
	if err := fixture.pool.QueryRow(
		context.Background(),
		"SELECT count(*) FROM avatar_cleanup_queue WHERE object_key = $1",
		objectKey,
	).Scan(&count); err != nil {
		t.Fatalf("read completed avatar cleanup queue: %v", err)
	}
	if count != 0 {
		t.Fatalf("cleanup queue rows after retry = %d, want 0", count)
	}
}

func TestUploadAvatarFailureReleasesTransactionBeforeCleanupQueue(t *testing.T) {
	fixture := newMaxConnsServiceFixture(t, 1)
	fixture.service.avatarStore = failedAvatarDeleteStore{fixture.avatars}
	const accountID = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"

	_, err := fixture.service.UploadAvatar(
		context.Background(),
		AuthenticatedSession{Account: Account{ID: accountID}},
		fixturePNG(t, 64, 64))
	if err == nil {
		t.Fatal("UploadAvatar() unexpectedly succeeded for a missing account")
	}

	var count int
	if err := fixture.pool.QueryRow(
		context.Background(),
		"SELECT count(*) FROM avatar_cleanup_queue WHERE object_key = $1",
		"avatars/"+accountID+"/fixture.png",
	).Scan(&count); err != nil {
		t.Fatalf("read avatar cleanup queue: %v", err)
	}
	if count != 1 {
		t.Fatalf("cleanup queue rows = %d, want 1", count)
	}
}

func TestDeleteAvatarOrQueueSurfacesCleanupQueueFailure(t *testing.T) {
	fixture := newServiceFixture(t)
	fixture.service.avatarStore = failedAvatarDeleteStore{fixture.avatars}
	fixture.pool.Close()

	err := fixture.service.deleteAvatarOrQueue("avatars/cleanup/unqueueable.png")
	if !errors.Is(err, errAvatarCleanupQueueUnavailable) {
		t.Fatalf("deleteAvatarOrQueue() error = %v, want cleanup queue failure", err)
	}
}
