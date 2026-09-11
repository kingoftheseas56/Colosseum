package account

import (
	"context"
	"encoding/json"
	"strconv"
	"testing"
)

func historySemanticMutation(
	mutationID,
	deviceID string,
	physical int64,
	counter uint64,
	payload string,
) SyncMutationInput {
	return SyncMutationInput{
		MutationID:    mutationID,
		DeviceID:      deviceID,
		Category:      "full_history",
		RecordKey:     fixtureHistoryRecordKey("episode", "show-1/e1"),
		SchemaVersion: 1,
		HLCPhysicalMS: formatInt64(physical),
		HLCCounter:    formatUint64(counter),
		Operation:     "put",
		Payload:       json.RawMessage(payload),
	}
}

func activityResetSemanticMutation(
	mutationID,
	deviceID string,
	generation uint64,
	resetAtMS,
	physical int64,
	counter uint64,
) SyncMutationInput {
	payload := json.RawMessage(
		`{"resetGeneration":` + strconv.FormatUint(generation, 10) +
			`,"resetAtMs":` + strconv.FormatInt(resetAtMS, 10) + `}`)
	return SyncMutationInput{
		MutationID:    mutationID,
		DeviceID:      deviceID,
		Category:      "activity_fact",
		RecordKey:     "activity/reset",
		SchemaVersion: 1,
		HLCPhysicalMS: formatInt64(physical),
		HLCCounter:    formatUint64(counter),
		Operation:     "put",
		Payload:       payload,
	}
}

func TestFullHistoryServerMergeConvergesInBothUploadOrders(t *testing.T) {
	orders := []struct {
		name  string
		first int
	}{
		{name: "older then newer", first: 0},
		{name: "newer then older", first: 1},
	}

	const (
		olderPayload = `{"kind":"episode","id":"show-1/e1","firstActivityAt":1000,"lastActivityAt":5000,"completedAt":3000}`
		newerPayload = `{"kind":"episode","id":"show-1/e1","firstActivityAt":2000,"lastActivityAt":7000,"completedAt":2500}`
		wantPayload  = `{"completedAt":2500,"firstActivityAt":1000,"id":"show-1/e1","kind":"episode","lastActivityAt":7000}`
	)

	for _, order := range orders {
		t.Run(order.name, func(t *testing.T) {
			fixture := newServiceFixture(t)
			username := "HistoryMergeOlder"
			if order.first == 1 {
				username = "HistoryMergeNewer"
			}
			created := createFixtureAccount(t, fixture, username)
			auth := authenticateFixtureSession(t, fixture, created.Session)
			now := fixture.clock.Now().UnixMilli()

			older := historySemanticMutation(
				"81000000-0000-4000-8000-000000000001",
				auth.Device.ID,
				now,
				0,
				olderPayload)
			newer := historySemanticMutation(
				"81000000-0000-4000-8000-000000000002",
				auth.Device.ID,
				now+1,
				0,
				newerPayload)
			mutations := []SyncMutationInput{older, newer}
			if order.first == 1 {
				mutations = []SyncMutationInput{newer, older}
			}

			for _, mutation := range mutations {
				push, err := fixture.service.PushSync(
					context.Background(), auth, []SyncMutationInput{mutation})
				if err != nil || len(push.Results) != 1 || !push.Results[0].Accepted {
					t.Fatalf("History push = %+v, err=%v", push.Results, err)
				}
			}

			pull, err := fixture.service.PullSync(context.Background(), auth, 0)
			if err != nil {
				t.Fatalf("PullSync(History) error = %v", err)
			}
			var winner *SyncPullEntry
			for index := range pull.Entries {
				if pull.Entries[index].Won {
					winner = &pull.Entries[index]
				}
			}
			if winner == nil {
				t.Fatalf("History pull has no winner: %+v", pull.Entries)
			}
			if string(winner.Mutation.Payload) != wantPayload {
				t.Fatalf("canonical History payload = %s, want %s",
					winner.Mutation.Payload, wantPayload)
			}
		})
	}
}

func TestFullHistoryPullPreservesRequestAndMaterializedHLC(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "HistoryMaterializedHLC")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	now := fixture.clock.Now().UnixMilli()

	const highPayload = `{"firstActivityAt":2000,"id":"show-1/e1","kind":"episode","lastActivityAt":7000}`
	const mergedPayload = `{"firstActivityAt":1000,"id":"show-1/e1","kind":"episode","lastActivityAt":7000}`
	high := historySemanticMutation(
		"87000000-0000-4000-8000-000000000001",
		auth.Device.ID,
		now+100,
		0,
		highPayload)
	merged := historySemanticMutation(
		"87000000-0000-4000-8000-000000000002",
		auth.Device.ID,
		now+50,
		0,
		mergedPayload)

	first := pushOneSyncMutation(t, fixture, auth, high)
	if !first.Accepted || !first.Won {
		t.Fatalf("first History push = %+v", first)
	}
	second := pushOneSyncMutation(t, fixture, auth, merged)
	if !second.Accepted || !second.Won {
		t.Fatalf("merged History push = %+v", second)
	}

	pull, err := fixture.service.PullSync(
		context.Background(),
		auth,
		first.ServerSeq)
	if err != nil {
		t.Fatalf("PullSync after first winner = %v", err)
	}
	if len(pull.Entries) != 1 {
		t.Fatalf("PullSync entries = %d, want 1: %+v", len(pull.Entries), pull.Entries)
	}
	entry := pull.Entries[0]
	if entry.ServerSeq != second.ServerSeq || !entry.Won {
		t.Fatalf("merged History pull entry = %+v", entry)
	}
	if entry.Mutation.MutationID != merged.MutationID ||
		entry.Mutation.HLCPhysicalMS != merged.HLCPhysicalMS ||
		entry.Mutation.HLCCounter != merged.HLCCounter ||
		entry.Mutation.DeviceID != merged.DeviceID {
		t.Fatalf("pull request identity = %+v, want mutation %s at %s/%s",
			entry.Mutation,
			merged.MutationID,
			merged.HLCPhysicalMS,
			merged.HLCCounter)
	}
	if entry.Mutation.MaterializedHLCPhysicalMS != high.HLCPhysicalMS ||
		entry.Mutation.MaterializedHLCCounter != high.HLCCounter ||
		entry.Mutation.MaterializedDeviceID != high.DeviceID {
		t.Fatalf("pull materialized HLC = %s/%s/%s, want %s/%s/%s",
			entry.Mutation.MaterializedHLCPhysicalMS,
			entry.Mutation.MaterializedHLCCounter,
			entry.Mutation.MaterializedDeviceID,
			high.HLCPhysicalMS,
			high.HLCCounter,
			high.DeviceID)
	}
	if string(entry.Mutation.Payload) != mergedPayload {
		t.Fatalf("merged History payload = %s, want %s",
			entry.Mutation.Payload,
			mergedPayload)
	}
}

func TestActivityResetSuppressesFactsInBothUploadOrders(t *testing.T) {
	orders := []struct {
		name       string
		resetFirst bool
	}{
		{name: "fact then reset", resetFirst: false},
		{name: "reset then fact", resetFirst: true},
	}

	for _, order := range orders {
		t.Run(order.name, func(t *testing.T) {
			fixture := newServiceFixture(t)
			username := "ActivityResetFactFirst"
			if order.resetFirst {
				username = "ActivityResetResetFirst"
			}
			created := createFixtureAccount(t, fixture, username)
			auth := authenticateFixtureSession(t, fixture, created.Session)
			now := fixture.clock.Now().UnixMilli()
			fact := activityMutation(
				"82000000-0000-4000-8000-000000000001",
				auth.Device.ID,
				"82000000-0000-4000-8000-0000000000a1",
				activityPlaybackPayload("82000000-0000-4000-8000-0000000000a1"))
			reset := activityResetSemanticMutation(
				"82000000-0000-4000-8000-000000000002",
				auth.Device.ID,
				1,
				now,
				now+1,
				0)

			mutations := []SyncMutationInput{fact, reset}
			if order.resetFirst {
				mutations = []SyncMutationInput{reset, fact}
			}
			for _, mutation := range mutations {
				result := pushOneActivity(t, fixture, auth, mutation)
				if !result.Accepted {
					t.Fatalf("Activity mutation = %+v", result)
				}
			}

			if got := queryInt(t, fixture,
				"SELECT count(*) FROM account_activity_facts WHERE account_id = $1::uuid AND suppressed = true",
				auth.Account.ID); got != 1 {
				t.Fatalf("suppressed Activity facts = %d, want 1", got)
			}
			pull, err := fixture.service.PullSync(context.Background(), auth, 0)
			if err != nil {
				t.Fatalf("PullSync(after reset) error = %v", err)
			}
			if len(pull.Entries) != 1 ||
				pull.Entries[0].Mutation.RecordKey != "activity/reset" {
				t.Fatalf("PullSync(after reset) = %+v, want only reset marker", pull.Entries)
			}
		})
	}
}

func TestActivityResetGenerationDominatesOfflineHLC(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivityResetGeneration")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	now := fixture.clock.Now().UnixMilli()

	first := activityResetSemanticMutation(
		"83000000-0000-4000-8000-000000000001",
		auth.Device.ID,
		1,
		now,
		now+200,
		0)
	second := activityResetSemanticMutation(
		"83000000-0000-4000-8000-000000000002",
		auth.Device.ID,
		2,
		now+1,
		now+100,
		0)
	stale := activityResetSemanticMutation(
		"83000000-0000-4000-8000-000000000003",
		auth.Device.ID,
		1,
		now+500,
		now+300,
		0)

	if result := pushOneActivity(t, fixture, auth, first); !result.Won {
		t.Fatalf("first reset = %+v", result)
	}
	if result := pushOneActivity(t, fixture, auth, second); !result.Won {
		t.Fatalf("higher-generation reset = %+v", result)
	}
	if result := pushOneActivity(t, fixture, auth, stale); result.Won {
		t.Fatalf("stale offline reset unexpectedly won: %+v", result)
	}

	var generation, resetAt, physical, counter int64
	if err := fixture.pool.QueryRow(context.Background(), `
		SELECT reset_generation, reset_at_ms, hlc_physical_ms, hlc_counter
		FROM account_activity_reset_state
		WHERE account_id = $1::uuid`, auth.Account.ID).
		Scan(&generation, &resetAt, &physical, &counter); err != nil {
		t.Fatalf("load Activity reset state: %v", err)
	}
	if generation != 2 || resetAt != now+1 || physical != now+100 || counter != 0 {
		t.Fatalf("Activity reset state = generation %d at %d HLC %d/%d, want 2 at %d HLC %d/0",
			generation, resetAt, physical, counter, now+1, now+100)
	}
}

func TestHistoryResetBlocksDelayedOfflineRecordsInBothOrders(t *testing.T) {
	orders := []struct {
		name       string
		resetFirst bool
	}{
		{name: "reset then delayed record", resetFirst: true},
		{name: "record then reset", resetFirst: false},
	}

	for _, order := range orders {
		t.Run(order.name, func(t *testing.T) {
			fixture := newServiceFixture(t)
			username := "HistoryResetFirst"
			if !order.resetFirst {
				username = "HistoryRecordFirst"
			}
			created := createFixtureAccount(t, fixture, username)
			auth := authenticateFixtureSession(t, fixture, created.Session)
			now := fixture.clock.Now().UnixMilli()
			old := historySemanticMutation(
				"84000000-0000-4000-8000-000000000001",
				auth.Device.ID,
				now,
				0,
				`{"kind":"episode","id":"show-1/e1","firstActivityAt":1000,"lastActivityAt":2000}`)
			reset := activityResetSemanticMutation(
				"84000000-0000-4000-8000-000000000002",
				auth.Device.ID,
				1,
				now,
				now+1,
				0)
			reset.Category = "full_history"
			reset.RecordKey = "history/reset"

			first, second := old, reset
			if order.resetFirst {
				first, second = reset, old
			}
			if result := pushOneSyncMutation(t, fixture, auth, first); !result.Accepted {
				t.Fatalf("first History reset mutation = %+v", result)
			}
			if result := pushOneSyncMutation(t, fixture, auth, second); !result.Accepted {
				t.Fatalf("second History reset mutation = %+v", result)
			}

			stale := old
			stale.MutationID = "84000000-0000-4000-8000-000000000003"
			stale.HLCPhysicalMS = formatInt64(now + 2)
			if result := pushOneSyncMutation(t, fixture, auth, stale); !result.Accepted || result.Won {
				t.Fatalf("delayed History record = %+v, want accepted loser", result)
			}
			if got := queryInt(t, fixture, `
				SELECT count(*) FROM account_sync_current
				WHERE account_id = $1::uuid AND category = 'full_history'
				  AND record_key <> 'history/reset'`, auth.Account.ID); got != 0 {
				t.Fatalf("current History records after reset = %d, want 0", got)
			}
		})
	}
}

func TestActivityResetBlocksDelayedHistoryRecordsInBothOrders(t *testing.T) {
	orders := []struct {
		name       string
		resetFirst bool
	}{
		{name: "reset then delayed history", resetFirst: true},
		{name: "history then reset", resetFirst: false},
	}

	for _, order := range orders {
		t.Run(order.name, func(t *testing.T) {
			fixture := newServiceFixture(t)
			username := "ActivityHistoryFirst"
			if order.resetFirst {
				username = "ActivityResetFirst"
			}
			created := createFixtureAccount(t, fixture, username)
			auth := authenticateFixtureSession(t, fixture, created.Session)
			now := fixture.clock.Now().UnixMilli()
			old := historySemanticMutation(
				"85000000-0000-4000-8000-000000000001",
				auth.Device.ID,
				now,
				0,
				`{"kind":"episode","id":"show-1/e1","firstActivityAt":1000,"lastActivityAt":2000}`)
			reset := activityResetSemanticMutation(
				"85000000-0000-4000-8000-000000000002",
				auth.Device.ID,
				1,
				now,
				now+1,
				0)

			first, second := old, reset
			if order.resetFirst {
				first, second = reset, old
			}
			if result := pushOneSyncMutation(t, fixture, auth, first); !result.Accepted {
				t.Fatalf("first Activity/History mutation = %+v", result)
			}
			if reset.Category == second.Category && second.RecordKey == reset.RecordKey {
				if result := pushOneActivity(t, fixture, auth, second); !result.Accepted {
					t.Fatalf("second Activity reset mutation = %+v", result)
				}
			} else if result := pushOneSyncMutation(t, fixture, auth, second); !result.Accepted {
				t.Fatalf("second History mutation = %+v", result)
			}

			stale := old
			stale.MutationID = "85000000-0000-4000-8000-000000000003"
			stale.HLCPhysicalMS = formatInt64(now + 2)
			if result := pushOneSyncMutation(t, fixture, auth, stale); !result.Accepted || result.Won {
				t.Fatalf("delayed History record after Activity reset = %+v, want accepted loser", result)
			}
			if got := queryInt(t, fixture, `
				SELECT count(*) FROM account_sync_current
				WHERE account_id = $1::uuid AND category = 'full_history'`, auth.Account.ID); got != 0 {
				t.Fatalf("History current rows after Activity reset = %d, want 0", got)
			}
		})
	}
}

func TestActivityResetHLCBarrierBlocksFutureDatedHistoryPayload(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivityResetHistoryHLC")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	now := fixture.clock.Now().UnixMilli()

	reset := activityResetSemanticMutation(
		"86000000-0000-4000-8000-000000000001",
		auth.Device.ID,
		1,
		now,
		now+200,
		0)
	if result := pushOneActivity(t, fixture, auth, reset); !result.Accepted {
		t.Fatalf("Activity reset = %+v", result)
	}

	// The payload looks newer by wall time, but its mutation HLC predates the
	// privacy reset. The HLC barrier must prevent it from becoming current.
	futureDated := historySemanticMutation(
		"86000000-0000-4000-8000-000000000002",
		auth.Device.ID,
		now+100,
		0,
		`{"kind":"episode","id":"show-1/e1","firstActivityAt":`+
			strconv.FormatInt(now+50, 10)+
			`,"lastActivityAt":`+
			strconv.FormatInt(now+1000, 10)+`}`)
	if result := pushOneSyncMutation(t, fixture, auth, futureDated); !result.Accepted || result.Won {
		t.Fatalf("future-dated History record = %+v, want accepted loser", result)
	}
}
