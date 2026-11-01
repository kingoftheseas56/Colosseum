package httpserver

import (
	"bytes"
	"context"
	"encoding/json"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"net/netip"
	"strconv"
	"strings"
	"testing"
	"time"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/avatar"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/database"
	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/testsupport/testdb"
)

const networkTestPassword = "Mango river lantern stone 773!"

func networkTestHandler(t *testing.T) http.Handler {
	t.Helper()
	pool := testdb.Open(t)
	testdb.ResetPublicSchema(t, pool)
	ctx := context.Background()
	if err := database.RunMigrations(ctx, pool); err != nil {
		t.Fatal(err)
	}
	hasher, err := account.NewPasswordHasher(account.DefaultArgon2Params())
	if err != nil {
		t.Fatal(err)
	}
	verifier, err := account.NewRecoveryKeyVerifier(bytes.Repeat([]byte{0x31}, 32))
	if err != nil {
		t.Fatal(err)
	}
	cipher, err := account.NewSessionCipher(bytes.Repeat([]byte{0x52}, 32))
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
		Pool: pool, PasswordPolicy: account.PasswordPolicy{Blocklist: account.NewMemoryPasswordBlocklist(nil)},
		PasswordHasher: hasher, RecoveryVerifier: verifier, SessionCipher: cipher, SyncCipher: syncCipher,
		RateLimiter: limiter, AvatarStore: avatar.DisabledStore{}, Clock: account.SystemClock{}, RegistrationGlobalLimit: 500,
		SyncMaxFutureSkew: 10 * time.Minute,
	})
	if err != nil {
		t.Fatal(err)
	}
	return NewWithTrustedProxies(pool, service, time.Second, nil, []netip.Prefix{netip.MustParsePrefix("10.20.0.5/32")})
}

func networkTestRequest(t *testing.T, h http.Handler, path, source, username, password string, want int) *httptest.ResponseRecorder {
	t.Helper()
	body, err := json.Marshal(map[string]string{"username": username, "password": password, "device_install_id": "11111111-1111-4111-8111-111111111111", "device_label": "Rate limit test", "platform": "Windows"})
	if err != nil {
		t.Fatal(err)
	}
	r := httptest.NewRequest("POST", path, bytes.NewReader(body))
	r.RemoteAddr = "10.20.0.5:4321"
	r.Header.Set("X-Forwarded-For", source)
	w := httptest.NewRecorder()
	h.ServeHTTP(w, r)
	if w.Code != want {
		t.Fatalf("%s status=%d, want %d (response omitted to protect session secrets)", path, w.Code, want)
	}
	return w
}

func requireHTTPRetry(t *testing.T, w *httptest.ResponseRecorder, max int) {
	t.Helper()
	seconds, err := strconv.Atoi(w.Header().Get("Retry-After"))
	if err != nil || seconds < 1 || seconds > max || !strings.Contains(w.Body.String(), `"rate_limited"`) {
		t.Fatal("429 must carry a bounded Retry-After and stable safe error")
	}
}

func TestHTTPRegistrationSeparatesVisitorsBehindTrustedProxy(t *testing.T) {
	h := networkTestHandler(t)
	for i, source := range []string{"198.51.100.42", "198.51.100.43", "198.51.100.44"} {
		name := "NetworkTester" + strconv.Itoa(i)
		networkTestRequest(t, h, "/v1/accounts", source, name, networkTestPassword, 201)
		networkTestRequest(t, h, "/v1/sessions", source, name, networkTestPassword, 200)
	}
	// Two successful registrations per source remain the ceiling, even with
	// a forged leftmost forwarded address or a different username.
	networkTestRequest(t, h, "/v1/accounts", "198.51.100.42", "NetworkSecond", networkTestPassword, 201)
	w := networkTestRequest(t, h, "/v1/accounts", "198.51.100.99, 198.51.100.42", "NetworkThird", networkTestPassword, 429)
	requireHTTPRetry(t, w, 86400)
}

func TestHTTPSignInLimitDoesNotBlockAnotherVisitor(t *testing.T) {
	h := networkTestHandler(t)
	networkTestRequest(t, h, "/v1/accounts", "198.51.100.42", "NetworkLogin", networkTestPassword, 201)
	for i := 0; i < 10; i++ {
		networkTestRequest(t, h, "/v1/sessions", "198.51.100.42", "NetworkLogin", "wrong password", 401)
	}
	w := networkTestRequest(t, h, "/v1/sessions", "198.51.100.99, 198.51.100.42", "NetworkLogin", networkTestPassword, 429)
	requireHTTPRetry(t, w, 900)
	// Sign-in and registration have independent budgets; both work for B.
	networkTestRequest(t, h, "/v1/accounts", "198.51.100.43", "NetworkOther", networkTestPassword, 201)
	networkTestRequest(t, h, "/v1/sessions", "198.51.100.43", "NetworkOther", networkTestPassword, 200)
}

func TestHTTPInvalidRegistrationStillCountsAgainstSource(t *testing.T) {
	h := networkTestHandler(t)
	for i := 0; i < 10; i++ {
		networkTestRequest(t, h, "/v1/accounts", "198.51.100.42", "x", networkTestPassword, 400)
	}
	w := networkTestRequest(t, h, "/v1/accounts", "198.51.100.42", "HealthyUsername", networkTestPassword, 429)
	requireHTTPRetry(t, w, 3600)
	networkTestRequest(t, h, "/v1/accounts", "198.51.100.43", "HealthyUsername", networkTestPassword, 201)
}

func TestRateLimitLogsOnlyAnonymousScopeAndRetry(t *testing.T) {
	var logs bytes.Buffer
	h := Handler{logger: slog.New(slog.NewJSONHandler(&logs, nil))}
	w := httptest.NewRecorder()
	h.writeAccountError(w, &account.RateLimitError{Scope: "signin_source", RetryAfter: 1400 * time.Millisecond})
	if !strings.Contains(logs.String(), `"scope":"signin_source"`) || !strings.Contains(logs.String(), `"retry_after_seconds":2`) {
		t.Fatalf("missing anonymous bucket diagnostics: %s", logs.String())
	}
	if strings.Contains(w.Body.String(), "signin_source") {
		t.Fatal("internal limiter scope leaked to client")
	}
}
