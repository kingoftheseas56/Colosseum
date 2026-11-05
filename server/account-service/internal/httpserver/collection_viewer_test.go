package httpserver

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
	"time"
)

func TestLegacyCollectionViewerRedirectsWhenEdgeIsRequired(t *testing.T) {
	id := strings.Repeat("a", 43)
	handler := NewWithOptions(nil, nil, time.Second, nil, Options{
		CollectionsEdgeSecret: strings.Repeat("x", 32),
	})
	target := "https://getcolosseum.world/c/" + id

	for _, method := range []string{http.MethodGet, http.MethodHead} {
		t.Run(method, func(t *testing.T) {
			request := httptest.NewRequest(method, "https://untrusted.example/collections/s/"+id+"/?ignored=value", nil)
			response := httptest.NewRecorder()
			handler.ServeHTTP(response, request)
			if response.Code != http.StatusFound || response.Header().Get("Location") != target {
				t.Fatalf("expected redirect to %s, got %d %q", target, response.Code, response.Header().Get("Location"))
			}
			if response.Header().Get("Cache-Control") != "no-store" || response.Header().Get("Referrer-Policy") != "no-referrer" {
				t.Fatal("redirect must retain caching and referrer protections")
			}
			if method == http.MethodHead && response.Body.Len() != 0 {
				t.Fatal("HEAD returned a response body")
			}
		})
	}

	for _, test := range []struct {
		method string
		path   string
		status int
	}{
		{http.MethodGet, "/collections/s/short", http.StatusNotFound},
		{http.MethodPost, "/collections/s/" + id, http.StatusMethodNotAllowed},
		{http.MethodGet, "/collections/assets/viewer.js", http.StatusOK},
	} {
		response := httptest.NewRecorder()
		handler.ServeHTTP(response, httptest.NewRequest(test.method, test.path, nil))
		if response.Code != test.status || response.Header().Get("Location") != "" {
			t.Fatalf("%s %s: got %d %q", test.method, test.path, response.Code, response.Header().Get("Location"))
		}
	}

	response := httptest.NewRecorder()
	New(nil, nil, time.Second, nil).ServeHTTP(response, httptest.NewRequest(http.MethodGet, "/collections/s/"+id, nil))
	if response.Code != http.StatusOK || response.Header().Get("Location") != "" || response.Body.Len() == 0 {
		t.Fatal("legacy viewer should remain self-contained when edge enforcement is off")
	}
}
