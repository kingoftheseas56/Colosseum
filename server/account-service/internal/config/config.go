package config

import (
	"encoding/base64"
	"errors"
	"fmt"
	"net/netip"
	"net/url"
	"os"
	"strconv"
	"strings"
	"time"
)

const (
	defaultHTTPAddr                = ":8080"
	defaultDatabaseMaxConnections  = 8
	defaultMaintenanceConnections  = 2
	defaultReadinessTimeout        = 2 * time.Second
	defaultShutdownTimeout         = 10 * time.Second
	minCollectionsEdgeSecretLength = 32
	defaultRegistrationGlobal10m   = 500
	defaultSyncMaxFutureSkew       = 10 * time.Minute
)

type Config struct {
	Environment                  string
	HTTPAddr                     string
	TrustedProxyCIDRs            []netip.Prefix
	CollectionsAllowedWebOrigins []string
	CollectionsEdgeSecret        string
	DatabaseURL                  string
	DatabaseMaxConnections       int32
	ReadinessTimeout             time.Duration
	ShutdownTimeout              time.Duration
	RecoveryHMACKey              []byte
	AbuseHMACKey                 []byte
	SessionWrapKey               []byte
	SyncDataKey                  []byte
	SyncMaxFutureSkew            time.Duration
	PasswordBlocklistPath        string
	RegistrationGlobalLimit10m   int
	AvatarBucketName             string
	AvatarEndpoint               string
	AvatarRegion                 string
}

// MigrationConfig deliberately contains no authentication or avatar
// settings. A migration job receives only the separately managed direct
// database URL and therefore cannot accidentally become a second service
// instance with runtime secrets.
type MigrationConfig struct {
	DatabaseURL string
}

// MaintenanceConfig contains only the database and optional object-storage
// settings needed by the bounded maintenance command. It does not load
// session, recovery, abuse, or sync encryption keys.
type MaintenanceConfig struct {
	DatabaseURL            string
	DatabaseMaxConnections int32
	AvatarBucketName       string
	AvatarEndpoint         string
	AvatarRegion           string
}

func Load() (Config, error) {
	environment := strings.ToLower(strings.TrimSpace(os.Getenv("COLOSSEUM_ACCOUNT_ENV")))
	if environment == "" {
		environment = "development"
	}
	switch environment {
	case "development", "test", "production":
	default:
		return Config{}, fmt.Errorf("COLOSSEUM_ACCOUNT_ENV must be development, test, or production")
	}

	databaseURL := strings.TrimSpace(os.Getenv("DATABASE_URL"))
	if databaseURL == "" {
		return Config{}, errors.New("DATABASE_URL is required")
	}

	httpAddr := strings.TrimSpace(os.Getenv("HTTP_ADDR"))
	if httpAddr == "" {
		httpAddr = defaultHTTPAddr
	}

	var trustedProxyCIDRs []netip.Prefix
	if raw := strings.TrimSpace(os.Getenv("TRUSTED_PROXY_CIDRS")); raw != "" {
		for _, entry := range strings.Split(raw, ",") {
			prefix, err := netip.ParsePrefix(strings.TrimSpace(entry))
			if err != nil || prefix.Bits() == 0 || prefix.Addr().Is4In6() {
				return Config{}, errors.New("TRUSTED_PROXY_CIDRS must contain explicit IPv4/IPv6 CIDRs, never a catch-all")
			}
			trustedProxyCIDRs = append(trustedProxyCIDRs, prefix.Masked())
		}
	}
	origins, err := collectionOriginsEnv()
	if err != nil {
		return Config{}, err
	}
	edgeSecret := os.Getenv("COLLECTIONS_EDGE_SECRET")
	if edgeSecret != "" && (len(edgeSecret) < minCollectionsEdgeSecretLength || strings.TrimSpace(edgeSecret) != edgeSecret) {
		return Config{}, fmt.Errorf("COLLECTIONS_EDGE_SECRET must be at least %d characters with no surrounding whitespace", minCollectionsEdgeSecretLength)
	}

	maxConnections, err := positiveInt32Env("DATABASE_MAX_CONNECTIONS", defaultDatabaseMaxConnections)
	if err != nil {
		return Config{}, err
	}

	recoveryHMACKey, err := base64KeyEnv("RECOVERY_HMAC_KEY", 32, false)
	if err != nil {
		return Config{}, err
	}
	abuseHMACKey, err := base64KeyEnv("ABUSE_HMAC_KEY", 32, false)
	if err != nil {
		return Config{}, err
	}
	sessionWrapKey, err := base64KeyEnv("SESSION_WRAP_KEY", 32, true)
	if err != nil {
		return Config{}, err
	}
	syncDataKey, err := base64KeyEnv("SYNC_DATA_KEY", 32, true)
	if err != nil {
		return Config{}, err
	}
	syncSkewSeconds, err := positiveIntEnv(
		"SYNC_MAX_FUTURE_SKEW_SECONDS",
		int(defaultSyncMaxFutureSkew/time.Second))
	if err != nil {
		return Config{}, err
	}

	passwordBlocklistPath := strings.TrimSpace(os.Getenv("PASSWORD_BLOCKLIST_PATH"))

	globalRegistrationLimit, err := positiveIntEnv("REGISTRATION_GLOBAL_LIMIT_10M", defaultRegistrationGlobal10m)
	if err != nil {
		return Config{}, err
	}

	avatarBucketName := strings.TrimSpace(os.Getenv("BUCKET_NAME"))
	avatarEndpoint := strings.TrimSpace(os.Getenv("AWS_ENDPOINT_URL_S3"))
	avatarRegion := strings.TrimSpace(os.Getenv("AWS_REGION"))
	if avatarRegion == "" {
		avatarRegion = "auto"
	}

	// Preserve deployments using built-in avatars without upload storage.
	// Only an explicit disabled value waives the production storage requirement.
	avatarStorage := strings.ToLower(strings.TrimSpace(os.Getenv("AVATAR_STORAGE")))
	if environment == "production" && avatarStorage != "disabled" {
		if avatarBucketName == "" {
			return Config{}, errors.New("BUCKET_NAME is required in production")
		}
		if avatarEndpoint == "" {
			return Config{}, errors.New("AWS_ENDPOINT_URL_S3 is required in production")
		}
	}

	return Config{
		Environment:                  environment,
		HTTPAddr:                     httpAddr,
		TrustedProxyCIDRs:            trustedProxyCIDRs,
		CollectionsAllowedWebOrigins: origins,
		CollectionsEdgeSecret:        edgeSecret,
		DatabaseURL:                  databaseURL,
		DatabaseMaxConnections:       maxConnections,
		ReadinessTimeout:             defaultReadinessTimeout,
		ShutdownTimeout:              defaultShutdownTimeout,
		RecoveryHMACKey:              recoveryHMACKey,
		AbuseHMACKey:                 abuseHMACKey,
		SessionWrapKey:               sessionWrapKey,
		SyncDataKey:                  syncDataKey,
		SyncMaxFutureSkew:            time.Duration(syncSkewSeconds) * time.Second,
		PasswordBlocklistPath:        passwordBlocklistPath,
		RegistrationGlobalLimit10m:   globalRegistrationLimit,
		AvatarBucketName:             avatarBucketName,
		AvatarEndpoint:               avatarEndpoint,
		AvatarRegion:                 avatarRegion,
	}, nil
}

func LoadMigration() (MigrationConfig, error) {
	databaseURL := strings.TrimSpace(os.Getenv("MIGRATION_DATABASE_URL"))
	if databaseURL == "" {
		return MigrationConfig{}, errors.New("MIGRATION_DATABASE_URL is required")
	}
	if runtimeURL := strings.TrimSpace(os.Getenv("DATABASE_URL")); runtimeURL != "" && runtimeURL == databaseURL {
		return MigrationConfig{}, errors.New("MIGRATION_DATABASE_URL must be separate from DATABASE_URL")
	}
	return MigrationConfig{DatabaseURL: databaseURL}, nil
}

func LoadMaintenance() (MaintenanceConfig, error) {
	databaseURL := strings.TrimSpace(os.Getenv("MAINTENANCE_DATABASE_URL"))
	if databaseURL == "" {
		databaseURL = strings.TrimSpace(os.Getenv("DATABASE_URL"))
	}
	if databaseURL == "" {
		return MaintenanceConfig{}, errors.New("DATABASE_URL or MAINTENANCE_DATABASE_URL is required")
	}

	maxConnections, err := positiveInt32Env(
		"MAINTENANCE_DATABASE_MAX_CONNECTIONS",
		defaultMaintenanceConnections)
	if err != nil {
		return MaintenanceConfig{}, err
	}

	bucket := strings.TrimSpace(os.Getenv("BUCKET_NAME"))
	endpoint := strings.TrimSpace(os.Getenv("AWS_ENDPOINT_URL_S3"))
	region := strings.TrimSpace(os.Getenv("AWS_REGION"))
	if region == "" {
		region = "auto"
	}
	if bucket != "" && endpoint == "" {
		return MaintenanceConfig{}, errors.New("AWS_ENDPOINT_URL_S3 is required when BUCKET_NAME is set")
	}

	return MaintenanceConfig{
		DatabaseURL:            databaseURL,
		DatabaseMaxConnections: maxConnections,
		AvatarBucketName:       bucket,
		AvatarEndpoint:         endpoint,
		AvatarRegion:           region,
	}, nil
}

func positiveInt32Env(name string, fallback int) (int32, error) {
	const maxInt32 = int64(1<<31 - 1)
	raw := strings.TrimSpace(os.Getenv(name))
	if raw == "" {
		if int64(fallback) > maxInt32 {
			return 0, fmt.Errorf("%s is too large", name)
		}
		return int32(fallback), nil
	}
	value, err := strconv.ParseInt(raw, 10, 64)
	if err != nil || value <= 0 {
		return 0, fmt.Errorf("%s must be a positive integer", name)
	}
	if value > maxInt32 {
		return 0, fmt.Errorf("%s is too large", name)
	}
	return int32(value), nil
}

func positiveIntEnv(name string, fallback int) (int, error) {
	raw := strings.TrimSpace(os.Getenv(name))
	if raw == "" {
		return fallback, nil
	}
	value, err := strconv.Atoi(raw)
	if err != nil || value <= 0 {
		return 0, fmt.Errorf("%s must be a positive integer", name)
	}
	return value, nil
}

func base64KeyEnv(name string, minimumBytes int, exact bool) ([]byte, error) {
	raw := strings.TrimSpace(os.Getenv(name))
	if raw == "" {
		return nil, fmt.Errorf("%s is required", name)
	}
	value, err := base64.StdEncoding.DecodeString(raw)
	if err != nil {
		return nil, fmt.Errorf("%s must be standard base64: %w", name, err)
	}
	if exact {
		if len(value) != minimumBytes {
			return nil, fmt.Errorf("%s must decode to exactly %d bytes", name, minimumBytes)
		}
		return value, nil
	}
	if len(value) < minimumBytes {
		return nil, fmt.Errorf("%s must decode to at least %d bytes", name, minimumBytes)
	}
	return value, nil
}

// Origins are exact scheme/host/port values, never URLs with credentials or paths.
func collectionOriginsEnv() ([]string, error) {
	raw := strings.TrimSpace(os.Getenv("COLLECTIONS_ALLOWED_WEB_ORIGINS"))
	if raw == "" {
		return nil, nil
	}
	var origins []string
	for _, entry := range strings.Split(raw, ",") {
		origin := strings.TrimSpace(entry)
		u, err := url.Parse(origin)
		if err != nil || u == nil || (u.Scheme != "https" && u.Scheme != "http") || u.Host == "" || u.User != nil || u.Opaque != "" || u.Path != "" || u.RawQuery != "" || u.ForceQuery || u.Fragment != "" || strings.ContainsAny(origin, "?#") || strings.HasSuffix(u.Host, ":") {
			return nil, errors.New("COLLECTIONS_ALLOWED_WEB_ORIGINS must contain exact HTTP(S) origins without paths, credentials, queries or fragments")
		}
		host := u.Hostname()
		if host == "" {
			return nil, errors.New("COLLECTIONS_ALLOWED_WEB_ORIGINS requires a host")
		}
		if ip, err := netip.ParseAddr(host); err == nil {
			if ip.Zone() != "" {
				return nil, errors.New("COLLECTIONS_ALLOWED_WEB_ORIGINS cannot contain scoped addresses")
			}
		} else {
			for _, label := range strings.Split(host, ".") {
				if label == "" || len(label) > 63 || strings.HasPrefix(label, "-") || strings.HasSuffix(label, "-") {
					return nil, errors.New("COLLECTIONS_ALLOWED_WEB_ORIGINS contains an invalid host")
				}
				for _, ch := range label {
					if !(ch >= 'a' && ch <= 'z' || ch >= 'A' && ch <= 'Z' || ch >= '0' && ch <= '9' || ch == '-') {
						return nil, errors.New("COLLECTIONS_ALLOWED_WEB_ORIGINS contains an invalid host")
					}
				}
			}
		}
		if port := u.Port(); port != "" {
			n, err := strconv.Atoi(port)
			if err != nil || n < 1 || n > 65535 {
				return nil, errors.New("COLLECTIONS_ALLOWED_WEB_ORIGINS contains an invalid port")
			}
		}
		origins = append(origins, origin)
	}
	return origins, nil
}
