package account

import (
	"bytes"
	"context"
	"encoding/json"
	"testing"
)

func TestActivityFactSharesJournalPullCursor(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivityUnifiedPull")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	now := fixture.clock.Now().UnixMilli()

	first := fixtureSyncMutation(
		"71000000-0000-4000-8000-000000000001",
		auth.Device.ID,
		"activity-pull/first",
		"one",
		now,
		0)
	const eventID = "71000000-0000-4000-8000-0000000000aa"
	activity := activityMutation(
		"71000000-0000-4000-8000-0000000000bb",
		auth.Device.ID,
		eventID,
		activityPlaybackPayload(eventID))
	second := fixtureSyncMutation(
		"71000000-0000-4000-8000-000000000002",
		auth.Device.ID,
		"activity-pull/second",
		"two",
		now+1,
		0)

	firstPush, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{first})
	if err != nil || len(firstPush.Results) != 1 || !firstPush.Results[0].Accepted {
		t.Fatalf("first mutable push = %+v err=%v", firstPush.Results, err)
	}
	activityPush, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{activity})
	if err != nil || len(activityPush.Results) != 1 || !activityPush.Results[0].Accepted {
		t.Fatalf("activity push = %+v err=%v", activityPush.Results, err)
	}
	secondPush, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{second})
	if err != nil || len(secondPush.Results) != 1 || !secondPush.Results[0].Accepted {
		t.Fatalf("second mutable push = %+v err=%v", secondPush.Results, err)
	}

	firstSeq := firstPush.Results[0].ServerSeq
	activitySeq := activityPush.Results[0].ServerSeq
	secondSeq := secondPush.Results[0].ServerSeq
	if !(firstSeq < activitySeq && activitySeq < secondSeq) {
		t.Fatalf("server_seq order = %d, %d, %d", firstSeq, activitySeq, secondSeq)
	}

	pull, err := fixture.service.PullSync(context.Background(), auth, 0)
	if err != nil {
		t.Fatalf("PullSync() error = %v", err)
	}
	if len(pull.Entries) != 3 || pull.HasMore {
		t.Fatalf("pull = %d entries has_more=%v, want 3/false", len(pull.Entries), pull.HasMore)
	}
	if pull.Entries[0].ServerSeq != firstSeq || pull.Entries[2].ServerSeq != secondSeq {
		t.Fatalf("mutable entries are not ordered around Activity: %+v", pull.Entries)
	}

	got := pull.Entries[1]
	if got.ServerSeq != activitySeq || !got.Won ||
		got.Mutation.Category != "activity_fact" ||
		got.Mutation.RecordKey != "activity/"+eventID ||
		got.Mutation.Operation != "put" ||
		got.Mutation.MutationID != activity.MutationID ||
		got.Mutation.DeviceID != auth.Device.ID ||
		got.Mutation.SchemaVersion != 1 {
		t.Fatalf("Activity pull metadata = %+v", got)
	}

	var payload map[string]any
	decoder := json.NewDecoder(bytes.NewReader(got.Mutation.Payload))
	decoder.UseNumber()
	if err := decoder.Decode(&payload); err != nil {
		t.Fatalf("Activity pull payload is invalid JSON: %v", err)
	}
	if payload["eventId"] != eventID || payload["type"] != "playback_delta" {
		t.Fatalf("Activity pull payload = %v", payload)
	}

	after, err := fixture.service.PullSync(context.Background(), auth, activitySeq)
	if err != nil {
		t.Fatalf("PullSync(after Activity) error = %v", err)
	}
	if len(after.Entries) != 1 || after.Entries[0].ServerSeq != secondSeq {
		t.Fatalf("cursor after Activity = %+v, want only seq %d", after.Entries, secondSeq)
	}
}
