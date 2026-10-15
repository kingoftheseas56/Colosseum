package database

import (
	"context"
	"strings"
	"testing"
)

func TestOpenDoesNotReflectDatabaseURLSecret(t *testing.T) {
	const sentinel = "database-password-sentinel-84e1"
	_, err := Open(
		context.Background(),
		"postgres://user:"+sentinel+"@%zz/not-valid",
		1)
	if err == nil {
		t.Fatal("Open() accepted a malformed database URL")
	}
	if strings.Contains(err.Error(), sentinel) {
		t.Fatalf("Open() reflected DATABASE_URL secret in error: %q", err.Error())
	}
}
