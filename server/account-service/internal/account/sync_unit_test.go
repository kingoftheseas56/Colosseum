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
