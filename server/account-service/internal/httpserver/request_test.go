package httpserver

import (
	"net/http/httptest"
	"testing"
)

func TestClientNetworkKeyIgnoresFlyHeaderOutsideFly(t *testing.T) {
	t.Setenv("FLY_APP_NAME", "")
	r := httptest.NewRequest("POST", "http://service.test/v1/sessions", nil)
	r.RemoteAddr = "203.0.113.9:4321"
	r.Header.Set("Fly-Client-IP", "198.51.100.42")

	if got := clientNetworkKey(r); got != "203.0.113.9" {
		t.Fatalf("clientNetworkKey = %q, want socket peer", got)
	}
}

func TestClientNetworkKeyTrustsFlyHeaderInsideFly(t *testing.T) {
	t.Setenv("FLY_APP_NAME", "colosseum-account-service")
	r := httptest.NewRequest("POST", "http://service.test/v1/sessions", nil)
	r.RemoteAddr = "203.0.113.9:4321"
	r.Header.Set("Fly-Client-IP", "198.51.100.42")

	if got := clientNetworkKey(r); got != "198.51.100.42" {
		t.Fatalf("clientNetworkKey = %q, want Fly proxy client IP", got)
	}
}
