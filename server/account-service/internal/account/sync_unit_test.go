package account

import (
	"bytes"
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

func TestSyncWirePortableProfileContracts(t *testing.T) {
	mutation := SyncMutationInput{
		MutationID:    "11111111-1111-4111-8111-111111111111",
		DeviceID:      "22222222-2222-4222-8222-222222222222",
		Category:      "watch_state",
		RecordKey:     "watch/mark/c2VyaWVzLTE",
		SchemaVersion: 1,
		HLCPhysicalMS: "1234",
		HLCCounter:    "0",
		Operation:     "put",
		Payload:       json.RawMessage(`{"mark":1}`),
	}
	envelope := SyncPushEnvelope{
		AttachmentID: "33333333-3333-4333-8333-333333333333",
		Mutations:    []SyncMutationInput{mutation},
	}
	encodedEnvelope, err := json.Marshal(envelope)
	if err != nil {
		t.Fatalf("marshal SyncPushEnvelope: %v", err)
	}
	var envelopeJSON map[string]json.RawMessage
	if err := json.Unmarshal(encodedEnvelope, &envelopeJSON); err != nil {
		t.Fatalf("decode SyncPushEnvelope JSON: %v", err)
	}
	if _, ok := envelopeJSON["attachment_id"]; !ok {
		t.Fatal("SyncPushEnvelope omitted attachment_id")
	}
	var mutationJSON []map[string]json.RawMessage
	if err := json.Unmarshal(envelopeJSON["mutations"], &mutationJSON); err != nil {
		t.Fatalf("decode mutations JSON: %v", err)
	}
	if _, ok := mutationJSON[0]["attachment_id"]; ok {
		t.Fatal("SyncMutationInput unexpectedly gained attachment_id")
	}

	withoutAttachment, err := json.Marshal(SyncPushEnvelope{Mutations: []SyncMutationInput{mutation}})
	if err != nil {
		t.Fatalf("marshal envelope without attachment: %v", err)
	}
	if bytes.Contains(withoutAttachment, []byte(`"attachment_id"`)) {
		t.Fatalf("empty envelope attachment_id was not omitted: %s", withoutAttachment)
	}

	canonicalEntry := SyncPullEntry{Canonical: true, Mutation: SyncMutationView{Category: "watch_state"}}
	canonicalJSON, err := json.Marshal(canonicalEntry)
	if err != nil {
		t.Fatalf("marshal canonical SyncPullEntry: %v", err)
	}
	if !bytes.Contains(canonicalJSON, []byte(`"canonical":true`)) {
		t.Fatalf("canonical=true missing from SyncPullEntry JSON: %s", canonicalJSON)
	}
	legacyJSON, err := json.Marshal(SyncPullEntry{})
	if err != nil {
		t.Fatalf("marshal noncanonical SyncPullEntry: %v", err)
	}
	if bytes.Contains(legacyJSON, []byte(`"canonical"`)) {
		t.Fatalf("canonical=false was not omitted: %s", legacyJSON)
	}

	attachment := ProfileAttachment{
		ID:                "33333333-3333-4333-8333-333333333333",
		DeviceID:          mutation.DeviceID,
		BaselineServerSeq: uint64(1)<<63 + 17,
		State:             "open",
	}
	begin := BeginProfileAttachmentInput{
		AttachmentID:         attachment.ID,
		SourceKind:           "local_only",
		SourceSemanticDigest: "sha256:portable-profile",
	}
	snapshot := SyncSnapshotResponse{
		ServerTimeMS:  123456,
		Cursor:        attachment.BaselineServerSeq,
		Entries:       []SyncPullEntry{canonicalEntry},
		NextPageToken: "next-token",
		HasMore:       true,
	}

	encodedAttachment, err := json.Marshal(attachment)
	if err != nil {
		t.Fatalf("marshal ProfileAttachment: %v", err)
	}
	for _, field := range []string{"attachment_id", "device_id", "baseline_server_seq", "state"} {
		if !bytes.Contains(encodedAttachment, []byte(`"`+field+`"`)) {
			t.Fatalf("ProfileAttachment missing %s: %s", field, encodedAttachment)
		}
	}
	var attachmentRoundTrip ProfileAttachment
	if err := json.Unmarshal(encodedAttachment, &attachmentRoundTrip); err != nil {
		t.Fatalf("round-trip ProfileAttachment: %v", err)
	}
	if attachmentRoundTrip != attachment {
		t.Fatalf("ProfileAttachment round-trip = %+v, want %+v", attachmentRoundTrip, attachment)
	}

	encodedBegin, err := json.Marshal(begin)
	if err != nil {
		t.Fatalf("marshal BeginProfileAttachmentInput: %v", err)
	}
	for _, field := range []string{"attachment_id", "source_kind", "source_semantic_digest"} {
		if !bytes.Contains(encodedBegin, []byte(`"`+field+`"`)) {
			t.Fatalf("BeginProfileAttachmentInput missing %s: %s", field, encodedBegin)
		}
	}
	var beginRoundTrip BeginProfileAttachmentInput
	if err := json.Unmarshal(encodedBegin, &beginRoundTrip); err != nil {
		t.Fatalf("round-trip BeginProfileAttachmentInput: %v", err)
	}
	if beginRoundTrip != begin {
		t.Fatalf("BeginProfileAttachmentInput round-trip = %+v, want %+v", beginRoundTrip, begin)
	}

	encodedSnapshot, err := json.Marshal(snapshot)
	if err != nil {
		t.Fatalf("marshal SyncSnapshotResponse: %v", err)
	}
	for _, field := range []string{"server_time_ms", "cursor", "entries", "next_page_token", "has_more"} {
		if !bytes.Contains(encodedSnapshot, []byte(`"`+field+`"`)) {
			t.Fatalf("SyncSnapshotResponse missing %s: %s", field, encodedSnapshot)
		}
	}
	var snapshotRoundTrip SyncSnapshotResponse
	if err := json.Unmarshal(encodedSnapshot, &snapshotRoundTrip); err != nil {
		t.Fatalf("round-trip SyncSnapshotResponse: %v", err)
	}
	if snapshotRoundTrip.Cursor != snapshot.Cursor || len(snapshotRoundTrip.Entries) != 1 || !snapshotRoundTrip.Entries[0].Canonical {
		t.Fatalf("snapshot round-trip = %+v, want cursor %d and canonical SyncPullEntry", snapshotRoundTrip, snapshot.Cursor)
	}
}

func TestSyncPolicyAllowsPortableProfileCategories(t *testing.T) {
	for _, category := range []string{"watch_state", "activity_fact"} {
		if err := validateSyncCategory(category, 1); err != nil {
			t.Fatalf("%s schema 1 rejected: %v", category, err)
		}
		if err := validateSyncCategory(category, 2); err == nil {
			t.Fatalf("%s unsupported schema 2 was accepted", category)
		}
	}
}

func TestSyncPolicyStillRejectsForbiddenFieldsForPortableCategories(t *testing.T) {
	safe := json.RawMessage(`{"logicalId":"series-1","mark":1,"poster":"https://example.invalid/poster.jpg"}`)
	forbidden := []json.RawMessage{
		json.RawMessage(`{"path":"C:\\Private\\show.mkv"}`),
		json.RawMessage(`{"nested":{"clientSecret":"secret"}}`),
		json.RawMessage(`{"logicalValue":"../private/show.mkv"}`),
	}
	for _, category := range []string{"watch_state", "activity_fact"} {
		if err := validateSyncCategory(category, 1); err != nil {
			t.Fatalf("%s category rejected before payload validation: %v", category, err)
		}
		if err := validateSyncPayload(safe); err != nil {
			t.Fatalf("%s safe payload rejected: %v", category, err)
		}
		for _, payload := range forbidden {
			if err := validateSyncPayload(payload); err == nil {
				t.Fatalf("%s accepted forbidden payload %s", category, payload)
			}
		}
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
		"manga/item",
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
		"manga/item",
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
		"manga/item",
		sealed); err == nil {
		t.Fatal("tampered ciphertext opened successfully")
	}

	clean, err := cipher.Seal(
		"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
		"collection",
		"manga/item",
		plain)
	if err != nil {
		t.Fatalf("Seal(second) error = %v", err)
	}
	if _, err := cipher.Open(
		"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
		"collection",
		"manga/other",
		clean); err == nil {
		t.Fatal("ciphertext opened under a different record AAD")
	}
}
