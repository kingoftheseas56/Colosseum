package account

import (
	"context"
	"errors"
	"testing"
	"time"
)

func TestRefreshRotationRetriesWithinGraceAndRevokesOnLateReplay(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "RefreshOwner")
	oldRefresh := created.Session.RefreshToken

	first, err := fixture.service.RefreshSession(context.Background(), oldRefresh)
	if err != nil {
		t.Fatalf("first RefreshSession() error = %v", err)
	}
	if first.Session.RefreshToken == "" || first.Session.RefreshToken == oldRefresh {
		t.Fatal("refresh token did not rotate")
	}

	fixture.clock.Advance(5 * time.Second)
	retry, err := fixture.service.RefreshSession(context.Background(), oldRefresh)
	if err != nil {
		t.Fatalf("grace retry RefreshSession() error = %v", err)
	}
	if retry.Session.RefreshToken != first.Session.RefreshToken {
		t.Fatal("grace retry returned a different rotated refresh token")
	}
	if retry.Session.AccessToken == first.Session.AccessToken {
		t.Fatal("grace retry did not issue a fresh access token")
	}

	fixture.clock.Advance(refreshRetryGrace + time.Second)
	if _, err := fixture.service.RefreshSession(context.Background(), oldRefresh); !errors.Is(err, ErrSessionRevoked) {
		t.Fatalf("late replay error = %v, want ErrSessionRevoked", err)
	}

	if _, err := fixture.service.RefreshSession(
		context.Background(),
		first.Session.RefreshToken); !errors.Is(err, ErrSessionRevoked) {
		t.Fatalf("rotated token after replay error = %v, want ErrSessionRevoked", err)
	}
}

func TestLogoutEverywhereRevokesCurrentAndOtherSessions(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "LogoutOwner")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	second, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "LogoutOwner",
			Password:        testPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Laptop",
			Platform:        "Windows",
			SourceKey:       "203.0.113.60",
		})
	if err != nil {
		t.Fatalf("second SignIn() error = %v", err)
	}
	if second.Session == nil {
		t.Fatal("second SignIn() did not issue a session")
	}

	if err := fixture.service.LogoutEverywhere(context.Background(), auth); err != nil {
		t.Fatalf("LogoutEverywhere() error = %v", err)
	}

	for label, token := range map[string]string{
		"current": created.Session.AccessToken,
		"other":   second.Session.AccessToken,
	} {
		if _, err := fixture.service.AuthenticateAccessToken(
			context.Background(),
			token); !errors.Is(err, ErrSessionInvalid) {
			t.Fatalf("%s access token error = %v, want ErrSessionInvalid", label, err)
		}
	}
}

func TestRevokeRefreshTokenIsIdempotent(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "RevokeRefreshOwner")

	if err := fixture.service.RevokeRefreshToken(
		context.Background(),
		created.Session.RefreshToken); err != nil {
		t.Fatalf("first RevokeRefreshToken() error = %v", err)
	}
	if err := fixture.service.RevokeRefreshToken(
		context.Background(),
		created.Session.RefreshToken); err != nil {
		t.Fatalf("second RevokeRefreshToken() error = %v", err)
	}

	if _, err := fixture.service.RefreshSession(
		context.Background(),
		created.Session.RefreshToken); !errors.Is(err, ErrSessionRevoked) &&
		!errors.Is(err, ErrSessionInvalid) {
		t.Fatalf("refresh after revoke error = %v, want revoked/invalid", err)
	}
}
