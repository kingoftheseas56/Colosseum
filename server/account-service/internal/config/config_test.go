package config

import (
	"encoding/base64"
	"strings"
	"testing"
	"time"
)

func setRequiredTestEnvironment(t *testing.T) {
	t.Helper()
	key := base64.StdEncoding.EncodeToString(make([]byte, 32))
	t.Setenv("COLOSSEUM_ACCOUNT_ENV", "test")
	t.Setenv("DATABASE_URL", "postgres://localhost/colosseum_account_test?sslmode=disable")
	t.Setenv("RECOVERY_HMAC_KEY", key)
	t.Setenv("ABUSE_HMAC_KEY", key)
	t.Setenv("SESSION_WRAP_KEY", key)
	t.Setenv("SYNC_DATA_KEY", key)
	t.Setenv("SYNC_MAX_FUTURE_SKEW_SECONDS", "")
	t.Setenv("PASSWORD_BLOCKLIST_PATH", "/tmp/colosseum-password-blocklist.txt")
	t.Setenv("BUCKET_NAME", "")
	t.Setenv("AWS_ENDPOINT_URL_S3", "")
}

func TestLoadRequiresSecurityKeys(t *testing.T) {
	setRequiredTestEnvironment(t)
	t.Setenv("RECOVERY_HMAC_KEY", "")
	if _, err := Load(); err == nil {
		t.Fatal("Load() succeeded without RECOVERY_HMAC_KEY")
	}
}

func TestLoadRejectsShortSessionWrapKey(t *testing.T) {
	setRequiredTestEnvironment(t)
	t.Setenv("SESSION_WRAP_KEY", base64.StdEncoding.EncodeToString(make([]byte, 31)))
	if _, err := Load(); err == nil {
		t.Fatal("Load() accepted a short SESSION_WRAP_KEY")
	}
}

func TestLoadRejectsShortSyncDataKey(t *testing.T) {
	setRequiredTestEnvironment(t)
	t.Setenv("SYNC_DATA_KEY", base64.StdEncoding.EncodeToString(make([]byte, 31)))
	if _, err := Load(); err == nil {
		t.Fatal("Load() accepted a short SYNC_DATA_KEY")
	}
}

func TestLoadRequiresProductionAvatarStorage(t *testing.T) {
	setRequiredTestEnvironment(t)
	t.Setenv("COLOSSEUM_ACCOUNT_ENV", "production")
	if _, err := Load(); err == nil {
		t.Fatal("Load() accepted production without Tigris configuration")
	}
}

func TestPositiveInt32EnvRejectsTooLargeValue(t *testing.T) {
	t.Setenv("TEST_POSITIVE_INT32", "2147483648")
	_, err := positiveInt32Env("TEST_POSITIVE_INT32", 1)
	if err == nil || !strings.Contains(err.Error(), "too large") {
		t.Fatalf("positiveInt32Env() error = %v, want too-large refusal", err)
	}
}

func TestLoadUsesApprovedDefaults(t *testing.T) {
	setRequiredTestEnvironment(t)
	cfg, err := Load()
	if err != nil {
		t.Fatalf("Load() error = %v", err)
	}
	if cfg.HTTPAddr != ":8080" {
		t.Fatalf("HTTPAddr = %q, want :8080", cfg.HTTPAddr)
	}
	if cfg.DatabaseMaxConnections != 8 {
		t.Fatalf("DatabaseMaxConnections = %d, want 8", cfg.DatabaseMaxConnections)
	}
	if cfg.RegistrationGlobalLimit10m != 500 {
		t.Fatalf("RegistrationGlobalLimit10m = %d, want 500", cfg.RegistrationGlobalLimit10m)
	}
	if cfg.SyncMaxFutureSkew != 10*time.Minute {
		t.Fatalf("SyncMaxFutureSkew = %v, want 10m", cfg.SyncMaxFutureSkew)
	}
	if cfg.AvatarRegion != "auto" {
		t.Fatalf("AvatarRegion = %q, want auto", cfg.AvatarRegion)
	}
}
