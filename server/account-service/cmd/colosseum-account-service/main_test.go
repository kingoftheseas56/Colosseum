package main

import (
	"context"
	"testing"

	"github.com/jackc/pgx/v5/pgxpool"
)

func TestRunDatabaseMigrationsHonorsStartupMode(t *testing.T) {
	called := false
	migrate := func(context.Context, *pgxpool.Pool) error {
		called = true
		return nil
	}

	if err := runDatabaseMigrations(context.Background(), nil, false, migrate); err != nil {
		t.Fatalf("disabled runDatabaseMigrations() error = %v", err)
	}
	if called {
		t.Fatal("disabled startup mode invoked migration runner")
	}

	if err := runDatabaseMigrations(context.Background(), nil, true, migrate); err != nil {
		t.Fatalf("enabled runDatabaseMigrations() error = %v", err)
	}
	if !called {
		t.Fatal("enabled startup mode did not invoke migration runner")
	}
}
