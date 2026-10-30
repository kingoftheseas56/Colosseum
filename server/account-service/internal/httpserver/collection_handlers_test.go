package httpserver

import (
	"bytes"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"os"
	"strings"
	"testing"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
)

func TestCollectionHTTPProjectionFirewall(t *testing.T) {
	raw, err := os.ReadFile("../../../../tests/collections_sharing/fixtures/mixed-gift-v1.json")
	if err != nil {
		t.Fatal(err)
	}
	for _, tc := range []struct {
		name, body string
		status     int
	}{
		{"allowed", `{"operation_id":"11111111-1111-4111-8111-111111111111","kind":"gift","snapshot":` + string(raw) + `,"passphrase":""}`, 200},
		{"raw provider payload", `{"snapshot":{"payload":{"token":"private"}}}`, 400},
		{"unknown account owner", `{"account_id":"someone_else"}`, 400},
		{"unknown nested presentation", `{"snapshot":{"presentation":{"html":"<script>alert(1)</script>"}}}`, 400},
		{"trailing JSON", `{} {}`, 400},
		{"oversize", `{"passphrase":"` + strings.Repeat("x", int(account.CollectionMaxBodyBytes)) + `"}`, 413},
	} {
		t.Run(tc.name, func(t *testing.T) {
			r := httptest.NewRequest(http.MethodPost, "/", strings.NewReader(tc.body))
			w := httptest.NewRecorder()
			var input account.PublishCollectionInput
			ok := decodeCollectionJSON(w, r, &input)
			if ok {
				w.WriteHeader(200)
			}
			if w.Code != tc.status {
				t.Fatalf("status %d body %s", w.Code, w.Body.String())
			}
		})
	}
}
func TestCollectionHTTPErrorContract(t *testing.T) {
	for _, tc := range []struct {
		err    error
		status int
		code   string
	}{
		{account.ErrCollectionNotFound, 404, "publication_not_found"}, {account.ErrCollectionPassphraseRequired, 403, "passphrase_required"}, {account.ErrCollectionPassphrase, 403, "passphrase_invalid"}, {account.ErrCollectionImmutable, 409, "gift_immutable"}, {account.ErrCollectionConflict, 409, "revision_conflict"}, {account.ErrCollectionIdempotency, 409, "idempotency_conflict"},
	} {
		w := httptest.NewRecorder()
		writeCollectionError(w, tc.err)
		if w.Code != tc.status || !bytes.Contains(w.Body.Bytes(), []byte(tc.code)) {
			t.Fatalf("%v: %s", tc.err, w.Body.String())
		}
		var body any
		if json.Unmarshal(w.Body.Bytes(), &body) != nil {
			t.Fatal("invalid error JSON")
		}
		if w.Header().Get("Cache-Control") != "no-store" {
			t.Fatal("cacheable private error")
		}
	}
}
