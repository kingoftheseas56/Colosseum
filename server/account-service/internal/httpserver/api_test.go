package httpserver

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/database"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/testsupport/testdb"
)

func TestAPIContract(t *testing.T) {
	if APIPrefix != "/v1" {
		t.Fatalf("APIPrefix = %q, want /v1", APIPrefix)
	}

	response := httptest.NewRecorder()
	WriteAPIError(response,
		http.StatusUnauthorized,
		"invalid_credentials",
		"The credentials were not accepted.")

	if response.Code != http.StatusUnauthorized {
		t.Fatalf("status = %d, want %d", response.Code, http.StatusUnauthorized)
	}

	var body APIError
	if err := json.Unmarshal(response.Body.Bytes(), &body); err != nil {
		t.Fatalf("decode API error: %v", err)
	}
	if body.Error.Code != "invalid_credentials" {
		t.Fatalf("error code = %q, want invalid_credentials", body.Error.Code)
	}
	if body.Error.Message != "The credentials were not accepted." {
		t.Fatalf("error message = %q", body.Error.Message)
	}
}

func TestSyncPushEnvelopeDecodesOptionalAttachmentID(t *testing.T) {
	const attachedEnvelope = `{
		"attachment_id": "8e000000-0000-4000-8000-000000000001",
		"mutations": [{
			"mutation_id": "8e000000-0000-4000-8000-000000000002",
			"device_id": "8e000000-0000-4000-8000-000000000003",
			"category": "collection",
			"record_key": "envelope/item",
			"schema_version": 1,
			"hlc_physical_ms": "1770000000000",
			"hlc_counter": "0",
			"operation": "put",
			"payload": {"value": "one"}
		}]
	}`
	const ordinaryEnvelope = `{
		"mutations": [{
			"mutation_id": "8e000000-0000-4000-8000-000000000004",
			"device_id": "8e000000-0000-4000-8000-000000000003",
			"category": "collection",
			"record_key": "envelope/item",
			"schema_version": 1,
			"hlc_physical_ms": "1770000000000",
			"hlc_counter": "0",
			"operation": "put",
			"payload": {"value": "one"}
		}]
	}`

	// The frozen wire model carries the optional attachment id.
	var envelope account.SyncPushEnvelope
	if err := json.Unmarshal([]byte(attachedEnvelope), &envelope); err != nil {
		t.Fatalf("decode attached wire envelope: %v", err)
	}
	if envelope.AttachmentID != "8e000000-0000-4000-8000-000000000001" ||
		len(envelope.Mutations) != 1 ||
		envelope.Mutations[0].MutationID != "8e000000-0000-4000-8000-000000000002" {
		t.Fatalf("attached envelope = %+v", envelope)
	}

	// The push handler's strict decoder must accept both envelope shapes
	// without rejecting the attachment_id field as unknown.
	attachedRequest := httptest.NewRequest(
		http.MethodPost, "/v1/sync/push", strings.NewReader(attachedEnvelope))
	var attachedDecoded syncPushRequest
	if err := decodeJSON(httptest.NewRecorder(), attachedRequest, &attachedDecoded); err != nil {
		t.Fatalf("attached sync push request rejected: %v", err)
	}
	if len(attachedDecoded.Mutations) != 1 {
		t.Fatalf("attached request mutations = %d, want 1", len(attachedDecoded.Mutations))
	}

	ordinaryRequest := httptest.NewRequest(
		http.MethodPost, "/v1/sync/push", strings.NewReader(ordinaryEnvelope))
	var ordinaryDecoded syncPushRequest
	if err := decodeJSON(httptest.NewRecorder(), ordinaryRequest, &ordinaryDecoded); err != nil {
		t.Fatalf("ordinary sync push request rejected: %v", err)
	}
	if len(ordinaryDecoded.Mutations) != 1 {
		t.Fatalf("ordinary request mutations = %d, want 1", len(ordinaryDecoded.Mutations))
	}
}

type attachmentHTTPFixture struct {
	handler http.Handler
}

func newAttachmentHTTPFixture(t *testing.T) attachmentHTTPFixture {
	t.Helper()

	pool := testdb.Open(t)
	testdb.ResetPublicSchema(t, pool)

	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	if err := database.RunMigrations(ctx, pool); err != nil {
		t.Fatalf("RunMigrations() error = %v", err)
	}

	passwordHasher, err := account.NewPasswordHasher(account.DefaultArgon2Params())
	if err != nil {
		t.Fatalf("NewPasswordHasher() error = %v", err)
	}
	recoveryVerifier, err := account.NewRecoveryKeyVerifier(bytes.Repeat([]byte{0x31}, 32))
	if err != nil {
		t.Fatalf("NewRecoveryKeyVerifier() error = %v", err)
	}
	sessionCipher, err := account.NewSessionCipher(bytes.Repeat([]byte{0x52}, 32))
	if err != nil {
		t.Fatalf("NewSessionCipher() error = %v", err)
	}
	syncCipher, err := account.NewSyncPayloadCipher(bytes.Repeat([]byte{0x64}, 32))
	if err != nil {
		t.Fatalf("NewSyncPayloadCipher() error = %v", err)
	}
	rateLimiter, err := account.NewRateLimiter(
		pool,
		bytes.Repeat([]byte{0x73}, 32),
		account.SystemClock{})
	if err != nil {
		t.Fatalf("NewRateLimiter() error = %v", err)
	}

	service, err := account.NewService(account.Dependencies{
		Pool:                    pool,
		PasswordHasher:          passwordHasher,
		RecoveryVerifier:        recoveryVerifier,
		SessionCipher:           sessionCipher,
		SyncCipher:              syncCipher,
		SyncMaxFutureSkew:       10 * time.Minute,
		RateLimiter:             rateLimiter,
		Clock:                   account.SystemClock{},
		RegistrationGlobalLimit: 500,
	})
	if err != nil {
		t.Fatalf("NewService() error = %v", err)
	}

	return attachmentHTTPFixture{
		handler: New(fakePinger{}, service, time.Second, nil),
	}
}

func doJSON(
	t *testing.T,
	handler http.Handler,
	method,
	path,
	token string,
	body any,
) (*httptest.ResponseRecorder, map[string]any) {
	t.Helper()

	var reader *strings.Reader
	if body != nil {
		encoded, err := json.Marshal(body)
		if err != nil {
			t.Fatalf("marshal request body: %v", err)
		}
		reader = strings.NewReader(string(encoded))
	} else {
		reader = strings.NewReader("")
	}
	request := httptest.NewRequest(method, path, reader)
	if token != "" {
		request.Header.Set("Authorization", "Bearer "+token)
	}
	response := httptest.NewRecorder()
	handler.ServeHTTP(response, request)

	decoded := map[string]any(nil)
	if strings.TrimSpace(response.Body.String()) != "" {
		if err := json.Unmarshal(response.Body.Bytes(), &decoded); err != nil {
			t.Fatalf("response body %q is not JSON: %v", response.Body.String(), err)
		}
	}
	return response, decoded
}

func createHTTPAccount(
	t *testing.T,
	handler http.Handler,
	username,
	installID string,
) map[string]any {
	t.Helper()
	response, body := doJSON(t, handler, http.MethodPost, "/v1/accounts", "", map[string]any{
		"username":          username,
		"password":          "Mango river lantern stone 773!",
		"device_install_id": installID,
		"device_label":      "Desktop",
		"platform":          "Windows",
	})
	if response.Code != http.StatusCreated {
		t.Fatalf("create account %s status = %d body %v",
			username, response.Code, body)
	}
	return body
}

func accessTokenOf(t *testing.T, created map[string]any) string {
	t.Helper()
	session, ok := created["session"].(map[string]any)
	if !ok {
		t.Fatalf("created account has no session: %v", created)
	}
	token, ok := session["access_token"].(string)
	if !ok || token == "" {
		t.Fatalf("created session has no access token: %v", session)
	}
	return token
}

func TestProfileAttachmentAndSnapshotHTTPFlow(t *testing.T) {
	fixture := newAttachmentHTTPFixture(t)
	createdA := createHTTPAccount(t, fixture.handler, "AttachHTTP",
		"92000000-0000-4000-8000-000000000001")
	tokenA := accessTokenOf(t, createdA)
	createdB := createHTTPAccount(t, fixture.handler, "AttachHTTP2",
		"92000000-0000-4000-8000-000000000002")
	tokenB := accessTokenOf(t, createdB)
	deviceA, ok := createdA["session"].(map[string]any)["device"].(map[string]any)
	if !ok {
		t.Fatalf("created account has no device: %v", createdA)
	}

	const attachmentID = "91000000-0000-4000-8000-000000000001"
	const beginBody = `{
		"attachment_id": "` + attachmentID + `",
		"source_kind": "legacy_local",
		"source_semantic_digest": "digest-http"
	}`

	// Unauthenticated access is rejected before any attachment work.
	response, body := doJSON(t, fixture.handler,
		http.MethodPost, "/v1/profile/attachments", "", json.RawMessage(beginBody))
	if response.Code != http.StatusUnauthorized {
		t.Fatalf("unauthenticated begin status = %d, want 401", response.Code)
	}

	// Begin returns the frozen wire shape: attachment id, device id,
	// baseline server seq, and state.
	response, body = doJSON(t, fixture.handler,
		http.MethodPost, "/v1/profile/attachments", tokenA, json.RawMessage(beginBody))
	if response.Code != http.StatusOK {
		t.Fatalf("begin status = %d body %v", response.Code, body)
	}
	if body["attachment_id"] != attachmentID ||
		body["device_id"] != deviceA["id"] ||
		body["state"] != "open" {
		t.Fatalf("begin body = %v", body)
	}
	if baseline, ok := body["baseline_server_seq"].(float64); !ok || baseline != 0 {
		t.Fatalf("begin baseline = %v, want 0", body["baseline_server_seq"])
	}

	// Attached push through the envelope tags the mutation.
	response, pushBody := doJSON(t, fixture.handler,
		http.MethodPost, "/v1/sync/push", tokenA, map[string]any{
			"attachment_id": attachmentID,
			"mutations": []map[string]any{{
				"mutation_id":     "91000000-0000-4000-8000-000000000002",
				"device_id":       deviceA["id"],
				"category":        "collection",
				"record_key":      "http/item",
				"schema_version":  1,
				"hlc_physical_ms": "1770000000000",
				"hlc_counter":     "0",
				"operation":       "put",
				"payload":         map[string]any{"value": "http"},
			}},
		})
	if response.Code != http.StatusOK {
		t.Fatalf("attached push status = %d body %v", response.Code, pushBody)
	}
	results, ok := pushBody["results"].([]any)
	if !ok || len(results) != 1 {
		t.Fatalf("attached push body = %v", pushBody)
	}
	if result, ok := results[0].(map[string]any); !ok || result["accepted"] != true {
		t.Fatalf("attached push result = %v", results[0])
	}

	// Status reflects the uploaded transition.
	response, body = doJSON(t, fixture.handler,
		http.MethodGet, "/v1/profile/attachments/"+attachmentID, tokenA, nil)
	if response.Code != http.StatusOK || body["state"] != "uploaded" {
		t.Fatalf("get status = %d body %v, want uploaded", response.Code, body)
	}

	// Commit is idempotent over HTTP.
	for attempt := 0; attempt < 2; attempt++ {
		response, body = doJSON(t, fixture.handler,
			http.MethodPost, "/v1/profile/attachments/"+attachmentID+"/commit", tokenA, nil)
		if response.Code != http.StatusOK || body["state"] != "committed" {
			t.Fatalf("commit %d status = %d body %v, want committed",
				attempt, response.Code, body)
		}
	}

	// Pushing after commit fails closed with a conflict, not a server error.
	response, body = doJSON(t, fixture.handler,
		http.MethodPost, "/v1/sync/push", tokenA, map[string]any{
			"attachment_id": attachmentID,
			"mutations": []map[string]any{{
				"mutation_id":     "91000000-0000-4000-8000-000000000003",
				"device_id":       deviceA["id"],
				"category":        "collection",
				"record_key":      "http/item-2",
				"schema_version":  1,
				"hlc_physical_ms": "1770000000001",
				"hlc_counter":     "0",
				"operation":       "put",
				"payload":         map[string]any{"value": "post-commit"},
			}},
		})
	if response.Code != http.StatusConflict {
		t.Fatalf("post-commit push status = %d body %v, want 409", response.Code, body)
	}
	if errorBody, ok := body["error"].(map[string]any); !ok ||
		errorBody["code"] != "attachment_not_active" {
		t.Fatalf("post-commit push error = %v", body)
	}

	// Snapshot exposes the frozen canonical page shape.
	response, body = doJSON(t, fixture.handler,
		http.MethodGet, "/v1/sync/snapshot", tokenA, nil)
	if response.Code != http.StatusOK {
		t.Fatalf("snapshot status = %d body %v", response.Code, body)
	}
	if cursor, ok := body["cursor"].(float64); !ok || cursor <= 0 {
		t.Fatalf("snapshot cursor = %v, want > 0", body["cursor"])
	}
	if entries, ok := body["entries"].([]any); !ok || len(entries) != 1 {
		t.Fatalf("snapshot entries = %v, want the single pushed row", body["entries"])
	}
	if hasMore, ok := body["has_more"].(bool); !ok || hasMore {
		t.Fatalf("snapshot has_more = %v, want false", body["has_more"])
	}
	if _, present := body["next_page_token"]; present {
		t.Fatalf("final snapshot page carried next_page_token: %v", body)
	}

	// Invalid page tokens are rejected without leaking rows.
	response, body = doJSON(t, fixture.handler,
		http.MethodGet, "/v1/sync/snapshot?after_key=%21%21garbage%21%21", tokenA, nil)
	if response.Code != http.StatusBadRequest {
		t.Fatalf("invalid token status = %d body %v, want 400", response.Code, body)
	}
	if errorBody, ok := body["error"].(map[string]any); !ok ||
		errorBody["code"] != "invalid_page_token" {
		t.Fatalf("invalid token error = %v", body)
	}

	// Cross-account access fails closed.
	response, body = doJSON(t, fixture.handler,
		http.MethodGet, "/v1/profile/attachments/"+attachmentID, tokenB, nil)
	if response.Code != http.StatusNotFound {
		t.Fatalf("cross-account get status = %d body %v, want 404", response.Code, body)
	}
	if errorBody, ok := body["error"].(map[string]any); !ok ||
		errorBody["code"] != "attachment_not_found" {
		t.Fatalf("cross-account get error = %v", body)
	}

	response, body = doJSON(t, fixture.handler,
		http.MethodPost, "/v1/profile/attachments", tokenB, json.RawMessage(beginBody))
	if response.Code != http.StatusConflict {
		t.Fatalf("cross-account begin retry status = %d body %v, want 409", response.Code, body)
	}
	if errorBody, ok := body["error"].(map[string]any); !ok ||
		errorBody["code"] != "attachment_conflict" {
		t.Fatalf("cross-account begin retry error = %v", body)
	}

	// Invalid begin payloads are rejected as bad requests.
	response, body = doJSON(t, fixture.handler,
		http.MethodPost, "/v1/profile/attachments", tokenB, map[string]any{
			"attachment_id":          "not-a-uuid",
			"source_kind":            "local_only",
			"source_semantic_digest": "digest",
		})
	if response.Code != http.StatusBadRequest {
		t.Fatalf("invalid begin status = %d body %v, want 400", response.Code, body)
	}
	if errorBody, ok := body["error"].(map[string]any); !ok ||
		errorBody["code"] != "invalid_attachment" {
		t.Fatalf("invalid begin error = %v", body)
	}
}
