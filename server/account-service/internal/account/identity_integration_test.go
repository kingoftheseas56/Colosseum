package account

import (
	"context"
	"errors"
	"testing"
	"time"
)

func TestUsernameReservationIsCaseInsensitiveAndPermanent(t *testing.T) {
	fixture := newServiceFixture(t)
	first := createFixtureAccount(t, fixture, "Hemanth56")
	auth := authenticateFixtureSession(t, fixture, first.Session)

	if _, err := fixture.service.CreateAccount(
		context.Background(),
		CreateAccountInput{
			Username:        "hEmAnTh56",
			Password:        secondPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Second PC",
			Platform:        "Windows",
			SourceKey:       "203.0.113.11",
		}); !errors.Is(err, ErrUsernameUnavailable) {
		t.Fatalf("case-insensitive duplicate error = %v, want ErrUsernameUnavailable", err)
	}

	updated, err := fixture.service.RenameUsername(
		context.Background(),
		auth,
		RenameUsernameInput{NewUsername: "SeaKing_56"})
	if err != nil {
		t.Fatalf("RenameUsername() error = %v", err)
	}
	if updated.DisplayUsername != "SeaKing_56" {
		t.Fatalf("display username = %q, want SeaKing_56", updated.DisplayUsername)
	}

	if _, err := fixture.service.CreateAccount(
		context.Background(),
		CreateAccountInput{
			Username:        "HEMANTH56",
			Password:        secondPassword,
			DeviceInstallID: deviceCInstall,
			DeviceLabel:     "Third PC",
			Platform:        "Windows",
			SourceKey:       "203.0.113.12",
		}); !errors.Is(err, ErrUsernameUnavailable) {
		t.Fatalf("old username reuse error = %v, want ErrUsernameUnavailable", err)
	}

	if _, err := fixture.service.RenameUsername(
		context.Background(),
		auth,
		RenameUsernameInput{NewUsername: "Another_Name"}); !errors.Is(err, ErrRenameCooldown) {
		t.Fatalf("cooldown error = %v, want ErrRenameCooldown", err)
	}

	fixture.clock.Advance(30*24*time.Hour + time.Second)
	auth.Account = updated

	renamedAgain, err := fixture.service.RenameUsername(
		context.Background(),
		auth,
		RenameUsernameInput{NewUsername: "Another_Name"})
	if err != nil {
		t.Fatalf("RenameUsername() after cooldown error = %v", err)
	}
	if renamedAgain.DisplayUsername != "Another_Name" {
		t.Fatalf("second rename = %q, want Another_Name", renamedAgain.DisplayUsername)
	}
}

func TestSignInAlwaysPerformsPasswordVerification(t *testing.T) {
	fixture := newServiceFixture(t)
	createFixtureAccount(t, fixture, "TimingOwner")

	tests := []struct {
		name      string
		username  string
		sourceKey string
	}{
		{
			name:      "unknown username",
			username:  "UnknownTimingOwner",
			sourceKey: "203.0.113.50",
		},
		{
			name:      "existing username wrong password",
			username:  "TimingOwner",
			sourceKey: "203.0.113.51",
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			calls := 0
			fixture.service.passwordVerify = func(encoded, password string) (bool, error) {
				calls++
				return false, nil
			}

			_, err := fixture.service.SignIn(
				context.Background(),
				SignInInput{
					Username:        test.username,
					Password:        "wrong password value",
					DeviceInstallID: deviceBInstall,
					DeviceLabel:     "Timing Test PC",
					Platform:        "Windows",
					SourceKey:       test.sourceKey,
				})
			if !errors.Is(err, ErrInvalidCredentials) {
				t.Fatalf("error = %v, want ErrInvalidCredentials", err)
			}
			if calls != 1 {
				t.Fatalf("password verify calls = %d, want 1", calls)
			}
		})
	}
}

func TestPasswordChangeKeepsCurrentSessionAndRevokesOthers(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "PasswordOwner")

	second, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "passwordowner",
			Password:        testPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Laptop",
			Platform:        "Windows",
			SourceKey:       "203.0.113.20",
		})
	if err != nil {
		t.Fatalf("SignIn() second device error = %v", err)
	}
	if second.Session == nil {
		t.Fatal("second sign-in did not produce a session")
	}

	auth := authenticateFixtureSession(t, fixture, created.Session)
	if err := fixture.service.ChangePassword(
		context.Background(),
		auth,
		ChangePasswordInput{
			CurrentPassword: testPassword,
			NewPassword:     secondPassword,
		}); err != nil {
		t.Fatalf("ChangePassword() error = %v", err)
	}

	if _, err := fixture.service.AuthenticateAccessToken(
		context.Background(),
		created.Session.AccessToken); err != nil {
		t.Fatalf("current access token stopped working: %v", err)
	}

	if _, err := fixture.service.AuthenticateAccessToken(
		context.Background(),
		second.Session.AccessToken); !errors.Is(err, ErrSessionInvalid) {
		t.Fatalf("other access token error = %v, want ErrSessionInvalid", err)
	}

	if _, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "PasswordOwner",
			Password:        testPassword,
			DeviceInstallID: deviceCInstall,
			DeviceLabel:     "Old Password PC",
			Platform:        "Windows",
			SourceKey:       "203.0.113.21",
		}); !errors.Is(err, ErrInvalidCredentials) {
		t.Fatalf("old-password sign-in error = %v, want ErrInvalidCredentials", err)
	}

	newPasswordSignIn, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "PasswordOwner",
			Password:        secondPassword,
			DeviceInstallID: deviceCInstall,
			DeviceLabel:     "New Password PC",
			Platform:        "Windows",
			SourceKey:       "203.0.113.22",
		})
	if err != nil {
		t.Fatalf("new-password sign-in error = %v", err)
	}
	if newPasswordSignIn.Session == nil {
		t.Fatal("new-password sign-in did not produce a session")
	}
}

func TestPasswordRecoveryConsumesKeyAndRevokesSessions(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "RecoveryOwner")

	result, err := fixture.service.RecoverPassword(
		context.Background(),
		RecoverPasswordInput{
			Username:    "recoveryowner",
			RecoveryKey: created.RecoveryKey,
			NewPassword: secondPassword,
			SourceKey:   "203.0.113.30",
		})
	if err != nil {
		t.Fatalf("RecoverPassword() error = %v", err)
	}
	if result.RecoveryKey == "" || result.RecoveryKey == created.RecoveryKey {
		t.Fatal("recovery did not issue a fresh recovery key")
	}

	if _, err := fixture.service.AuthenticateAccessToken(
		context.Background(),
		created.Session.AccessToken); !errors.Is(err, ErrSessionInvalid) {
		t.Fatalf("pre-recovery session error = %v, want ErrSessionInvalid", err)
	}

	if _, err := fixture.service.RecoverPassword(
		context.Background(),
		RecoverPasswordInput{
			Username:    "RecoveryOwner",
			RecoveryKey: created.RecoveryKey,
			NewPassword: thirdPassword,
			SourceKey:   "203.0.113.31",
		}); !errors.Is(err, ErrInvalidCredentials) {
		t.Fatalf("replayed recovery-key error = %v, want ErrInvalidCredentials", err)
	}

	signIn, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "RecoveryOwner",
			Password:        secondPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Recovered PC",
			Platform:        "Windows",
			SourceKey:       "203.0.113.32",
		})
	if err != nil {
		t.Fatalf("recovered-password sign-in error = %v", err)
	}
	if signIn.Session == nil {
		t.Fatal("recovered-password sign-in did not produce a session")
	}
}

func TestManualRecoveryKeyReplacementRequiresCurrentPassword(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "KeyOwner")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	if _, err := fixture.service.ReplaceRecoveryKey(
		context.Background(),
		auth,
		ReplaceRecoveryKeyInput{CurrentPassword: "wrong password value"}); !errors.Is(err, ErrInvalidCredentials) {
		t.Fatalf("wrong-password replacement error = %v, want ErrInvalidCredentials", err)
	}

	replaced, err := fixture.service.ReplaceRecoveryKey(
		context.Background(),
		auth,
		ReplaceRecoveryKeyInput{CurrentPassword: testPassword})
	if err != nil {
		t.Fatalf("ReplaceRecoveryKey() error = %v", err)
	}
	if replaced.RecoveryKey == "" || replaced.RecoveryKey == created.RecoveryKey {
		t.Fatal("replacement recovery key was not fresh")
	}

	if _, err := fixture.service.RecoverPassword(
		context.Background(),
		RecoverPasswordInput{
			Username:    "KeyOwner",
			RecoveryKey: created.RecoveryKey,
			NewPassword: secondPassword,
			SourceKey:   "203.0.113.41",
		}); !errors.Is(err, ErrInvalidCredentials) {
		t.Fatalf("old replacement key error = %v, want ErrInvalidCredentials", err)
	}
}
