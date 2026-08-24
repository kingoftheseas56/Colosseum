package account

import (
	"context"
	"encoding/json"
	"strconv"
	"testing"
	"time"
)

func fixtureSyncMutation(
	mutationID,
	deviceID,
	recordKey,
	value string,
	physical int64,
	counter uint64,
) SyncMutationInput {
	payload := json.RawMessage(nil)
	operation := "delete"
	if value != "" {
		operation = "put"
		payload = json.RawMessage(`{"value":"` + value + `"}`)
	}
	return SyncMutationInput{
		MutationID:    mutationID,
		DeviceID:      deviceID,
		Category:      "collection",
		RecordKey:     recordKey,
		SchemaVersion: 1,
		HLCPhysicalMS: formatInt64(physical),
		HLCCounter:    formatUint64(counter),
		Operation:     operation,
		Payload:       payload,
	}
}

func formatInt64(value int64) string {
	return strconv.FormatInt(value, 10)
}

func formatUint64(value uint64) string {
	return strconv.FormatUint(value, 10)
}

func TestSyncFullHistoryCategoryRoundTrips(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncHistory")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)

	mutation := fixtureSyncMutation(
		"89012345-8901-4890-8890-89012345abcd",
		auth.Device.ID,
		"history/ZXBpc29kZQ/c2hvdy0xLWUx",
		"",
		fixture.clock.Now().UnixMilli(),
		0)
	mutation.Category = "full_history"
	mutation.Operation = "put"
	mutation.Payload = json.RawMessage(
		`{"kind":"episode","id":"show-1/e1","firstActivityAt":1000,"lastActivityAt":2000,"completedAt":2000}`)

	push, err := fixture.service.PushSync(
		context.Background(),
		auth,
		[]SyncMutationInput{mutation})
	if err != nil {
		t.Fatalf("PushSync(history) error = %v", err)
	}
	if len(push.Results) != 1 ||
		!push.Results[0].Accepted ||
		!push.Results[0].Won {
		t.Fatalf("history push result = %+v", push.Results)
	}

	pull, err := fixture.service.PullSync(
		context.Background(),
		auth,
		0)
	if err != nil {
		t.Fatalf("PullSync(history) error = %v", err)
	}
	if len(pull.Entries) != 1 {
		t.Fatalf("history pull entries = %d, want 1", len(pull.Entries))
	}
	if pull.Entries[0].Mutation.Category != "full_history" {
		t.Fatalf("history pull category = %q", pull.Entries[0].Mutation.Category)
	}
	if string(pull.Entries[0].Mutation.Payload) != string(mutation.Payload) {
		t.Fatalf(
			"history pull payload = %s, want %s",
			pull.Entries[0].Mutation.Payload,
			mutation.Payload)
	}
}

func TestSyncDuplicateMutationIsIdempotent(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncDuplicate")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)

	mutation := fixtureSyncMutation(
		"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
		auth.Device.ID,
		"manga/item",
		"one",
		fixture.clock.Now().UnixMilli(),
		0)

	first, err := fixture.service.PushSync(
		context.Background(),
		auth,
		[]SyncMutationInput{mutation})
	if err != nil {
		t.Fatalf("PushSync(first) error = %v", err)
	}
	second, err := fixture.service.PushSync(
		context.Background(),
		auth,
		[]SyncMutationInput{mutation})
	if err != nil {
		t.Fatalf("PushSync(second) error = %v", err)
	}

	if len(first.Results) != 1 || len(second.Results) != 1 {
		t.Fatal("unexpected push result count")
	}
	if first.Results[0].ServerSeq != second.Results[0].ServerSeq {
		t.Fatalf("duplicate seq changed: %d != %d",
			first.Results[0].ServerSeq,
			second.Results[0].ServerSeq)
	}

	var count int
	if err := fixture.pool.QueryRow(
		context.Background(),
		`SELECT count(*) FROM account_sync_journal WHERE account_id = $1::uuid`,
		auth.Account.ID).Scan(&count); err != nil {
		t.Fatalf("count journal: %v", err)
	}
	if count != 1 {
		t.Fatalf("journal count = %d, want 1", count)
	}
}

func TestSyncConcurrentDuplicateMutationStaysIdempotent(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncConcurrentDuplicate")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)

	mutation := fixtureSyncMutation(
		"56789012-5678-4567-8567-56789012abcd",
		auth.Device.ID,
		"manga/item",
		"one",
		fixture.clock.Now().UnixMilli(),
		0)

	type pushResult struct {
		response SyncPushResponse
		err      error
	}

	start := make(chan struct{})
	results := make(chan pushResult, 2)

	for index := 0; index < 2; index++ {
		go func() {
			<-start
			response, err := fixture.service.PushSync(
				context.Background(),
				auth,
				[]SyncMutationInput{mutation})
			results <- pushResult{
				response: response,
				err:      err,
			}
		}()
	}

	close(start)
	first := <-results
	second := <-results

	if first.err != nil || second.err != nil {
		t.Fatalf("concurrent duplicate errors = %v / %v", first.err, second.err)
	}
	if len(first.response.Results) != 1 || len(second.response.Results) != 1 {
		t.Fatal("concurrent duplicate response count mismatch")
	}
	if first.response.Results[0].ServerSeq != second.response.Results[0].ServerSeq {
		t.Fatalf(
			"concurrent duplicate server seq = %d / %d, want identical",
			first.response.Results[0].ServerSeq,
			second.response.Results[0].ServerSeq)
	}

	var count int
	if err := fixture.pool.QueryRow(
		context.Background(),
		`SELECT count(*)
         FROM account_sync_journal
         WHERE account_id = $1::uuid
           AND mutation_id = $2::uuid`,
		auth.Account.ID,
		mutation.MutationID).Scan(&count); err != nil {
		t.Fatalf("count concurrent duplicate journal: %v", err)
	}
	if count != 1 {
		t.Fatalf("concurrent duplicate journal count = %d, want 1", count)
	}
}

func TestSyncConcurrentSameRecordUsesHLCTuple(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncConcurrentWinner")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	now := fixture.clock.Now().UnixMilli()

	older := fixtureSyncMutation(
		"67890123-6789-4678-8678-67890123abcd",
		auth.Device.ID,
		"manga/item",
		"older",
		now+100,
		99)
	newer := fixtureSyncMutation(
		"78901234-7890-4789-8789-78901234abcd",
		auth.Device.ID,
		"manga/item",
		"newer",
		now+200,
		0)

	type pushError struct {
		err error
	}
	start := make(chan struct{})
	results := make(chan pushError, 2)

	for _, mutation := range []SyncMutationInput{older, newer} {
		mutation := mutation
		go func() {
			<-start
			_, err := fixture.service.PushSync(
				context.Background(),
				auth,
				[]SyncMutationInput{mutation})
			results <- pushError{err: err}
		}()
	}

	close(start)
	for index := 0; index < 2; index++ {
		result := <-results
		if result.err != nil {
			t.Fatalf("concurrent same-record push error = %v", result.err)
		}
	}

	var mutationID string
	if err := fixture.pool.QueryRow(
		context.Background(),
		`SELECT mutation_id::text
         FROM account_sync_current
         WHERE account_id = $1::uuid
           AND category = 'collection'
           AND record_key = 'manga/item'`,
		auth.Account.ID).Scan(&mutationID); err != nil {
		t.Fatalf("load concurrent current winner: %v", err)
	}
	if mutationID != newer.MutationID {
		t.Fatalf("concurrent current winner = %s, want %s", mutationID, newer.MutationID)
	}
}

func TestSyncWinnerIgnoresArrivalOrderAndTombstoneIsFirstClass(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncWinner")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	now := fixture.clock.Now().UnixMilli()

	newer := fixtureSyncMutation(
		"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
		auth.Device.ID,
		"manga/item",
		"newer",
		now+200,
		0)
	older := fixtureSyncMutation(
		"cccccccc-cccc-4ccc-8ccc-cccccccccccc",
		auth.Device.ID,
		"manga/item",
		"older",
		now+100,
		9)

	if _, err := fixture.service.PushSync(
		context.Background(),
		auth,
		[]SyncMutationInput{newer, older}); err != nil {
		t.Fatalf("PushSync() error = %v", err)
	}

	var mutationID, operation string
	if err := fixture.pool.QueryRow(
		context.Background(),
		`SELECT mutation_id::text, operation
         FROM account_sync_current
         WHERE account_id = $1::uuid
           AND category = 'collection'
           AND record_key = 'manga/item'`,
		auth.Account.ID).Scan(&mutationID, &operation); err != nil {
		t.Fatalf("load current: %v", err)
	}
	if mutationID != newer.MutationID || operation != "put" {
		t.Fatalf("current = %s/%s, want newer put", mutationID, operation)
	}

	tombstone := fixtureSyncMutation(
		"dddddddd-dddd-4ddd-8ddd-dddddddddddd",
		auth.Device.ID,
		"manga/item",
		"",
		now+300,
		0)
	if _, err := fixture.service.PushSync(
		context.Background(),
		auth,
		[]SyncMutationInput{tombstone}); err != nil {
		t.Fatalf("PushSync(tombstone) error = %v", err)
	}

	if err := fixture.pool.QueryRow(
		context.Background(),
		`SELECT mutation_id::text, operation
         FROM account_sync_current
         WHERE account_id = $1::uuid
           AND category = 'collection'
           AND record_key = 'manga/item'`,
		auth.Account.ID).Scan(&mutationID, &operation); err != nil {
		t.Fatalf("load tombstone current: %v", err)
	}
	if mutationID != tombstone.MutationID || operation != "delete" {
		t.Fatalf("current = %s/%s, want tombstone", mutationID, operation)
	}
}

func TestSyncPullCursorReturnsAscendingJournalIncludingLosers(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncCursor")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	now := fixture.clock.Now().UnixMilli()

	mutations := []SyncMutationInput{
		fixtureSyncMutation(
			"eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee",
			auth.Device.ID, "manga/item", "winner", now+20, 0),
		fixtureSyncMutation(
			"ffffffff-ffff-4fff-8fff-ffffffffffff",
			auth.Device.ID, "manga/item", "loser", now+10, 0),
	}
	if _, err := fixture.service.PushSync(
		context.Background(), auth, mutations); err != nil {
		t.Fatalf("PushSync() error = %v", err)
	}

	pull, err := fixture.service.PullSync(
		context.Background(), auth, 0)
	if err != nil {
		t.Fatalf("PullSync() error = %v", err)
	}
	if len(pull.Entries) != 2 {
		t.Fatalf("entries = %d, want 2", len(pull.Entries))
	}
	if pull.Entries[0].ServerSeq >= pull.Entries[1].ServerSeq {
		t.Fatal("pull journal is not ascending")
	}
	if !pull.Entries[0].Won || pull.Entries[1].Won {
		t.Fatalf("won flags = %v/%v, want true/false",
			pull.Entries[0].Won,
			pull.Entries[1].Won)
	}

	after, err := fixture.service.PullSync(
		context.Background(),
		auth,
		pull.Entries[0].ServerSeq)
	if err != nil {
		t.Fatalf("PullSync(after) error = %v", err)
	}
	if len(after.Entries) != 1 ||
		after.Entries[0].ServerSeq != pull.Entries[1].ServerSeq {
		t.Fatal("cursor replay did not return exactly the later entry")
	}
}

func TestSyncFutureClockIsRejectedPerMutationWithServiceTime(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncSkew")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)

	seed := fixtureSyncMutation(
		"45678901-4567-4456-8456-45678901abcd",
		auth.Device.ID,
		"manga/item",
		"seed",
		fixture.clock.Now().UnixMilli(),
		0)
	if _, err := fixture.service.PushSync(
		context.Background(),
		auth,
		[]SyncMutationInput{seed}); err != nil {
		t.Fatalf("PushSync(seed) error = %v", err)
	}

	mutation := fixtureSyncMutation(
		"12345678-1234-4234-8234-1234567890ab",
		auth.Device.ID,
		"manga/item",
		"future",
		fixture.clock.Now().Add(11*time.Minute).UnixMilli(),
		0)

	response, err := fixture.service.PushSync(
		context.Background(),
		auth,
		[]SyncMutationInput{mutation})
	if err != nil {
		t.Fatalf("PushSync() error = %v", err)
	}
	if len(response.Results) != 1 ||
		response.Results[0].Accepted ||
		response.Results[0].Code != "clock_skew" {
		t.Fatalf("result = %+v, want clock_skew rejection", response.Results)
	}
	if response.ServerTimeMS != fixture.clock.Now().UnixMilli() {
		t.Fatalf("server time = %d, want %d",
			response.ServerTimeMS,
			fixture.clock.Now().UnixMilli())
	}
	if response.Results[0].Current == nil ||
		response.Results[0].Current.MutationID != seed.MutationID {
		t.Fatalf("clock_skew current metadata = %+v, want seed winner",
			response.Results[0].Current)
	}
}

func TestSyncPreviousWinnerIsStoredAndPrunedByRetention(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncVersions")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	now := fixture.clock.Now().UnixMilli()

	first := fixtureSyncMutation(
		"23456789-2345-4234-8234-234567890abc",
		auth.Device.ID,
		"manga/item",
		"one",
		now,
		0)
	second := fixtureSyncMutation(
		"34567890-3456-4345-8345-34567890abcd",
		auth.Device.ID,
		"manga/item",
		"two",
		now+1,
		0)

	if _, err := fixture.service.PushSync(
		context.Background(),
		auth,
		[]SyncMutationInput{first, second}); err != nil {
		t.Fatalf("PushSync() error = %v", err)
	}

	var count int
	if err := fixture.pool.QueryRow(
		context.Background(),
		`SELECT count(*) FROM account_sync_versions WHERE account_id = $1::uuid`,
		auth.Account.ID).Scan(&count); err != nil {
		t.Fatalf("count versions: %v", err)
	}
	if count != 1 {
		t.Fatalf("version count = %d, want 1", count)
	}

	fixture.clock.Advance(31 * 24 * time.Hour)
	if err := fixture.service.PruneSyncVersions(
		context.Background(),
		fixture.clock.Now().Add(-30*24*time.Hour)); err != nil {
		t.Fatalf("PruneSyncVersions() error = %v", err)
	}
	if err := fixture.pool.QueryRow(
		context.Background(),
		`SELECT count(*) FROM account_sync_versions WHERE account_id = $1::uuid`,
		auth.Account.ID).Scan(&count); err != nil {
		t.Fatalf("count versions after prune: %v", err)
	}
	if count != 0 {
		t.Fatalf("version count after prune = %d, want 0", count)
	}
}
