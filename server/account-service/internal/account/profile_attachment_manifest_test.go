package account

import (
	"context"
	"fmt"
	"reflect"
	"strings"
	"testing"
)

func beginManifestAttachment(
	t *testing.T,
	fixture serviceFixture,
	auth AuthenticatedSession,
	attachmentID string,
	manifest []SyncMutationInput,
) ProfileAttachment {
	t.Helper()
	attachment, err := fixture.service.BeginProfileAttachment(
		context.Background(),
		auth,
		BeginProfileAttachmentInput{
			AttachmentID:         attachmentID,
			SourceKind:           "local_only",
			SourceProfileID:      "local-only",
			SourceSemanticDigest: "sha256:source",
			SourceActivityDigest: "sha256:activity",
			Manifest:             manifest,
		})
	if err != nil {
		t.Fatalf("BeginProfileAttachment() error = %v", err)
	}
	return attachment
}

func TestProfileAttachmentManifestLifecycleAndFreshExport(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "AttachManifest")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	now := fixture.clock.Now().UnixMilli()

	mutable := fixtureSyncMutation(
		"91000000-0000-4000-8000-000000000001",
		auth.Device.ID, "attach/item", "local", now, 0)
	const eventID = "91000000-0000-4000-8000-0000000000aa"
	activity := activityMutation(
		"91000000-0000-4000-8000-0000000000bb",
		auth.Device.ID, eventID, activityPlaybackPayload(eventID))
	manifest := []SyncMutationInput{mutable, activity}
	const attachmentID = "91000000-0000-4000-8000-0000000000cc"

	attachment := beginManifestAttachment(
		t, fixture, auth, attachmentID, manifest)
	if attachment.State != "open" || attachment.ManifestCount != len(manifest) ||
		!strings.HasPrefix(attachment.ManifestDigest, "sha256:") {
		t.Fatalf("begin attachment = %+v", attachment)
	}
	retry := beginManifestAttachment(t, fixture, auth, attachmentID, manifest)
	if !reflect.DeepEqual(retry, attachment) {
		t.Fatalf("idempotent begin = %+v, want %+v", retry, attachment)
	}

	changed := append([]SyncMutationInput(nil), manifest...)
	changed[0].Payload = []byte(`{"world":"attach","id":"item","value":"changed"}`)
	if _, err := fixture.service.BeginProfileAttachment(
		context.Background(), auth,
		BeginProfileAttachmentInput{
			AttachmentID:         attachmentID,
			SourceKind:           "local_only",
			SourceProfileID:      "local-only",
			SourceSemanticDigest: "sha256:source",
			SourceActivityDigest: "sha256:activity",
			Manifest:             changed,
		}); err == nil {
		t.Fatal("begin accepted a changed manifest for the same attachment")
	} else {
		requireErrorIs(t, err, ErrAttachmentConflict)
	}

	push, err := fixture.service.PushSyncWithAttachment(
		context.Background(), auth, attachmentID, manifest)
	if err != nil || len(push.Results) != len(manifest) {
		t.Fatalf("PushSyncWithAttachment() = %+v, err=%v", push, err)
	}
	for _, result := range push.Results {
		if !result.Accepted || result.ServerSeq == 0 {
			t.Fatalf("attached push result = %+v", result)
		}
	}

	replayed, err := fixture.service.PushSyncWithAttachment(
		context.Background(), auth, attachmentID, manifest)
	if err != nil || len(replayed.Results) != len(push.Results) {
		t.Fatalf("PushSyncWithAttachment(replay) = %+v, err=%v", replayed, err)
	}
	for index := range push.Results {
		if replayed.Results[index].ServerSeq != push.Results[index].ServerSeq {
			t.Fatalf("replay result %d seq = %d, want %d", index,
				replayed.Results[index].ServerSeq, push.Results[index].ServerSeq)
		}
	}

	committed, err := fixture.service.CommitProfileAttachment(
		context.Background(), auth, attachmentID)
	if err != nil {
		t.Fatalf("CommitProfileAttachment() error = %v", err)
	}
	if committed.State != "committed" ||
		committed.FreshExportSnapshotID == "" || committed.FreshExportCursor == "" ||
		committed.FreshExportHighWaterSeq == 0 ||
		len(committed.Dispositions) != len(manifest) {
		t.Fatalf("commit receipt = %+v", committed)
	}
	for _, disposition := range committed.Dispositions {
		if disposition.Disposition != "materialized" ||
			len(disposition.MaterializedPayloadHash) != 64 {
			t.Fatalf("commit disposition = %+v", disposition)
		}
	}

	export, err := fixture.service.ExportAccount(
		context.Background(), auth,
		ExportAccountInput{Cursor: committed.FreshExportCursor, Limit: 100})
	if err != nil {
		t.Fatalf("ExportAccount(commit cursor) error = %v", err)
	}
	if export.SnapshotID != committed.FreshExportSnapshotID ||
		export.HighWaterServerSeq != committed.FreshExportHighWaterSeq {
		t.Fatalf("fresh export = %+v, commit = %+v", export, committed)
	}
	seenMutable, seenActivity := false, false
	for _, item := range export.Items {
		if item.Category == mutable.Category && item.Key == mutable.RecordKey {
			seenMutable = true
		}
		if item.Category == activity.Category && item.Key == activity.RecordKey {
			seenActivity = true
		}
	}
	if !seenMutable || !seenActivity {
		t.Fatalf("fresh export missing attachment contribution: mutable=%v activity=%v", seenMutable, seenActivity)
	}

	idempotent, err := fixture.service.CommitProfileAttachment(
		context.Background(), auth, attachmentID)
	if err != nil || idempotent.State != "committed" ||
		len(idempotent.Dispositions) != len(manifest) ||
		idempotent.FreshExportSnapshotID == "" {
		t.Fatalf("idempotent commit = %+v, err=%v", idempotent, err)
	}
	if _, err := fixture.service.PushSyncWithAttachment(
		context.Background(), auth, attachmentID, manifest); err == nil {
		t.Fatal("committed attachment accepted another push")
	} else {
		requireErrorIs(t, err, ErrAttachmentNotActive)
	}
}

func TestProfileAttachmentRejectsManifestMismatchBoundsAndStorageFailure(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "AttachFailures")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	now := fixture.clock.Now().UnixMilli()
	valid := fixtureSyncMutation(
		"92000000-0000-4000-8000-000000000001",
		auth.Device.ID, "attach/failure", "value", now, 0)
	const attachmentID = "92000000-0000-4000-8000-0000000000aa"
	beginManifestAttachment(t, fixture, auth, attachmentID, []SyncMutationInput{valid})

	missing := valid
	missing.MutationID = "92000000-0000-4000-8000-000000000002"
	push, err := fixture.service.PushSyncWithAttachment(
		context.Background(), auth, attachmentID, []SyncMutationInput{missing})
	if err != nil || len(push.Results) != 1 || push.Results[0].Accepted ||
		push.Results[0].Code != "attachment_manifest_missing" {
		t.Fatalf("missing manifest push = %+v, err=%v", push, err)
	}

	mismatch := valid
	mismatch.Payload = []byte(`{"world":"attach","id":"failure","value":"different"}`)
	push, err = fixture.service.PushSyncWithAttachment(
		context.Background(), auth, attachmentID, []SyncMutationInput{mismatch})
	if err != nil || len(push.Results) != 1 || push.Results[0].Accepted ||
		push.Results[0].Code != "attachment_manifest_mismatch" {
		t.Fatalf("mismatched manifest push = %+v, err=%v", push, err)
	}

	unknown := valid
	unknown.MutationID = "92000000-0000-4000-8000-000000000003"
	unknown.Category = "unknown"
	if _, err := fixture.service.BeginProfileAttachment(
		context.Background(), auth,
		BeginProfileAttachmentInput{
			AttachmentID:         "92000000-0000-4000-8000-0000000000ab",
			SourceKind:           "local_only",
			SourceSemanticDigest: "sha256:source",
			Manifest:             []SyncMutationInput{unknown},
		}); err == nil {
		t.Fatal("begin accepted an unknown manifest category")
	} else {
		requireErrorIs(t, err, ErrAttachmentInvalid)
	}

	oversize := make([]SyncMutationInput, profileAttachmentManifestLimit+1)
	for index := range oversize {
		oversize[index] = fixtureSyncMutation(
			fmtUUID(0x93000000, index+1), auth.Device.ID,
			fmt.Sprintf("oversize/item-%03d", index),
			"value", now, uint64(index))
	}
	if _, err := fixture.service.BeginProfileAttachment(
		context.Background(), auth,
		BeginProfileAttachmentInput{
			AttachmentID:         "92000000-0000-4000-8000-0000000000ac",
			SourceKind:           "local_only",
			SourceSemanticDigest: "sha256:source",
			Manifest:             oversize,
		}); err == nil {
		t.Fatal("begin accepted an oversized manifest")
	} else {
		requireErrorIs(t, err, ErrAttachmentInvalid)
	}

	if _, err := fixture.pool.Exec(context.Background(), `
		CREATE FUNCTION fail_attachment_manifest_write() RETURNS trigger
		LANGUAGE plpgsql AS $$ BEGIN RAISE EXCEPTION 'fixture storage failure'; END $$;
		CREATE TRIGGER fail_attachment_manifest_write
		BEFORE INSERT ON account_device_attachment_manifest
		FOR EACH ROW EXECUTE FUNCTION fail_attachment_manifest_write();
	`); err != nil {
		t.Fatalf("install storage failure trigger: %v", err)
	}
	failedID := "92000000-0000-4000-8000-0000000000ad"
	failed := valid
	failed.MutationID = "92000000-0000-4000-8000-000000000004"
	if _, err := fixture.service.BeginProfileAttachment(
		context.Background(), auth,
		BeginProfileAttachmentInput{
			AttachmentID:         failedID,
			SourceKind:           "local_only",
			SourceSemanticDigest: "sha256:source",
			Manifest:             []SyncMutationInput{failed},
		}); err == nil {
		t.Fatal("begin succeeded through a manifest storage failure")
	}
	var rows int
	if err := fixture.pool.QueryRow(context.Background(),
		"SELECT count(*) FROM account_device_attachments WHERE id = $1::uuid",
		failedID).Scan(&rows); err != nil {
		t.Fatalf("count failed attachment rows: %v", err)
	}
	if rows != 0 {
		t.Fatalf("storage failure left %d attachment rows, want 0", rows)
	}
}

func TestTwoDevicesCanAttachSimultaneouslyWithoutLosingCanonicalWinner(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "AttachTwoDevices")
	authA := authenticateFixtureSession(t, fixture, created.Session)
	authB := signInSecondDevice(t, fixture, "AttachTwoDevices")
	now := fixture.clock.Now().UnixMilli()
	mutationA := fixtureSyncMutation(
		"94000000-0000-4000-8000-000000000001",
		authA.Device.ID, "concurrent/item", "device-a", now, 0)
	mutationB := fixtureSyncMutation(
		"94000000-0000-4000-8000-000000000002",
		authB.Device.ID, "concurrent/item", "device-b", now+1, 0)
	const attachmentA = "94000000-0000-4000-8000-0000000000aa"
	const attachmentB = "94000000-0000-4000-8000-0000000000bb"
	beginManifestAttachment(t, fixture, authA, attachmentA, []SyncMutationInput{mutationA})
	beginManifestAttachment(t, fixture, authB, attachmentB, []SyncMutationInput{mutationB})

	type pushResult struct {
		response SyncPushResponse
		err      error
	}
	start := make(chan struct{})
	results := make(chan pushResult, 2)
	push := func(auth AuthenticatedSession, attachment string, mutation SyncMutationInput) {
		<-start
		response, err := fixture.service.PushSyncWithAttachment(
			context.Background(), auth, attachment, []SyncMutationInput{mutation})
		results <- pushResult{response: response, err: err}
	}
	go push(authA, attachmentA, mutationA)
	go push(authB, attachmentB, mutationB)
	close(start)
	for range 2 {
		result := <-results
		if result.err != nil || len(result.response.Results) != 1 ||
			!result.response.Results[0].Accepted {
			t.Fatalf("simultaneous attached push = %+v, err=%v", result.response, result.err)
		}
	}

	commitA, err := fixture.service.CommitProfileAttachment(
		context.Background(), authA, attachmentA)
	if err != nil {
		t.Fatalf("CommitProfileAttachment(A) error = %v", err)
	}
	commitB, err := fixture.service.CommitProfileAttachment(
		context.Background(), authB, attachmentB)
	if err != nil {
		t.Fatalf("CommitProfileAttachment(B) error = %v", err)
	}
	if len(commitA.Dispositions) != 1 || commitA.Dispositions[0].Disposition != "superseded" {
		t.Fatalf("device A disposition = %+v, want superseded", commitA.Dispositions)
	}
	if len(commitB.Dispositions) != 1 || commitB.Dispositions[0].Disposition != "materialized" {
		t.Fatalf("device B disposition = %+v, want materialized", commitB.Dispositions)
	}

	pull, err := fixture.service.PullSync(context.Background(), authA, 0)
	if err != nil {
		t.Fatalf("PullSync() error = %v", err)
	}
	winners := 0
	for _, entry := range pull.Entries {
		if entry.Won && entry.Mutation.Category == mutationB.Category &&
			entry.Mutation.RecordKey == mutationB.RecordKey {
			winners++
			if string(entry.Mutation.Payload) != string(mutationB.Payload) {
				t.Fatalf("canonical winner payload = %s, want %s", entry.Mutation.Payload, mutationB.Payload)
			}
		}
	}
	if winners != 1 {
		t.Fatalf("canonical winner count = %d, want 1", winners)
	}
}

func fmtUUID(prefix uint32, suffix int) string {
	return fmt.Sprintf("%08x-0000-4000-8000-%012x", prefix, suffix)
}
