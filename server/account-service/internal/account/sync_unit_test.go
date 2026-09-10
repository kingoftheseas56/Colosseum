package account

import (
	"bytes"
	"encoding/base64"
	"encoding/json"
	"testing"
)

func TestCompareServerHLCUsesPhysicalCounterThenDevice(t *testing.T) {
	a := "11111111-1111-4111-8111-111111111111"
	b := "22222222-2222-4222-8222-222222222222"

	if got := compareServerHLC(10, 1, a, 11, 0, a); got >= 0 {
		t.Fatalf("physical ordering = %d, want < 0", got)
	}
	if got := compareServerHLC(10, 2, a, 10, 1, b); got <= 0 {
		t.Fatalf("counter ordering = %d, want > 0", got)
	}
	if got := compareServerHLC(10, 2, a, 10, 2, b); got >= 0 {
		t.Fatalf("device tie-break = %d, want < 0", got)
	}
}

func TestSyncPolicyRejectsLocalSecretAndPathMaterial(t *testing.T) {
	tests := []json.RawMessage{
		json.RawMessage(`{"path":"C:\\Private\\book.epub"}`),
		json.RawMessage(`{"nested":{"recoveryKey":"secret"}}`),
		json.RawMessage(`{"savedState":{"page":3}}`),
		json.RawMessage(`{"logicalValue":"../private/file.cbz"}`),
	}
	for _, payload := range tests {
		if err := validateSyncPayload(payload); err == nil {
			t.Fatalf("validateSyncPayload(%s) succeeded", payload)
		}
	}

	if err := validateSyncPayload(
		json.RawMessage(`{"logicalId":"series-1","cover":"https://example.invalid/poster.jpg"}`)); err != nil {
		t.Fatalf("safe payload rejected: %v", err)
	}
}

func TestSyncPolicyAllowsFrozenCategoriesThrough7B(t *testing.T) {
	if err := validateSyncCategory("collection", 1); err != nil {
		t.Fatalf("collection rejected: %v", err)
	}
	if err := validateSyncCategory("search_history", 1); err == nil {
		t.Fatal("search_history was accepted")
	}
	if err := validateSyncCategory("full_history", 1); err != nil {
		t.Fatalf("full_history rejected: %v", err)
	}
	if err := validateSyncCategory("collection", 2); err == nil {
		t.Fatal("unknown collection schema was accepted")
	}
}

func TestSyncPolicyEnforcesShippingRecordShapes(t *testing.T) {
	encodedWorld := base64.RawURLEncoding.EncodeToString([]byte("theatre"))
	encodedID := base64.RawURLEncoding.EncodeToString([]byte("movie-1"))
	coreKey := "collection/" + encodedWorld + "/" + encodedID

	tests := []struct {
		name     string
		category string
		key      string
		payload  string
		wantErr  string
	}{
		{
			name:     "collection accepts canonical encoded identity",
			category: "collection",
			key:      coreKey,
			payload:  `{"world":"theatre","id":"movie-1","title":"Movie"}`,
		},
		{
			name:     "collection rejects identity mismatch",
			category: "collection",
			key:      coreKey,
			payload:  `{"world":"theatre","id":"movie-2"}`,
			wantErr:  "record_identity_mismatch",
		},
		{
			name:     "desired download keeps raw world id key",
			category: "desired_download_intent",
			key:      "theatre/movie-1",
			payload:  `{"world":"theatre","id":"movie-1","kind":"movie","title":"Movie"}`,
		},
		{
			name:     "desired download rejects encoded key",
			category: "desired_download_intent",
			key:      coreKey,
			payload:  `{"world":"theatre","id":"movie-1","kind":"movie"}`,
			wantErr:  "invalid_record_key",
		},
		{
			name:     "explicit preference has exact payload",
			category: "explicit_content_preference",
			key:      "preferences/explicit-content",
			payload:  `{"showExplicit":true}`,
		},
		{
			name:     "history accepts owner semantic fields",
			category: "full_history",
			key:      "history/ZXBpc29kZQ/c2hvdy0xL2Ux",
			payload:  `{"kind":"episode","id":"show-1/e1","firstActivityAt":1000,"lastActivityAt":2000,"completedAt":2000}`,
		},
		{
			name:     "history rejects incomplete owner record",
			category: "full_history",
			key:      "history/ZXBpc29kZQ/c2hvdy0xL2Ux",
			payload:  `{"kind":"episode","id":"show-1/e1"}`,
			wantErr:  "payload_invalid",
		},
		{
			name:     "explicit preference rejects extra field",
			category: "explicit_content_preference",
			key:      "preferences/explicit-content",
			payload:  `{"showExplicit":true,"value":1}`,
			wantErr:  "payload_field_not_allowed",
		},
		{
			name:     "server only category is explicitly unsupported",
			category: "extension_roster",
			key:      "extension/item",
			payload:  `{"id":"item"}`,
			wantErr:  "category_not_supported",
		},
	}

	for _, test := range tests {
		t.Run(test.name, func(t *testing.T) {
			err := validateSyncRecordShape(
				test.category,
				1,
				test.key,
				"put",
				json.RawMessage(test.payload))
			if test.wantErr == "" {
				if err != nil {
					t.Fatalf("validateSyncRecordShape() error = %v", err)
				}
				return
			}
			if err == nil || err.Error() != test.wantErr {
				t.Fatalf("validateSyncRecordShape() error = %v, want %s", err, test.wantErr)
			}
		})
	}
}

func TestSyncPolicyStrictlyValidatesActivityPayload(t *testing.T) {
	valid := json.RawMessage(`{
		"v":1,"type":"playback_delta","eventId":"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
		"sessionId":"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb","world":"theatre","kind":"movie",
		"titleKey":"movie-1","itemKey":"movie-1","title":"Movie",
		"itemLabel":"","cover":"","source":"test","syncable":true,
		"utcOffsetMinutes":0,
		"startAtMs":1000,"endAtMs":2500,"activeMs":1500,"rateMilli":1000
	}`)
	if err := validateSyncRecordShape(
		"activity_fact", 1,
		"activity/aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
		"put", valid); err != nil {
		t.Fatalf("valid Activity payload rejected: %v", err)
	}

	invalid := json.RawMessage(`{
		"v":1,"type":"playback_delta","eventId":"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
		"sessionId":"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb","world":"theatre","kind":"movie",
		"titleKey":"movie-1","itemKey":"movie-1","title":"Movie",
		"itemLabel":"","cover":"","source":"test","syncable":true,
		"utcOffsetMinutes":0,
		"startAtMs":1000,"endAtMs":2500,"activeMs":1500,"rateMilli":0
	}`)
	if err := validateSyncRecordShape(
		"activity_fact", 1,
		"activity/aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
		"put", invalid); err == nil {
		t.Fatal("Activity payload with zero rate was accepted")
	}
}

func TestActivityIntegerRejectsRoundedOrFractionalBoundaries(t *testing.T) {
	cases := []struct {
		literal string
		valid   bool
	}{
		{literal: "9223372036854775807", valid: true},
		{literal: "9223372036854775808", valid: false},
		{literal: "-9223372036854775808", valid: true},
		{literal: "-9223372036854775809", valid: false},
		{literal: "1.2", valid: false},
		{literal: "9223372036854775807.5", valid: false},
		{literal: "1e3", valid: true},
		{literal: "1000e-3", valid: true},
		{literal: "1e1000000000", valid: false},
		{literal: "1e-1000000000", valid: false},
		{literal: "9.223372036854776e18", valid: false},
	}
	for _, test := range cases {
		t.Run(test.literal, func(t *testing.T) {
			_, ok := activityInteger(json.Number(test.literal))
			if ok != test.valid {
				t.Fatalf("activityInteger(%q) valid = %v, want %v", test.literal, ok, test.valid)
			}
		})
	}
}

func TestSyncPolicyRejectsTrailingJSON(t *testing.T) {
	if err := validateSyncPayload(json.RawMessage(`{"value":1}{"value":2}`)); err == nil {
		t.Fatal("trailing JSON was accepted")
	}
}

func TestSyncPayloadCipherRoundTripAndTamperFailure(t *testing.T) {
	cipher, err := NewSyncPayloadCipher(bytes.Repeat([]byte{0x42}, 32))
	if err != nil {
		t.Fatalf("NewSyncPayloadCipher() error = %v", err)
	}
	plain := []byte(`{"logicalId":"fixture"}`)
	sealed, err := cipher.Seal(
		"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
		"collection",
		"collection/bWFuZ2E/aXRlbQ",
		plain)
	if err != nil {
		t.Fatalf("Seal() error = %v", err)
	}
	if bytes.Contains(sealed, plain) {
		t.Fatal("ciphertext contains plaintext payload")
	}
	opened, err := cipher.Open(
		"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
		"collection",
		"collection/bWFuZ2E/aXRlbQ",
		sealed)
	if err != nil {
		t.Fatalf("Open() error = %v", err)
	}
	if !bytes.Equal(opened, plain) {
		t.Fatalf("Open() = %q, want %q", opened, plain)
	}

	sealed[len(sealed)-1] ^= 0x01
	if _, err := cipher.Open(
		"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
		"collection",
		"collection/bWFuZ2E/aXRlbQ",
		sealed); err == nil {
		t.Fatal("tampered ciphertext opened successfully")
	}

	clean, err := cipher.Seal(
		"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
		"collection",
		"collection/bWFuZ2E/aXRlbQ",
		plain)
	if err != nil {
		t.Fatalf("Seal(second) error = %v", err)
	}
	if _, err := cipher.Open(
		"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
		"collection",
		"collection/bWFuZ2E/b3RoZXI",
		clean); err == nil {
		t.Fatal("ciphertext opened under a different record AAD")
	}
}
