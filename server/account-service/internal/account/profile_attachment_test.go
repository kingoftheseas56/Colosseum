package account

import (
	"context"
	"testing"
	"time"
)

func beginFixtureAttachment(
	t *testing.T,
	fixture serviceFixture,
	auth AuthenticatedSession,
	attachmentID,
	sourceKind,
	digest string,
) ProfileAttachment {
	t.Helper()
	attachment, err := fixture.service.BeginProfileAttachment(
		context.Background(),
		auth,
		BeginProfileAttachmentInput{
			AttachmentID:         attachmentID,
			SourceKind:           sourceKind,
			SourceSemanticDigest: digest,
		})
	if err != nil {
		t.Fatalf("BeginProfileAttachment() error = %v", err)
	}
	return attachment
}

func signInSecondDevice(
	t *testing.T,
	fixture serviceFixture,
	username string,
) AuthenticatedSession {
	t.Helper()
	signIn, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        username,
			Password:        testPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Laptop",
			Platform:        "Windows",
			SourceKey:       "203.0.113.72",
		})
	if err != nil {
		t.Fatalf("SignIn(device B) error = %v", err)
	}
	if signIn.Status != "signed_in" || signIn.Session == nil {
		t.Fatalf("SignIn(device B) = %#v", signIn)
	}
	return authenticateFixtureSession(t, fixture, *signIn.Session)
}

func TestBeginProfileAttachmentFreezesCommittedBaseline(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "AttachBaseline")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	now := fixture.clock.Now().UnixMilli()

	winner := fixtureSyncMutation(
		"81000000-0000-4000-8000-000000000001",
		auth.Device.ID, "attach-baseline/record", "winner", now+20, 0)
	push, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{winner})
	if err != nil || !push.Results[0].Accepted {
		t.Fatalf("PushSync(winner) = %+v err %v", push.Results, err)
	}
	winnerSeq := push.Results[0].ServerSeq

	const activityEventID = "81000000-0000-4000-8000-0000000000aa"
	push, err = fixture.service.PushSync(
		context.Background(), auth,
		[]SyncMutationInput{activityMutation(
			"81000000-0000-4000-8000-0000000000bb",
			auth.Device.ID,
			activityEventID,
			activityPlaybackPayload(activityEventID))})
	if err != nil || !push.Results[0].Accepted {
		t.Fatalf("PushSync(activity) = %+v err %v", push.Results, err)
	}
	activitySeq := push.Results[0].ServerSeq

	// A journal-only loser allocates a sequence value without becoming
	// canonical, so sequence last_value must exceed the committed baseline.
	loser := fixtureSyncMutation(
		"81000000-0000-4000-8000-000000000002",
		auth.Device.ID, "attach-baseline/record", "stale-loser", now-1000, 0)
	push, err = fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{loser})
	if err != nil || !push.Results[0].Accepted {
		t.Fatalf("PushSync(loser) = %+v err %v", push.Results, err)
	}
	loserSeq := push.Results[0].ServerSeq
	if loserSeq <= activitySeq {
		t.Fatalf("loser seq = %d, want > activity seq %d", loserSeq, activitySeq)
	}
	lastValue := queryInt(t, fixture,
		"SELECT last_value FROM account_change_seq")
	if uint64(lastValue) != loserSeq {
		t.Fatalf("sequence last_value = %d, want loser seq %d", lastValue, loserSeq)
	}

	attachment := beginFixtureAttachment(t, fixture, auth,
		"82000000-0000-4000-8000-000000000001",
		"legacy_local", "digest-baseline")
	if attachment.ID != "82000000-0000-4000-8000-000000000001" {
		t.Fatalf("attachment id = %s", attachment.ID)
	}
	if attachment.DeviceID != auth.Device.ID {
		t.Fatalf("attachment device = %s, want session device %s",
			attachment.DeviceID, auth.Device.ID)
	}
	if attachment.State != "open" {
		t.Fatalf("attachment state = %s, want open", attachment.State)
	}
	if attachment.BaselineServerSeq != activitySeq {
		t.Fatalf("baseline = %d, want committed canonical max %d (activity seq), not last_value %d",
			attachment.BaselineServerSeq, activitySeq, loserSeq)
	}
	if activitySeq < winnerSeq {
		t.Fatalf("committed canonical max %d below winner seq %d", activitySeq, winnerSeq)
	}

	// More data landing must not move the frozen baseline of an existing
	// attachment; the identical retry returns the original row.
	fixture.clock.Advance(time.Minute)
	extra := fixtureSyncMutation(
		"81000000-0000-4000-8000-000000000003",
		auth.Device.ID, "attach-baseline/late", "late", fixture.clock.Now().UnixMilli(), 0)
	push, err = fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{extra})
	if err != nil || !push.Results[0].Accepted {
		t.Fatalf("PushSync(extra) = %+v err %v", push.Results, err)
	}

	retry := beginFixtureAttachment(t, fixture, auth,
		"82000000-0000-4000-8000-000000000001",
		"legacy_local", "digest-baseline")
	if retry != attachment {
		t.Fatalf("retry begin = %+v, want identical %+v", retry, attachment)
	}

	var rowCount int
	if err := fixture.pool.QueryRow(
		context.Background(),
		`SELECT count(*) FROM account_device_attachments WHERE account_id = $1::uuid`,
		auth.Account.ID).Scan(&rowCount); err != nil {
		t.Fatalf("count attachments: %v", err)
	}
	if rowCount != 1 {
		t.Fatalf("attachment rows = %d, want 1", rowCount)
	}
}

func TestBeginProfileAttachmentRetryConflictsAndInputValidation(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "AttachConflict")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	otherResult := createFixtureAccount(t, fixture, "AttachConflictOther")
	otherAuth := authenticateFixtureSession(t, fixture, otherResult.Session)
	deviceB := signInSecondDevice(t, fixture, "AttachConflict")

	const attachmentID = "83000000-0000-4000-8000-000000000001"
	beginFixtureAttachment(t, fixture, auth, attachmentID, "local_only", "digest-one")

	conflicts := []struct {
		name string
		auth AuthenticatedSession
		id   string
		kind string
		dig  string
	}{
		{"different digest", auth, attachmentID, "local_only", "digest-two"},
		{"different source kind", auth, attachmentID, "legacy_local", "digest-one"},
		{"different device same account", deviceB, attachmentID, "local_only", "digest-one"},
		{"different account", otherAuth, attachmentID, "local_only", "digest-one"},
	}
	for _, conflictCase := range conflicts {
		t.Run(conflictCase.name, func(t *testing.T) {
			_, err := fixture.service.BeginProfileAttachment(
				context.Background(),
				conflictCase.auth,
				BeginProfileAttachmentInput{
					AttachmentID:         conflictCase.id,
					SourceKind:           conflictCase.kind,
					SourceSemanticDigest: conflictCase.dig,
				})
			requireErrorIs(t, err, ErrAttachmentConflict)
		})
	}

	invalid := []struct {
		name string
		id   string
		kind string
		dig  string
	}{
		{"non-uuid attachment id", "not-a-uuid", "local_only", "digest"},
		{"empty attachment id", "", "local_only", "digest"},
		{"unknown source kind", "83000000-0000-4000-8000-000000000002", "cloud", "digest"},
		{"empty source kind", "83000000-0000-4000-8000-000000000002", "", "digest"},
		{"empty digest", "83000000-0000-4000-8000-000000000002", "local_only", "   "},
	}
	for _, invalidCase := range invalid {
		t.Run(invalidCase.name, func(t *testing.T) {
			_, err := fixture.service.BeginProfileAttachment(
				context.Background(),
				auth,
				BeginProfileAttachmentInput{
					AttachmentID:         invalidCase.id,
					SourceKind:           invalidCase.kind,
					SourceSemanticDigest: invalidCase.dig,
				})
			requireErrorIs(t, err, ErrAttachmentInvalid)
		})
	}
}

func TestGetProfileAttachmentIsAccountAndDeviceScoped(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "AttachScoped")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	otherResult := createFixtureAccount(t, fixture, "AttachScopedOther")
	otherAuth := authenticateFixtureSession(t, fixture, otherResult.Session)
	deviceB := signInSecondDevice(t, fixture, "AttachScoped")

	const attachmentID = "84000000-0000-4000-8000-000000000001"
	created := beginFixtureAttachment(t, fixture, auth, attachmentID, "local_only", "digest")

	fetched, err := fixture.service.GetProfileAttachment(
		context.Background(), auth, attachmentID)
	if err != nil {
		t.Fatalf("GetProfileAttachment(owner) error = %v", err)
	}
	if fetched != created {
		t.Fatalf("GetProfileAttachment(owner) = %+v, want %+v", fetched, created)
	}

	for _, scoped := range []struct {
		name string
		auth AuthenticatedSession
	}{
		{"second device same account", deviceB},
		{"other account", otherAuth},
	} {
		t.Run(scoped.name, func(t *testing.T) {
			_, err := fixture.service.GetProfileAttachment(
				context.Background(), scoped.auth, attachmentID)
			requireErrorIs(t, err, ErrAttachmentNotFound)
		})
	}

	if _, err := fixture.service.GetProfileAttachment(
		context.Background(), auth,
		"84000000-0000-4000-8000-00000000ffff"); err == nil {
		t.Fatal("GetProfileAttachment(unknown) succeeded")
	} else {
		requireErrorIs(t, err, ErrAttachmentNotFound)
	}
	_, err = fixture.service.GetProfileAttachment(
		context.Background(), auth, "not-a-uuid")
	requireErrorIs(t, err, ErrAttachmentInvalid)
}

func TestCommitProfileAttachmentIdempotentLifecycle(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "AttachCommit")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)

	const attachmentID = "85000000-0000-4000-8000-000000000001"
	beginFixtureAttachment(t, fixture, auth, attachmentID, "legacy_local", "digest")

	fixture.clock.Advance(2 * time.Minute)
	first, err := fixture.service.CommitProfileAttachment(
		context.Background(), auth, attachmentID)
	if err != nil {
		t.Fatalf("CommitProfileAttachment() error = %v", err)
	}
	if first.State != "committed" {
		t.Fatalf("commit state = %s, want committed", first.State)
	}
	var firstCommittedAt time.Time
	if err := fixture.pool.QueryRow(
		context.Background(),
		`SELECT committed_at FROM account_device_attachments WHERE id = $1::uuid`,
		attachmentID).Scan(&firstCommittedAt); err != nil {
		t.Fatalf("load committed_at: %v", err)
	}
	if firstCommittedAt.IsZero() {
		t.Fatal("committed_at was not set")
	}

	fixture.clock.Advance(2 * time.Minute)
	second, err := fixture.service.CommitProfileAttachment(
		context.Background(), auth, attachmentID)
	if err != nil {
		t.Fatalf("CommitProfileAttachment(retry) error = %v", err)
	}
	if second.State != "committed" || second.BaselineServerSeq != first.BaselineServerSeq {
		t.Fatalf("commit retry = %+v, want committed retry of %+v", second, first)
	}
	var retryCommittedAt time.Time
	if err := fixture.pool.QueryRow(
		context.Background(),
		`SELECT committed_at FROM account_device_attachments WHERE id = $1::uuid`,
		attachmentID).Scan(&retryCommittedAt); err != nil {
		t.Fatalf("load retry committed_at: %v", err)
	}
	if !retryCommittedAt.Equal(firstCommittedAt) {
		t.Fatalf("commit retry rewrote committed_at: %v != %v",
			retryCommittedAt, firstCommittedAt)
	}

	var rowCount int
	if err := fixture.pool.QueryRow(
		context.Background(),
		`SELECT count(*) FROM account_device_attachments WHERE account_id = $1::uuid`,
		auth.Account.ID).Scan(&rowCount); err != nil {
		t.Fatalf("count attachments: %v", err)
	}
	if rowCount != 1 {
		t.Fatalf("attachment rows = %d, want 1 (commit must not duplicate work)", rowCount)
	}

	// Committing directly from open is allowed; commit is a state transition
	// only and moves no bulk data.
	const openAttachmentID = "85000000-0000-4000-8000-000000000002"
	beginFixtureAttachment(t, fixture, auth, openAttachmentID, "local_only", "digest-two")
	direct, err := fixture.service.CommitProfileAttachment(
		context.Background(), auth, openAttachmentID)
	if err != nil || direct.State != "committed" {
		t.Fatalf("direct commit = %+v err %v, want committed", direct, err)
	}

	if _, err := fixture.service.CommitProfileAttachment(
		context.Background(), auth,
		"85000000-0000-4000-8000-00000000ffff"); err == nil {
		t.Fatal("CommitProfileAttachment(unknown id) succeeded")
	}

	_, err = fixture.service.PushSyncWithAttachment(
		context.Background(), auth, attachmentID,
		[]SyncMutationInput{fixtureSyncMutation(
			"85000000-0000-4000-8000-000000000003",
			auth.Device.ID, "attach-commit/record", "value",
			fixture.clock.Now().UnixMilli(), 0)})
	requireErrorIs(t, err, ErrAttachmentNotActive)

	// An aborted attachment is terminal for commit and push.
	const abortedAttachmentID = "85000000-0000-4000-8000-000000000004"
	beginFixtureAttachment(t, fixture, auth, abortedAttachmentID, "local_only", "digest-three")
	if _, err := fixture.pool.Exec(
		context.Background(),
		`UPDATE account_device_attachments SET state = 'aborted' WHERE id = $1::uuid`,
		abortedAttachmentID); err != nil {
		t.Fatalf("abort attachment: %v", err)
	}
	if _, err := fixture.service.CommitProfileAttachment(
		context.Background(), auth, abortedAttachmentID); err == nil {
		t.Fatal("committing an aborted attachment succeeded")
	} else {
		requireErrorIs(t, err, ErrAttachmentNotActive)
	}
	if _, err := fixture.service.PushSyncWithAttachment(
		context.Background(), auth, abortedAttachmentID,
		[]SyncMutationInput{fixtureSyncMutation(
			"85000000-0000-4000-8000-000000000005",
			auth.Device.ID, "attach-commit/aborted", "value",
			fixture.clock.Now().UnixMilli(), 0)}); err == nil {
		t.Fatal("pushing to an aborted attachment succeeded")
	} else {
		requireErrorIs(t, err, ErrAttachmentNotActive)
	}
}
