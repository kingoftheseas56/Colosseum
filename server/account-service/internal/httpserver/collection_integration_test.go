package httpserver

import (
	"bytes"
	"context"
	"encoding/json"
	"net/http/httptest"
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
