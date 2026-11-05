package httpserver

import (
	"net/http"
	"net/http/httptest"
	"net/netip"
	"testing"
)

const testEdgeSecret = "edge-secret-0123456789-abcdefghijklmnop"

func edgeRequest(mutate func(*http.Request)) *http.Request {
	r := httptest.NewRequest("GET", "http://service.test/v1/collections/shared/x", nil)
	// The socket peer is the hosting provider's proxy; it must never matter.
	r.RemoteAddr = "10.20.0.5:4321"
	r.Header.Set(CollectionEdgeSecretHeader, testEdgeSecret)
	r.Header.Set(collectionEdgeClientIPHeader, "198.51.100.42")
	if mutate != nil {
		mutate(r)
	}
	return r
}

func TestEdgeClientNetworkKey(t *testing.T) {
	secret := edgeSecretDigest(testEdgeSecret)
	cases := []struct {
		name   string
		mutate func(*http.Request)
		want   string
	}{
		{"edge request uses Cloudflare visitor address", nil, "198.51.100.42"},
		{"IPv6 visitor shares its /64", func(r *http.Request) {
			r.Header.Set(collectionEdgeClientIPHeader, "2001:db8:1:2:3:4:5:6")
		}, "2001:db8:1:2::/64"},
		{"IPv4-mapped visitor is unmapped", func(r *http.Request) {
			r.Header.Set(collectionEdgeClientIPHeader, "::ffff:198.51.100.7")
		}, "198.51.100.7"},
		{"forged address without secret is refused", func(r *http.Request) {
			r.Header.Del(CollectionEdgeSecretHeader)
		}, ""},
		{"wrong secret is refused", func(r *http.Request) {
			r.Header.Set(CollectionEdgeSecretHeader, testEdgeSecret+"x")
		}, ""},
		{"secret prefix is refused", func(r *http.Request) {
			r.Header.Set(CollectionEdgeSecretHeader, testEdgeSecret[:10])
		}, ""},
		{"duplicate secret headers are refused", func(r *http.Request) {
			r.Header.Add(CollectionEdgeSecretHeader, testEdgeSecret)
		}, ""},
		{"missing visitor address fails closed", func(r *http.Request) {
			r.Header.Del(collectionEdgeClientIPHeader)
		}, ""},
		{"duplicate visitor addresses fail closed", func(r *http.Request) {
			r.Header.Add(collectionEdgeClientIPHeader, "203.0.113.9")
		}, ""},
		{"address list fails closed", func(r *http.Request) {
			r.Header.Set(collectionEdgeClientIPHeader, "198.51.100.42, 203.0.113.9")
		}, ""},
		{"garbage address fails closed", func(r *http.Request) {
			r.Header.Set(collectionEdgeClientIPHeader, "not-an-ip")
		}, ""},
		{"private address fails closed", func(r *http.Request) {
			r.Header.Set(collectionEdgeClientIPHeader, "10.1.2.3")
		}, ""},
		{"loopback fails closed", func(r *http.Request) {
			r.Header.Set(collectionEdgeClientIPHeader, "127.0.0.1")
		}, ""},
		{"zoned address fails closed", func(r *http.Request) {
			r.Header.Set(collectionEdgeClientIPHeader, "fe80::1%eth0")
		}, ""},
		{"Cloudflare IPv4 hop fails closed", func(r *http.Request) {
			r.Header.Set(collectionEdgeClientIPHeader, "172.70.1.2")
		}, ""},
		{"Cloudflare IPv6 hop fails closed", func(r *http.Request) {
			r.Header.Set(collectionEdgeClientIPHeader, "2a06:98c0:3600::103")
		}, ""},
		{"X-Forwarded-For and other headers are ignored", func(r *http.Request) {
			r.Header.Set("X-Forwarded-For", "203.0.113.77")
			r.Header.Set("Fly-Client-IP", "203.0.113.78")
			r.Header.Set("True-Client-IP", "203.0.113.79")
		}, "198.51.100.42"},
	}
	for _, tc := range cases {
		t.Run(tc.name, func(t *testing.T) {
			if got := edgeClientNetworkKey(edgeRequest(tc.mutate), secret); got != tc.want {
				t.Fatalf("edgeClientNetworkKey = %q, want %q", got, tc.want)
			}
		})
	}
}

func TestCollectionSourceKeyRequiresEdgeWhenConfigured(t *testing.T) {
	// A caller that reaches the origin directly, even through a proxy that is
	// otherwise trusted, cannot fall back to forwarded headers or the peer.
	h := &Handler{
		trustedProxies: []netip.Prefix{netip.MustParsePrefix("10.20.0.5/32")},
		edgeSecret:     edgeSecretDigest(testEdgeSecret),
	}
	direct := httptest.NewRequest("GET", "http://service.test/v1/collections/shared/x", nil)
	direct.RemoteAddr = "10.20.0.5:4321"
	direct.Header.Set("X-Forwarded-For", "198.51.100.42")
	direct.Header.Set(collectionEdgeClientIPHeader, "198.51.100.42")
	if got := h.collectionSourceKey(direct); got != "" {
		t.Fatalf("direct request key = %q, want refused", got)
	}
	if got := h.collectionSourceKey(edgeRequest(nil)); got != "198.51.100.42" {
		t.Fatalf("edge request key = %q", got)
	}
}

func TestCollectionSourceKeyUnchangedWithoutEdgeSecret(t *testing.T) {
	h := &Handler{trustedProxies: []netip.Prefix{netip.MustParsePrefix("10.20.0.5/32")}}
	r := edgeRequest(func(r *http.Request) { r.Header.Set("X-Forwarded-For", "203.0.113.9") })
	if got, want := h.collectionSourceKey(r), clientNetworkKey(r, h.trustedProxies...); got != want || got != "203.0.113.9" {
		t.Fatalf("key = %q, want existing trusted-proxy behavior %q", got, want)
	}
}

func TestNewWithOptionsWiresEdgeSecret(t *testing.T) {
	h := NewWithOptions(nil, nil, 0, nil, Options{CollectionsEdgeSecret: testEdgeSecret})
	if h == nil {
		t.Fatal("nil handler")
	}
	if edgeSecretDigest("") != nil {
		t.Fatal("empty secret must disable the edge rule")
	}
}
