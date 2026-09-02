package account

import (
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"testing"
	"time"
)

func seedSnapshotRows(
	t *testing.T,
	fixture serviceFixture,
	auth AuthenticatedSession,
	mutableCount,
	activityCount int,
) uint64 {
	t.Helper()
	now := fixture.clock.Now().UnixMilli()
	maxSeq := uint64(0)

	batch := make([]SyncMutationInput, 0, 100)
	flush := func() {
		t.Helper()
		if len(batch) == 0 {
			return
		}
		push, err := fixture.service.PushSync(
			context.Background(), auth, batch)
		if err != nil {
			t.Fatalf("PushSync(seed) error = %v", err)
		}
		for index, result := range push.Results {
			if !result.Accepted || result.ServerSeq == 0 {
				t.Fatalf("seed result %d = %+v", index, result)
			}
			if result.ServerSeq > maxSeq {
				maxSeq = result.ServerSeq
			}
		}
		batch = batch[:0]
	}

	for index := 0; index < mutableCount; index++ {
		key := fmt.Sprintf("snapshot/item-%03d", index)
		batch = append(batch, fixtureSyncMutation(
			fmt.Sprintf("86000000-0000-4000-8000-%012x", index+1),
			auth.Device.ID,
			key,
			fmt.Sprintf("value-%03d", index),
			now+int64(index), 0))
		if len(batch) == 100 {
			flush()
		}
	}
	for index := 0; index < activityCount; index++ {
		eventID := fmt.Sprintf("87000000-0000-4000-8000-%012x", index+1)
		batch = append(batch, activityMutation(
			fmt.Sprintf("88000000-0000-4000-8000-%012x", index+1),
			auth.Device.ID,
			eventID,
			activityPlaybackPayload(eventID)))
		if len(batch) == 100 {
			flush()
		}
	}
	flush()
	return maxSeq
}

func snapshotEntryKey(entry SyncPullEntry) string {
	return entry.Mutation.Category + "/" + entry.Mutation.RecordKey
}

func TestSyncSnapshotFirstPageFreezesCursorAcrossPages(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SnapshotFrozenCursor")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)

	// 205 mutable collection rows plus 3 Activity facts: the activity keys
	// sort first, so page one must open with the three activity_fact entries.
	committedMax := seedSnapshotRows(t, fixture, auth, 205, 3)
	if committedMax == 0 {
		t.Fatal("seed produced no committed rows")
	}

	first, err := fixture.service.SnapshotSync(
		context.Background(), auth, "")
	if err != nil {
		t.Fatalf("SnapshotSync(first) error = %v", err)
	}
	if first.Cursor != committedMax {
		t.Fatalf("first page cursor = %d, want committed max %d",
			first.Cursor, committedMax)
	}
	if len(first.Entries) != syncPullPageSize || !first.HasMore {
		t.Fatalf("first page = %d entries hasMore=%v, want %d/true",
			len(first.Entries), first.HasMore, syncPullPageSize)
	}
	if first.NextPageToken == "" {
		t.Fatal("first page omitted next_page_token with more pages pending")
	}
	for index := 0; index < 3; index++ {
		if first.Entries[index].Mutation.Category != "activity_fact" {
			t.Fatalf("entry %d category = %s, want activity_fact first by key order",
				index, first.Entries[index].Mutation.Category)
		}
	}
	for index, entry := range first.Entries {
		if !entry.Canonical || !entry.Won {
			t.Fatalf("entry %d flags = canonical:%v won:%v", index, entry.Canonical, entry.Won)
		}
		if entry.ServerSeq > first.Cursor {
			t.Fatalf("entry %d seq %d exceeds cursor %d",
				index, entry.ServerSeq, first.Cursor)
		}
		if index > 0 {
			previous := first.Entries[index-1].Mutation
			current := entry.Mutation
			if previous.Category > current.Category ||
				(previous.Category == current.Category && previous.RecordKey >= current.RecordKey) {
				t.Fatalf("first page not sorted by (category, record_key) at %d: %s/%s then %s/%s",
					index, previous.Category, previous.RecordKey, current.Category, current.RecordKey)
			}
		}
	}

	// A mutation landing after cursor capture must be invisible to later
	// snapshot pages but recoverable through ordinary pull after the cursor.
	fixture.clock.Advance(time.Minute)
	updated := fixtureSyncMutation(
		"89000000-0000-4000-8000-000000000001",
		auth.Device.ID, "snapshot/item-204", "updated-after-cursor",
		fixture.clock.Now().UnixMilli(), 5)
	push, err := fixture.service.PushSync(
		context.Background(), auth, []SyncMutationInput{updated})
	if err != nil || !push.Results[0].Accepted {
		t.Fatalf("PushSync(post-freeze update) = %+v err %v", push.Results, err)
	}
	postFreezeSeq := push.Results[0].ServerSeq
	if postFreezeSeq <= first.Cursor {
		t.Fatalf("post-freeze update seq = %d, want > frozen cursor %d",
			postFreezeSeq, first.Cursor)
	}

	second, err := fixture.service.SnapshotSync(
		context.Background(), auth, first.NextPageToken)
	if err != nil {
		t.Fatalf("SnapshotSync(second) error = %v", err)
	}
	if second.Cursor != first.Cursor {
		t.Fatalf("second page cursor = %d, want frozen %d", second.Cursor, first.Cursor)
	}
	if second.HasMore || second.NextPageToken != "" {
		t.Fatalf("second page hasMore=%v token=%q, want final page",
			second.HasMore, second.NextPageToken)
	}
	if len(second.Entries) != 7 {
		t.Fatalf("second page = %d entries, want 7 (eight remaining minus the frozen-out update)",
			len(second.Entries))
	}
	seen := map[string]bool{}
	for _, entry := range second.Entries {
		if entry.Mutation.RecordKey == "snapshot/item-204" {
			t.Fatalf("row mutated after cursor capture leaked into later page: %+v", entry)
		}
		if entry.ServerSeq > second.Cursor {
			t.Fatalf("entry seq %d exceeds frozen cursor %d", entry.ServerSeq, second.Cursor)
		}
		seen[snapshotEntryKey(entry)] = true
	}
	for index := 197; index <= 203; index++ {
		key := fmt.Sprintf("collection/snapshot/item-%03d", index)
		if !seen[key] {
			t.Fatalf("second page missing expected row %s", key)
		}
	}

	recovery, err := fixture.service.PullSync(
		context.Background(), auth, first.Cursor)
	if err != nil {
		t.Fatalf("PullSync(after frozen cursor) error = %v", err)
	}
	if len(recovery.Entries) != 1 ||
		recovery.Entries[0].ServerSeq != postFreezeSeq ||
		recovery.Entries[0].Mutation.RecordKey != "snapshot/item-204" ||
		string(recovery.Entries[0].Mutation.Payload) != `{"value":"updated-after-cursor"}` {
		t.Fatalf("pull after cursor = %+v, want only the post-freeze update", recovery.Entries)
	}
}

func TestSyncSnapshotAccountIsolationAndTokenRejection(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SnapshotIsolation")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)
	otherResult := createFixtureAccount(t, fixture, "SnapshotIsolationOther")
	otherAuth := authenticateFixtureSession(t, fixture, otherResult.Session)

	maxSeqA := seedSnapshotRows(t, fixture, auth, 5, 2)
	seedSnapshotRows(t, fixture, otherAuth, 4, 1)

	first, err := fixture.service.SnapshotSync(
		context.Background(), auth, "")
	if err != nil {
		t.Fatalf("SnapshotSync(first) error = %v", err)
	}
	if first.Cursor != maxSeqA {
		t.Fatalf("cursor = %d, want account-scoped committed max %d", first.Cursor, maxSeqA)
	}
	if len(first.Entries) != 7 || first.HasMore {
		t.Fatalf("first page = %d entries hasMore=%v, want 7/false (only own rows)",
			len(first.Entries), first.HasMore)
	}
	for _, entry := range first.Entries {
		if !entry.Canonical || !entry.Won {
			t.Fatalf("isolation entry = %+v, want canonical winner", entry)
		}
	}

	// A token owned by the primary account replayed against the other
	// account is rejected as invalid and never leaks rows.
	probeToken := map[string]any{
		"v":          1,
		"account_id": auth.Account.ID,
		"cursor":     first.Cursor,
		"category":   "collection",
		"record_key": "snapshot/item-000",
	}
	encoded, err := json.Marshal(probeToken)
	if err != nil {
		t.Fatalf("marshal probe token: %v", err)
	}
	foreignToken := base64.RawURLEncoding.EncodeToString(encoded)
	if _, err := fixture.service.SnapshotSync(
		context.Background(), otherAuth, foreignToken); err == nil {
		t.Fatal("replaying another account's page token succeeded")
	} else {
		requireErrorIs(t, err, ErrInvalidPageToken)
	}

	invalidTokens := map[string]string{
		"not base64":          "!!!not-base64!!!",
		"not json":            base64.RawURLEncoding.EncodeToString([]byte("not-json")),
		"missing fields":      base64.RawURLEncoding.EncodeToString([]byte(`{"v":1}`)),
		"unknown version":     tokenJSON(t, 2, auth.Account.ID, 1, "collection", "k"),
		"zero cursor":         tokenJSON(t, 1, auth.Account.ID, 0, "collection", "k"),
		"oversized cursor":    base64.RawURLEncoding.EncodeToString([]byte(`{"v":1,"account_id":"` + auth.Account.ID + `","cursor":9223372036854775808,"category":"collection","record_key":"k"}`)),
		"empty record key":    tokenJSON(t, 1, auth.Account.ID, 1, "collection", ""),
		"json null payload":   base64.RawURLEncoding.EncodeToString([]byte("null")),
		"foreign account":     tokenJSON(t, 1, otherAuth.Account.ID, 1, "collection", "k"),
		"json type confusion": base64.RawURLEncoding.EncodeToString([]byte(`{"v":1,"account_id":"` + auth.Account.ID + `","cursor":"1","category":"collection","record_key":"k"}`)),
	}
	for name, token := range invalidTokens {
		t.Run(name, func(t *testing.T) {
			_, err := fixture.service.SnapshotSync(
				context.Background(), auth, token)
			requireErrorIs(t, err, ErrInvalidPageToken)
		})
	}
}

func tokenJSON(
	t *testing.T,
	version int,
	accountID string,
	cursor uint64,
	category,
	recordKey string,
) string {
	t.Helper()
	payload := map[string]any{
		"v":          version,
		"account_id": accountID,
		"cursor":     cursor,
		"category":   category,
		"record_key": recordKey,
	}
	encoded, err := json.Marshal(payload)
	if err != nil {
		t.Fatalf("marshal token: %v", err)
	}
	return base64.RawURLEncoding.EncodeToString(encoded)
}

func TestSyncSnapshotEmptyAccountHasZeroCursor(t *testing.T) {
	fixture := newServiceFixture(t)
	accountResult := createFixtureAccount(t, fixture, "SnapshotEmpty")
	auth := authenticateFixtureSession(t, fixture, accountResult.Session)

	snapshot, err := fixture.service.SnapshotSync(
		context.Background(), auth, "")
	if err != nil {
		t.Fatalf("SnapshotSync(empty account) error = %v", err)
	}
	if snapshot.Cursor != 0 {
		t.Fatalf("empty account cursor = %d, want 0", snapshot.Cursor)
	}
	if len(snapshot.Entries) != 0 || snapshot.HasMore || snapshot.NextPageToken != "" {
		t.Fatalf("empty account snapshot = %+v, want no entries and no token", snapshot)
	}
}
