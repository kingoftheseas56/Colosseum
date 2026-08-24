package account

import (
	"bytes"
	"context"
	"errors"
	"image"
	"image/color"
	"image/png"
	"sync"
	"testing"
	"time"
)

func TestConcurrentCreateAllowsOnlyOneCanonicalUsername(t *testing.T) {
	fixture := newServiceFixture(t)

	type result struct {
		err error
	}
	start := make(chan struct{})
	results := make(chan result, 2)

	inputs := []CreateAccountInput{
		{
			Username:        "CollisionName",
			Password:        testPassword,
			DeviceInstallID: deviceAInstall,
			DeviceLabel:     "Desktop A",
			Platform:        "Windows",
			SourceKey:       "203.0.113.80",
		},
		{
			Username:        "collisionname",
			Password:        secondPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Desktop B",
			Platform:        "Windows",
			SourceKey:       "203.0.113.81",
		},
	}

	var wait sync.WaitGroup
	for _, input := range inputs {
		input := input
		wait.Add(1)
		go func() {
			defer wait.Done()
			<-start
			_, err := fixture.service.CreateAccount(context.Background(), input)
			results <- result{err: err}
		}()
	}
	close(start)
	wait.Wait()
	close(results)

	successes := 0
	unavailable := 0
	for result := range results {
		switch {
		case result.err == nil:
			successes++
		case errors.Is(result.err, ErrUsernameUnavailable):
			unavailable++
		default:
			t.Fatalf("unexpected concurrent CreateAccount() error = %v", result.err)
		}
	}
	if successes != 1 || unavailable != 1 {
		t.Fatalf("concurrent creates successes=%d unavailable=%d, want 1/1",
			successes, unavailable)
	}
}

func TestCreateAttemptRateLimitStopsEleventhAttempt(t *testing.T) {
	fixture := newServiceFixture(t)

	for index := 0; index < createAttemptLimit; index++ {
		_, _ = fixture.service.CreateAccount(
			context.Background(),
			CreateAccountInput{
				Username:        "x",
				Password:        "short",
				DeviceInstallID: deviceAInstall,
				DeviceLabel:     "Desktop",
				Platform:        "Windows",
				SourceKey:       "203.0.113.90",
			})
	}

	_, err := fixture.service.CreateAccount(
		context.Background(),
		CreateAccountInput{
			Username:        "StillBad",
			Password:        "short",
			DeviceInstallID: deviceAInstall,
			DeviceLabel:     "Desktop",
			Platform:        "Windows",
			SourceKey:       "203.0.113.90",
		})
	var rateLimit *RateLimitError
	if !errors.As(err, &rateLimit) {
		t.Fatalf("eleventh CreateAccount() error = %v, want RateLimitError", err)
	}
	if rateLimit.RetryAfter <= 0 {
		t.Fatalf("RetryAfter = %v, want positive", rateLimit.RetryAfter)
	}
}

func TestTrustedDeviceCanApprovePasswordRecovery(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "TrustedRecoveryOwner")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	started, err := fixture.service.StartTrustedRecovery(
		context.Background(),
		TrustedRecoveryInput{
			Username:        "TrustedRecoveryOwner",
			NewPassword:     secondPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Recovery Laptop",
			Platform:        "Windows",
			SourceKey:       "203.0.113.91",
		})
	if err != nil {
		t.Fatalf("StartTrustedRecovery() error = %v", err)
	}
	if started.Status != "approval_required" || started.ChallengeToken == "" {
		t.Fatalf("trusted recovery start = %#v, want approval_required", started)
	}

	approvals, err := fixture.service.Approvals(context.Background(), auth)
	if err != nil {
		t.Fatalf("Approvals() error = %v", err)
	}
	var challengeID string
	for _, approval := range approvals {
		if approval.Kind == "trusted_recovery" {
			challengeID = approval.ID
			break
		}
	}
	if challengeID == "" {
		t.Fatalf("Approvals() = %#v, missing trusted_recovery challenge", approvals)
	}

	if err := fixture.service.DecideApproval(
		context.Background(),
		auth,
		"trusted_recovery",
		challengeID,
		true); err != nil {
		t.Fatalf("DecideApproval() error = %v", err)
	}

	recovered, err := fixture.service.PollTrustedRecovery(
		context.Background(),
		started.ChallengeToken)
	if err != nil {
		t.Fatalf("PollTrustedRecovery() error = %v", err)
	}
	if recovered.Status != "recovered" || recovered.RecoveryKey == "" {
		t.Fatalf("trusted recovery result = %#v, want recovered + recovery key", recovered)
	}

	if _, err := fixture.service.AuthenticateAccessToken(
		context.Background(),
		created.Session.AccessToken); !errors.Is(err, ErrSessionInvalid) {
		t.Fatalf("old trusted session error = %v, want ErrSessionInvalid", err)
	}

	signIn, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "TrustedRecoveryOwner",
			Password:        secondPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Recovery Laptop",
			Platform:        "Windows",
			SourceKey:       "203.0.113.92",
		})
	if err != nil {
		t.Fatalf("post-recovery SignIn() error = %v", err)
	}
	if signIn.Session == nil {
		t.Fatal("post-recovery SignIn() did not issue a session")
	}
}

func TestUploadedAvatarIsPrivateAccountStateAndReplacementDeletesOldObject(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "AvatarOwner")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	pngBytes := fixturePNG(t, 64, 64)
	uploaded, err := fixture.service.UploadAvatar(
		context.Background(),
		auth,
		pngBytes)
	if err != nil {
		t.Fatalf("UploadAvatar() error = %v", err)
	}
	if uploaded.Profile.AvatarURL == "" {
		t.Fatal("uploaded profile did not expose a signed fixture avatar URL")
	}
	if len(fixture.avatars.objects) != 1 {
		t.Fatalf("avatar object count = %d, want 1", len(fixture.avatars.objects))
	}

	builtIn, err := fixture.service.SetBuiltinAvatar(
		context.Background(),
		auth,
		"laurel-01")
	if err != nil {
		t.Fatalf("SetBuiltinAvatar() error = %v", err)
	}
	if builtIn.Profile.BuiltinAvatarID != "laurel-01" {
		t.Fatalf("builtin avatar = %q, want laurel-01", builtIn.Profile.BuiltinAvatarID)
	}
	if builtIn.Profile.AvatarURL != "" {
		t.Fatalf("builtin avatar retained uploaded URL %q", builtIn.Profile.AvatarURL)
	}
	if len(fixture.avatars.objects) != 0 {
		t.Fatalf("old uploaded avatar was not deleted; object count = %d", len(fixture.avatars.objects))
	}
}

func TestApprovalCannotBeDecidedByTargetDevice(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "SelfApprovalOwner")
	authA := authenticateFixtureSession(t, fixture, created.Session)

	if _, err := fixture.service.SetNewDeviceProtection(
		context.Background(),
		authA,
		true); err != nil {
		t.Fatalf("SetNewDeviceProtection() error = %v", err)
	}

	signIn, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "SelfApprovalOwner",
			Password:        testPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Target",
			Platform:        "Windows",
			SourceKey:       "203.0.113.93",
		})
	if err != nil {
		t.Fatalf("SignIn() error = %v", err)
	}

	approvals, err := fixture.service.Approvals(context.Background(), authA)
	if err != nil {
		t.Fatalf("Approvals() error = %v", err)
	}
	if len(approvals) != 1 {
		t.Fatalf("approvals count = %d, want 1", len(approvals))
	}

	// The target has no session yet, so there is no authorized self-approval path.
	if _, err := fixture.service.PollDeviceSignInChallenge(
		context.Background(),
		signIn.ChallengeToken); err != nil {
		t.Fatalf("pending PollDeviceSignInChallenge() error = %v", err)
	}
}

func TestRecoveryChallengeExpiryDoesNotChangePassword(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "TrustedExpiryOwner")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	started, err := fixture.service.StartTrustedRecovery(
		context.Background(),
		TrustedRecoveryInput{
			Username:        "TrustedExpiryOwner",
			NewPassword:     secondPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Recovery Target",
			Platform:        "Windows",
			SourceKey:       "203.0.113.94",
		})
	if err != nil {
		t.Fatalf("StartTrustedRecovery() error = %v", err)
	}

	approvals, err := fixture.service.Approvals(context.Background(), auth)
	if err != nil {
		t.Fatalf("Approvals() error = %v", err)
	}
	if len(approvals) != 1 {
		t.Fatalf("approvals = %#v, want one challenge", approvals)
	}

	fixture.clock.Advance(trustedRecoveryLifetime + time.Second)
	if _, err := fixture.service.PollTrustedRecovery(
		context.Background(),
		started.ChallengeToken); !errors.Is(err, ErrChallengeExpired) {
		t.Fatalf("expired trusted recovery error = %v, want ErrChallengeExpired", err)
	}

	signIn, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "TrustedExpiryOwner",
			Password:        testPassword,
			DeviceInstallID: deviceCInstall,
			DeviceLabel:     "Still Old Password",
			Platform:        "Windows",
			SourceKey:       "203.0.113.95",
		})
	if err != nil {
		t.Fatalf("old password stopped working after expired recovery: %v", err)
	}
	if signIn.Session == nil {
		t.Fatal("old password sign-in did not produce a session")
	}
}

func fixturePNG(t *testing.T, width, height int) []byte {
	t.Helper()

	imageData := image.NewRGBA(image.Rect(0, 0, width, height))
	imageData.Set(0, 0, color.RGBA{R: 255, G: 196, B: 74, A: 255})

	var buffer bytes.Buffer
	if err := png.Encode(&buffer, imageData); err != nil {
		t.Fatalf("png.Encode() error = %v", err)
	}
	return buffer.Bytes()
}
