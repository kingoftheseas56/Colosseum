package main

import (
	"bytes"
	"encoding/json"
	"io"
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func testConfig(upstream string) config {
	return config{
		ClientID:           "colosseum-client",
		ClientSecret:       "server-secret",
		RedirectURI:        "https://colosseum.invalid/trakt/callback",
		RateLimitPerMinute: 60,
		DeviceTokenURL:     upstream + "/device",
		TokenURL:           upstream + "/token",
		RevokeURL:          upstream + "/revoke",
		HTTPClient:         http.DefaultClient,
	}
}

func postJSON(t *testing.T, client *http.Client, url string, body string) *http.Response {
	t.Helper()
	req, err := http.NewRequest(http.MethodPost, url, strings.NewReader(body))
	if err != nil {
		t.Fatal(err)
	}
	req.Header.Set("Content-Type", "application/json")
	resp, err := client.Do(req)
	if err != nil {
		t.Fatal(err)
	}
	return resp
}

func decodeMap(t *testing.T, r *http.Request) map[string]any {
	t.Helper()
	var value map[string]any
	if err := json.NewDecoder(r.Body).Decode(&value); err != nil {
		t.Fatal(err)
	}
	return value
}

func TestDeviceTokenInjectsServerSecret(t *testing.T) {
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path != "/device" {
			t.Fatalf("path=%s", r.URL.Path)
		}
		body := decodeMap(t, r)
		if body["code"] != "device-code" || body["client_id"] != "colosseum-client" || body["client_secret"] != "server-secret" {
			t.Fatalf("unexpected upstream body: %#v", body)
		}
		writeJSON(w, http.StatusOK, map[string]any{
			"access_token": "access", "refresh_token": "refresh", "expires_in": 604800, "created_at": 1700000000,
		})
	}))
	defer upstream.Close()
	broker := httptest.NewServer(newServer(testConfig(upstream.URL)).routes())
	defer broker.Close()
	resp := postJSON(t, broker.Client(), broker.URL+"/v1/trakt/device-token", `{"code":"device-code"}`)
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("status=%d", resp.StatusCode)
	}
	data, _ := io.ReadAll(resp.Body)
	if !bytes.Contains(data, []byte(`"refresh_token":"refresh"`)) {
		t.Fatalf("body=%s", data)
	}
}

func TestRefreshInjectsRedirectAndGrantType(t *testing.T) {
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		body := decodeMap(t, r)
		if r.URL.Path != "/token" || body["refresh_token"] != "old-refresh" ||
			body["client_secret"] != "server-secret" || body["grant_type"] != "refresh_token" ||
			body["redirect_uri"] != "https://colosseum.invalid/trakt/callback" {
			t.Fatalf("unexpected refresh request: %#v", body)
		}
		writeJSON(w, http.StatusOK, map[string]any{
			"access_token": "new-access", "refresh_token": "new-refresh", "expires_in": 604800, "created_at": 1700000100,
		})
	}))
	defer upstream.Close()
	broker := httptest.NewServer(newServer(testConfig(upstream.URL)).routes())
	defer broker.Close()
	resp := postJSON(t, broker.Client(), broker.URL+"/v1/trakt/refresh", `{"refresh_token":"old-refresh"}`)
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("status=%d", resp.StatusCode)
	}
}

func TestRevokeInjectsSecret(t *testing.T) {
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		body := decodeMap(t, r)
		if r.URL.Path != "/revoke" || body["token"] != "access-token" ||
			body["client_id"] != "colosseum-client" || body["client_secret"] != "server-secret" {
			t.Fatalf("unexpected revoke request: %#v", body)
		}
		w.WriteHeader(http.StatusOK)
	}))
	defer upstream.Close()
	broker := httptest.NewServer(newServer(testConfig(upstream.URL)).routes())
	defer broker.Close()
	resp := postJSON(t, broker.Client(), broker.URL+"/v1/trakt/revoke", `{"token":"access-token"}`)
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusOK {
		t.Fatalf("status=%d", resp.StatusCode)
	}
}

func TestMalformedAndUnknownFieldsFailClosed(t *testing.T) {
	upstream := httptest.NewServer(http.NotFoundHandler())
	defer upstream.Close()
	broker := httptest.NewServer(newServer(testConfig(upstream.URL)).routes())
	defer broker.Close()
	for _, body := range []string{`{"code":"ok","extra":true}`, `{`, `{"code":""}`} {
		resp := postJSON(t, broker.Client(), broker.URL+"/v1/trakt/device-token", body)
		if resp.StatusCode != http.StatusBadRequest {
			resp.Body.Close()
			t.Fatalf("body=%q status=%d", body, resp.StatusCode)
		}
		resp.Body.Close()
	}
}

func TestUpstreamAuthorizationStatusIsPreserved(t *testing.T) {
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusTeapot, map[string]any{"error": "denied"})
	}))
	defer upstream.Close()
	broker := httptest.NewServer(newServer(testConfig(upstream.URL)).routes())
	defer broker.Close()
	resp := postJSON(t, broker.Client(), broker.URL+"/v1/trakt/device-token", `{"code":"device-code"}`)
	defer resp.Body.Close()
	if resp.StatusCode != http.StatusTeapot {
		t.Fatalf("status=%d", resp.StatusCode)
	}
}

func TestRateLimitIsPerClientAndFailClosed(t *testing.T) {
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": "pending"})
	}))
	defer upstream.Close()
	cfg := testConfig(upstream.URL)
	cfg.RateLimitPerMinute = 1
	broker := httptest.NewServer(newServer(cfg).routes())
	defer broker.Close()
	first := postJSON(t, broker.Client(), broker.URL+"/v1/trakt/device-token", `{"code":"device-code"}`)
	first.Body.Close()
	if first.StatusCode != http.StatusBadRequest {
		t.Fatalf("first=%d", first.StatusCode)
	}
	second := postJSON(t, broker.Client(), broker.URL+"/v1/trakt/device-token", `{"code":"device-code"}`)
	second.Body.Close()
	if second.StatusCode != http.StatusTooManyRequests {
		t.Fatalf("second=%d", second.StatusCode)
	}
}

func TestNoSecretOrRawUpstreamBodyEscapes(t *testing.T) {
	for _, status := range []int{http.StatusOK, http.StatusBadRequest} {
		upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
			writeJSON(w, status, map[string]any{"client_secret": "server-secret", "raw": "private",
				"access_token": "access", "refresh_token": "refresh", "expires_in": 604800, "created_at": 1700000000})
		}))
		broker := httptest.NewServer(newServer(testConfig(upstream.URL)).routes())
		response := postJSON(t, broker.Client(), broker.URL+"/v1/trakt/device-token", `{"code":"device-code"}`)
		data, _ := io.ReadAll(response.Body)
		response.Body.Close()
		broker.Close()
		upstream.Close()
		if bytes.Contains(data, []byte("server-secret")) || bytes.Contains(data, []byte("private")) {
			t.Fatalf("upstream private fields escaped")
		}
		if response.StatusCode != status {
			t.Fatalf("status=%d", response.StatusCode)
		}
	}
}

func TestReflectedSecretTokenRejected(t *testing.T) {
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, map[string]any{"access_token": "server-secret", "refresh_token": "refresh", "expires_in": 604800, "created_at": 1700000000})
	}))
	defer upstream.Close()
	broker := httptest.NewServer(newServer(testConfig(upstream.URL)).routes())
	defer broker.Close()
	response := postJSON(t, broker.Client(), broker.URL+"/v1/trakt/device-token", `{"code":"device-code"}`)
	defer response.Body.Close()
	if response.StatusCode != http.StatusBadGateway {
		t.Fatalf("status=%d", response.StatusCode)
	}
	data, _ := io.ReadAll(response.Body)
	if bytes.Contains(data, []byte("server-secret")) {
		t.Fatal("secret escaped")
	}
}

func TestUpstreamRedirectIsNeverFollowed(t *testing.T) {
	redirected := false
	target := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { redirected = true }))
	defer target.Close()
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Redirect(w, r, target.URL, http.StatusTemporaryRedirect)
	}))
	defer upstream.Close()
	broker := httptest.NewServer(newServer(testConfig(upstream.URL)).routes())
	defer broker.Close()
	response := postJSON(t, broker.Client(), broker.URL+"/v1/trakt/device-token", `{"code":"device-code"}`)
	defer response.Body.Close()
	if redirected || response.StatusCode != http.StatusBadGateway {
		t.Fatalf("redirected=%v status=%d", redirected, response.StatusCode)
	}
}

func TestMissingServerCredentialsNeverReachUpstream(t *testing.T) {
	called := false
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { called = true }))
	defer upstream.Close()
	for _, missing := range []string{"id", "secret"} {
		cfg := testConfig(upstream.URL)
		if missing == "id" {
			cfg.ClientID = ""
		} else {
			cfg.ClientSecret = ""
		}
		broker := httptest.NewServer(newServer(cfg).routes())
		response := postJSON(t, broker.Client(), broker.URL+"/v1/trakt/device-token", `{"code":"device-code"}`)
		response.Body.Close()
		broker.Close()
		if called || response.StatusCode != http.StatusServiceUnavailable {
			t.Fatalf("called=%v status=%d", called, response.StatusCode)
		}
	}
}

func TestClientCannotChooseURLOrSecret(t *testing.T) {
	called := false
	upstream := httptest.NewServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { called = true }))
	defer upstream.Close()
	broker := httptest.NewServer(newServer(testConfig(upstream.URL)).routes())
	defer broker.Close()
	for _, body := range []string{`{"code":"valid","url":"https://evil.invalid"}`, `{"code":"valid","client_secret":"override"}`} {
		response := postJSON(t, broker.Client(), broker.URL+"/v1/trakt/device-token", body)
		response.Body.Close()
		if called || response.StatusCode != http.StatusBadRequest {
			t.Fatalf("called=%v status=%d", called, response.StatusCode)
		}
	}
}

func TestMissingEnvironmentConfigurationRejected(t *testing.T) {
	t.Setenv("TRAKT_CLIENT_ID", "")
	t.Setenv("TRAKT_CLIENT_SECRET", "")
	if _, err := loadConfig(); err == nil {
		t.Fatal("empty configuration accepted")
	}
	t.Setenv("TRAKT_CLIENT_ID", "owned-id")
	t.Setenv("TRAKT_CLIENT_SECRET", "owned-secret")
	t.Setenv("TRAKT_REDIRECT_URI", "")
	if _, err := loadConfig(); err == nil {
		t.Fatal("empty redirect accepted")
	}
}
