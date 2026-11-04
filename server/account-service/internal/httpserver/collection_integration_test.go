package httpserver

import (
	"bytes"
	"context"
	"encoding/json"
	"fmt"
	"net/http/httptest"
	"net/netip"
	"os"
	"testing"
	"time"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/database"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/testsupport/testdb"
)

func TestCollectionAssembledHTTPJourney(t *testing.T) {
	pool := testdb.Open(t)
	testdb.ResetPublicSchema(t, pool)
	ctx := context.Background()
	if err := database.RunMigrations(ctx, pool); err != nil {
		t.Fatal(err)
	}
	key := bytes.Repeat([]byte{0x61}, 32)
	hasher, err := account.NewPasswordHasher(account.DefaultArgon2Params())
	if err != nil {
		t.Fatal(err)
	}
	recovery, err := account.NewRecoveryKeyVerifier(key)
	if err != nil {
		t.Fatal(err)
	}
	session, err := account.NewSessionCipher(key)
	if err != nil {
		t.Fatal(err)
	}
	sync, err := account.NewSyncPayloadCipher(key)
	if err != nil {
		t.Fatal(err)
	}
	limiter, err := account.NewRateLimiter(pool, key, nil)
	if err != nil {
		t.Fatal(err)
	}
	service, err := account.NewService(account.Dependencies{Pool: pool, PasswordPolicy: account.PasswordPolicy{Blocklist: account.NewMemoryPasswordBlocklist([]string{"password123456789"})}, PasswordHasher: hasher, RecoveryVerifier: recovery, SessionCipher: session, SyncCipher: sync, SyncMaxFutureSkew: time.Minute, RateLimiter: limiter, RegistrationGlobalLimit: 500})
	if err != nil {
		t.Fatal(err)
	}
	create := func(name string) account.CreateAccountResult {
		out, err := service.CreateAccount(ctx, account.CreateAccountInput{Username: name, Password: "Synthetic river lantern stone 773!", DeviceInstallID: "11111111-1111-4111-8111-111111111111", DeviceLabel: "Synthetic test", Platform: "Linux", SourceKey: "203.0.113.9"})
		if err != nil {
			t.Fatal(err)
		}
		return out
	}
	owner, other := create("http_owner"), create("http_other")
	handler := New(pool, service, time.Second, nil)
	request := func(method, path, token string, body any) *httptest.ResponseRecorder {
		var raw []byte
		if body != nil {
			raw, _ = json.Marshal(body)
		}
		r := httptest.NewRequest(method, path, bytes.NewReader(raw))
		r.RemoteAddr = "203.0.113.5:12345"
		if token != "" {
			r.Header.Set("Authorization", "Bearer "+token)
		}
		w := httptest.NewRecorder()
		handler.ServeHTTP(w, r)
		return w
	}
	raw, err := os.ReadFile("../../../../tests/collections_sharing/fixtures/mixed-gift-v1.json")
	if err != nil {
		t.Fatal(err)
	}
	var snapshot account.CollectionSnapshot
	if err = json.Unmarshal(raw, &snapshot); err != nil {
		t.Fatal(err)
	}
	in := account.PublishCollectionInput{OperationID: "11111111-1111-4111-8111-111111111111", Kind: "gift", Snapshot: snapshot, Passphrase: "gift passphrase"}
	denied := request("POST", "/v1/collections/publications", "", in)
	if denied.Code != 401 {
		t.Fatalf("anonymous publish=%d", denied.Code)
	}
	created := request("POST", "/v1/collections/publications", owner.Session.AccessToken, in)
	if created.Code != 201 {
		t.Fatalf("publish=%d %s", created.Code, created.Body.String())
	}
	var gift account.CollectionPublication
	if err = json.Unmarshal(created.Body.Bytes(), &gift); err != nil {
		t.Fatal(err)
	}
	page := request("GET", "/collections/s/"+gift.ShareID, "", nil)
	if page.Code != 200 || page.Header().Get("Referrer-Policy") != "no-referrer" {
		t.Fatalf("anonymous viewer=%d", page.Code)
	}
	url := "/v1/collections/shared/" + gift.ShareID
	if w := request("GET", url, "", nil); w.Code != 403 {
		t.Fatalf("protected GET=%d", w.Code)
	}
	if w := request("POST", url+"/open", "", map[string]string{"passphrase": "incorrect"}); w.Code != 403 {
		t.Fatalf("wrong phrase=%d", w.Code)
	}
	opened := request("POST", url+"/open", "", map[string]string{"passphrase": "gift passphrase"})
	if opened.Code != 200 {
		t.Fatalf("anonymous open=%d %s", opened.Code, opened.Body.String())
	}
	if opened.Header().Get("Cache-Control") != "no-store" {
		t.Fatal("cacheable gift")
	}

	t.Run("verified proxy buckets and spoof resistance", func(t *testing.T) {
		proxyHandler := NewWithOptions(pool, service, time.Second, nil, Options{TrustedProxyCIDRs: []netip.Prefix{netip.MustParsePrefix("10.20.0.5/32")}, CollectionsAllowedWebOrigins: []string{"https://getcolosseum.world"}})
		open := func(peer, chain, phrase string) *httptest.ResponseRecorder {
			body, _ := json.Marshal(map[string]string{"passphrase": phrase})
			r := httptest.NewRequest("POST", url+"/open", bytes.NewReader(body))
			r.RemoteAddr = peer
			r.Header.Set("X-Forwarded-For", chain)
			r.Header.Set("Origin", "https://getcolosseum.world")
			w := httptest.NewRecorder()
			proxyHandler.ServeHTTP(w, r)
			if w.Header().Get("Access-Control-Allow-Origin") != "https://getcolosseum.world" || w.Header().Get("Cache-Control") != "no-store" {
				t.Fatalf("missing CORS on open: %v", w.Header())
			}
			return w
		}
		for i := 0; i < 10; i++ {
			w := open("10.20.0.5:12", fmt.Sprintf("198.51.100.%d, 203.0.113.51", i+1), "incorrect")
			if w.Code != 403 {
				t.Fatalf("attempt %d: %d %s", i, w.Code, w.Body.String())
			}
		}
		if w := open("10.20.0.5:12", "198.51.100.90, 203.0.113.51", "gift passphrase"); w.Code != 429 {
			t.Fatalf("spoof bypass: %d %s", w.Code, w.Body.String())
		}
		if w := open("10.20.0.5:12", "203.0.113.52", "gift passphrase"); w.Code != 200 {
			t.Fatalf("independent client locked: %d %s", w.Code, w.Body.String())
		}
		for _, chain := range []string{"", "invalid", "203.0.113.53,", "203.0.113.53, 10.20.0.6"} {
			if w := open("10.20.0.5:12", chain, "gift passphrase"); w.Code != 503 {
				t.Fatalf("ambiguous source accepted: %d", w.Code)
			}
		}
		// The source-wide bucket also remains bound to the verified rightmost client.
		missing := "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA"
		for i := 0; i < 121; i++ {
			r := httptest.NewRequest("GET", "/v1/collections/shared/"+missing, nil)
			r.RemoteAddr = "10.20.0.5:12"
			r.Header.Set("X-Forwarded-For", fmt.Sprintf("198.51.100.%d, 203.0.113.54", i%200+1))
			w := httptest.NewRecorder()
			proxyHandler.ServeHTTP(w, r)
			want := 404
			if i == 120 {
				want = 429
			}
			if w.Code != want {
				t.Fatalf("source limit attempt %d: %d %s", i, w.Code, w.Body.String())
			}
		}
		r := httptest.NewRequest("GET", "/v1/collections/shared/"+missing, nil)
		r.RemoteAddr = "10.20.0.5:12"
		r.Header.Set("X-Forwarded-For", "203.0.113.55")
		w := httptest.NewRecorder()
		proxyHandler.ServeHTTP(w, r)
		if w.Code != 404 {
			t.Fatalf("independent source bucket: %d", w.Code)
		}
	})
	revoke := account.RevokeCollectionInput{OperationID: "22222222-2222-4222-8222-222222222222", ExpectedRevision: 1}
	target := "/v1/collections/publications/" + gift.ShareID
	if w := request("DELETE", target, other.Session.AccessToken, revoke); w.Code != 404 {
		t.Fatalf("foreign revoke=%d", w.Code)
	}
	if w := request("DELETE", target, owner.Session.AccessToken, revoke); w.Code != 200 {
		t.Fatalf("owner revoke=%d %s", w.Code, w.Body.String())
	}
	if w := request("POST", url+"/open", "", map[string]string{"passphrase": "gift passphrase"}); w.Code != 404 {
		t.Fatalf("revoked open=%d", w.Code)
	}
	if w := request("GET", "/v1/collections/publications", other.Session.AccessToken, nil); w.Code != 200 || !bytes.Contains(w.Body.Bytes(), []byte(`"publications":[]`)) {
		t.Fatal("owner list leaked publications")
	}
}
