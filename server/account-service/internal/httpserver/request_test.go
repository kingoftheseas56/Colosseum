package httpserver

import (
	"bytes"
	"net/http/httptest"
	"testing"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
)

func TestClientNetworkKeyIgnoresFlyHeaderOutsideFly(t *testing.T) {
	t.Setenv("FLY_APP_NAME", "")
	r := httptest.NewRequest("POST", "http://service.test/v1/sessions", nil)
	r.RemoteAddr = "203.0.113.9:4321"
	r.Header.Set("Fly-Client-IP", "198.51.100.42")

	if got := clientNetworkKey(r); got != "203.0.113.9" {
		t.Fatalf("clientNetworkKey = %q, want socket peer", got)
	}
}

func TestClientNetworkKeyTrustsFlyHeaderInsideFly(t *testing.T) {
	t.Setenv("FLY_APP_NAME", "colosseum-account-service")
	r := httptest.NewRequest("POST", "http://service.test/v1/sessions", nil)
	r.RemoteAddr = "203.0.113.9:4321"
	r.Header.Set("Fly-Client-IP", "198.51.100.42")

	if got := clientNetworkKey(r); got != "198.51.100.42" {
		t.Fatalf("clientNetworkKey = %q, want Fly proxy client IP", got)
	}
}

func TestDecodeNativeProfileAttachmentManifest(t *testing.T) {
	body := []byte(`{
		"attachment_id":"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
		"source_kind":"local_only",
		"source_profile_id":"local-only",
		"source_semantic_digest":"sha256:source",
		"manifest_digest":"sha256:manifest",
		"manifest":[{
			"mutation_id":"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
			"device_id":"11111111-1111-4111-8111-111111111111",
			"category":"collection",
			"record_key":"manga/item-1",
			"schema_version":1,
			"hlc_physical_ms":"9000",
			"hlc_counter":"1",
			"operation":"put",
			"payload":{"value":"fixture"}
		}]
	}`)
	request := httptest.NewRequest("POST", "/v1/profile/attachments", bytes.NewReader(body))
	response := httptest.NewRecorder()

	var input account.BeginProfileAttachmentInput
	if err := decodeJSON(response, request, &input); err != nil {
		t.Fatalf("decode native attachment manifest: %v", err)
	}
	if len(input.Manifest) != 1 || input.Manifest[0].MutationID != "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa" {
		t.Fatalf("decoded manifest = %#v", input.Manifest)
	}
}
