package httpserver

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestCollectionCORS(t *testing.T) {
	allowed := []string{"https://getcolosseum.world", "https://www.getcolosseum.world"}
	for _, origin := range append(append([]string{}, allowed...), "", "null", "https://getcolosseum.world.evil", "http://getcolosseum.world", "https://evil.test") {
		for _, status := range []int{200, 403, 404, 429, 503} {
			h := collectionCORS(allowed, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { w.WriteHeader(status) }))
			r := httptest.NewRequest("GET", "/", nil)
			r.Header.Set("Origin", origin)
			w := httptest.NewRecorder()
			h.ServeHTTP(w, r)
			want := ""
			if origin == allowed[0] || origin == allowed[1] {
				want = origin
			}
			if w.Code != status || w.Header().Get("Access-Control-Allow-Origin") != want || w.Header().Get("Access-Control-Allow-Credentials") != "" || w.Header().Get("Vary") != "Origin" || w.Header().Get("Cache-Control") != "no-store" {
				t.Fatalf("origin=%q status=%d headers=%v", origin, w.Code, w.Header())
			}
		}
	}
}
func TestCollectionCORSPreflight(t *testing.T) {
	for _, tc := range []struct {
		method, headers string
		allowed         bool
	}{{"GET", "", true}, {"POST", "Content-Type", true}, {"POST", "content-type", true}, {"DELETE", "Content-Type", false}, {"POST", "Authorization", false}, {"POST", "Content-Type, X-Client-IP", false}} {
		h := collectionCORS([]string{"https://getcolosseum.world"}, http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) { t.Fatal("preflight reached service") }))
		r := httptest.NewRequest("OPTIONS", "/", nil)
		r.Header.Set("Origin", "https://getcolosseum.world")
		r.Header.Set("Access-Control-Request-Method", tc.method)
		r.Header.Set("Access-Control-Request-Headers", tc.headers)
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		if tc.allowed {
			if w.Code != 204 || w.Header().Get("Access-Control-Allow-Methods") != "GET, POST" || w.Header().Get("Access-Control-Allow-Headers") != "Content-Type" {
				t.Fatalf("allowed preflight: %d %v", w.Code, w.Header())
			}
		} else if w.Header().Get("Access-Control-Allow-Origin") != "" {
			t.Fatal("unsupported preflight granted access")
		}
		if w.Header().Get("Access-Control-Allow-Credentials") != "" || strings.Contains(w.Header().Get("Access-Control-Allow-Origin"), "*") || w.Header().Get("Cache-Control") != "no-store" {
			t.Fatal(w.Header())
		}
	}
}
func TestCollectionCORSScopeAndUnavailableSource(t *testing.T) {
	h := NewWithOptions(nil, nil, time.Second, nil, Options{CollectionsAllowedWebOrigins: []string{"https://getcolosseum.world"}})
	for _, path := range []string{"/v1/collections/shared/test", "/v1/collections/shared/test/open", "/v1/collections/publications", "/v1/profile", "/healthz", "/collections/test"} {
		r := httptest.NewRequest("OPTIONS", path, nil)
		r.Header.Set("Origin", "https://getcolosseum.world")
		r.Header.Set("Access-Control-Request-Method", "POST")
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		want := ""
		if strings.HasPrefix(path, "/v1/collections/shared/") {
			want = "https://getcolosseum.world"
		}
		if w.Header().Get("Access-Control-Allow-Origin") != want {
			t.Fatalf("scope %s: %v", path, w.Header())
		}
	}
	for _, path := range []string{"/v1/collections/shared/test", "/v1/collections/shared/test/open"} {
		r := httptest.NewRequest("POST", path, strings.NewReader("{}"))
		if !strings.HasSuffix(path, "/open") {
			r.Method = "GET"
		}
		r.RemoteAddr = "invalid"
		w := httptest.NewRecorder()
		h.ServeHTTP(w, r)
		if w.Code != 503 || !strings.Contains(w.Body.String(), "source_unavailable") {
			t.Fatalf("source gate %d %s", w.Code, w.Body.String())
		}
	}
}
