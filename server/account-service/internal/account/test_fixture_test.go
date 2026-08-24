package account

import (
	"bytes"
	"context"
	"errors"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/avatar"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/database"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/testsupport/testdb"
)

const (
	testPassword   = "Mango river lantern stone 773!"
	secondPassword = "Orbit silver library cedar 884!"
	thirdPassword  = "Harbor violet window quartz 995!"
	deviceAInstall = "11111111-1111-4111-8111-111111111111"
	deviceBInstall = "22222222-2222-4222-8222-222222222222"
	deviceCInstall = "33333333-3333-4333-8333-333333333333"
)

type fakeClock struct {
	now time.Time
}

func (c *fakeClock) Now() time.Time {
	return c.now.UTC()
}

func (c *fakeClock) Advance(duration time.Duration) {
	c.now = c.now.Add(duration)
}

type memoryAvatarStore struct {
	objects map[string][]byte
}

func newMemoryAvatarStore() *memoryAvatarStore {
	return &memoryAvatarStore{objects: make(map[string][]byte)}
}

func (s *memoryAvatarStore) Put(
	_ context.Context,
	accountID string,
	data []byte,
) (string, error) {
	key := "avatars/" + accountID + "/fixture.png"
	s.objects[key] = append([]byte(nil), data...)
	return key, nil
}

func (s *memoryAvatarStore) Delete(_ context.Context, key string) error {
	delete(s.objects, key)
	return nil
}

func (s *memoryAvatarStore) PresignGet(
	_ context.Context,
	key string,
	_ time.Duration,
) (string, error) {
	if _, ok := s.objects[key]; !ok {
		return "", errors.New("avatar missing")
	}
	return "https://avatar.invalid/" + key, nil
}

type serviceFixture struct {
	service *Service
	pool    *pgxpool.Pool
	clock   *fakeClock
	avatars *memoryAvatarStore
}

func newServiceFixture(t *testing.T) serviceFixture {
	t.Helper()

	pool := testdb.Open(t)
	testdb.ResetPublicSchema(t, pool)

	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	if err := database.RunMigrations(ctx, pool); err != nil {
		t.Fatalf("RunMigrations() error = %v", err)
	}

	clock := &fakeClock{
		now: time.Date(2026, 8, 15, 16, 30, 0, 0, time.UTC),
	}
	passwordHasher, err := NewPasswordHasher(DefaultArgon2Params())
	if err != nil {
		t.Fatalf("NewPasswordHasher() error = %v", err)
	}
	recoveryVerifier, err := NewRecoveryKeyVerifier(bytes.Repeat([]byte{0x31}, 32))
	if err != nil {
		t.Fatalf("NewRecoveryKeyVerifier() error = %v", err)
	}
	sessionCipher, err := NewSessionCipher(bytes.Repeat([]byte{0x52}, 32))
	if err != nil {
		t.Fatalf("NewSessionCipher() error = %v", err)
	}
	syncCipher, err := NewSyncPayloadCipher(bytes.Repeat([]byte{0x64}, 32))
	if err != nil {
		t.Fatalf("NewSyncPayloadCipher() error = %v", err)
	}
	rateLimiter, err := NewRateLimiter(
		pool,
		bytes.Repeat([]byte{0x73}, 32),
		clock)
	if err != nil {
		t.Fatalf("NewRateLimiter() error = %v", err)
	}

	avatars := newMemoryAvatarStore()
	service, err := NewService(Dependencies{
		Pool: pool,
		PasswordPolicy: PasswordPolicy{
			Blocklist: NewMemoryPasswordBlocklist([]string{
				"password123456789",
				"colosseum1234567",
			}),
		},
		PasswordHasher:          passwordHasher,
		RecoveryVerifier:        recoveryVerifier,
		SessionCipher:           sessionCipher,
		SyncCipher:              syncCipher,
		SyncMaxFutureSkew:       10 * time.Minute,
		RateLimiter:             rateLimiter,
		AvatarStore:             avatars,
		Clock:                   clock,
		RegistrationGlobalLimit: 500,
	})
	if err != nil {
		t.Fatalf("NewService() error = %v", err)
	}

	return serviceFixture{
		service: service,
		pool:    pool,
		clock:   clock,
		avatars: avatars,
	}
}

func createFixtureAccount(
	t *testing.T,
	fixture serviceFixture,
	username string,
) CreateAccountResult {
	t.Helper()

	result, err := fixture.service.CreateAccount(
		context.Background(),
		CreateAccountInput{
			Username:        username,
			Password:        testPassword,
			DeviceInstallID: deviceAInstall,
			DeviceLabel:     "Desktop",
			Platform:        "Windows",
			SourceKey:       "203.0.113.10",
		})
	if err != nil {
		t.Fatalf("CreateAccount() error = %v", err)
	}
	return result
}

func authenticateFixtureSession(
	t *testing.T,
	fixture serviceFixture,
	session IssuedSession,
) AuthenticatedSession {
	t.Helper()

	auth, err := fixture.service.AuthenticateAccessToken(
		context.Background(),
		session.AccessToken)
	if err != nil {
		t.Fatalf("AuthenticateAccessToken() error = %v", err)
	}
	return auth
}

func requireErrorIs(t *testing.T, err, target error) {
	t.Helper()
	if !errors.Is(err, target) {
		t.Fatalf("error = %v, want errors.Is(..., %v)", err, target)
	}
}

var _ avatar.Store = (*memoryAvatarStore)(nil)
