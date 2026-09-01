package account

import (
	"context"
	"encoding/json"
	"fmt"
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
	if !pull.Entries[0].Canonical || !pull.Entries[0].Won {
		t.Fatalf("history pull flags = canonical:%v won:%v, want true/true",
			pull.Entries[0].Canonical, pull.Entries[0].Won)
	}
	if pull.Entries[0].Mutation.Category != "full_history" {
		t.Fatalf("history pull category = %q", pull.Entries[0].Mutation.Category)
	}
	const canonical = `{"completedAt":2000,"firstActivityAt":1000,"id":"show-1/e1","kind":"episode","lastActivityAt":2000}`
	if string(pull.Entries[0].Mutation.Payload) != canonical {
		t.Fatalf("history pull payload = %s, want %s",
			pull.Entries[0].Mutation.Payload, canonical)
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

func TestSyncCanonicalPullReturnsCurrentStateOnly(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncCanonicalPull")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	now := fixture.clock.Now().UnixMilli()

	winner := fixtureSyncMutation(
		"eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee",
		auth.Device.ID, "manga/item", "winner", now+20, 0)
	loser := fixtureSyncMutation(
		"ffffffff-ffff-4fff-8fff-ffffffffffff",
		auth.Device.ID, "manga/item", "loser", now+10, 0)
	push, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{winner, loser})
	if err != nil {
		t.Fatalf("PushSync() error = %v", err)
	}
	if len(push.Results) != 2 || !push.Results[0].Won || push.Results[1].Won {
		t.Fatalf("push results = %+v, want winner then HLC loser", push.Results)
	}

	pull, err := fixture.service.PullSync(context.Background(), auth, 0)
	if err != nil {
		t.Fatalf("PullSync() error = %v", err)
	}
	if len(pull.Entries) != 1 {
		t.Fatalf("canonical entries = %d, want 1", len(pull.Entries))
	}
	entry := pull.Entries[0]
	if !entry.Canonical || !entry.Won || entry.Mutation.MutationID != winner.MutationID {
		t.Fatalf("canonical entry = %+v, want current winner only", entry)
	}
	if string(entry.Mutation.Payload) != `{"value":"winner"}` {
		t.Fatalf("canonical payload = %s, want winner payload", entry.Mutation.Payload)
	}

	var journalCount int
	if err := fixture.pool.QueryRow(context.Background(),
		`SELECT count(*) FROM account_sync_journal WHERE account_id = $1::uuid`,
		auth.Account.ID).Scan(&journalCount); err != nil {
		t.Fatalf("count sync journal: %v", err)
	}
	if journalCount != 2 {
		t.Fatalf("journal rows = %d, want both accepted mutations", journalCount)
	}

	after, err := fixture.service.PullSync(context.Background(), auth, entry.ServerSeq)
	if err != nil {
		t.Fatalf("PullSync(after current) error = %v", err)
	}
	if len(after.Entries) != 0 {
		t.Fatalf("pull after current returned superseded journal rows: %+v", after.Entries)
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

func fixtureHistoryMutation(
	mutationID,
	deviceID,
	recordKey,
	payload string,
	physical int64,
	counter uint64,
) SyncMutationInput {
	return SyncMutationInput{
		MutationID:    mutationID,
		DeviceID:      deviceID,
		Category:      "full_history",
		RecordKey:     recordKey,
		SchemaVersion: 1,
		HLCPhysicalMS: formatInt64(physical),
		HLCCounter:    formatUint64(counter),
		Operation:     "put",
		Payload:       json.RawMessage(payload),
	}
}

func requireSyncCurrent(
	t *testing.T,
	fixture serviceFixture,
	auth AuthenticatedSession,
	category,
	recordKey string,
) (syncStoredMutation, SyncMutationView) {
	t.Helper()
	stored, found, err := fixture.service.loadCurrent(
		context.Background(), auth.Account.ID, category, recordKey)
	if err != nil {
		t.Fatalf("loadCurrent(%s/%s) error = %v", category, recordKey, err)
	}
	if !found {
		t.Fatalf("loadCurrent(%s/%s) not found", category, recordKey)
	}
	view, err := fixture.service.decodeStoredMutation(auth.Account.ID, stored)
	if err != nil {
		t.Fatalf("decodeStoredMutation(%s/%s) error = %v", category, recordKey, err)
	}
	return stored, view
}

func TestSyncHistorySemanticMergeAdvancesCanonicalSeq(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncHistorySemantic")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	now := fixture.clock.Now().UnixMilli()
	recordKey := "history/ZXBpc29kZQ/c2VtYW50aWM"

	newer := fixtureHistoryMutation(
		"61000000-0000-4000-8000-000000000001",
		auth.Device.ID, recordKey,
		`{"kind":"episode","id":"show-1/e1","firstActivityAt":2000,"lastActivityAt":5000,"title":"newer"}`,
		now+200, 0)
	olderContribution := fixtureHistoryMutation(
		"61000000-0000-4000-8000-000000000002",
		auth.Device.ID, recordKey,
		`{"kind":"episode","id":"show-1/e1","firstActivityAt":1000,"lastActivityAt":4000,"completedAt":3000,"title":"older"}`,
		now+100, 0)

	first, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{newer})
	if err != nil {
		t.Fatalf("PushSync(newer History) error = %v", err)
	}
	second, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{olderContribution})
	if err != nil {
		t.Fatalf("PushSync(older History contribution) error = %v", err)
	}
	if len(first.Results) != 1 || len(second.Results) != 1 {
		t.Fatalf("unexpected push results: %+v / %+v", first.Results, second.Results)
	}
	if !first.Results[0].Won || second.Results[0].Won {
		t.Fatalf("History won flags = %v/%v, want true/false",
			first.Results[0].Won, second.Results[0].Won)
	}
	var journalWon bool
	if err := fixture.pool.QueryRow(context.Background(),
		`SELECT won FROM account_sync_journal WHERE account_id = $1::uuid AND mutation_id = $2::uuid`,
		auth.Account.ID, olderContribution.MutationID).Scan(&journalWon); err != nil {
		t.Fatalf("load semantic contributor journal won: %v", err)
	}
	if journalWon {
		t.Fatal("semantic contributor journal won = true, want false")
	}
	if second.Results[0].ServerSeq <= first.Results[0].ServerSeq {
		t.Fatalf("semantic contributor seq = %d, want > %d",
			second.Results[0].ServerSeq, first.Results[0].ServerSeq)
	}
	current, view := requireSyncCurrent(
		t, fixture, auth, "full_history", recordKey)
	if current.ServerSeq != second.Results[0].ServerSeq {
		t.Fatalf("canonical server_seq = %d, want semantic contributor seq %d",
			current.ServerSeq, second.Results[0].ServerSeq)
	}
	if view.MutationID != newer.MutationID ||
		view.HLCPhysicalMS != newer.HLCPhysicalMS ||
		view.DeviceID != newer.DeviceID {
		t.Fatalf("canonical winner metadata = %+v, want newer mutation metadata", view)
	}
	const wantPayload = `{"completedAt":3000,"firstActivityAt":1000,"id":"show-1/e1","kind":"episode","lastActivityAt":5000,"title":"newer"}`
	if string(view.Payload) != wantPayload {
		t.Fatalf("canonical History payload = %s, want %s", view.Payload, wantPayload)
	}

	pull, err := fixture.service.PullSync(
		context.Background(), auth, first.Results[0].ServerSeq)
	if err != nil {
		t.Fatalf("PullSync(after prior canonical seq) error = %v", err)
	}
	if len(pull.Entries) != 1 ||
		pull.Entries[0].ServerSeq != second.Results[0].ServerSeq ||
		!pull.Entries[0].Canonical || !pull.Entries[0].Won {
		t.Fatalf("semantic canonical pull = %+v", pull.Entries)
	}
	if pull.Entries[0].Mutation.MutationID != newer.MutationID {
		t.Fatalf("semantic pull winner mutation = %s, want %s",
			pull.Entries[0].Mutation.MutationID, newer.MutationID)
	}
}
func TestSyncHistoryNoContributionKeepsCanonicalSeq(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncHistNoContrib")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	now := fixture.clock.Now().UnixMilli()
	recordKey := "history/ZXBpc29kZQ/bm9jb250cmlidXRpb24"

	newer := fixtureHistoryMutation(
		"62000000-0000-4000-8000-000000000001",
		auth.Device.ID, recordKey,
		`{"kind":"episode","id":"show-2/e1","firstActivityAt":1000,"lastActivityAt":5000,"title":"newer"}`,
		now+200, 0)
	stale := fixtureHistoryMutation(
		"62000000-0000-4000-8000-000000000002",
		auth.Device.ID, recordKey,
		`{"kind":"episode","id":"show-2/e1","firstActivityAt":2000,"lastActivityAt":4000,"title":"stale"}`,
		now+100, 0)

	first, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{newer})
	if err != nil {
		t.Fatalf("PushSync(newer) error = %v", err)
	}
	second, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{stale})
	if err != nil {
		t.Fatalf("PushSync(stale) error = %v", err)
	}
	if second.Results[0].Won {
		t.Fatal("stale History with no semantic contribution won HLC order")
	}
	current, view := requireSyncCurrent(
		t, fixture, auth, "full_history", recordKey)
	if current.ServerSeq != first.Results[0].ServerSeq {
		t.Fatalf("canonical server_seq advanced to %d, want unchanged %d",
			current.ServerSeq, first.Results[0].ServerSeq)
	}
	if view.MutationID != newer.MutationID {
		t.Fatalf("canonical mutation = %s, want %s", view.MutationID, newer.MutationID)
	}
	if second.Results[0].ServerSeq <= current.ServerSeq {
		t.Fatalf("stale journal seq = %d, want > canonical seq %d",
			second.Results[0].ServerSeq, current.ServerSeq)
	}
	pull, err := fixture.service.PullSync(
		context.Background(), auth, current.ServerSeq)
	if err != nil {
		t.Fatalf("PullSync(after unchanged canonical) error = %v", err)
	}
	if len(pull.Entries) != 0 {
		t.Fatalf("stale non-contribution appeared in canonical pull: %+v", pull.Entries)
	}
}

func TestSyncHistorySemanticRetryIsIdempotent(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncHistoryRetry")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	now := fixture.clock.Now().UnixMilli()
	recordKey := "history/ZXBpc29kZQ/cmV0cnk"
	newer := fixtureHistoryMutation(
		"63000000-0000-4000-8000-000000000001",
		auth.Device.ID, recordKey,
		`{"kind":"episode","id":"show-3/e1","firstActivityAt":2000,"lastActivityAt":5000}`,
		now+200, 0)
	contributor := fixtureHistoryMutation(
		"63000000-0000-4000-8000-000000000002",
		auth.Device.ID, recordKey,
		`{"kind":"episode","id":"show-3/e1","firstActivityAt":1000,"lastActivityAt":4000}`,
		now+100, 0)

	if _, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{newer}); err != nil {
		t.Fatalf("PushSync(newer) error = %v", err)
	}
	first, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{contributor})
	if err != nil {
		t.Fatalf("PushSync(contributor first) error = %v", err)
	}
	currentAfterFirst, _ := requireSyncCurrent(
		t, fixture, auth, "full_history", recordKey)
	if currentAfterFirst.ServerSeq != first.Results[0].ServerSeq {
		t.Fatalf("first semantic contribution canonical seq = %d, want %d",
			currentAfterFirst.ServerSeq, first.Results[0].ServerSeq)
	}

	retry, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{contributor})
	if err != nil {
		t.Fatalf("PushSync(contributor retry) error = %v", err)
	}
	if retry.Results[0].ServerSeq != first.Results[0].ServerSeq ||
		retry.Results[0].Won != first.Results[0].Won {
		t.Fatalf("retry result = %+v, want original %+v",
			retry.Results[0], first.Results[0])
	}
	currentAfterRetry, _ := requireSyncCurrent(
		t, fixture, auth, "full_history", recordKey)
	if currentAfterRetry.ServerSeq != currentAfterFirst.ServerSeq {
		t.Fatalf("semantic retry advanced canonical seq %d -> %d",
			currentAfterFirst.ServerSeq, currentAfterRetry.ServerSeq)
	}

	var count int
	if err := fixture.pool.QueryRow(context.Background(),
		`SELECT count(*) FROM account_sync_journal
         WHERE account_id = $1::uuid AND mutation_id = $2::uuid`,
		auth.Account.ID, contributor.MutationID).Scan(&count); err != nil {
		t.Fatalf("count semantic retry journal: %v", err)
	}
	if count != 1 {
		t.Fatalf("semantic retry journal rows = %d, want 1", count)
	}
}

func TestSyncCanonicalTombstoneAndResurrection(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncCanonicalTombstone")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	now := fixture.clock.Now().UnixMilli()
	recordKey := "history/ZXBpc29kZQ/dG9tYnN0b25l"
	put := fixtureHistoryMutation(
		"64000000-0000-4000-8000-000000000001",
		auth.Device.ID, recordKey,
		`{"kind":"episode","id":"show-4/e1","firstActivityAt":1000,"lastActivityAt":2000}`,
		now+100, 0)
	deleteMutation := fixtureSyncMutation(
		"64000000-0000-4000-8000-000000000002",
		auth.Device.ID, recordKey, "", now+200, 0)
	deleteMutation.Category = "full_history"
	stalePut := fixtureHistoryMutation(
		"64000000-0000-4000-8000-000000000003",
		auth.Device.ID, recordKey,
		`{"kind":"episode","id":"show-4/e1","firstActivityAt":500,"lastActivityAt":2500}`,
		now+150, 0)
	resurrect := fixtureHistoryMutation(
		"64000000-0000-4000-8000-000000000004",
		auth.Device.ID, recordKey,
		`{"kind":"episode","id":"show-4/e1","firstActivityAt":700,"lastActivityAt":3000}`,
		now+300, 0)

	if _, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{put}); err != nil {
		t.Fatalf("PushSync(initial put) error = %v", err)
	}
	deleted, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{deleteMutation})
	if err != nil {
		t.Fatalf("PushSync(delete) error = %v", err)
	}
	currentDelete, viewDelete := requireSyncCurrent(
		t, fixture, auth, "full_history", recordKey)
	if !deleted.Results[0].Won || viewDelete.Operation != "delete" || len(viewDelete.Payload) != 0 {
		t.Fatalf("canonical delete = result:%+v view:%+v", deleted.Results[0], viewDelete)
	}

	stale, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{stalePut})
	if err != nil {
		t.Fatalf("PushSync(stale put) error = %v", err)
	}
	if stale.Results[0].Won {
		t.Fatal("stale put won against newer tombstone")
	}
	currentAfterStale, viewAfterStale := requireSyncCurrent(
		t, fixture, auth, "full_history", recordKey)
	if currentAfterStale.ServerSeq != currentDelete.ServerSeq || viewAfterStale.Operation != "delete" {
		t.Fatalf("stale put changed tombstone: stored:%+v view:%+v",
			currentAfterStale, viewAfterStale)
	}
	pullAfterDelete, err := fixture.service.PullSync(
		context.Background(), auth, currentDelete.ServerSeq)
	if err != nil {
		t.Fatalf("PullSync(after delete) error = %v", err)
	}
	if len(pullAfterDelete.Entries) != 0 {
		t.Fatalf("stale put appeared after tombstone cursor: %+v", pullAfterDelete.Entries)
	}

	later, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{resurrect})
	if err != nil {
		t.Fatalf("PushSync(later put) error = %v", err)
	}
	if !later.Results[0].Won {
		t.Fatal("later put failed to win after tombstone")
	}
	currentResurrected, viewResurrected := requireSyncCurrent(
		t, fixture, auth, "full_history", recordKey)
	if currentResurrected.ServerSeq != later.Results[0].ServerSeq ||
		viewResurrected.Operation != "put" ||
		viewResurrected.MutationID != resurrect.MutationID {
		t.Fatalf("resurrected canonical row = stored:%+v view:%+v",
			currentResurrected, viewResurrected)
	}
	pull, err := fixture.service.PullSync(
		context.Background(), auth, currentDelete.ServerSeq)
	if err != nil {
		t.Fatalf("PullSync(after tombstone, post resurrection) error = %v", err)
	}
	if len(pull.Entries) != 1 || !pull.Entries[0].Canonical ||
		pull.Entries[0].Mutation.Operation != "put" {
		t.Fatalf("resurrection canonical pull = %+v", pull.Entries)
	}
}

func TestSyncCanonicalPullPaginatesCurrentRowsAscending(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncCanonicalPagination")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	now := fixture.clock.Now().UnixMilli()
	mutations := make([]SyncMutationInput, 0, syncPullPageSize+1)
	for index := 0; index < syncPullPageSize+1; index++ {
		mutationID := fmt.Sprintf(
			"65000000-0000-4000-8000-%012x", index+1)
		mutations = append(mutations, fixtureSyncMutation(
			mutationID,
			auth.Device.ID,
			fmt.Sprintf("pagination/item-%03d", index),
			fmt.Sprintf("value-%03d", index),
			now+int64(index),
			0))
	}
	for start := 0; start < len(mutations); start += 100 {
		end := start + 100
		if end > len(mutations) {
			end = len(mutations)
		}
		if _, err := fixture.service.PushSync(
			context.Background(), auth, mutations[start:end]); err != nil {
			t.Fatalf("PushSync(page fixtures %d:%d) error = %v", start, end, err)
		}
	}

	loser := fixtureSyncMutation(
		"65000000-0000-4000-8000-000000000fff",
		auth.Device.ID,
		"pagination/item-000",
		"stale-loser",
		now-1,
		0)
	if _, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{loser}); err != nil {
		t.Fatalf("PushSync(pagination loser) error = %v", err)
	}

	first, err := fixture.service.PullSync(context.Background(), auth, 0)
	if err != nil {
		t.Fatalf("PullSync(first page) error = %v", err)
	}
	if len(first.Entries) != syncPullPageSize || !first.HasMore {
		t.Fatalf("first page = %d entries hasMore=%v, want %d/true",
			len(first.Entries), first.HasMore, syncPullPageSize)
	}
	for index, entry := range first.Entries {
		if !entry.Canonical || !entry.Won {
			t.Fatalf("first page entry %d flags = canonical:%v won:%v",
				index, entry.Canonical, entry.Won)
		}
		if index > 0 && first.Entries[index-1].ServerSeq >= entry.ServerSeq {
			t.Fatalf("canonical first page not ascending at %d: %d >= %d",
				index, first.Entries[index-1].ServerSeq, entry.ServerSeq)
		}
		if entry.Mutation.MutationID == loser.MutationID {
			t.Fatal("superseded loser appeared in canonical first page")
		}
	}

	second, err := fixture.service.PullSync(
		context.Background(), auth, first.Entries[len(first.Entries)-1].ServerSeq)
	if err != nil {
		t.Fatalf("PullSync(second page) error = %v", err)
	}
	if len(second.Entries) != 1 || second.HasMore {
		t.Fatalf("second page = %d entries hasMore=%v, want 1/false",
			len(second.Entries), second.HasMore)
	}
	if !second.Entries[0].Canonical || !second.Entries[0].Won ||
		second.Entries[0].Mutation.MutationID == loser.MutationID {
		t.Fatalf("second canonical page entry = %+v", second.Entries[0])
	}
	if second.Entries[0].ServerSeq <= first.Entries[len(first.Entries)-1].ServerSeq {
		t.Fatal("second canonical page did not continue ascending server_seq")
	}
}
