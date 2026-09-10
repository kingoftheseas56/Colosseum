package account

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"os"
	"testing"
	"time"

	"github.com/jackc/pgx/v5/pgxpool"
)

type syncPushOutcome struct {
	response SyncPushResponse
	err      error
}

func pushOneSyncMutation(
	t *testing.T,
	fixture serviceFixture,
	auth AuthenticatedSession,
	mutation SyncMutationInput,
) SyncPushResult {
	t.Helper()
	response, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{mutation})
	if err != nil {
		t.Fatalf("PushSync() error = %v", err)
	}
	if len(response.Results) != 1 {
		t.Fatalf("PushSync() results = %d, want 1", len(response.Results))
	}
	return response.Results[0]
}

func TestSyncMutationIDBindsCanonicalIdentity(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "SyncMutationIdentity")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	now := fixture.clock.Now().UnixMilli()

	const mutationID = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
	first := fixtureSyncMutation(
		mutationID,
		auth.Device.ID,
		"collection/item",
		"one",
		now,
		0)
	first.Payload = json.RawMessage(`{"world":"collection","id":"item","value":9007199254740993,"nested":{"b":2,"a":1}}`)
	firstResult := pushOneSyncMutation(t, fixture, auth, first)
	if !firstResult.Accepted {
		t.Fatalf("first result = %+v, want accepted", firstResult)
	}

	// Whitespace and object key order are transport details. The original
	// payload remains the stored representation while its canonical identity
	// is used for retry comparison.
	exact := first
	exact.Payload = json.RawMessage(` { "nested": { "a": 1, "b": 2 }, "id":"item", "value": 9007199254740993, "world":"collection" } `)
	exactResult := pushOneSyncMutation(t, fixture, auth, exact)
	if !exactResult.Accepted || exactResult.ServerSeq != firstResult.ServerSeq {
		t.Fatalf("canonical retry = %+v, want original seq %d", exactResult, firstResult.ServerSeq)
	}

	cases := []struct {
		name   string
		mutate func(SyncMutationInput) SyncMutationInput
	}{
		{
			name: "numeric payload precision",
			mutate: func(m SyncMutationInput) SyncMutationInput {
				m.Payload = json.RawMessage(`{"world":"collection","id":"item","value":9007199254740992,"nested":{"a":1,"b":2}}`)
				return m
			},
		},
		{
			name: "record key",
			mutate: func(m SyncMutationInput) SyncMutationInput {
				m.RecordKey = fixtureCollectionRecordKey("collection/other-item")
				m.Payload = json.RawMessage(`{"world":"collection","id":"other-item","value":9007199254740993,"nested":{"a":1,"b":2}}`)
				return m
			},
		},
		{
			name: "operation",
			mutate: func(m SyncMutationInput) SyncMutationInput {
				m.Operation = "delete"
				m.Payload = nil
				return m
			},
		},
		{
			name: "HLC",
			mutate: func(m SyncMutationInput) SyncMutationInput {
				m.HLCCounter = "1"
				return m
			},
		},
	}
	for _, testCase := range cases {
		t.Run(testCase.name, func(t *testing.T) {
			result := pushOneSyncMutation(t, fixture, auth, testCase.mutate(first))
			if result.Accepted || result.Code != "mutation_id_conflict" {
				t.Fatalf("result = %+v, want mutation_id_conflict", result)
			}
		})
	}

	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_sync_journal WHERE account_id = $1::uuid",
		auth.Account.ID); got != 1 {
		t.Fatalf("journal rows = %d, want 1", got)
	}
}

func TestActivitySemanticAliasBindsMutationID(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivityAliasIdentity")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	const eventID = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
	const aliasMutationID = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"
	payload := activityPlaybackPayload(eventID)

	first := pushOneActivity(t, fixture, auth, activityMutation(
		"cccccccc-cccc-4ccc-8ccc-cccccccccccc",
		auth.Device.ID,
		eventID,
		payload))
	if !first.Accepted {
		t.Fatalf("first activity result = %+v", first)
	}

	changedOriginal := activityPlaybackPayload(eventID)
	changedOriginal = bytes.Replace(
		changedOriginal,
		[]byte(`"endAtMs":6000`),
		[]byte(`"endAtMs":7000`),
		1)
	changedOriginal = bytes.Replace(
		changedOriginal,
		[]byte(`"activeMs":5000`),
		[]byte(`"activeMs":6000`),
		1)
	originalResult := pushOneActivity(t, fixture, auth, activityMutation(
		"cccccccc-cccc-4ccc-8ccc-cccccccccccc",
		auth.Device.ID,
		eventID,
		changedOriginal))
	if originalResult.Accepted || originalResult.Code != "mutation_id_conflict" {
		t.Fatalf("changed original activity mutation = %+v, want mutation_id_conflict", originalResult)
	}

	aliasMutation := activityMutation(
		aliasMutationID,
		auth.Device.ID,
		eventID,
		payload)
	alias := pushOneActivity(t, fixture, auth, aliasMutation)
	if !alias.Accepted || alias.ServerSeq != first.ServerSeq {
		t.Fatalf("semantic alias = %+v, want original seq %d", alias, first.ServerSeq)
	}

	retry := pushOneActivity(t, fixture, auth, aliasMutation)
	if !retry.Accepted || retry.ServerSeq != first.ServerSeq {
		t.Fatalf("alias retry = %+v, want original seq %d", retry, first.ServerSeq)
	}

	changedContent := aliasMutation
	changedContent.Payload = bytes.Replace(
		changedContent.Payload,
		[]byte(`"endAtMs":6000`),
		[]byte(`"endAtMs":7000`),
		1)
	changedContent.Payload = bytes.Replace(
		changedContent.Payload,
		[]byte(`"activeMs":5000`),
		[]byte(`"activeMs":6000`),
		1)
	contentResult := pushOneActivity(t, fixture, auth, changedContent)
	if contentResult.Accepted || contentResult.Code != "mutation_id_conflict" {
		t.Fatalf("changed alias content = %+v, want mutation_id_conflict", contentResult)
	}

	changedEvent := aliasMutation
	changedEvent.RecordKey = "activity/bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"
	changedEvent.Payload = activityPlaybackPayload(changedEvent.RecordKey[len(activityRecordKeyPrefix):])
	eventResult := pushOneActivity(t, fixture, auth, changedEvent)
	if eventResult.Accepted || eventResult.Code != "mutation_id_conflict" {
		t.Fatalf("changed alias event = %+v, want mutation_id_conflict", eventResult)
	}

	changedHLC := aliasMutation
	changedHLC.HLCCounter = "1"
	hlcResult := pushOneActivity(t, fixture, auth, changedHLC)
	if hlcResult.Accepted || hlcResult.Code != "mutation_id_conflict" {
		t.Fatalf("changed alias HLC = %+v, want mutation_id_conflict", hlcResult)
	}

	secondSession, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "ActivityAliasIdentity",
			Password:        testPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Laptop",
			Platform:        "Windows",
			SourceKey:       "203.0.113.11",
		})
	if err != nil || secondSession.Status != "signed_in" || secondSession.Session == nil {
		t.Fatalf("sign in second alias device = %+v, err=%v", secondSession, err)
	}
	authB := authenticateFixtureSession(t, fixture, *secondSession.Session)
	changedDevice := aliasMutation
	changedDevice.DeviceID = authB.Device.ID
	deviceResult := pushOneActivity(t, fixture, authB, changedDevice)
	if deviceResult.Accepted || deviceResult.Code != "mutation_id_conflict" {
		t.Fatalf("changed alias device = %+v, want mutation_id_conflict", deviceResult)
	}

	mutableReuse := fixtureSyncMutation(
		aliasMutationID,
		auth.Device.ID,
		"collection/reused-alias",
		"value",
		fixture.clock.Now().UnixMilli(),
		0)
	mutableResult := pushOneSyncMutation(t, fixture, auth, mutableReuse)
	if mutableResult.Accepted || mutableResult.Code != "mutation_id_conflict" {
		t.Fatalf("mutable alias reuse = %+v, want mutation_id_conflict", mutableResult)
	}

	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_sync_mutation_aliases WHERE account_id = $1::uuid",
		auth.Account.ID); got != 1 {
		t.Fatalf("activity alias rows = %d, want 1", got)
	}
}

func TestSyncMutationNamespaceIsSharedAcrossDomains(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "SyncSharedNamespace")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	now := fixture.clock.Now().UnixMilli()

	mutableID := "dddddddd-dddd-4ddd-8ddd-dddddddddddd"
	mutable := fixtureSyncMutation(
		mutableID, auth.Device.ID, "collection/mutable", "one", now, 0)
	if result := pushOneSyncMutation(t, fixture, auth, mutable); !result.Accepted {
		t.Fatalf("mutable seed = %+v", result)
	}

	activity := activityMutation(
		mutableID,
		auth.Device.ID,
		"eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee",
		activityPlaybackPayload("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"))
	if result := pushOneActivity(t, fixture, auth, activity); result.Accepted || result.Code != "mutation_id_conflict" {
		t.Fatalf("activity reuse of mutable ID = %+v, want mutation_id_conflict", result)
	}

	activityID := "ffffffff-ffff-4fff-8fff-ffffffffffff"
	activity = activityMutation(
		activityID,
		auth.Device.ID,
		"eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee",
		activityPlaybackPayload("eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"))
	if result := pushOneActivity(t, fixture, auth, activity); !result.Accepted {
		t.Fatalf("activity seed = %+v", result)
	}

	mutable.MutationID = activityID
	mutable.RecordKey = fixtureCollectionRecordKey("collection/activity-id-reuse")
	mutable.Payload = json.RawMessage(`{"world":"collection","id":"activity-id-reuse","value":"one"}`)
	if result := pushOneSyncMutation(t, fixture, auth, mutable); result.Accepted || result.Code != "mutation_id_conflict" {
		t.Fatalf("mutable reuse of activity ID = %+v, want mutation_id_conflict", result)
	}
}

func TestSyncAccountLockKeepsMixedFeedCommitOrder(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "SyncMixedCommitOrder")
	auth := authenticateFixtureSession(t, fixture, created.Session)
	now := fixture.clock.Now().UnixMilli()

	mutable := fixtureSyncMutation(
		"11111111-1111-4111-8111-111111111112",
		auth.Device.ID,
		"collection/audit-low",
		"mutable",
		now,
		0)
	const eventID = "22222222-2222-4222-8222-222222222222"
	activity := activityMutation(
		"33333333-3333-4333-8333-333333333334",
		auth.Device.ID,
		eventID,
		activityPlaybackPayload(eventID))

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	lockTx, err := fixture.pool.Begin(ctx)
	if err != nil {
		t.Fatalf("begin low record lock: %v", err)
	}
	defer func() { _ = lockTx.Rollback(context.Background()) }()
	lowRecordLockKey := auth.Account.ID + "\x1f" + mutable.Category + "\x1f" + mutable.RecordKey
	if _, err := lockTx.Exec(ctx, `
        SELECT pg_advisory_xact_lock(hashtextextended($1, 0))
    `, lowRecordLockKey); err != nil {
		t.Fatalf("lock low record: %v", err)
	}

	lowDone := make(chan syncPushOutcome, 1)
	go func() {
		response, err := fixture.service.PushSync(ctx, auth, []SyncMutationInput{mutable})
		lowDone <- syncPushOutcome{response: response, err: err}
	}()
	waitForAdvisoryWait(t, ctx, fixture)

	highDone := make(chan syncPushOutcome, 1)
	go func() {
		response, err := fixture.service.PushSync(ctx, auth, []SyncMutationInput{activity})
		highDone <- syncPushOutcome{response: response, err: err}
	}()
	waitForAdvisoryWaitAtLeast(t, ctx, fixture, 2, highDone)

	pull, err := fixture.service.PullSync(ctx, auth, 0)
	if err != nil {
		t.Fatalf("PullSync while low writer is blocked: %v", err)
	}
	if len(pull.Entries) != 0 {
		t.Fatalf("pull advanced while low writer was blocked = %+v", pull.Entries)
	}

	if err := lockTx.Commit(ctx); err != nil {
		t.Fatalf("release low record lock: %v", err)
	}
	lowOutcome := <-lowDone
	highOutcome := <-highDone
	for name, outcome := range map[string]syncPushOutcome{
		"low":  lowOutcome,
		"high": highOutcome,
	} {
		if outcome.err != nil || len(outcome.response.Results) != 1 || !outcome.response.Results[0].Accepted {
			t.Fatalf("%s mixed push = %+v, err=%v", name, outcome.response, outcome.err)
		}
	}

	pull, err = fixture.service.PullSync(ctx, auth, 0)
	if err != nil {
		t.Fatalf("PullSync() error = %v", err)
	}
	if len(pull.Entries) != 2 || pull.Entries[0].ServerSeq >= pull.Entries[1].ServerSeq {
		t.Fatalf("mixed feed entries = %+v, want two ascending entries", pull.Entries)
	}
	after, err := fixture.service.PullSync(ctx, auth, pull.Entries[0].ServerSeq)
	if err != nil {
		t.Fatalf("PullSync(after) error = %v", err)
	}
	if len(after.Entries) != 1 || after.Entries[0].ServerSeq != pull.Entries[1].ServerSeq {
		t.Fatalf("mixed feed after first = %+v, want second entry", after.Entries)
	}
}

func waitForAdvisoryWaitAtLeast(
	t *testing.T,
	ctx context.Context,
	fixture serviceFixture,
	want int,
	unexpected <-chan syncPushOutcome,
) {
	t.Helper()
	for {
		var waiting int
		err := fixture.pool.QueryRow(ctx, `
            SELECT count(*)
            FROM pg_stat_activity
            WHERE pid <> pg_backend_pid()
              AND datname = current_database()
              AND wait_event_type = 'Lock'
              AND wait_event = 'advisory'
        `).Scan(&waiting)
		if err != nil {
			t.Fatalf("inspect advisory waits: %v", err)
		}
		if waiting >= want {
			return
		}
		select {
		case outcome := <-unexpected:
			t.Fatalf("high mixed writer completed while low writer was blocked: %+v, err=%v",
				outcome.response, outcome.err)
		case <-ctx.Done():
			t.Fatalf("timed out waiting for %d advisory lock waiters: %v", want, ctx.Err())
		case <-time.After(10 * time.Millisecond):
		}
	}
}

func TestSyncAccountLocksDoNotSerializeUnrelatedAccounts(t *testing.T) {
	fixture := newServiceFixture(t)
	accountA := createFixtureAccount(t, fixture, "SyncIndependentA")
	accountB := createFixtureAccount(t, fixture, "SyncIndependentB")
	authA := authenticateFixtureSession(t, fixture, accountA.Session)
	authB := authenticateFixtureSession(t, fixture, accountB.Session)

	ctx, cancel := context.WithTimeout(context.Background(), 10*time.Second)
	defer cancel()
	lockTx, err := fixture.pool.Begin(ctx)
	if err != nil {
		t.Fatalf("begin account lock: %v", err)
	}
	defer func() { _ = lockTx.Rollback(context.Background()) }()
	if err := lockAccountSyncTx(ctx, lockTx, authA.Account.ID); err != nil {
		t.Fatalf("lock account A: %v", err)
	}

	waiting := make(chan error, 1)
	go func() {
		_, pushErr := fixture.service.PushSync(ctx, authA, []SyncMutationInput{
			fixtureSyncMutation(
				"44444444-4444-4444-8444-444444444444",
				authA.Device.ID,
				"collection/blocked",
				"A",
				fixture.clock.Now().UnixMilli(),
				0),
		})
		waiting <- pushErr
	}()
	waitForAdvisoryWait(t, ctx, fixture)

	result, err := fixture.service.PushSync(ctx, authB, []SyncMutationInput{
		fixtureSyncMutation(
			"55555555-5555-4555-8555-555555555555",
			authB.Device.ID,
			"collection/independent",
			"B",
			fixture.clock.Now().UnixMilli(),
			0),
	})
	if err != nil || len(result.Results) != 1 || !result.Results[0].Accepted {
		t.Fatalf("unrelated account push = %+v, err=%v", result, err)
	}

	if err := lockTx.Commit(ctx); err != nil {
		t.Fatalf("release account A lock: %v", err)
	}
	if err := <-waiting; err != nil {
		t.Fatalf("blocked account push: %v", err)
	}
}

func waitForAdvisoryWait(
	t *testing.T,
	ctx context.Context,
	fixture serviceFixture,
) {
	t.Helper()
	for {
		var waiting int
		err := fixture.pool.QueryRow(ctx, `
            SELECT count(*)
            FROM pg_stat_activity
            WHERE pid <> pg_backend_pid()
              AND datname = current_database()
              AND wait_event_type = 'Lock'
              AND wait_event = 'advisory'
        `).Scan(&waiting)
		if err != nil {
			t.Fatalf("inspect advisory waits: %v", err)
		}
		if waiting > 0 {
			return
		}
		select {
		case <-ctx.Done():
			t.Fatalf("timed out waiting for advisory lock contention: %v", ctx.Err())
		case <-time.After(10 * time.Millisecond):
		}
	}
}

func TestCreateAccountMaxConnsOneAndQuotaRollback(t *testing.T) {
	fixture := newServiceFixture(t)

	cfg, err := pgxpool.ParseConfig(os.Getenv("TEST_DATABASE_URL"))
	if err != nil {
		t.Fatalf("parse TEST_DATABASE_URL: %v", err)
	}
	cfg.MaxConns = 1
	pool, err := pgxpool.NewWithConfig(context.Background(), cfg)
	if err != nil {
		t.Fatalf("open MaxConns=1 pool: %v", err)
	}
	defer pool.Close()
	ctx, cancel := context.WithTimeout(context.Background(), 8*time.Second)
	defer cancel()
	if err := pool.Ping(ctx); err != nil {
		t.Fatalf("ping MaxConns=1 pool: %v", err)
	}

	limiter, err := NewRateLimiter(pool, bytes.Repeat([]byte{0x73}, 32), fixture.clock)
	if err != nil {
		t.Fatalf("new MaxConns=1 limiter: %v", err)
	}
	fixture.service.pool = pool
	fixture.service.rateLimiter = limiter
	fixture.service.registrationGlobalLimit = 1

	first, err := fixture.service.CreateAccount(ctx, CreateAccountInput{
		Username:        "MaxConnsOneFirst",
		Password:        testPassword,
		DeviceInstallID: deviceAInstall,
		DeviceLabel:     "Desktop",
		Platform:        "Windows",
		SourceKey:       "203.0.113.90",
	})
	if err != nil || first.Session.AccessToken == "" {
		t.Fatalf("first MaxConns=1 registration = %+v, err=%v", first, err)
	}

	_, err = fixture.service.CreateAccount(ctx, CreateAccountInput{
		Username:        "MaxConnsOneSecond",
		Password:        testPassword,
		DeviceInstallID: deviceBInstall,
		DeviceLabel:     "Laptop",
		Platform:        "Windows",
		SourceKey:       "203.0.113.90",
	})
	var rateErr *RateLimitError
	if !errors.As(err, &rateErr) {
		t.Fatalf("second registration error = %v, want rate limit", err)
	}

	var sourceCount, globalCount, accountCount int64
	if err := pool.QueryRow(ctx,
		"SELECT count(*) FROM auth_rate_events WHERE event_type = 'create_success_source'").Scan(&sourceCount); err != nil {
		t.Fatalf("count source quota events: %v", err)
	}
	if err := pool.QueryRow(ctx,
		"SELECT count(*) FROM auth_rate_events WHERE event_type = 'create_success_global'").Scan(&globalCount); err != nil {
		t.Fatalf("count global quota events: %v", err)
	}
	if err := pool.QueryRow(ctx, "SELECT count(*) FROM accounts").Scan(&accountCount); err != nil {
		t.Fatalf("count accounts: %v", err)
	}
	if sourceCount != 1 || globalCount != 1 || accountCount != 1 {
		t.Fatalf("quota/account rows = source %d, global %d, accounts %d; failed creation leaked state",
			sourceCount, globalCount, accountCount)
	}
}
