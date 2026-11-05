package httpserver

import (
	"bytes"
	"context"
	"encoding/base64"
	"encoding/json"
	"fmt"
	"image"
	"image/png"
	"math/rand/v2"
	"net/http/httptest"
	"net/netip"
	"os"
	"strings"
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
	request := func(method, path, token string, body any, validators ...string) *httptest.ResponseRecorder {
		var raw []byte
		if body != nil {
			raw, _ = json.Marshal(body)
		}
		r := httptest.NewRequest(method, path, bytes.NewReader(raw))
		r.RemoteAddr = "203.0.113.5:12345"
		if token != "" {
			r.Header.Set("Authorization", "Bearer "+token)
		}
		for _, validator := range validators {
			r.Header.Add("If-None-Match", validator)
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

	t.Run("public revalidation and snapshot sizes", func(t *testing.T) {
		list := snapshot
		list.Presentation.CoverData = ""
		input := account.PublishCollectionInput{
			OperationID: "33333333-3333-4333-8333-333333333333", Kind: "collection", Snapshot: list,
		}
		created := request("POST", "/v1/collections/publications", owner.Session.AccessToken, input)
		var pub account.CollectionPublication
		if created.Code != 201 || json.Unmarshal(created.Body.Bytes(), &pub) != nil {
			t.Fatalf("publish public fixture: %d %s", created.Code, created.Body)
		}
		path := "/v1/collections/shared/" + pub.ShareID
		target := "/v1/collections/publications/" + pub.ShareID
		requirePublic := func(w *httptest.ResponseRecorder, status int) string {
			t.Helper()
			tag := w.Header().Get("ETag")
			if w.Code != status || tag == "" || w.Header().Get("Cache-Control") != "public, max-age=60, must-revalidate" {
				t.Fatalf("public GET: %d, headers %v", w.Code, w.Header())
			}
			return tag
		}
		reportSize := func(label string, w *httptest.ResponseRecorder) {
			t.Helper()
			var body struct{ Snapshot json.RawMessage }
			if err := json.Unmarshal(w.Body.Bytes(), &body); err != nil || len(body.Snapshot) == 0 {
				t.Fatalf("read %s snapshot: %v", label, err)
			}
			var dbBytes int
			if err := pool.QueryRow(ctx, "SELECT octet_length(snapshot::text) FROM collection_publications WHERE share_id=$1", pub.ShareID).Scan(&dbBytes); err != nil {
				t.Fatal(err)
			}
			t.Logf("%s: %d bytes HTTP snapshot, %d bytes HTTP body, %d bytes PostgreSQL snapshot text", label, len(body.Snapshot), w.Body.Len(), dbBytes)
		}
		first := request("GET", path, "", nil)
		tag := requirePublic(first, 200)
		reportSize("without cover", first)
		for _, validators := range [][]string{{tag}, {strings.TrimPrefix(tag, "W/")}, {"*"}, {`"another,tag"`, tag}} {
			w := request("GET", path, "", nil, validators...)
			if requirePublic(w, 304) != tag || w.Body.Len() != 0 {
				t.Fatalf("conditional response changed tag or carried a body: %v", w.Header())
			}
		}

		// A deliberately high-entropy opaque PNG exercises the existing cover
		// sanitizer close to its 256 KiB limit.
		img := image.NewRGBA(image.Rect(0, 0, 290, 290))
		rng := rand.New(rand.NewPCG(1, 2))
		for i := 0; i < len(img.Pix); i += 4 {
			color := rng.Uint32()
			img.Pix[i], img.Pix[i+1], img.Pix[i+2], img.Pix[i+3] = byte(color), byte(color>>8), byte(color>>16), 255
		}
		var cover bytes.Buffer
		if err := png.Encode(&cover, img); err != nil {
			t.Fatal(err)
		}
		if cover.Len() < 240*1024 || cover.Len() > 256*1024 {
			t.Fatalf("cover fixture size %d outside intended range", cover.Len())
		}
		t.Logf("input PNG: %d bytes", cover.Len())
		list.Presentation.CoverData = "data:image/png;base64," + base64.StdEncoding.EncodeToString(cover.Bytes())
		update := account.UpdateCollectionInput{OperationID: "44444444-4444-4444-8444-444444444444", ExpectedRevision: 1, Snapshot: list}
		if w := request("PUT", target, owner.Session.AccessToken, update); w.Code != 200 {
			t.Fatalf("update with cover: %d", w.Code)
		}
		second := request("GET", path, "", nil, tag)
		nextTag := requirePublic(second, 200)
		if nextTag == tag {
			t.Fatal("revision change reused ETag")
		}
		reportSize("with cover", second)

		update.OperationID, update.ExpectedRevision, update.Passphrase = "55555555-5555-4555-8555-555555555555", 2, "public then protected"
		if w := request("PUT", target, owner.Session.AccessToken, update); w.Code != 200 {
			t.Fatalf("protect publication: %d", w.Code)
		}
		requirePrivate := func(w *httptest.ResponseRecorder, status int) {
			t.Helper()
			if w.Code != status || w.Header().Get("ETag") != "" || w.Header().Get("Cache-Control") != "no-store" {
				t.Fatalf("uncacheable response: %d, headers %v", w.Code, w.Header())
			}
		}
		requirePrivate(request("GET", path, "", nil, "*"), 403)
		requirePrivate(request("POST", path+"/open", "", map[string]string{"passphrase": update.Passphrase}, "*"), 200)
		revoke := account.RevokeCollectionInput{OperationID: "66666666-6666-4666-8666-666666666666", ExpectedRevision: 3}
		if w := request("DELETE", target, owner.Session.AccessToken, revoke); w.Code != 200 {
			t.Fatalf("revoke publication: %d", w.Code)
		}
		requirePrivate(request("GET", path, "", nil, "*"), 404)
	})

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
