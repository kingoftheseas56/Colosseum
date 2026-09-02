package account

import (
	"bytes"
	"context"
	"encoding/json"
	"strings"
	"testing"
)

// The portable Activity fact shape exported by the desktop N-09
// ActivitySyncAdapter: category "activity_fact", schema 1, record key
// "activity/<lowercase-eventId>", PUT-only payload whose eventId keeps its
// portable casing while the wire identity stays the normalized lowercase UUID.
func activityPlaybackPayload(eventID string) json.RawMessage {
	object := map[string]any{
		"v":                json.Number("1"),
		"type":             "playback_delta",
		"eventId":          eventID,
		"sessionId":        "11111111-1111-4111-8111-111111111111",
		"world":            "theatre",
		"kind":             "movie",
		"titleKey":         "movie:fixture",
		"itemKey":          "movie:fixture",
		"title":            "Fixture Movie",
		"itemLabel":        "",
		"cover":            "",
		"utcOffsetMinutes": json.Number("0"),
		"syncable":         true,
		"source":           "test",
		"startAtMs":        json.Number("1000"),
		"endAtMs":          json.Number("6000"),
		"activeMs":         json.Number("5000"),
		"rateMilli":        json.Number("1000"),
	}
	encoded, err := json.Marshal(object)
	if err != nil {
		panic(err)
	}
	return encoded
}

func activityMutation(
	mutationID,
	deviceID,
	eventID string,
	payload json.RawMessage,
) SyncMutationInput {
	return SyncMutationInput{
		MutationID:    mutationID,
		DeviceID:      deviceID,
		Category:      "activity_fact",
		RecordKey:     "activity/" + strings.ToLower(eventID),
		SchemaVersion: 1,
		HLCPhysicalMS: "1770000000000",
		HLCCounter:    "0",
		Operation:     "put",
		Payload:       payload,
	}
}

func pushOneActivity(
	t *testing.T,
	fixture serviceFixture,
	auth AuthenticatedSession,
	mutation SyncMutationInput,
) SyncPushResult {
	t.Helper()
	push, err := fixture.service.PushSync(
		context.Background(),
		auth,
		[]SyncMutationInput{mutation})
	if err != nil {
		t.Fatalf("PushSync(activity) error = %v", err)
	}
	if len(push.Results) != 1 {
		t.Fatalf("PushSync(activity) results = %d, want 1", len(push.Results))
	}
	return push.Results[0]
}

func queryInt(
	t *testing.T,
	fixture serviceFixture,
	sql string,
	args ...any,
) int64 {
	t.Helper()
	var value int64
	if err := fixture.pool.
		QueryRow(context.Background(), sql, args...).
		Scan(&value); err != nil {
		t.Fatalf("query %q failed: %v", sql, err)
	}
	return value
}

func TestActivityFactValidInsertIsEncryptedAndStaysOutOfMutableSync(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivityInsert")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	const eventID = "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa"
	result := pushOneActivity(t, fixture, auth, activityMutation(
		"12345678-1234-4123-8123-123456789abc",
		auth.Device.ID,
		eventID,
		activityPlaybackPayload(eventID)))

	if !result.Accepted || result.ServerSeq == 0 || !result.Won {
		t.Fatalf("activity insert result = %+v, want accepted seq/won", result)
	}

	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_activity_facts WHERE account_id = $1::uuid",
		auth.Account.ID); got != 1 {
		t.Fatalf("activity fact rows = %d, want 1", got)
	}
	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_sync_current WHERE account_id = $1::uuid",
		auth.Account.ID); got != 0 {
		t.Fatalf("account_sync_current rows = %d, want 0", got)
	}
	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_sync_journal WHERE account_id = $1::uuid",
		auth.Account.ID); got != 0 {
		t.Fatalf("account_sync_journal rows = %d, want 0", got)
	}

	var ciphertext []byte
	if err := fixture.pool.
		QueryRow(context.Background(), `
			SELECT payload_ciphertext FROM account_activity_facts
			WHERE account_id = $1::uuid AND event_id = $2::uuid`,
			auth.Account.ID, eventID).
		Scan(&ciphertext); err != nil {
		t.Fatalf("load activity ciphertext: %v", err)
	}
	if bytes.Contains(ciphertext, []byte("Fixture Movie")) {
		t.Fatal("activity fact payload is stored as plaintext")
	}

	plain, err := fixture.service.syncCipher.Open(
		auth.Account.ID,
		"activity_fact",
		"activity/"+eventID,
		ciphertext)
	if err != nil {
		t.Fatalf("open activity ciphertext: %v", err)
	}
	var canonical map[string]any
	decoder := json.NewDecoder(bytes.NewReader(plain))
	decoder.UseNumber()
	if err := decoder.Decode(&canonical); err != nil {
		t.Fatalf("decrypted activity payload is invalid JSON: %v", err)
	}
	if canonical["title"] != "Fixture Movie" || canonical["type"] != "playback_delta" {
		t.Fatalf("decrypted activity payload = %v", canonical)
	}
	var schemaVersion int64
	var eventType string
	if err := fixture.pool.
		QueryRow(context.Background(),
			"SELECT schema_version, event_type FROM account_activity_facts WHERE account_id = $1::uuid AND event_id = $2::uuid",
			auth.Account.ID, eventID).
		Scan(&schemaVersion, &eventType); err != nil {
		t.Fatalf("load activity fact columns: %v", err)
	}
	if schemaVersion != 1 || eventType != "playback_delta" {
		t.Fatalf("activity fact schema/type = %d/%s", schemaVersion, eventType)
	}
}

func TestActivityFactAcceptsPutOnly(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivityPutOnly")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	const eventID = "bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb"
	mutation := activityMutation(
		"23456789-2345-4234-8234-23456789abcd",
		auth.Device.ID,
		eventID,
		nil)
	mutation.Operation = "delete"

	result := pushOneActivity(t, fixture, auth, mutation)
	if result.Accepted || result.Code != "invalid_operation" {
		t.Fatalf("activity delete result = %+v, want invalid_operation", result)
	}
	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_activity_facts WHERE account_id = $1::uuid",
		auth.Account.ID); got != 0 {
		t.Fatalf("activity fact rows = %d, want 0", got)
	}
}

func TestActivityFactKeyAndPayloadIdentity(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivityIdentity")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	const eventID = "cccccccc-cccc-4ccc-8ccc-cccccccccccc"
	otherPayload := activityPlaybackPayload("dddddddd-dddd-4ddd-8ddd-dddddddddddd")

	cases := []struct {
		name      string
		recordKey string
		payload   json.RawMessage
		code      string
	}{
		{
			name:      "payload event belongs to another key",
			recordKey: "activity/" + eventID,
			payload:   otherPayload,
			code:      "activity_key_payload_mismatch",
		},
		{
			name:      "uppercase wire key",
			recordKey: "activity/" + strings.ToUpper(eventID),
			payload:   activityPlaybackPayload(eventID),
			code:      "invalid_record_key",
		},
		{
			name:      "non-uuid suffix",
			recordKey: "activity/not-a-uuid",
			payload:   activityPlaybackPayload(eventID),
			code:      "invalid_record_key",
		},
		{
			name:      "non-activity key prefix",
			recordKey: "other/" + eventID,
			payload:   activityPlaybackPayload(eventID),
			code:      "invalid_record_key",
		},
	}
	for _, testCase := range cases {
		t.Run(testCase.name, func(t *testing.T) {
			mutation := activityMutation(
				"3456789a-3456-4345-8345-3456789abcde",
				auth.Device.ID,
				eventID,
				testCase.payload)
			mutation.RecordKey = testCase.recordKey
			result := pushOneActivity(t, fixture, auth, mutation)
			if result.Accepted || result.Code != testCase.code {
				t.Fatalf("result = %+v, want rejected %s", result, testCase.code)
			}
		})
	}
	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_activity_facts WHERE account_id = $1::uuid",
		auth.Account.ID); got != 0 {
		t.Fatalf("activity fact rows = %d, want 0", got)
	}

	// Uppercase portable eventId with a lowercase wire key is the N-09
	// export shape and must be accepted.
	upperPayload := activityPlaybackPayload(strings.ToUpper(eventID))
	accepted := pushOneActivity(t, fixture, auth, activityMutation(
		"456789ab-4567-4456-8456-456789abcdef",
		auth.Device.ID,
		eventID,
		upperPayload))
	if !accepted.Accepted || accepted.ServerSeq == 0 {
		t.Fatalf("uppercase portable eventId result = %+v, want accepted", accepted)
	}
}

func TestActivityFactUnsupportedTypeAndSyncableFalse(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivityTypeSyncable")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	const eventID = "eeeeeeee-eeee-4eee-8eee-eeeeeeeeeeee"

	badType := activityPlaybackPayload(eventID)
	var badTypeObject map[string]any
	if err := json.Unmarshal(badType, &badTypeObject); err != nil {
		t.Fatalf("unmarshal bogus-type payload: %v", err)
	}
	badTypeObject["type"] = "watch_party_delta"
	badType, err := json.Marshal(badTypeObject)
	if err != nil {
		t.Fatalf("marshal bogus-type payload: %v", err)
	}
	result := pushOneActivity(t, fixture, auth, activityMutation(
		"56789abc-5678-4567-8567-56789abcdef1",
		auth.Device.ID,
		eventID,
		badType))
	if result.Accepted || result.Code != "activity_unsupported_type" {
		t.Fatalf("bogus type result = %+v", result)
	}

	notSyncable := activityPlaybackPayload(eventID)
	var notSyncableObject map[string]any
	if err := json.Unmarshal(notSyncable, &notSyncableObject); err != nil {
		t.Fatalf("unmarshal not-syncable payload: %v", err)
	}
	notSyncableObject["syncable"] = false
	notSyncable, err = json.Marshal(notSyncableObject)
	if err != nil {
		t.Fatalf("marshal not-syncable payload: %v", err)
	}
	result = pushOneActivity(t, fixture, auth, activityMutation(
		"6789abcd-6789-4678-8678-6789abcdef12",
		auth.Device.ID,
		eventID,
		notSyncable))
	if result.Accepted || result.Code != "activity_not_syncable" {
		t.Fatalf("syncable=false result = %+v", result)
	}

	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_activity_facts WHERE account_id = $1::uuid",
		auth.Account.ID); got != 0 {
		t.Fatalf("activity fact rows = %d, want 0", got)
	}
}

func TestActivityFactKeepsSyncPayloadFirewall(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivityFirewall")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	const eventID = "ffffffff-ffff-4fff-8fff-ffffffffffff"

	forbidden := activityPlaybackPayload(eventID)
	var forbiddenObject map[string]any
	if err := json.Unmarshal(forbidden, &forbiddenObject); err != nil {
		t.Fatalf("unmarshal firewall payload: %v", err)
	}
	forbiddenObject["password"] = "must-not-sync"
	forbidden, err := json.Marshal(forbiddenObject)
	if err != nil {
		t.Fatalf("marshal firewall payload: %v", err)
	}
	result := pushOneActivity(t, fixture, auth, activityMutation(
		"789abcde-789a-4789-8789-789abcdef123",
		auth.Device.ID,
		eventID,
		forbidden))
	if result.Accepted || result.Code == "" {
		t.Fatalf("firewall result = %+v, want rejected", result)
	}

	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_activity_facts WHERE account_id = $1::uuid",
		auth.Account.ID); got != 0 {
		t.Fatalf("activity fact rows = %d, want 0", got)
	}
}

func TestActivityFactTransportRetryIsIdempotent(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivityRetry")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	const eventID = "10101010-1010-4101-8101-101010101010"
	const mutationID = "89abcdef-89ab-489a-889a-89abcdef1234"
	payload := activityPlaybackPayload(eventID)

	first := pushOneActivity(t, fixture, auth, activityMutation(
		mutationID, auth.Device.ID, eventID, payload))
	if !first.Accepted {
		t.Fatalf("first push = %+v", first)
	}

	second := pushOneActivity(t, fixture, auth, activityMutation(
		mutationID, auth.Device.ID, eventID, payload))
	if !second.Accepted || second.ServerSeq != first.ServerSeq {
		t.Fatalf("retry result = %+v, want same seq as %+v", second, first)
	}

	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_activity_facts WHERE account_id = $1::uuid",
		auth.Account.ID); got != 1 {
		t.Fatalf("activity fact rows = %d, want 1", got)
	}
}

func TestActivityFactSemanticDuplicateKeepsOneFactWithoutSequencing(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivitySemantic")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	const eventID = "12121212-1212-4121-8121-121212121212"
	payload := activityPlaybackPayload(eventID)

	first := pushOneActivity(t, fixture, auth, activityMutation(
		"9abcdef0-9abc-49ab-89ab-9abcdef12345",
		auth.Device.ID,
		eventID,
		payload))
	if !first.Accepted {
		t.Fatalf("first push = %+v", first)
	}

	before := queryInt(t, fixture,
		"SELECT last_value FROM account_change_seq")

	duplicate := pushOneActivity(t, fixture, auth, activityMutation(
		"abcdef01-abcd-4abc-8abc-abcdef012345",
		auth.Device.ID,
		eventID,
		payload))
	if !duplicate.Accepted || duplicate.ServerSeq != first.ServerSeq {
		t.Fatalf("semantic duplicate = %+v, want original seq %+v", duplicate, first)
	}

	after := queryInt(t, fixture,
		"SELECT last_value FROM account_change_seq")
	if after != before {
		t.Fatalf("account_change_seq moved %d -> %d on a semantic duplicate",
			before, after)
	}

	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_activity_facts WHERE account_id = $1::uuid",
		auth.Account.ID); got != 1 {
		t.Fatalf("activity fact rows = %d, want 1", got)
	}
}

func TestActivityFactCrossDeviceDuplicateRemainsOneFact(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivityCrossDevice")
	authA := authenticateFixtureSession(t, fixture, created.Session)

	signInB, err := fixture.service.SignIn(
		context.Background(),
		SignInInput{
			Username:        "ActivityCrossDevice",
			Password:        testPassword,
			DeviceInstallID: deviceBInstall,
			DeviceLabel:     "Laptop",
			Platform:        "Windows",
			SourceKey:       "203.0.113.71",
		})
	if err != nil {
		t.Fatalf("SignIn(device B) error = %v", err)
	}
	if signInB.Status != "signed_in" || signInB.Session == nil {
		t.Fatalf("SignIn(device B) = %#v", signInB)
	}
	authB := authenticateFixtureSession(t, fixture, *signInB.Session)

	const eventID = "13131313-1313-4131-8131-131313131313"
	payload := activityPlaybackPayload(eventID)

	first := pushOneActivity(t, fixture, authA, activityMutation(
		"bcdef012-bcde-4bcd-8bcd-bcdef0123456",
		authA.Device.ID,
		eventID,
		payload))
	if !first.Accepted {
		t.Fatalf("device A push = %+v", first)
	}

	fromB := pushOneActivity(t, fixture, authB, activityMutation(
		"cdef0123-cdef-4cde-8cde-cdef01234567",
		authB.Device.ID,
		eventID,
		payload))
	if !fromB.Accepted || fromB.ServerSeq != first.ServerSeq {
		t.Fatalf("device B duplicate = %+v, want one fact at %+v", fromB, first)
	}

	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_activity_facts WHERE account_id = $1::uuid",
		authA.Account.ID); got != 1 {
		t.Fatalf("activity fact rows = %d, want 1", got)
	}
	var origin string
	if err := fixture.pool.
		QueryRow(context.Background(), `
			SELECT origin_device_id::text FROM account_activity_facts
			WHERE account_id = $1::uuid AND event_id = $2::uuid`,
			authA.Account.ID, eventID).
		Scan(&origin); err != nil {
		t.Fatalf("load activity origin: %v", err)
	}
	if origin != authA.Device.ID {
		t.Fatalf("activity origin = %s, want the first pushing device", origin)
	}
}

func TestActivityFactConflictingContentIsRejected(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivityConflict")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	const eventID = "14141414-1414-4141-8141-141414141414"
	first := pushOneActivity(t, fixture, auth, activityMutation(
		"def01234-def0-4def-8def-def012345678",
		auth.Device.ID,
		eventID,
		activityPlaybackPayload(eventID)))
	if !first.Accepted {
		t.Fatalf("first push = %+v", first)
	}

	conflicting := activityPlaybackPayload(eventID)
	var payloadObject map[string]any
	if err := json.Unmarshal(conflicting, &payloadObject); err != nil {
		t.Fatalf("unmarshal conflicting payload: %v", err)
	}
	payloadObject["activeMs"] = json.Number("6000")
	reencoded, err := json.Marshal(payloadObject)
	if err != nil {
		t.Fatalf("marshal conflicting payload: %v", err)
	}

	result := pushOneActivity(t, fixture, auth, activityMutation(
		"ef012345-ef01-4ef0-8ef0-ef0123456789",
		auth.Device.ID,
		eventID,
		reencoded))
	if result.Accepted || result.Code != "activity_event_conflict" {
		t.Fatalf("conflict result = %+v, want activity_event_conflict", result)
	}

	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_activity_facts WHERE account_id = $1::uuid",
		auth.Account.ID); got != 1 {
		t.Fatalf("activity fact rows = %d, want 1", got)
	}
	var seq int64
	if err := fixture.pool.
		QueryRow(context.Background(), `
			SELECT server_seq FROM account_activity_facts
			WHERE account_id = $1::uuid AND event_id = $2::uuid`,
			auth.Account.ID, eventID).
		Scan(&seq); err != nil || uint64(seq) != first.ServerSeq {
		t.Fatalf("stored seq = %d (err %v), want the original %d",
			seq, err, first.ServerSeq)
	}
	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_sync_journal WHERE account_id = $1::uuid",
		auth.Account.ID); got != 0 {
		t.Fatalf("account_sync_journal rows = %d, want 0", got)
	}
}

func TestActivityFactCanonicalCompareIgnoresKeyOrder(t *testing.T) {
	fixture := newServiceFixture(t)
	created := createFixtureAccount(t, fixture, "ActivityKeyOrder")
	auth := authenticateFixtureSession(t, fixture, created.Session)

	const eventID = "15151515-1515-4151-8151-151515151515"
	first := pushOneActivity(t, fixture, auth, activityMutation(
		"f0123456-f012-40f1-80f1-f0123456789a",
		auth.Device.ID,
		eventID,
		activityPlaybackPayload(eventID)))
	if !first.Accepted {
		t.Fatalf("first push = %+v", first)
	}

	// Same object with every key manually reversed — a different JSON byte
	// order that must still compare equal under canonical portable JSON.
	var object map[string]any
	if err := json.Unmarshal(activityPlaybackPayload(eventID), &object); err != nil {
		t.Fatalf("unmarshal payload: %v", err)
	}
	keys := make([]string, 0, len(object))
	for key := range object {
		keys = append(keys, key)
	}
	for i, j := 0, len(keys)-1; i < j; i, j = i+1, j-1 {
		keys[i], keys[j] = keys[j], keys[i]
	}
	var builder strings.Builder
	builder.WriteByte('{')
	for index, key := range keys {
		if index > 0 {
			builder.WriteByte(',')
		}
		encodedKey, _ := json.Marshal(key)
		encodedValue, _ := json.Marshal(object[key])
		builder.Write(encodedKey)
		builder.WriteByte(':')
		builder.Write(encodedValue)
	}
	builder.WriteByte('}')
	reordered := json.RawMessage(builder.String())

	duplicate := pushOneActivity(t, fixture, auth, activityMutation(
		"0123456f-0123-4012-8012-0123456789ab",
		auth.Device.ID,
		eventID,
		reordered))
	if !duplicate.Accepted || duplicate.ServerSeq != first.ServerSeq {
		t.Fatalf("reordered duplicate = %+v, want one fact at %+v", duplicate, first)
	}

	if got := queryInt(t, fixture,
		"SELECT count(*) FROM account_activity_facts WHERE account_id = $1::uuid",
		auth.Account.ID); got != 1 {
		t.Fatalf("activity fact rows = %d, want 1", got)
	}
}
