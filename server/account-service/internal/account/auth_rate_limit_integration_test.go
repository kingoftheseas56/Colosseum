package account

import (
	"context"
	"errors"
	"testing"
	"time"
)

func requireRateScope(t *testing.T, err error, scope string, max time.Duration) {
	t.Helper()
	var rate *RateLimitError
	if !errors.As(err, &rate) || rate.Scope != scope || rate.RetryAfter <= 0 || rate.RetryAfter > max {
		t.Fatalf("expected %s rate limit with retry in (0,%v], got %v", scope, max, err)
	}
}

func TestSignInSourceIsolationAndWindow(t *testing.T) {
	f := newServiceFixture(t)
	createFixtureAccount(t, f, "LoginOwner")
	ctx := context.Background()
	input := SignInInput{Username: "LoginOwner", Password: "incorrect password", DeviceInstallID: deviceAInstall, DeviceLabel: "Test", Platform: "Windows", SourceKey: "198.51.100.42"}
	for i := 0; i < 10; i++ {
		// Different usernames cannot evade the shared source budget. Unknown
		// and invalid usernames count before credential validation as before.
		if i%2 == 1 {
			input.Username = "x"
		} else {
			input.Username = "LoginOwner"
		}
		if _, err := f.service.SignIn(ctx, input); !errors.Is(err, ErrInvalidCredentials) {
			t.Fatalf("attempt %d: %v", i+1, err)
		}
	}
	input.Username, input.Password = "LoginOwner", testPassword
	_, err := f.service.SignIn(ctx, input)
	requireRateScope(t, err, "signin_source", 15*time.Minute)
	input.SourceKey = "198.51.100.43"
	result, err := f.service.SignIn(ctx, input)
	if err != nil || result.Session == nil {
		t.Fatalf("unrelated client login: %v", err)
	}
	if _, err := f.service.AuthenticateAccessToken(ctx, result.Session.AccessToken); err != nil {
		t.Fatalf("login token: %v", err)
	}
	f.clock.Advance(15*time.Minute + time.Millisecond)
	input.SourceKey = "198.51.100.42"
	if result, err = f.service.SignIn(ctx, input); err != nil || result.Session == nil {
		t.Fatalf("login after window: %v", err)
	}
}

func TestRegistrationAttemptSourceIsolation(t *testing.T) {
	f := newServiceFixture(t)
	ctx := context.Background()
	input := CreateAccountInput{Username: "x", Password: testPassword, DeviceInstallID: deviceAInstall, DeviceLabel: "Test", Platform: "Windows", SourceKey: "198.51.100.42"}
	for i := 0; i < 10; i++ {
		if _, err := f.service.CreateAccount(ctx, input); !errors.Is(err, ErrInvalidUsername) {
			t.Fatalf("invalid attempt %d: %v", i+1, err)
		}
	}
	input.Username = "FreshRegistration"
	_, err := f.service.CreateAccount(ctx, input)
	requireRateScope(t, err, "create_attempt_source", time.Hour)
	input.SourceKey = "198.51.100.43"
	result, err := f.service.CreateAccount(ctx, input)
	if err != nil || result.RecoveryKey == "" {
		t.Fatalf("unrelated client registration: %v", err)
	}
	if _, err := f.service.AuthenticateAccessToken(ctx, result.Session.AccessToken); err != nil {
		t.Fatalf("registration token: %v", err)
	}
	f.clock.Advance(time.Hour + time.Millisecond)
	input.SourceKey, input.Username = "198.51.100.42", "RecoveredRegistration"
	if _, err := f.service.CreateAccount(ctx, input); err != nil {
		t.Fatalf("registration after window: %v", err)
	}
}

func TestRegistrationUsernameBudgetStillProtectsProbing(t *testing.T) {
	f := newServiceFixture(t)
	input := CreateAccountInput{Username: "ProbeOwner", Password: "short", DeviceInstallID: deviceAInstall, DeviceLabel: "Test", Platform: "Windows", SourceKey: "198.51.100.42"}
	for i := 0; i < 5; i++ {
		if _, err := f.service.CreateAccount(context.Background(), input); !errors.Is(err, ErrInvalidPassword) {
			t.Fatalf("attempt %d: %v", i+1, err)
		}
	}
	input.Username = "pRoBeOwNeR"
	_, err := f.service.CreateAccount(context.Background(), input)
	requireRateScope(t, err, "create_attempt_username", time.Hour)
}

func TestRegistrationSuccessBudgetsAndRollback(t *testing.T) {
	f := newServiceFixture(t)
	ctx := context.Background()
	input := CreateAccountInput{Password: testPassword, DeviceInstallID: deviceAInstall, DeviceLabel: "Test", Platform: "Windows", SourceKey: "198.51.100.42"}
	for _, name := range []string{"FirstSuccess", "SecondSuccess"} {
		input.Username = name
		if _, err := f.service.CreateAccount(ctx, input); err != nil {
			t.Fatal(err)
		}
	}
	input.Username = "ThirdSuccess"
	_, err := f.service.CreateAccount(ctx, input)
	requireRateScope(t, err, "create_success_source", 24*time.Hour)
	input.SourceKey = "198.51.100.43"
	if _, err := f.service.CreateAccount(ctx, input); err != nil {
		t.Fatalf("unrelated source or rolled-back username reservation blocked: %v", err)
	}
	// Global success circuit breaker still applies across independent sources.
	f.service.registrationGlobalLimit = 3
	input.SourceKey, input.Username = "198.51.100.44", "GlobalLimited"
	_, err = f.service.CreateAccount(ctx, input)
	requireRateScope(t, err, "create_success_global", 10*time.Minute)
	f.clock.Advance(10*time.Minute + time.Millisecond)
	if _, err = f.service.CreateAccount(ctx, input); err != nil {
		t.Fatalf("global window or reservation did not recover: %v", err)
	}
	f.clock.Advance(24 * time.Hour)
	input.SourceKey, input.Username = "198.51.100.42", "DayLater"
	if _, err = f.service.CreateAccount(ctx, input); err != nil {
		t.Fatalf("source success window: %v", err)
	}
}
