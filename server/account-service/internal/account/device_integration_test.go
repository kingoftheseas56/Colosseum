package account

import (
	"context"
	"errors"
	"testing"
	"time"
)

func TestNewDeviceProtectionRequiresTrustedApprovalAndIsOneUse(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ProtectedOwner")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	if _, err := fixture.service.SetNewDeviceProtection(
		context.Background(),
		auth,
		true); err != nil {
		t.Fatalf("SetNewDeviceProtection() error = %v", err)
	}

	signIn, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "ProtectedOwner",
			Password:        testPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Laptop",
			Platform:        "Windows",
			SourceKey:       "203.0.113.70",
		})
	if err != nil {
		t.Fatalf("protected SignIn() error = %v", err)
	}
	if signIn.Status != "approval_required" || signIn.ChallengeToken == "" {
		t.Fatalf("protected SignIn() = %#v, want approval_required", signIn)
	}
	if signIn.Session != nil {
		t.Fatal("protected SignIn() issued a session before approval")
	}

	approvals, err := fixture.service.Approvals(context.Background(), auth)
	if err != nil {
		t.Fatalf("Approvals() error = %v", err)
	}
	if len(approvals) != 1 || approvals[0].Kind != "device_signin" {
		t.Fatalf("Approvals() = %#v, want one device_signin approval", approvals)
	}

	if err := fixture.service.DecideApproval(
		context.Background(),
		auth,
		"device_signin",
		approvals[0].ID,
		true); err != nil {
		t.Fatalf("DecideApproval() error = %v", err)
	}

	polled, err := fixture.service.PollDeviceSignInChallenge(
		context.Background(),
		signIn.ChallengeToken)
	if err != nil {
		t.Fatalf("PollDeviceSignInChallenge() error = %v", err)
	}
	if polled.Status != "signed_in" || polled.Session == nil {
		t.Fatalf("approved poll = %#v, want signed_in session", polled)
	}

	if _, err := fixture.service.PollDeviceSignInChallenge(
		context.Background(),
		signIn.ChallengeToken); !errors.Is(err, ErrChallengeInvalid) {
		t.Fatalf("second poll error = %v, want ErrChallengeInvalid", err)
	}
}

func TestNewDeviceProtectionChallengeExpires(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ExpiryOwner")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	if _, err := fixture.service.SetNewDeviceProtection(
		context.Background(),
		auth,
		true); err != nil {
		t.Fatalf("SetNewDeviceProtection() error = %v", err)
	}

	signIn, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "ExpiryOwner",
			Password:        testPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Laptop",
			Platform:        "Windows",
			SourceKey:       "203.0.113.71",
		})
	if err != nil {
		t.Fatalf("SignIn() error = %v", err)
	}

	fixture.clock.Advance(deviceChallengeLifetime + time.Second)
	if _, err := fixture.service.PollDeviceSignInChallenge(
		context.Background(),
		signIn.ChallengeToken); !errors.Is(err, ErrChallengeExpired) {
		t.Fatalf("expired poll error = %v, want ErrChallengeExpired", err)
	}
}

func TestRecoveryKeyFallbackConsumesKeyAndTrustsTargetDevice(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "FallbackOwner")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	if _, err := fixture.service.SetNewDeviceProtection(
		context.Background(),
		auth,
		true); err != nil {
		t.Fatalf("SetNewDeviceProtection() error = %v", err)
	}

	signIn, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "FallbackOwner",
			Password:        testPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Laptop",
			Platform:        "Windows",
			SourceKey:       "203.0.113.72",
		})
	if err != nil {
		t.Fatalf("SignIn() error = %v", err)
	}

	recovered, err := fixture.service.RecoverDeviceSignInWithKey(
		context.Background(),
		ChallengeRecoveryInput{
			ChallengeToken: signIn.ChallengeToken,
			RecoveryKey:    created.RecoveryKey,
		})
	if err != nil {
		t.Fatalf("RecoverDeviceSignInWithKey() error = %v", err)
	}
	if recovered.RecoveryKey == "" || recovered.RecoveryKey == created.RecoveryKey {
		t.Fatal("recovery fallback did not rotate the recovery key")
	}
	if recovered.Session.Device.InstallID != deviceBInstall ||
		!recovered.Session.Device.Trusted {
		t.Fatalf("recovered device = %#v, want trusted target device", recovered.Session.Device)
	}

	if _, err := fixture.service.RecoverPassword(
		context.Background(),
		RecoverPasswordInput{
			Username:    "FallbackOwner",
			RecoveryKey: created.RecoveryKey,
			NewPassword: secondPassword,
			SourceKey:   "203.0.113.73",
		}); !errors.Is(err, ErrInvalidCredentials) {
		t.Fatalf("consumed recovery-key error = %v, want ErrInvalidCredentials", err)
	}
}

func TestDeviceListAndRevokeAreAccountScoped(t *testing.T) {
	fixture := newServiceFixture(t)
	first := createFixtureAccount(t, fixture, "DeviceOwnerA")
	authA := authenticateFixtureSession(t, fixture, first.Session)

	secondAccount, err := fixture.service.CreateAccount(
		context.Background(),
		CreateAccountInput{
			Username:        "DeviceOwnerB",
			Password:        secondPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Other Account PC",
			Platform:        "Windows",
			SourceKey:       "203.0.113.74",
		})
	if err != nil {
		t.Fatalf("second CreateAccount() error = %v", err)
	}
	authB := authenticateFixtureSession(t, fixture, secondAccount.Session)

	devicesA, err := fixture.service.ListDevices(context.Background(), authA)
	if err != nil {
		t.Fatalf("ListDevices(A) error = %v", err)
	}
	if len(devicesA) != 1 || devicesA[0].ID != authA.Device.ID {
		t.Fatalf("devices A = %#v, want only A device", devicesA)
	}

	if err := fixture.service.RevokeDevice(
		context.Background(),
		authA,
		authB.Device.ID); !errors.Is(err, ErrDeviceNotFound) {
		t.Fatalf("cross-account revoke error = %v, want ErrDeviceNotFound", err)
	}

	if _, err := fixture.service.AuthenticateAccessToken(
		context.Background(),
		secondAccount.Session.AccessToken); err != nil {
		t.Fatalf("cross-account revoke damaged B session: %v", err)
	}
}
