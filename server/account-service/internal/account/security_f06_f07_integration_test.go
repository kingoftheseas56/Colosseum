package account

import (
	"context"
	"errors"
	"fmt"
	"sync"
	"testing"
	"time"

	"github.com/jackc/pgx/v5"
	"github.com/jackc/pgx/v5/pgconn"
)

func TestSignInCannotIssueAfterPasswordRotationWhileVerificationIsPaused(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "SignInRotationOwner")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	if _, err := fixture.service.SetNewDeviceProtection(
		context.Background(),
		auth,
		true); err != nil {
		t.Fatalf("SetNewDeviceProtection() error = %v", err)
	}

	originalVerify := fixture.service.passwordVerify
	verified := make(chan struct{})
	release := make(chan struct{})
	var releaseOnce sync.Once
	releaseSignIn := func() {
		releaseOnce.Do(func() { close(release) })
	}
	defer func() {
		releaseSignIn()
		fixture.service.passwordVerify = originalVerify
	}()

	fixture.service.passwordVerify = func(encoded, password string) (bool, error) {
		valid, err := originalVerify(encoded, password)
		if valid && password == testPassword {
			close(verified)
			<-release
		}
		return valid, err
	}

	type signInResult struct {
		result SignInResult
		err    error
	}
	resultCh := make(chan signInResult, 1)
	signInCtx, cancelSignIn := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancelSignIn()
	go func() {
		result, err := fixture.service.SignIn(
			signInCtx,
			SignInInput{
				Username:        "SignInRotationOwner",
				Password:        testPassword,
				DeviceInstallID: deviceBInstall,
				DeviceLabel:     "Paused Sign-In",
				Platform:        "Windows",
				SourceKey:       "203.0.113.201",
			})
		resultCh <- signInResult{result: result, err: err}
	}()

	select {
	case <-verified:
	case <-time.After(10 * time.Second):
		t.Fatal("sign-in did not reach its paused password verification")
	}

	changeCtx, cancelChange := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancelChange()
	changeErr := fixture.service.ChangePassword(
		changeCtx,
		auth,
		ChangePasswordInput{
			CurrentPassword: testPassword,
			NewPassword:     secondPassword,
		})
	releaseSignIn()
	var result signInResult
	select {
	case result = <-resultCh:
	case <-time.After(5 * time.Second):
		t.Fatal("sign-in did not finish after verification was released")
	}

	if changeErr != nil {
		t.Fatalf("ChangePassword() error = %v", changeErr)
	}
	if !errors.Is(result.err, ErrInvalidCredentials) {
		t.Fatalf("late old-password SignIn() error = %v, want ErrInvalidCredentials", result.err)
	}
	if result.result.Session != nil {
		t.Fatal("late old-password SignIn() issued a session")
	}
}

func TestLogoutEverywhereUsesAccountFirstLockOrder(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "LogoutLockOrderOwner")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	heldTx, err := fixture.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		t.Fatalf("begin lock holder: %v", err)
	}
	defer func() { _ = heldTx.Rollback(context.Background()) }()
	if _, err := heldTx.Exec(ctx, `
        SELECT id
        FROM sessions
        WHERE id = $1::uuid
        FOR UPDATE
    `, auth.SessionID); err != nil {
		t.Fatalf("lock session for ordering probe: %v", err)
	}

	logoutCtx, cancelLogout := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancelLogout()
	logoutDone := make(chan error, 1)
	go func() {
		logoutDone <- fixture.service.LogoutEverywhere(logoutCtx, auth)
	}()

	if err := waitForAccountLock(ctx, fixture.pool, auth.Account.ID); err != nil {
		_ = heldTx.Commit(context.Background())
		select {
		case <-logoutDone:
		case <-time.After(5 * time.Second):
			t.Fatal("logout did not unwind after ordering probe failure")
		}
		t.Fatalf("LogoutEverywhere() did not acquire account before waiting on session: %v", err)
	}

	changeCtx, cancelChange := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancelChange()
	changeDone := make(chan error, 1)
	go func() {
		changeDone <- fixture.service.ChangePassword(
			changeCtx,
			auth,
			ChangePasswordInput{
				CurrentPassword: testPassword,
				NewPassword:     secondPassword,
			})
	}()

	if err := heldTx.Commit(ctx); err != nil {
		t.Fatalf("release session lock: %v", err)
	}

	select {
	case err := <-logoutDone:
		if err != nil {
			t.Fatalf("LogoutEverywhere() error = %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("LogoutEverywhere() did not finish after session lock release")
	}
	select {
	case err := <-changeDone:
		if !errors.Is(err, ErrSessionInvalid) {
			t.Fatalf("ChangePassword() error = %v, want ErrSessionInvalid after logout wins", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("ChangePassword() did not finish after session lock release")
	}
}

func TestRefreshSessionUsesAccountBeforeDeviceLock(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "RefreshLockOrderOwner")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	heldTx, err := fixture.pool.BeginTx(ctx, pgx.TxOptions{})
	if err != nil {
		t.Fatalf("begin lock holder: %v", err)
	}
	defer func() { _ = heldTx.Rollback(context.Background()) }()
	if _, err := heldTx.Exec(ctx, `
        SELECT id
        FROM devices
        WHERE id = $1::uuid
        FOR UPDATE
    `, auth.Device.ID); err != nil {
		t.Fatalf("lock device for ordering probe: %v", err)
	}

	refreshCtx, cancelRefresh := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancelRefresh()
	refreshDone := make(chan error, 1)
	go func() {
		_, err := fixture.service.RefreshSession(refreshCtx, created.Session.RefreshToken)
		refreshDone <- err
	}()

	if err := waitForAccountLock(ctx, fixture.pool, auth.Account.ID); err != nil {
		_ = heldTx.Commit(context.Background())
		select {
		case <-refreshDone:
		case <-time.After(5 * time.Second):
			t.Fatal("refresh did not unwind after ordering probe failure")
		}
		t.Fatalf("RefreshSession() did not acquire account before waiting on device: %v", err)
	}

	if err := heldTx.Commit(ctx); err != nil {
		t.Fatalf("release device lock: %v", err)
	}
	select {
	case err := <-refreshDone:
		if err != nil {
			t.Fatalf("RefreshSession() error = %v", err)
		}
	case <-time.After(5 * time.Second):
		t.Fatal("RefreshSession() did not finish after device lock release")
	}
}

func waitForAccountLock(
	ctx context.Context,
	pool interface {
		BeginTx(context.Context, pgx.TxOptions) (pgx.Tx, error)
	},
	accountID string,
) error {
	ticker := time.NewTicker(10 * time.Millisecond)
	defer ticker.Stop()
	for {
		tx, err := pool.BeginTx(ctx, pgx.TxOptions{})
		if err != nil {
			return fmt.Errorf("begin account-lock probe: %w", err)
		}
		var lockedID string
		err = tx.QueryRow(ctx, `
            SELECT id::text
            FROM accounts
            WHERE id = $1::uuid
            FOR UPDATE NOWAIT
        `, accountID).Scan(&lockedID)
		_ = tx.Rollback(context.Background())
		if err != nil {
			var pgErr *pgconn.PgError
			if errors.As(err, &pgErr) && pgErr.Code == "55P03" {
				return nil
			}
			return fmt.Errorf("probe account lock: %w", err)
		}
		select {
		case <-ctx.Done():
			return fmt.Errorf("account lock not observed: %w", ctx.Err())
		case <-ticker.C:
		}
	}
}

func TestPasswordRotationInvalidatesDeviceApprovalsInEitherOrder(t *testing.T) {
	tests := []struct {
		name         string
		approveFirst bool
		username     string
		sourceSuffix string
	}{
		{
			name:         "rotation before approval",
			approveFirst: false,
			username:     "DevApprovalBefore",
			sourceSuffix: "211",
		},
		{
			name:         "approval before rotation",
			approveFirst: true,
			username:     "DevApprovalAfter",
			sourceSuffix: "212",
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			fixture := newServiceFixture(t)
			created := createFixtureAccount(t, fixture, test.username)
			auth := authenticateFixtureSession(t, fixture, created.Session)
			if _, err := fixture.service.SetNewDeviceProtection(
				context.Background(),
				auth,
				true); err != nil {
				t.Fatalf("SetNewDeviceProtection() error = %v", err)
			}

			challenge, err := fixture.service.SignIn(
				context.Background(),
				SignInInput{
					Username:        test.username,
					Password:        testPassword,
					DeviceInstallID: deviceBInstall,
					DeviceLabel:     "Approval Race Target",
					Platform:        "Windows",
					SourceKey:       "203.0.113." + test.sourceSuffix,
				})
			if err != nil {
				t.Fatalf("protected SignIn() error = %v", err)
			}
			if challenge.Status != "approval_required" || challenge.ChallengeToken == "" {
				t.Fatalf("protected SignIn() = %#v, want approval_required", challenge)
			}

			approvals, err := fixture.service.Approvals(context.Background(), auth)
			if err != nil {
				t.Fatalf("Approvals() error = %v", err)
			}
			if len(approvals) != 1 || approvals[0].Kind != "device_signin" {
				t.Fatalf("Approvals() = %#v, want one device_signin approval", approvals)
			}
			challengeID := approvals[0].ID

			if test.approveFirst {
				if err := fixture.service.DecideApproval(
					context.Background(),
					auth,
					"device_signin",
					challengeID,
					true); err != nil {
					t.Fatalf("DecideApproval() error = %v", err)
				}
			}

			if err := fixture.service.ChangePassword(
				context.Background(),
				auth,
				ChangePasswordInput{
					CurrentPassword: testPassword,
					NewPassword:     secondPassword,
				}); err != nil {
				t.Fatalf("ChangePassword() error = %v", err)
			}

			if !test.approveFirst {
				if err := fixture.service.DecideApproval(
					context.Background(),
					auth,
					"device_signin",
					challengeID,
					true); !errors.Is(err, ErrChallengeInvalid) {
					t.Fatalf("late DecideApproval() error = %v, want ErrChallengeInvalid", err)
				}
			}

			if _, err := fixture.service.PollDeviceSignInChallenge(
				context.Background(),
				challenge.ChallengeToken); !errors.Is(err, ErrChallengeDenied) {
				t.Fatalf("stale device challenge poll error = %v, want ErrChallengeDenied", err)
			}
		})
	}
}

func TestPasswordRotationInvalidatesTrustedRecoveryApproval(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "RecoveryRotationOwner")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	started, err := fixture.service.StartTrustedRecovery(
		context.Background(),
		TrustedRecoveryInput{
			Username:        "RecoveryRotationOwner",
			NewPassword:     thirdPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Recovery Approval Target",
			Platform:        "Windows",
			SourceKey:       "203.0.113.221",
		})
	if err != nil {
		t.Fatalf("StartTrustedRecovery() error = %v", err)
	}
	approvals, err := fixture.service.Approvals(context.Background(), auth)
	if err != nil {
		t.Fatalf("Approvals() error = %v", err)
	}
	if len(approvals) != 1 {
		t.Fatalf("Approvals() = %#v, want one recovery approval", approvals)
	}
	if err := fixture.service.DecideApproval(
		context.Background(),
		auth,
		"trusted_recovery",
		approvals[0].ID,
		true); err != nil {
		t.Fatalf("DecideApproval() error = %v", err)
	}

	if err := fixture.service.ChangePassword(
		context.Background(),
		auth,
		ChangePasswordInput{
			CurrentPassword: testPassword,
			NewPassword:     secondPassword,
		}); err != nil {
		t.Fatalf("ChangePassword() error = %v", err)
	}
	if _, err := fixture.service.PollTrustedRecovery(
		context.Background(),
		started.ChallengeToken); !errors.Is(err, ErrChallengeDenied) {
		t.Fatalf("stale trusted recovery poll error = %v, want ErrChallengeDenied", err)
	}
}

func TestTrustedRecoveryConsumesOneApprovalAndInvalidatesSiblings(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "SiblingRecoveryOwner")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	first, err := fixture.service.StartTrustedRecovery(
		context.Background(),
		TrustedRecoveryInput{
			Username:        "SiblingRecoveryOwner",
			NewPassword:     secondPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Recovery First",
			Platform:        "Windows",
			SourceKey:       "203.0.113.231",
		})
	if err != nil {
		t.Fatalf("first StartTrustedRecovery() error = %v", err)
	}
	second, err := fixture.service.StartTrustedRecovery(
		context.Background(),
		TrustedRecoveryInput{
			Username:        "SiblingRecoveryOwner",
			NewPassword:     thirdPassword,
			DeviceInstallID: deviceCInstall,
			DeviceLabel:     "Recovery Second",
			Platform:        "Windows",
			SourceKey:       "203.0.113.232",
		})
	if err != nil {
		t.Fatalf("second StartTrustedRecovery() error = %v", err)
	}

	approvals, err := fixture.service.Approvals(context.Background(), auth)
	if err != nil {
		t.Fatalf("Approvals() error = %v", err)
	}
	if len(approvals) != 2 {
		t.Fatalf("Approvals() = %#v, want two recovery approvals", approvals)
	}
	ids := make(map[string]string, len(approvals))
	for _, approval := range approvals {
		ids[approval.DeviceLabel] = approval.ID
	}
	if ids["Recovery First"] == "" || ids["Recovery Second"] == "" {
		t.Fatalf("Approvals() = %#v, missing both recovery targets", approvals)
	}
	for _, id := range ids {
		if err := fixture.service.DecideApproval(
			context.Background(),
			auth,
			"trusted_recovery",
			id,
			true); err != nil {
			t.Fatalf("DecideApproval(%q) error = %v", id, err)
		}
	}

	firstResult, err := fixture.service.PollTrustedRecovery(
		context.Background(),
		first.ChallengeToken)
	if err != nil {
		t.Fatalf("first PollTrustedRecovery() error = %v", err)
	}
	if firstResult.Status != "recovered" || firstResult.RecoveryKey == "" {
		t.Fatalf("first recovery result = %#v, want recovered + key", firstResult)
	}
	if _, err := fixture.service.PollTrustedRecovery(
		context.Background(),
		second.ChallengeToken); !errors.Is(err, ErrChallengeDenied) {
		t.Fatalf("sibling PollTrustedRecovery() error = %v, want ErrChallengeDenied", err)
	}

	if _, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "SiblingRecoveryOwner",
			Password:        secondPassword,
			DeviceInstallID: deviceCInstall,
			DeviceLabel:     "Post Recovery Device",
			Platform:        "Windows",
			SourceKey:       "203.0.113.233",
		}); err != nil {
		t.Fatalf("first recovery password SignIn() error = %v", err)
	}
	if _, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "SiblingRecoveryOwner",
			Password:        thirdPassword,
			DeviceInstallID: deviceCInstall,
			DeviceLabel:     "Stale Recovery Device",
			Platform:        "Windows",
			SourceKey:       "203.0.113.234",
		}); !errors.Is(err, ErrInvalidCredentials) {
		t.Fatalf("stale sibling recovery password error = %v, want ErrInvalidCredentials", err)
	}
}
