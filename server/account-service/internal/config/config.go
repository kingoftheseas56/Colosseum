package config

import (
	"encoding/base64"
	"errors"
	"fmt"
	"os"
	"strconv"
	"strings"
	"time"
)

const (
	defaultHTTPAddr               = ":8080"
	defaultDatabaseMaxConnections = 8
	defaultReadinessTimeout       = 2 * time.Second
	defaultShutdownTimeout        = 10 * time.Second
	defaultRegistrationGlobal10m  = 500
	defaultSyncMaxFutureSkew      = 10 * time.Minute
)

type Config struct {
	Environment                string
	HTTPAddr                   string
	DatabaseURL                string
	DatabaseMaxConnections     int32
	ReadinessTimeout           time.Duration
	ShutdownTimeout            time.Duration
	RecoveryHMACKey            []byte
	AbuseHMACKey               []byte
	SessionWrapKey             []byte
	SyncDataKey                []byte
	SyncMaxFutureSkew          time.Duration
	PasswordBlocklistPath      string
	RegistrationGlobalLimit10m int
	AvatarBucketName           string
	AvatarEndpoint             string
	AvatarRegion               string
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

	if environment == "production" {
		if avatarBucketName == "" {
			return Config{}, errors.New("BUCKET_NAME is required in production")
		}
		if avatarEndpoint == "" {
			return Config{}, errors.New("AWS_ENDPOINT_URL_S3 is required in production")
		}
	}

	return Config{
		Environment:                environment,
		HTTPAddr:                   httpAddr,
		DatabaseURL:                databaseURL,
		DatabaseMaxConnections:     maxConnections,
		ReadinessTimeout:           defaultReadinessTimeout,
		ShutdownTimeout:            defaultShutdownTimeout,
		RecoveryHMACKey:            recoveryHMACKey,
		AbuseHMACKey:               abuseHMACKey,
		SessionWrapKey:             sessionWrapKey,
		SyncDataKey:                syncDataKey,
		SyncMaxFutureSkew:          time.Duration(syncSkewSeconds) * time.Second,
		PasswordBlocklistPath:      passwordBlocklistPath,
		RegistrationGlobalLimit10m: globalRegistrationLimit,
		AvatarBucketName:           avatarBucketName,
		AvatarEndpoint:             avatarEndpoint,
		AvatarRegion:               avatarRegion,
	}, nil
}

func positiveInt32Env(name string, fallback int) (int32, error) {
	value, err := positiveIntEnv(name, fallback)
	if err != nil {
		return 0, err
	}
	if int64(value) > int64(^uint32(0)>>1) {
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
