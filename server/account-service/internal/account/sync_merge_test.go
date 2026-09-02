package account

import (
	"bytes"
	"encoding/json"
	"testing"
)

const (
	syncMergeDeviceA = "11111111-1111-4111-8111-111111111111"
	syncMergeDeviceB = "22222222-2222-4222-8222-222222222222"
)

func syncMergeCurrentFixture(
	mutationID, deviceID, operation string,
	physical int64, counter uint64,
	payload string,
) syncMergeCurrent {
	return syncMergeCurrent{
		MutationID:    mutationID,
		DeviceID:      deviceID,
		SchemaVersion: 1,
		HLCPhysicalMS: physical,
		HLCCounter:    counter,
		Operation:     operation,
		Payload:       json.RawMessage(payload),
	}
}

func syncMergeIncomingFixture(
	mutationID, deviceID, category, operation string,
	physical int64, counter uint64,
	payload string,
) parsedSyncMutation {
	return parsedSyncMutation{
		MutationID:    mutationID,
		DeviceID:      deviceID,
		Category:      category,
		RecordKey:     "history/fixture",
		SchemaVersion: 1,
		HLCPhysicalMS: physical,
		HLCCounter:    counter,
		Operation:     operation,
		Payload:       json.RawMessage(payload),
	}
}

func decodeSyncMergePayload(t *testing.T, raw json.RawMessage) map[string]any {
	t.Helper()
	decoder := json.NewDecoder(bytes.NewReader(raw))
	decoder.UseNumber()
	var payload map[string]any
	if err := decoder.Decode(&payload); err != nil {
		t.Fatalf("decode merge payload: %v", err)
	}
	return payload
}

func syncMergeInt64(t *testing.T, payload map[string]any, field string) int64 {
	t.Helper()
	number, ok := payload[field].(json.Number)
	if !ok {
		t.Fatalf("%s = %#v, want json.Number", field, payload[field])
	}
	value, err := number.Int64()
	if err != nil {
		t.Fatalf("%s = %q: %v", field, number, err)
	}
	return value
}

func TestSyncMergeHistoryOlderContribution(t *testing.T) {
	current := syncMergeCurrentFixture(
		"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", syncMergeDeviceB, "put", 200, 0,
		`{"kind":"episode","firstActivityAt":2000,"lastActivityAt":5000,"label":"newer","nested":{"z":2,"a":1}}`)
	incoming := syncMergeIncomingFixture(
		"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", syncMergeDeviceA, "full_history", "put", 100, 9,
		`{"kind":"episode","firstActivityAt":1000,"lastActivityAt":4000,"label":"older","nested":{"a":9}}`)

	resolution, err := resolveMutableSync(current, true, incoming)
	if err != nil {
		t.Fatalf("resolveMutableSync() error = %v", err)
	}
	if !resolution.Changed {
		t.Fatal("older semantic contribution did not change canonical History")
	}
	if resolution.WinnerMutationID != current.MutationID || resolution.WinnerDeviceID != current.DeviceID {
		t.Fatalf("winner metadata = %s/%s, want current %s/%s",
			resolution.WinnerMutationID, resolution.WinnerDeviceID, current.MutationID, current.DeviceID)
	}
	payload := decodeSyncMergePayload(t, resolution.Payload)
	if got := syncMergeInt64(t, payload, "firstActivityAt"); got != 1000 {
		t.Fatalf("firstActivityAt = %d, want 1000", got)
	}
	if got := syncMergeInt64(t, payload, "lastActivityAt"); got != 5000 {
		t.Fatalf("lastActivityAt = %d, want 5000", got)
	}
	if payload["label"] != "newer" {
		t.Fatalf("ordinary field label = %#v, want newer", payload["label"])
	}

	noContribution := incoming
	noContribution.MutationID = "cccccccc-cccc-4ccc-8ccc-cccccccccccc"
	noContribution.Payload = json.RawMessage(
		`{"kind":"episode","firstActivityAt":2500,"lastActivityAt":4500,"label":"stale"}`)
	unchanged, err := resolveMutableSync(current, true, noContribution)
	if err != nil {
		t.Fatalf("resolveMutableSync(no contribution) error = %v", err)
	}
	if unchanged.Changed {
		t.Fatal("stale History with no semantic contribution changed canonical state")
	}
}

func TestSyncMergeHistoryCompletionAndMetadata(t *testing.T) {
	current := syncMergeCurrentFixture(
		"dddddddd-dddd-4ddd-8ddd-dddddddddddd", syncMergeDeviceA, "put", 100, 0,
		`{"kind":"episode","firstActivityAt":1000,"lastActivityAt":4000,"completedAt":2000,"oldOnly":"drop","title":"old"}`)
	incoming := syncMergeIncomingFixture(
		"eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee", syncMergeDeviceB, "full_history", "put", 200, 0,
		`{"kind":"episode","firstActivityAt":1500,"lastActivityAt":5000,"completedAt":3000,"newOnly":"keep","title":"new"}`)

	resolution, err := resolveMutableSync(current, true, incoming)
	if err != nil {
		t.Fatalf("resolveMutableSync() error = %v", err)
	}
	if !resolution.Changed || resolution.WinnerMutationID != incoming.MutationID ||
		resolution.WinnerDeviceID != incoming.DeviceID || resolution.WinnerHLCPhysicalMS != incoming.HLCPhysicalMS {
		t.Fatalf("newer winner metadata = %+v", resolution)
	}
	payload := decodeSyncMergePayload(t, resolution.Payload)
	if got := syncMergeInt64(t, payload, "firstActivityAt"); got != 1000 {
		t.Fatalf("firstActivityAt = %d, want 1000", got)
	}
	if got := syncMergeInt64(t, payload, "lastActivityAt"); got != 5000 {
		t.Fatalf("lastActivityAt = %d, want 5000", got)
	}
	if got := syncMergeInt64(t, payload, "completedAt"); got != 2000 {
		t.Fatalf("completedAt = %d, want 2000", got)
	}
	if payload["title"] != "new" || payload["newOnly"] != "keep" {
		t.Fatalf("newer ordinary metadata not preserved: %#v", payload)
	}
	if _, ok := payload["oldOnly"]; ok {
		t.Fatalf("older ordinary field survived newer payload: %#v", payload)
	}

	withoutCompletion := incoming
	withoutCompletion.MutationID = "ffffffff-ffff-4fff-8fff-ffffffffffff"
	withoutCompletion.HLCPhysicalMS = 300
	withoutCompletion.Payload = json.RawMessage(
		`{"kind":"episode","firstActivityAt":1500,"lastActivityAt":6000,"title":"newest"}`)
	completionSurvives, err := resolveMutableSync(current, true, withoutCompletion)
	if err != nil {
		t.Fatalf("resolveMutableSync(completion survives) error = %v", err)
	}
	completionPayload := decodeSyncMergePayload(t, completionSurvives.Payload)
	if got := syncMergeInt64(t, completionPayload, "completedAt"); got != 2000 {
		t.Fatalf("completedAt from older side = %d, want 2000", got)
	}
}

func TestSyncMergeHistoryDeleteBarrier(t *testing.T) {
	putPayload := `{"kind":"episode","firstActivityAt":1000,"lastActivityAt":2000}`
	tests := []struct {
		name      string
		current   syncMergeCurrent
		incoming  parsedSyncMutation
		changed   bool
		operation string
		winner    string
	}{
		{
			name:     "newer delete wins",
			current:  syncMergeCurrentFixture("10000000-0000-4000-8000-000000000001", syncMergeDeviceA, "put", 100, 0, putPayload),
			incoming: syncMergeIncomingFixture("10000000-0000-4000-8000-000000000002", syncMergeDeviceB, "full_history", "delete", 200, 0, ""),
			changed:  true, operation: "delete", winner: "10000000-0000-4000-8000-000000000002",
		},
		{
			name:     "stale put cannot resurrect newer delete",
			current:  syncMergeCurrentFixture("10000000-0000-4000-8000-000000000003", syncMergeDeviceB, "delete", 200, 0, ""),
			incoming: syncMergeIncomingFixture("10000000-0000-4000-8000-000000000004", syncMergeDeviceA, "full_history", "put", 100, 0, putPayload),
			changed:  false, operation: "delete", winner: "10000000-0000-4000-8000-000000000003",
		},
		{
			name:     "later put resurrects delete",
			current:  syncMergeCurrentFixture("10000000-0000-4000-8000-000000000005", syncMergeDeviceA, "delete", 100, 0, ""),
			incoming: syncMergeIncomingFixture("10000000-0000-4000-8000-000000000006", syncMergeDeviceB, "full_history", "put", 200, 0, putPayload),
			changed:  true, operation: "put", winner: "10000000-0000-4000-8000-000000000006",
		},
		{
			name:     "older delete cannot clear newer put",
			current:  syncMergeCurrentFixture("10000000-0000-4000-8000-000000000007", syncMergeDeviceB, "put", 200, 0, putPayload),
			incoming: syncMergeIncomingFixture("10000000-0000-4000-8000-000000000008", syncMergeDeviceA, "full_history", "delete", 100, 0, ""),
			changed:  false, operation: "put", winner: "10000000-0000-4000-8000-000000000007",
		},
	}
	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			resolution, err := resolveMutableSync(test.current, true, test.incoming)
			if err != nil {
				t.Fatalf("resolveMutableSync() error = %v", err)
			}
			if resolution.Changed != test.changed || resolution.Operation != test.operation ||
				resolution.WinnerMutationID != test.winner {
				t.Fatalf("resolution = %+v, want changed=%v operation=%s winner=%s",
					resolution, test.changed, test.operation, test.winner)
			}
			if resolution.Operation == "delete" && len(resolution.Payload) != 0 {
				t.Fatalf("delete payload = %s, want empty", resolution.Payload)
			}
		})
	}
}

func TestSyncMergeNonHistoryUsesHLC(t *testing.T) {
	current := syncMergeCurrentFixture(
		"20000000-0000-4000-8000-000000000001", syncMergeDeviceA, "put", 100, 0,
		`{"progress":0.9}`)
	incoming := syncMergeIncomingFixture(
		"20000000-0000-4000-8000-000000000002", syncMergeDeviceB, "continue_progress", "put", 200, 0,
		`{"progress":0.2}`)

	resolution, err := resolveMutableSync(current, true, incoming)
	if err != nil {
		t.Fatalf("resolveMutableSync() error = %v", err)
	}
	if !resolution.Changed || resolution.WinnerMutationID != incoming.MutationID ||
		string(resolution.Payload) != string(incoming.Payload) {
		t.Fatalf("newer lower progress did not win whole-record LWW: %+v", resolution)
	}

	tieWinner := incoming
	tieWinner.MutationID = "20000000-0000-4000-8000-000000000003"
	tieWinner.HLCPhysicalMS = current.HLCPhysicalMS
	tieWinner.HLCCounter = current.HLCCounter
	tieWinner.Payload = json.RawMessage(`{"progress":0.1}`)
	tied, err := resolveMutableSync(current, true, tieWinner)
	if err != nil {
		t.Fatalf("resolveMutableSync(device tie) error = %v", err)
	}
	if !tied.Changed || tied.WinnerDeviceID != syncMergeDeviceB {
		t.Fatalf("device-ID tie break did not select lexicographically newer device: %+v", tied)
	}

	currentB := current
	currentB.DeviceID = syncMergeDeviceB
	tieLoser := tieWinner
	tieLoser.DeviceID = syncMergeDeviceA
	unchanged, err := resolveMutableSync(currentB, true, tieLoser)
	if err != nil {
		t.Fatalf("resolveMutableSync(reverse device tie) error = %v", err)
	}
	if unchanged.Changed || unchanged.WinnerDeviceID != syncMergeDeviceB {
		t.Fatalf("device-ID tie break replaced newer current device: %+v", unchanged)
	}
}

func TestSyncMergeHistoryCanonicalJSONDeterministic(t *testing.T) {
	current := syncMergeCurrentFixture(
		"30000000-0000-4000-8000-000000000001", syncMergeDeviceB, "put", 200, 0,
		`{"meta":{"z":2,"a":1},"lastActivityAt":9007199254740997,"id":"episode-1","firstActivityAt":9007199254740995}`)
	incoming := syncMergeIncomingFixture(
		"30000000-0000-4000-8000-000000000002", syncMergeDeviceA, "full_history", "put", 100, 0,
		`{"completedAt":9007199254740994,"lastActivityAt":9007199254740996,"firstActivityAt":9007199254740993,"id":"stale"}`)

	first, err := resolveMutableSync(current, true, incoming)
	if err != nil {
		t.Fatalf("resolveMutableSync(first) error = %v", err)
	}
	second, err := resolveMutableSync(current, true, incoming)
	if err != nil {
		t.Fatalf("resolveMutableSync(second) error = %v", err)
	}
	if !bytes.Equal(first.Payload, second.Payload) {
		t.Fatalf("canonical payload changed between runs: %s != %s", first.Payload, second.Payload)
	}
	want := `{"completedAt":9007199254740994,"firstActivityAt":9007199254740993,"id":"episode-1","lastActivityAt":9007199254740997,"meta":{"a":1,"z":2}}`
	if string(first.Payload) != want {
		t.Fatalf("canonical payload = %s, want %s", first.Payload, want)
	}
}

func TestSyncMergeHistoryRejectsMalformedPayload(t *testing.T) {
	tests := []struct {
		name    string
		payload string
	}{
		{name: "non-object", payload: `[]`},
		{name: "missing first", payload: `{"lastActivityAt":2000}`},
		{name: "string timestamp", payload: `{"firstActivityAt":"1000","lastActivityAt":2000}`},
		{name: "fractional timestamp", payload: `{"firstActivityAt":1000.5,"lastActivityAt":2000}`},
		{name: "negative completion", payload: `{"firstActivityAt":1000,"lastActivityAt":2000,"completedAt":-1}`},
		{name: "no positive first", payload: `{"firstActivityAt":0,"lastActivityAt":2000}`},
		{name: "no positive last", payload: `{"firstActivityAt":1000,"lastActivityAt":0}`},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			incoming := syncMergeIncomingFixture(
				"40000000-0000-4000-8000-000000000001", syncMergeDeviceA,
				"full_history", "put", 100, 0, test.payload)
			if _, err := resolveMutableSync(syncMergeCurrent{}, false, incoming); err == nil {
				t.Fatalf("resolveMutableSync(%s) succeeded", test.payload)
			}
		})
	}
}

func TestSyncMergeNoCurrentUsesIncoming(t *testing.T) {
	incoming := syncMergeIncomingFixture(
		"50000000-0000-4000-8000-000000000001", syncMergeDeviceA,
		"full_history", "put", 100, 0,
		`{"z":1,"lastActivityAt":2000,"firstActivityAt":1000,"a":2}`)
	resolution, err := resolveMutableSync(syncMergeCurrent{}, false, incoming)
	if err != nil {
		t.Fatalf("resolveMutableSync(no current put) error = %v", err)
	}
	if !resolution.Changed || resolution.Operation != "put" ||
		resolution.WinnerMutationID != incoming.MutationID {
		t.Fatalf("no-current put resolution = %+v", resolution)
	}
	if got := string(resolution.Payload); got != `{"a":2,"firstActivityAt":1000,"lastActivityAt":2000,"z":1}` {
		t.Fatalf("no-current canonical payload = %s", got)
	}

	incoming.Operation = "delete"
	incoming.MutationID = "50000000-0000-4000-8000-000000000002"
	incoming.Payload = nil
	deleted, err := resolveMutableSync(syncMergeCurrent{}, false, incoming)
	if err != nil {
		t.Fatalf("resolveMutableSync(no current delete) error = %v", err)
	}
	if !deleted.Changed || deleted.Operation != "delete" || len(deleted.Payload) != 0 {
		t.Fatalf("no-current delete resolution = %+v", deleted)
	}
}

func TestSyncMergeHistoryOlderCompletionContribution(t *testing.T) {
	current := syncMergeCurrentFixture(
		"60000000-0000-4000-8000-000000000001", syncMergeDeviceB, "put", 200, 0,
		`{"kind":"episode","firstActivityAt":1000,"lastActivityAt":5000,"title":"newer"}`)
	incoming := syncMergeIncomingFixture(
		"60000000-0000-4000-8000-000000000002", syncMergeDeviceA,
		"full_history", "put", 100, 0,
		`{"kind":"episode","firstActivityAt":1200,"lastActivityAt":3000,"completedAt":2500,"title":"older"}`)

	resolution, err := resolveMutableSync(current, true, incoming)
	if err != nil {
		t.Fatalf("resolveMutableSync() error = %v", err)
	}
	payload := decodeSyncMergePayload(t, resolution.Payload)
	if !resolution.Changed || resolution.WinnerMutationID != current.MutationID {
		t.Fatalf("older completion contribution resolution = %+v", resolution)
	}
	if got := syncMergeInt64(t, payload, "completedAt"); got != 2500 {
		t.Fatalf("completedAt = %d, want 2500", got)
	}
	if payload["title"] != "newer" {
		t.Fatalf("ordinary winner metadata changed: %#v", payload)
	}
}

func TestSyncMergeHistoryRejectsInconsistentTimestamps(t *testing.T) {
	for _, payload := range []string{
		`{"firstActivityAt":2000,"lastActivityAt":1000}`,
		`{"firstActivityAt":1000,"lastActivityAt":2000,"completedAt":0}`,
		`{"firstActivityAt":1000,"lastActivityAt":2000,"completedAt":500}`,
		`{"firstActivityAt":1000,"lastActivityAt":2000,"completedAt":2500}`,
	} {
		incoming := syncMergeIncomingFixture(
			"70000000-0000-4000-8000-000000000001", syncMergeDeviceA,
			"full_history", "put", 100, 0, payload)
		if _, err := resolveMutableSync(syncMergeCurrent{}, false, incoming); err == nil {
			t.Fatalf("resolveMutableSync(%s) succeeded", payload)
		}
	}
}
