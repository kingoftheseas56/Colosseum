package collectionviewer

import (
	"net/http"
	"net/http/httptest"
	"strings"
	"testing"
)

func TestViewerRoutes(t *testing.T) {
	for _, path := range []string{"/collections/s/abcdefghijklmnopqrstuvwxyz012345", "/collections/assets/viewer.js", "/collections/assets/viewer.css"} {
		r := httptest.NewRequest(http.MethodGet, path, nil)
		w := httptest.NewRecorder()
		Handler().ServeHTTP(w, r)
		if w.Code != http.StatusOK || w.Body.Len() == 0 {
			t.Fatalf("%s: %d", path, w.Code)
		}
		if w.Header().Get("Cache-Control") != "no-store" || w.Header().Get("Referrer-Policy") != "no-referrer" {
			t.Fatalf("unsafe caching/referrer policy: %s", path)
		}
		csp := w.Header().Get("Content-Security-Policy")
		for _, directive := range []string{"default-src 'none'", "img-src data:", "frame-ancestors 'none'", "base-uri 'none'"} {
			if !strings.Contains(csp, directive) {
				t.Fatalf("CSP missing %s", directive)
			}
		}
	}
}
func TestViewerRejectsUnknownRoutesAndMethods(t *testing.T) {
	for _, path := range []string{"/collections/assets/embed.go", "/collections/assets/viewer.test.cjs", "/collections/s/short", "/collections/s/../../secret"} {
		w := httptest.NewRecorder()
		Handler().ServeHTTP(w, httptest.NewRequest(http.MethodGet, path, nil))
		if w.Code != 404 {
			t.Fatalf("%s: %d", path, w.Code)
		}
	}
	w := httptest.NewRecorder()
	Handler().ServeHTTP(w, httptest.NewRequest(http.MethodPost, "/collections/assets/viewer.js", nil))
	if w.Code != 405 {
		t.Fatal(w.Code)
	}
	w = httptest.NewRecorder()
	Handler().ServeHTTP(w, httptest.NewRequest(http.MethodHead, "/collections/assets/viewer.js", nil))
	if w.Code != 200 || w.Body.Len() != 0 {
		t.Fatal("HEAD should omit body")
	}
}
