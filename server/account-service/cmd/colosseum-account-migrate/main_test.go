package main

import (
	"context"
	"errors"
	"testing"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/config"
)

type migrationTestPool struct {
	closed bool
}

func (p *migrationTestPool) Close() {
	p.closed = true
}

func TestRunMigrationReturnsFailureAndClosesPool(t *testing.T) {
	wantErr := errors.New("migration failed")
	pool := &migrationTestPool{}
	openedURL := ""
	err := runMigration(
		context.Background(),
		migrationConfigForTest(),
		func(_ context.Context, databaseURL string) (migrationPool, error) {
			openedURL = databaseURL
			return pool, nil
		},
		func(context.Context, migrationPool) error {
			return wantErr
		})
	if !errors.Is(err, wantErr) {
		t.Fatalf("runMigration() error = %v, want %v", err, wantErr)
	}
	if !pool.closed {
		t.Fatal("runMigration() did not close pool after failure")
	}
	if openedURL != migrationConfigForTest().DatabaseURL {
		t.Fatalf("opened URL = %q, want migration URL", openedURL)
	}
}

func TestRunMigrationAcceptsAppliedAndAlreadyAppliedPasses(t *testing.T) {
	passes := 0
	for range 2 {
		pool := &migrationTestPool{}
		err := runMigration(
			context.Background(),
			migrationConfigForTest(),
			func(context.Context, string) (migrationPool, error) {
				return pool, nil
			},
			func(context.Context, migrationPool) error {
				passes++
				return nil
			})
		if err != nil {
			t.Fatalf("runMigration() pass %d error = %v", passes, err)
		}
		if !pool.closed {
			t.Fatalf("runMigration() pass %d did not close pool", passes)
		}
	}
	if passes != 2 {
		t.Fatalf("migration passes = %d, want 2", passes)
	}
}

func migrationConfigForTest() config.MigrationConfig {
	return config.MigrationConfig{
		DatabaseURL: "postgres://migration-user:migration-secret@example.invalid/db",
	}
}
