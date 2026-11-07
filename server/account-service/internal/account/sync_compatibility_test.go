package account

import (
	"context"
	"encoding/json"
	"fmt"
	"testing"
)

const nuvioHistoryFixture = `{"kind":"episode","id":"tt0100:1:1","firstActivityAt":1000,"lastActivityAt":4000,"source":"nuvio","displayId":"tmdb:1396_s1e1","displayTitle":"Pilot","latestKnownAt":4000}`

func TestNuvioSyncFeatureNegotiation(t *testing.T) {
	for _, tc := range []struct {
		header string
		legacy bool
	}{
		{"", true}, {"nuvio", false}, {"other, nuvio", false}, {"not-nuvio", true},
	} {
		if got := legacyNuvioSync(WithSyncFeatures(context.Background(), tc.header)); got != tc.legacy {
			t.Fatalf("header %q legacy=%v, want %v", tc.header, got, tc.legacy)
		}
	}
}

func TestNuvioSyncPullProjection(t *testing.T) {
	for _, header := range []string{"", "nuvio"} {
		t.Run("features="+header, func(t *testing.T) {
			ctx := WithSyncFeatures(context.Background(), header)
			for _, category := range []string{"full_history", "stremio_link"} {
				payload := nuvioHistoryFixture
				if category == "stremio_link" {
					payload = `{"mainSyncProvider":"nuvio"}`
				}
				original := SyncMutationView{Category: category, Operation: "put", Payload: json.RawMessage(payload), HLCPhysicalMS: "1000"}
				view, err := projectSyncForClient(ctx, original)
				if err != nil {
					t.Fatal(err)
				}
				if string(original.Payload) != payload || original.Operation != "put" {
					t.Fatal("projection mutated canonical state")
				}
				if header == "nuvio" {
					if string(view.Payload) != payload || view.Operation != "put" {
						t.Fatalf("capable client lost %s: %+v", category, view)
					}
					continue
				}
				if category == "stremio_link" {
					if view.Operation != "delete" || len(view.Payload) != 0 || view.DeletedAtMS != "" {
						t.Fatalf("legacy empty provider: %+v", view)
					}
				} else {
					object := decodeSyncMergePayload(t, view.Payload)
					for _, field := range []string{"source", "displayId", "displayTitle", "latestKnownAt"} {
						if _, exists := object[field]; exists {
							t.Fatalf("legacy History includes %s", field)
						}
					}
					if object["id"] != "tt0100:1:1" || syncMergeInt64(t, object, "lastActivityAt") != 4000 {
						t.Fatal("projection changed History facts")
					}
				}
			}
		})
	}
	ctx := WithSyncFeatures(context.Background(), "")
	for _, payload := range []string{`{"mainSyncProvider":"stremio"}`, `{"source":"stremio"}`} {
		category := "full_history"
		if payload == `{"mainSyncProvider":"stremio"}` {
			category = "stremio_link"
		}
		view, err := projectSyncForClient(ctx, SyncMutationView{Category: category, Operation: "put", Payload: json.RawMessage(payload)})
		if err != nil || string(view.Payload) != payload || view.Operation != "put" {
			t.Fatalf("changed Stremio payload: %+v %v", view, err)
		}
	}
}

func TestNuvioLegacyEmptyProviderPushPreservesStoredChoice(t *testing.T) {
	current := syncMergeCurrentFixture("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", syncMergeDeviceA, "put", 100, 0, `{"mainSyncProvider":"nuvio"}`)
	incoming := syncMergeIncomingFixture("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", syncMergeDeviceB, "stremio_link", "delete", 200, 0, "")
	incoming.RecordKey = "preferences/main-sync-provider"
	incoming.LegacyClient = true
	resolution, err := resolveMutableSync(current, true, incoming)
	if err != nil || resolution.Changed {
		t.Fatalf("legacy clear replaced Nuvio: %+v %v", resolution, err)
	}
	// The ignored request must retain its own HLC in the journal. Pull rejects
	// materialized clocks older than the request, even for losing mutations.
	if resolution.WinnerHLCPhysicalMS < incoming.HLCPhysicalMS {
		t.Fatal("legacy no-op regressed journal ordering")
	}
	incoming.LegacyClient = false
	resolution, err = resolveMutableSync(current, true, incoming)
	if err != nil || !resolution.Changed || resolution.Operation != "delete" {
		t.Fatalf("capable clear was ignored: %+v %v", resolution, err)
	}
}

func TestNuvioLegacyClientChoosingStremio(t *testing.T) {
	current := syncMergeCurrentFixture("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", syncMergeDeviceA, "put", 100, 0, `{"mainSyncProvider":"nuvio"}`)
	incoming := syncMergeIncomingFixture("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", syncMergeDeviceB, "stremio_link", "put", 200, 0, `{"mainSyncProvider":"stremio"}`)
	incoming.RecordKey = "preferences/main-sync-provider"
	incoming.LegacyClient = true
	resolution, err := resolveMutableSync(current, true, incoming)
	if err != nil || !resolution.Changed || string(resolution.Payload) != string(incoming.Payload) {
		t.Fatalf("Stremio user choice rejected: %+v %v", resolution, err)
	}
}

func TestNuvioLegacyHistoryPushPreservesPresentation(t *testing.T) {
	current := syncMergeCurrentFixture("aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa", syncMergeDeviceA, "put", 100, 0, nuvioHistoryFixture)
	incoming := syncMergeIncomingFixture("bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb", syncMergeDeviceB, "full_history", "put", 200, 0, `{"kind":"episode","id":"tt0100:1:1","firstActivityAt":1000,"lastActivityAt":5000,"completedAt":5000}`)
	resolution, err := resolveMutableSync(current, true, incoming)
	if err != nil {
		t.Fatal(err)
	}
	object := decodeSyncMergePayload(t, resolution.Payload)
	if object["source"] != "nuvio" || object["displayId"] != "tmdb:1396_s1e1" || object["displayTitle"] != "Pilot" || syncMergeInt64(t, object, "latestKnownAt") != 4000 || syncMergeInt64(t, object, "completedAt") != 5000 {
		t.Fatalf("legacy History clobbered Nuvio: %s", resolution.Payload)
	}
}

func TestNuvioSyncJournalAndSnapshotCompatibility(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "Nuvio_Compatibility")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	modern := WithSyncFeatures(context.Background(), "nuvio")
	legacy := WithSyncFeatures(context.Background(), "")
	sequence := 0
	push := func(ctx context.Context, category, operation, payload string) SyncPushResult {
		t.Helper()
		sequence++
		key := "preferences/main-sync-provider"
		if category == "full_history" {
			key = fixtureHistoryRecordKey("episode", "tt0100:1:1")
		}
		input := SyncMutationInput{
			MutationID: fmt.Sprintf("88000000-0000-4000-8000-%012x", sequence),
			DeviceID:   auth.Device.ID, Category: category, RecordKey: key,
			SchemaVersion: 1, Operation: operation, Payload: json.RawMessage(payload),
			HLCPhysicalMS: formatInt64(fixture.clock.Now().UnixMilli() + int64(sequence)), HLCCounter: "0",
		}
		result, err := fixture.service.PushSync(ctx, auth, []SyncMutationInput{input})
		if err != nil || len(result.Results) != 1 || !result.Results[0].Accepted {
			t.Fatalf("push %s/%s: %+v %v", category, operation, result, err)
		}
		retry, err := fixture.service.PushSync(ctx, auth, []SyncMutationInput{input})
		if err != nil || len(retry.Results) != 1 || retry.Results[0] != result.Results[0] {
			t.Fatalf("retry changed receipt: %+v %v", retry, err)
		}
		return result.Results[0]
	}
	push(modern, "full_history", "put", nuvioHistoryFixture)
	push(modern, "stremio_link", "put", `{"mainSyncProvider":"nuvio"}`)

	for _, header := range []string{"", "nuvio"} {
		ctx := WithSyncFeatures(context.Background(), header)
		pull, err := fixture.service.PullSync(ctx, auth, 0)
		if err != nil {
			t.Fatal(err)
		}
		snapshot, err := fixture.service.SnapshotSync(ctx, auth, "")
		if err != nil {
			t.Fatal(err)
		}
		for _, entries := range [][]SyncPullEntry{pull.Entries, snapshot.Entries} {
			if len(entries) != 2 {
				t.Fatalf("header=%q entries=%d", header, len(entries))
			}
			for _, entry := range entries {
				view := entry.Mutation
				if view.Category == "stremio_link" {
					if header == "" {
						if view.Operation != "delete" || len(view.Payload) != 0 {
							t.Fatalf("legacy provider view: %+v", view)
						}
					} else if view.Operation != "put" || decodeSyncMergePayload(t, view.Payload)["mainSyncProvider"] != "nuvio" {
						t.Fatalf("modern provider view: %+v", view)
					}
				} else {
					object := decodeSyncMergePayload(t, view.Payload)
					for _, field := range []string{"source", "displayId", "displayTitle", "latestKnownAt"} {
						_, exists := object[field]
						if exists != (header == "nuvio") {
							t.Fatalf("header=%q presentation field %s present=%v", header, field, exists)
						}
					}
				}
			}
		}
	}

	push(legacy, "full_history", "put", `{"kind":"episode","id":"tt0100:1:1","firstActivityAt":1000,"lastActivityAt":5000,"completedAt":5000}`)
	clear := push(legacy, "stremio_link", "delete", "")
	if clear.Won {
		t.Fatal("legacy empty provider replaced Nuvio")
	}
	// Pull the losing delete too, exercising the materialized HLC contract.
	if _, err := fixture.service.PullSync(modern, auth, 0); err != nil {
		t.Fatalf("pull after legacy no-op: %v", err)
	}
	snapshot, err := fixture.service.SnapshotSync(modern, auth, "")
	if err != nil {
		t.Fatal(err)
	}
	for _, entry := range snapshot.Entries {
		object := decodeSyncMergePayload(t, entry.Mutation.Payload)
		if entry.Mutation.Category == "stremio_link" {
			if object["mainSyncProvider"] != "nuvio" {
				t.Fatal("legacy clear erased Nuvio")
			}
		} else if object["source"] != "nuvio" || object["displayId"] != "tmdb:1396_s1e1" || syncMergeInt64(t, object, "completedAt") != 5000 {
			t.Fatal("legacy History erased presentation or progress")
		}
	}
	choice := push(legacy, "stremio_link", "put", `{"mainSyncProvider":"stremio"}`)
	if !choice.Won {
		t.Fatal("real Stremio choice did not win")
	}
	snapshot, err = fixture.service.SnapshotSync(modern, auth, "")
	if err != nil {
		t.Fatal(err)
	}
	for _, entry := range snapshot.Entries {
		if entry.Mutation.Category == "stremio_link" && decodeSyncMergePayload(t, entry.Mutation.Payload)["mainSyncProvider"] != "stremio" {
			t.Fatal("Stremio choice was not stored")
		}
	}
}
