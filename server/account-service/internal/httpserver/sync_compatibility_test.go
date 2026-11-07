package httpserver

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"net/http"
	"net/http/httptest"
	"strconv"
	"testing"
	"time"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/database"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/testsupport/testdb"
)

func TestNuvioSyncHTTPCompatibility(t *testing.T) {
	pool := testdb.Open(t)
	testdb.ResetPublicSchema(t, pool)
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	if err := database.RunMigrations(ctx, pool); err != nil {
		t.Fatal(err)
	}

	hasher, err := account.NewPasswordHasher(account.DefaultArgon2Params())
	if err != nil {
		t.Fatal(err)
	}
	recovery, err := account.NewRecoveryKeyVerifier(bytes.Repeat([]byte{0x31}, 32))
	if err != nil {
		t.Fatal(err)
	}
	session, err := account.NewSessionCipher(bytes.Repeat([]byte{0x52}, 32))
	if err != nil {
		t.Fatal(err)
	}
	syncCipher, err := account.NewSyncPayloadCipher(bytes.Repeat([]byte{0x64}, 32))
	if err != nil {
		t.Fatal(err)
	}
	limiter, err := account.NewRateLimiter(pool, bytes.Repeat([]byte{0x73}, 32), account.SystemClock{})
	if err != nil {
		t.Fatal(err)
	}
	service, err := account.NewService(account.Dependencies{
		Pool:           pool,
		PasswordPolicy: account.PasswordPolicy{Blocklist: account.NewMemoryPasswordBlocklist(nil)},
		PasswordHasher: hasher, RecoveryVerifier: recovery, SessionCipher: session,
		SyncCipher: syncCipher, SyncMaxFutureSkew: 10 * time.Minute, RateLimiter: limiter,
		Clock: account.SystemClock{}, RegistrationGlobalLimit: 500,
	})
	if err != nil {
		t.Fatal(err)
	}
	created, err := service.CreateAccount(ctx, account.CreateAccountInput{
		Username: "Nuvio_HTTP_Compatibility", Password: "Mango river lantern stone 773!",
		DeviceInstallID: "11111111-1111-4111-8111-111111111111",
		DeviceLabel:     "HTTP compatibility fixture", Platform: "Windows", SourceKey: "203.0.113.10",
	})
	if err != nil {
		t.Fatal(err)
	}
	handler := New(pool, service, time.Second, nil)
	request := func(t *testing.T, method, path, feature string, body []byte) *httptest.ResponseRecorder {
		t.Helper()
		r := httptest.NewRequest(method, path, bytes.NewReader(body))
		r.Header.Set("Authorization", "Bearer "+created.Session.AccessToken)
		if body != nil {
			r.Header.Set("Content-Type", "application/json")
		}
		if feature != "" {
			r.Header.Set("X-Colosseum-Sync-Features", feature)
		}
		response := httptest.NewRecorder()
		handler.ServeHTTP(response, r)
		if response.Code != http.StatusOK {
			t.Fatalf("%s %s feature=%q status=%d", method, path, feature, response.Code)
		}
		return response
	}
	historyKey := "history/" + base64.RawURLEncoding.EncodeToString([]byte("episode")) + "/" + base64.RawURLEncoding.EncodeToString([]byte("tt0100:1:1"))
	historyPayload := json.RawMessage(`{"kind":"episode","id":"tt0100:1:1","firstActivityAt":1000,"lastActivityAt":4000,"source":"nuvio","displayId":"tmdb:1396_s1e1","displayTitle":"Pilot","latestKnownAt":3000}`)
	timestamp := strconv.FormatInt(time.Now().UnixMilli(), 10)
	body, err := json.Marshal(syncPushRequest{Mutations: []account.SyncMutationInput{
		{MutationID: "88000000-0000-4000-8000-000000000001", DeviceID: created.Session.Device.ID,
			Category: "full_history", RecordKey: historyKey, SchemaVersion: 1, Operation: "put",
			HLCPhysicalMS: timestamp, HLCCounter: "0", Payload: historyPayload},
		{MutationID: "88000000-0000-4000-8000-000000000002", DeviceID: created.Session.Device.ID,
			Category: "stremio_link", RecordKey: "preferences/main-sync-provider", SchemaVersion: 1, Operation: "put",
			HLCPhysicalMS: timestamp, HLCCounter: "1", Payload: json.RawMessage(`{"mainSyncProvider":"nuvio"}`)},
	}})
	if err != nil {
		t.Fatal(err)
	}
	var pushed account.SyncPushResponse
	if err := json.Unmarshal(request(t, http.MethodPost, "/v1/sync/push", "nuvio", body).Body.Bytes(), &pushed); err != nil {
		t.Fatal(err)
	}
	if len(pushed.Results) != 2 {
		t.Fatalf("push results=%d, want 2", len(pushed.Results))
	}
	for _, result := range pushed.Results {
		if !result.Accepted || !result.Won {
			t.Fatalf("capable HTTP seed rejected: accepted=%v won=%v code=%s", result.Accepted, result.Won, result.Code)
		}
	}

	// Exercise the actual header-to-context boundary independently for both
	// journal and canonical snapshot handlers. Headerless clients must never
	// receive payloads their v1.2.0 adapters cannot materialize.
	for _, path := range []string{"/v1/sync/pull?after=0", "/v1/sync/snapshot"} {
		for _, feature := range []string{"", "nuvio"} {
			name := path + "/legacy"
			if feature != "" {
				name = path + "/nuvio"
			}
			t.Run(name, func(t *testing.T) {
				response := request(t, http.MethodGet, path, feature, nil)
				var pulled account.SyncPullResponse
				if err := json.Unmarshal(response.Body.Bytes(), &pulled); err != nil {
					t.Fatal(err)
				}
				if pulled.HasMore || len(pulled.Entries) != 2 {
					t.Fatalf("entries=%d has_more=%v", len(pulled.Entries), pulled.HasMore)
				}
				seenHistory, seenProvider := false, false
				for _, entry := range pulled.Entries {
					view := entry.Mutation
					switch view.Category {
					case "full_history":
						if seenHistory || view.RecordKey != historyKey || view.Operation != "put" {
							t.Fatal("unexpected history identity/operation")
						}
						seenHistory = true
						var payload map[string]json.RawMessage
						if err := json.Unmarshal(view.Payload, &payload); err != nil {
							t.Fatal(err)
						}
						var expected map[string]json.RawMessage
						if err := json.Unmarshal(historyPayload, &expected); err != nil {
							t.Fatal(err)
						}
						for _, field := range []string{"source", "displayId", "displayTitle", "latestKnownAt"} {
							value, exists := payload[field]
							if exists != (feature == "nuvio") {
								t.Fatalf("field %s present=%v for feature=%q", field, exists, feature)
							}
							if exists && !bytes.Equal(value, expected[field]) {
								t.Fatalf("presentation field %s changed", field)
							}
							if feature == "" {
								delete(expected, field)
							}
						}
						if len(payload) != len(expected) {
							t.Fatalf("history field count=%d, want %d", len(payload), len(expected))
						}
						for field, value := range expected {
							if !bytes.Equal(payload[field], value) {
								t.Fatalf("history field %s changed", field)
							}
						}
					case "stremio_link":
						if seenProvider || view.RecordKey != "preferences/main-sync-provider" {
							t.Fatal("unexpected provider identity")
						}
						seenProvider = true
						if feature == "" {
							if view.Operation != "delete" || len(view.Payload) != 0 {
								t.Fatal("headerless provider must project to empty DELETE")
							}
						} else {
							var payload map[string]string
							if err := json.Unmarshal(view.Payload, &payload); err != nil {
								t.Fatal(err)
							}
							if view.Operation != "put" || len(payload) != 1 || payload["mainSyncProvider"] != "nuvio" {
								t.Fatal("capable provider must remain Nuvio PUT")
							}
						}
					default:
						t.Fatalf("unexpected category %q", view.Category)
					}
				}
				if !seenHistory || !seenProvider {
					t.Fatal("missing history or provider entry")
				}
			})
		}
	}

	push := func(mutations []account.SyncMutationInput) account.SyncPushResponse {
		t.Helper()
		body, err := json.Marshal(syncPushRequest{Mutations: mutations})
		if err != nil {
			t.Fatal(err)
		}
		var result account.SyncPushResponse
		if err := json.Unmarshal(request(t, http.MethodPost, "/v1/sync/push", "", body).Body.Bytes(), &result); err != nil {
			t.Fatal(err)
		}
		if len(result.Results) != len(mutations) {
			t.Fatal("legacy HTTP push lost receipts")
		}
		for _, receipt := range result.Results {
			if !receipt.Accepted {
				t.Fatalf("legacy HTTP push rejected: %s", receipt.Code)
			}
		}
		return result
	}
	legacy := push([]account.SyncMutationInput{
		{MutationID: "88000000-0000-4000-8000-000000000003", DeviceID: created.Session.Device.ID,
			Category: "full_history", RecordKey: historyKey, SchemaVersion: 1, Operation: "put",
			HLCPhysicalMS: timestamp, HLCCounter: "2",
			Payload: json.RawMessage(`{"kind":"episode","id":"tt0100:1:1","firstActivityAt":1000,"lastActivityAt":5000}`)},
		{MutationID: "88000000-0000-4000-8000-000000000004", DeviceID: created.Session.Device.ID,
			Category: "stremio_link", RecordKey: "preferences/main-sync-provider", SchemaVersion: 1, Operation: "delete",
			HLCPhysicalMS: timestamp, HLCCounter: "3"},
	})
	if !legacy.Results[0].Won || legacy.Results[1].Won {
		t.Fatal("legacy history edit must win and empty provider must preserve Nuvio")
	}
	var snapshot account.SyncSnapshotResponse
	if err := json.Unmarshal(request(t, http.MethodGet, "/v1/sync/snapshot", "nuvio", nil).Body.Bytes(), &snapshot); err != nil {
		t.Fatal(err)
	}
	if len(snapshot.Entries) != 2 {
		t.Fatal("legacy push lost canonical records")
	}
	for _, entry := range snapshot.Entries {
		var object map[string]json.RawMessage
		if err := json.Unmarshal(entry.Mutation.Payload, &object); err != nil {
			t.Fatal(err)
		}
		if entry.Mutation.Category == "full_history" {
			var expected map[string]json.RawMessage
			if err := json.Unmarshal(historyPayload, &expected); err != nil {
				t.Fatal(err)
			}
			expected["lastActivityAt"] = json.RawMessage("5000")
			for field, value := range expected {
				if !bytes.Equal(object[field], value) {
					t.Fatalf("legacy HTTP push clobbered history field %s", field)
				}
			}
		} else if entry.Mutation.Operation != "put" || string(object["mainSyncProvider"]) != `"nuvio"` {
			t.Fatal("legacy HTTP clear clobbered Nuvio provider")
		}
	}
	choice := push([]account.SyncMutationInput{
		{MutationID: "88000000-0000-4000-8000-000000000005", DeviceID: created.Session.Device.ID,
			Category: "stremio_link", RecordKey: "preferences/main-sync-provider", SchemaVersion: 1, Operation: "put",
			HLCPhysicalMS: timestamp, HLCCounter: "4", Payload: json.RawMessage(`{"mainSyncProvider":"stremio"}`)},
	})
	if !choice.Results[0].Won {
		t.Fatal("explicit legacy Stremio choice must win")
	}
	if err := json.Unmarshal(request(t, http.MethodGet, "/v1/sync/snapshot", "nuvio", nil).Body.Bytes(), &snapshot); err != nil {
		t.Fatal(err)
	}
	for _, entry := range snapshot.Entries {
		if entry.Mutation.Category == "stremio_link" && string(entry.Mutation.Payload) != `{"mainSyncProvider":"stremio"}` {
			t.Fatal("explicit legacy Stremio choice was not stored")
		}
	}
}
