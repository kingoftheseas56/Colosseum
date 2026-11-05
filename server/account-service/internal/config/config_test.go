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
	t.Setenv("TRUSTED_PROXY_CIDRS", "")
	t.Setenv("COLLECTIONS_ALLOWED_WEB_ORIGINS", "")
	t.Setenv("COLLECTIONS_EDGE_SECRET", "")
	t.Setenv("DATABASE_URL", "postgres://localhost/colosseum_account_test?sslmode=disable")
	t.Setenv("RECOVERY_HMAC_KEY", key)
	t.Setenv("ABUSE_HMAC_KEY", key)
	t.Setenv("SESSION_WRAP_KEY", key)
	t.Setenv("SYNC_DATA_KEY", key)
	t.Setenv("SYNC_MAX_FUTURE_SKEW_SECONDS", "")
	t.Setenv("PASSWORD_BLOCKLIST_PATH", "/tmp/colosseum-password-blocklist.txt")
	t.Setenv("BUCKET_NAME", "")
	t.Setenv("AWS_ENDPOINT_URL_S3", "")
	t.Setenv("AVATAR_STORAGE", "")
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
	for _, mode := range []string{"", "enabled", "disabld"} {
		t.Run("mode="+mode, func(t *testing.T) {
			setRequiredTestEnvironment(t)
			t.Setenv("COLOSSEUM_ACCOUNT_ENV", "production")
			t.Setenv("AVATAR_STORAGE", mode)
			if _, err := Load(); err == nil || err.Error() != "BUCKET_NAME is required in production" {
				t.Fatalf("Load() error = %v, want required avatar bucket", err)
			}
		})
	}
}

func TestLoadAllowsExplicitlyDisabledProductionAvatarStorage(t *testing.T) {
	for _, mode := range []string{"disabled", " DISABLED "} {
		t.Run(mode, func(t *testing.T) {
			setRequiredTestEnvironment(t)
			t.Setenv("COLOSSEUM_ACCOUNT_ENV", "production")
			t.Setenv("AVATAR_STORAGE", mode)
			cfg, err := Load()
			if err != nil {
				t.Fatalf("Load() error = %v, want production without avatar uploads", err)
			}
			if cfg.Environment != "production" || cfg.AvatarBucketName != "" || cfg.AvatarEndpoint != "" {
				t.Fatal("Load() did not preserve production with unconfigured avatar storage")
			}
		})
	}
}

func TestLoadDisabledProductionAvatarStorageStillRequiresSecurityKeys(t *testing.T) {
	for _, name := range []string{"RECOVERY_HMAC_KEY", "ABUSE_HMAC_KEY", "SESSION_WRAP_KEY", "SYNC_DATA_KEY"} {
		t.Run(name, func(t *testing.T) {
			setRequiredTestEnvironment(t)
			t.Setenv("COLOSSEUM_ACCOUNT_ENV", "production")
			t.Setenv("AVATAR_STORAGE", "disabled")
			t.Setenv(name, "")
			if _, err := Load(); err == nil || err.Error() != name+" is required" {
				t.Fatalf("Load() error = %v, want required security key %s", err, name)
			}
		})
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

func TestLoadMigrationUsesOnlyDirectDatabaseURL(t *testing.T) {
	t.Setenv("MIGRATION_DATABASE_URL", "postgres://migration@db.example/direct")
	t.Setenv("DATABASE_URL", "postgres://runtime@db.example/pool")
	t.Setenv("RECOVERY_HMAC_KEY", "")
	t.Setenv("ABUSE_HMAC_KEY", "")
	t.Setenv("SESSION_WRAP_KEY", "")
	t.Setenv("SYNC_DATA_KEY", "")

	cfg, err := LoadMigration()
	if err != nil {
		t.Fatalf("LoadMigration() error = %v", err)
	}
	if cfg.DatabaseURL != "postgres://migration@db.example/direct" {
		t.Fatalf("DatabaseURL = %q, want direct migration URL", cfg.DatabaseURL)
	}
}

func TestLoadMigrationRejectsRuntimeURLReuse(t *testing.T) {
	t.Setenv("MIGRATION_DATABASE_URL", "postgres://runtime@db.example/pool")
	t.Setenv("DATABASE_URL", "postgres://runtime@db.example/pool")
	if _, err := LoadMigration(); err == nil {
		t.Fatal("LoadMigration() accepted the runtime URL as the migration URL")
	}
}

func TestLoadMaintenanceDoesNotRequireAuthenticationSecrets(t *testing.T) {
	t.Setenv("DATABASE_URL", "postgres://runtime@db.example/pool")
	t.Setenv("RECOVERY_HMAC_KEY", "")
	t.Setenv("ABUSE_HMAC_KEY", "")
	t.Setenv("SESSION_WRAP_KEY", "")
	t.Setenv("SYNC_DATA_KEY", "")
	t.Setenv("MAINTENANCE_DATABASE_MAX_CONNECTIONS", "")

	cfg, err := LoadMaintenance()
	if err != nil {
		t.Fatalf("LoadMaintenance() error = %v", err)
	}
	if cfg.DatabaseURL != "postgres://runtime@db.example/pool" {
		t.Fatalf("DatabaseURL = %q, want runtime URL", cfg.DatabaseURL)
	}
	if cfg.DatabaseMaxConnections != 2 {
		t.Fatalf("DatabaseMaxConnections = %d, want 2", cfg.DatabaseMaxConnections)
	}
}

func TestLoadCollectionOrigins(t *testing.T) {
	for _, raw := range []string{"https://getcolosseum.world,https://www.getcolosseum.world", "http://localhost:8080"} {
		setRequiredTestEnvironment(t)
		t.Setenv("COLLECTIONS_ALLOWED_WEB_ORIGINS", raw)
		cfg, err := Load()
		if err != nil || strings.Join(cfg.CollectionsAllowedWebOrigins, ",") != raw {
			t.Fatalf("origins: %v %v", cfg.CollectionsAllowedWebOrigins, err)
		}
	}
	for _, raw := range []string{"*", "null", "https://user@host", "https://host/", "https://host/path", "https://host?x=1", "https://host#x", "https://host:", "https://host:99999", "ftp://host", "https://[example.com]", "https://[127.0.0.1]", "https://host,", "https://host\\evil"} {
		setRequiredTestEnvironment(t)
		t.Setenv("COLLECTIONS_ALLOWED_WEB_ORIGINS", raw)
		if _, err := Load(); err == nil {
			t.Fatalf("accepted origin %q", raw)
		}
	}
}

func TestLoadTrustedProxyCIDRs(t *testing.T) {
	setRequiredTestEnvironment(t)
	t.Setenv("TRUSTED_PROXY_CIDRS", "10.20.0.5/32,2001:db8:abcd::/48")
	cfg, err := Load()
	if err != nil || len(cfg.TrustedProxyCIDRs) != 2 || cfg.TrustedProxyCIDRs[0].String() != "10.20.0.5/32" || cfg.TrustedProxyCIDRs[1].String() != "2001:db8:abcd::/48" {
		t.Fatalf("CIDRs: %v %v", cfg.TrustedProxyCIDRs, err)
	}
	for _, raw := range []string{"0.0.0.0/0", "::/0", "bad", "::ffff:10.20.0.5/128", "10.20.0.5/32,"} {
		t.Setenv("TRUSTED_PROXY_CIDRS", raw)
		if _, err := Load(); err == nil {
			t.Fatalf("accepted CIDRs %q", raw)
		}
	}
}

func TestLoadCollectionsEdgeSecret(t *testing.T) {
	setRequiredTestEnvironment(t)
	cfg, err := Load()
	if err != nil || cfg.CollectionsEdgeSecret != "" {
		t.Fatalf("default edge secret: %q %v", cfg.CollectionsEdgeSecret, err)
	}
	secret := strings.Repeat("s", 32)
	t.Setenv("COLLECTIONS_EDGE_SECRET", secret)
	if cfg, err = Load(); err != nil || cfg.CollectionsEdgeSecret != secret {
		t.Fatalf("edge secret: %q %v", cfg.CollectionsEdgeSecret, err)
	}
	for _, raw := range []string{strings.Repeat("s", 31), " " + secret, secret + "\n"} {
		t.Setenv("COLLECTIONS_EDGE_SECRET", raw)
		if _, err := Load(); err == nil {
			t.Fatalf("accepted edge secret %q", raw)
		}
	}
}
