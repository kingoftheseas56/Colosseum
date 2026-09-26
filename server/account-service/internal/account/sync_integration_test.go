package account

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"strconv"
	"strings"
	"testing"
	"time"
)

func fixtureCollectionRecordKey(identity string) string {
	parts := strings.SplitN(identity, "/", 2)
	if len(parts) != 2 || parts[0] == "" || parts[1] == "" {
		panic("fixture collection identity must be world/id")
	}
	return "collection/" +
		base64.RawURLEncoding.EncodeToString([]byte(parts[0])) + "/" +
		base64.RawURLEncoding.EncodeToString([]byte(parts[1]))
}

func fixtureHistoryRecordKey(kind, id string) string {
	if kind == "" || id == "" {
		panic("fixture history identity must contain kind and id")
	}
	return "history/" +
		base64.RawURLEncoding.EncodeToString([]byte(kind)) + "/" +
		base64.RawURLEncoding.EncodeToString([]byte(id))
}

func fixtureSyncMutation(
	mutationID,
	deviceID,
	recordKey,
	value string,
	physical int64,
	counter uint64,
) SyncMutationInput {
	parts := strings.SplitN(recordKey, "/", 2)
	if len(parts) != 2 || parts[0] == "" || parts[1] == "" {
		panic("fixture collection identity must be world/id")
	}
	payload := json.RawMessage(nil)
	operation := "delete"
	if value != "" {
		operation = "put"
		payload = json.RawMessage(`{"world":"` + parts[0] +
			`","id":"` + parts[1] + `","value":"` + value + `"}`)
	}
	return SyncMutationInput{
		MutationID:    mutationID,
		DeviceID:      deviceID,
		Category:      "collection",
		RecordKey:     fixtureCollectionRecordKey(recordKey),
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

func fixtureRatingsReviewsMutation(
	mutationID, deviceID, operation string,
	physical, deletedAtMS int64,
) SyncMutationInput {
	input := SyncMutationInput{
		MutationID:    mutationID,
		DeviceID:      deviceID,
		Category:      "ratings_reviews",
		RecordKey:     ratingsReviewsTestKey("theatre", "series", "fixture-series"),
		SchemaVersion: 1,
		HLCPhysicalMS: formatInt64(physical),
		HLCCounter:    "0",
		Operation:     operation,
	}
	if operation == "put" {
		input.Payload = json.RawMessage(`{"world":"theatre","kind":"series","media_id":"fixture-series","rating":8.5,"review":"C:\\Notes\\review.txt","spoiler":false,"created_at_ms":1000,"updated_at_ms":2000}`)
	} else {
		input.DeletedAtMS = formatInt64(deletedAtMS)
	}
	return input
}

func TestRatingsReviewsDeleteTimestampSurvivesJournalCurrentPullAndSnapshot(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "RatingsReviewsDeleteTime")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	now := fixture.clock.Now().UnixMilli()
	put := fixtureRatingsReviewsMutation(
		"99100000-0000-4000-8000-000000000001", auth.Device.ID, "put", now, 0)
	if result, err := fixture.service.PushSync(context.Background(), auth, []SyncMutationInput{put}); err != nil ||
		len(result.Results) != 1 || !result.Results[0].Accepted || !result.Results[0].Won {
		t.Fatalf("ratings_reviews PUT = %+v, err=%v", result, err)
	}
	const deletedAt = int64(1699999999123)
	deleteMutation := fixtureRatingsReviewsMutation(
		"99100000-0000-4000-8000-000000000002", auth.Device.ID, "delete", now+1, deletedAt)
	result, err := fixture.service.PushSync(context.Background(), auth, []SyncMutationInput{deleteMutation})
	if err != nil || len(result.Results) != 1 || !result.Results[0].Accepted || !result.Results[0].Won {
		t.Fatalf("ratings_reviews DELETE = %+v, err=%v", result, err)
	}

	var journalDeleted, currentDeleted int64
	if err := fixture.pool.QueryRow(context.Background(),
		`SELECT deleted_at_ms FROM account_sync_journal WHERE account_id=$1::uuid AND mutation_id=$2::uuid`,
		auth.Account.ID, deleteMutation.MutationID).Scan(&journalDeleted); err != nil {
		t.Fatalf("load RR journal delete time: %v", err)
	}
	if err := fixture.pool.QueryRow(context.Background(),
		`SELECT deleted_at_ms FROM account_sync_current WHERE account_id=$1::uuid AND category='ratings_reviews' AND record_key=$2`,
		auth.Account.ID, deleteMutation.RecordKey).Scan(&currentDeleted); err != nil {
		t.Fatalf("load RR current delete time: %v", err)
	}
	if journalDeleted != deletedAt || currentDeleted != deletedAt {
		t.Fatalf("stored delete times journal=%d current=%d want=%d", journalDeleted, currentDeleted, deletedAt)
	}

	pull, err := fixture.service.PullSync(context.Background(), auth, 0)
	if err != nil {
		t.Fatalf("PullSync(RR) error = %v", err)
	}
	seenPull := false
	for _, entry := range pull.Entries {
		if entry.Mutation.MutationID == deleteMutation.MutationID {
			seenPull = true
			if entry.Mutation.DeletedAtMS != formatInt64(deletedAt) {
				t.Fatalf("pull deleted_at_ms = %q", entry.Mutation.DeletedAtMS)
			}
		}
	}
	if !seenPull {
		t.Fatal("RR delete missing from pull")
	}

	snapshot, err := fixture.service.SnapshotSync(context.Background(), auth, "")
	if err != nil {
		t.Fatalf("SnapshotSync(RR) error = %v", err)
	}
	seenSnapshot := false
	for _, entry := range snapshot.Entries {
		if entry.Mutation.Category == "ratings_reviews" && entry.Mutation.RecordKey == deleteMutation.RecordKey {
			seenSnapshot = true
			if entry.Mutation.Operation != "delete" || entry.Mutation.DeletedAtMS != formatInt64(deletedAt) {
				t.Fatalf("snapshot RR delete = %+v", entry.Mutation)
			}
		}
	}
	if !seenSnapshot {
		t.Fatal("RR delete missing from snapshot")
	}

	recreate := fixtureRatingsReviewsMutation(
		"99100000-0000-4000-8000-000000000003", auth.Device.ID, "put", now+2, 0)
	result, err = fixture.service.PushSync(context.Background(), auth, []SyncMutationInput{recreate})
	if err != nil || len(result.Results) != 1 || !result.Results[0].Accepted || !result.Results[0].Won {
		t.Fatalf("RR recreate = %+v err=%v", result, err)
	}
	var archivedDeleted int64
	if err := fixture.pool.QueryRow(context.Background(),
		`SELECT deleted_at_ms FROM account_sync_versions
		 WHERE account_id=$1::uuid AND category='ratings_reviews' AND record_key=$2
		   AND operation='delete' ORDER BY replaced_at DESC LIMIT 1`,
		auth.Account.ID, deleteMutation.RecordKey).Scan(&archivedDeleted); err != nil {
		t.Fatalf("load archived RR delete time: %v", err)
	}
	if archivedDeleted != deletedAt {
		t.Fatalf("archived deleted_at_ms = %d, want %d", archivedDeleted, deletedAt)
	}
	var currentDeletedIsNull bool
	if err := fixture.pool.QueryRow(context.Background(),
		`SELECT deleted_at_ms IS NULL FROM account_sync_current
		 WHERE account_id=$1::uuid AND category='ratings_reviews' AND record_key=$2`,
		auth.Account.ID, deleteMutation.RecordKey).Scan(&currentDeletedIsNull); err != nil {
		t.Fatalf("load recreated RR current row: %v", err)
	}
	if !currentDeletedIsNull {
		t.Fatal("RR PUT current row retained deleted_at_ms")
	}
}

func TestRatingsReviewsPutDeleteLWWKeepsWinningDeleteTimestamp(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "RR_LWW_DeleteTime")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	now := fixture.clock.Now().UnixMilli()

	put := fixtureRatingsReviewsMutation(
		"99110000-0000-4000-8000-000000000001", auth.Device.ID, "put", now, 0)
	first, err := fixture.service.PushSync(context.Background(), auth, []SyncMutationInput{put})
	if err != nil || len(first.Results) != 1 || !first.Results[0].Accepted || !first.Results[0].Won {
		t.Fatalf("initial RR PUT = %+v err=%v", first, err)
	}

	const deletedAt = int64(246813579)
	winnerDelete := fixtureRatingsReviewsMutation(
		"99110000-0000-4000-8000-000000000002", auth.Device.ID, "delete", now+20, deletedAt)
	second, err := fixture.service.PushSync(context.Background(), auth, []SyncMutationInput{winnerDelete})
	if err != nil || len(second.Results) != 1 || !second.Results[0].Accepted || !second.Results[0].Won {
		t.Fatalf("winning RR DELETE = %+v err=%v", second, err)
	}

	stalePut := fixtureRatingsReviewsMutation(
		"99110000-0000-4000-8000-000000000003", auth.Device.ID, "put", now+10, 0)
	third, err := fixture.service.PushSync(context.Background(), auth, []SyncMutationInput{stalePut})
	if err != nil || len(third.Results) != 1 || !third.Results[0].Accepted || third.Results[0].Won {
		t.Fatalf("stale RR PUT = %+v err=%v", third, err)
	}

	var operation string
	var storedDeleted int64
	if err := fixture.pool.QueryRow(context.Background(),
		`SELECT operation, deleted_at_ms FROM account_sync_current
		 WHERE account_id=$1::uuid AND category='ratings_reviews' AND record_key=$2`,
		auth.Account.ID, winnerDelete.RecordKey).Scan(&operation, &storedDeleted); err != nil {
		t.Fatalf("load RR LWW current: %v", err)
	}
	if operation != "delete" || storedDeleted != deletedAt {
		t.Fatalf("RR LWW current operation=%q deleted_at_ms=%d want delete/%d",
			operation, storedDeleted, deletedAt)
	}
}

func TestRatingsReviewsMutationIDBindsDeleteTimestamp(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "RR_MutationIdentity")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	now := fixture.clock.Now().UnixMilli()
	mutation := fixtureRatingsReviewsMutation(
		"99200000-0000-4000-8000-000000000001", auth.Device.ID, "delete", now, 1001)
	first, err := fixture.service.PushSync(context.Background(), auth, []SyncMutationInput{mutation})
	if err != nil || len(first.Results) != 1 || !first.Results[0].Accepted {
		t.Fatalf("first RR delete = %+v err=%v", first, err)
	}
	changed := mutation
	changed.DeletedAtMS = "1002"
	second, err := fixture.service.PushSync(context.Background(), auth, []SyncMutationInput{changed})
	if err != nil || len(second.Results) != 1 || second.Results[0].Accepted ||
		second.Results[0].Code != "mutation_id_conflict" {
		t.Fatalf("changed timestamp retry = %+v err=%v", second, err)
	}
}

func TestSyncFullHistoryCategoryRoundTrips(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SyncHistory")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)

	mutation := fixtureSyncMutation(
		"89012345-8901-4890-8890-89012345abcd",
		auth.Device.ID,
		"history/fixture",
		"",
		fixture.clock.Now().UnixMilli(),
		0)
	mutation.Category = "full_history"
	mutation.RecordKey = fixtureHistoryRecordKey("episode", "show-1/e1")
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
	wantPayload, err := canonicalSyncJSON(mutation.Payload)
	if err != nil {
		t.Fatalf("canonicalize expected History payload: %v", err)
	}
	if string(pull.Entries[0].Mutation.Payload) != string(wantPayload) {
		t.Fatalf(
			"history pull payload = %s, want %s",
			pull.Entries[0].Mutation.Payload,
			wantPayload)
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
           AND record_key = 'collection/bWFuZ2E/aXRlbQ'`,
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
           AND record_key = 'collection/bWFuZ2E/aXRlbQ'`,
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
           AND record_key = 'collection/bWFuZ2E/aXRlbQ'`,
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
