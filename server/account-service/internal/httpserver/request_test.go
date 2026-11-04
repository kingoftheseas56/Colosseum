package httpserver

import (
	"bytes"
	"net/http/httptest"
	"net/netip"
	"strings"
	"testing"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
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

func TestClientNetworkKeyDoesNotTrustEnvironmentAlone(t *testing.T) {
	t.Setenv("FLY_APP_NAME", "colosseum-account-service")
	r := httptest.NewRequest("POST", "http://service.test/v1/sessions", nil)
	r.RemoteAddr = "203.0.113.9:4321"
	r.Header.Set("Fly-Client-IP", "198.51.100.42")

	if got := clientNetworkKey(r); got != "203.0.113.9" {
		t.Fatalf("clientNetworkKey = %q, want socket peer without verified proxy configuration", got)
	}
}

func TestDecodeNativeProfileAttachmentManifest(t *testing.T) {
	body := []byte(`{
		"attachment_id":"bbbbbbbb-bbbb-4bbb-8bbb-bbbbbbbbbbbb",
		"source_kind":"local_only",
		"source_profile_id":"local-only",
		"source_semantic_digest":"sha256:source",
		"manifest_digest":"sha256:manifest",
		"manifest":[{
			"mutation_id":"aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa",
			"device_id":"11111111-1111-4111-8111-111111111111",
			"category":"collection",
			"record_key":"manga/item-1",
			"schema_version":1,
			"hlc_physical_ms":"9000",
			"hlc_counter":"1",
			"operation":"put",
			"payload":{"value":"fixture"}
		}]
	}`)
	request := httptest.NewRequest("POST", "/v1/profile/attachments", bytes.NewReader(body))
	response := httptest.NewRecorder()

	var input account.BeginProfileAttachmentInput
	if err := decodeJSON(response, request, &input); err != nil {
		t.Fatalf("decode native attachment manifest: %v", err)
	}
	if len(input.Manifest) != 1 || input.Manifest[0].MutationID != "aaaaaaaa-aaaa-4aaa-8aaa-aaaaaaaaaaaa" {
		t.Fatalf("decoded manifest = %#v", input.Manifest)
	}
}

func TestProxyClientsMustNotShareSourceBucket(t *testing.T) {
	proxy := netip.MustParsePrefix("10.20.0.5/32")
	first := httptest.NewRequest("POST", "/v1/sessions", nil)
	first.RemoteAddr = "10.20.0.5:4321"
	first.Header.Set("X-Forwarded-For", "198.51.100.42")
	second := first.Clone(first.Context())
	second.Header = first.Header.Clone()
	second.Header.Set("X-Forwarded-For", "198.51.100.43")
	if a, b := clientNetworkKey(first, proxy), clientNetworkKey(second, proxy); a != "198.51.100.42" || b != "198.51.100.43" {
		t.Fatalf("distinct client keys = %q and %q", a, b)
	}
}

func TestClientNetworkTrustBoundary(t *testing.T) {
	proxies := []netip.Prefix{netip.MustParsePrefix("10.20.0.5/32"), netip.MustParsePrefix("192.0.2.5/32")}
	for _, tc := range []struct{ name, peer, forwarded, want string }{
		{"direct", "198.51.100.4:12", "", "198.51.100.4"},
		{"spoofed direct", "198.51.100.4:12", "198.51.100.6", "198.51.100.4"},
		{"untrusted private peer", "10.20.0.6:12", "198.51.100.6", ""},
		{"bare IPv4", "198.51.100.4", "", "198.51.100.4"},
		{"mapped IPv4", "[::ffff:198.51.100.4]:12", "", "198.51.100.4"},
		{"IPv6 prefix", "[2001:db8:1234:5678::a]:12", "", "2001:db8:1234:5678::/64"},
		{"IPv6 sibling", "[2001:db8:1234:5678::b]:12", "", "2001:db8:1234:5678::/64"},
		{"IPv6 other prefix", "[2001:db8:1234:5679::b]:12", "", "2001:db8:1234:5679::/64"},
		{"empty peer", "", "198.51.100.4", ""},
		{"malformed peer", "not-an-address:42", "198.51.100.4", ""},
		{"malformed port", "198.51.100.4:bad", "", ""},
		{"port out of range", "198.51.100.4:65536", "", ""},
		{"zoned peer", "[fe80::1%eth0]:42", "", ""},
		{"missing forwarded", "10.20.0.5:12", "", ""},
		{"bad forwarded", "10.20.0.5:12", "garbage", ""},
		{"empty chain item", "10.20.0.5:12", "198.51.100.4,", ""},
		{"address with port", "10.20.0.5:12", "198.51.100.4:12", ""},
		{"zoned forwarded", "10.20.0.5:12", "fe80::1%eth0", ""},
		{"private ambiguous", "10.20.0.5:12", "198.51.100.4, 10.20.0.6", ""},
		{"loopback ambiguous", "10.20.0.5:12", "127.0.0.1", ""},
		{"multicast", "10.20.0.5:12", "ff02::1", ""},
		{"unspecified", "10.20.0.5:12", "0.0.0.0", ""},
		{"trusted chain", "10.20.0.5:12", "198.51.100.4, 192.0.2.5", "198.51.100.4"},
		{"spoofed leftmost", "10.20.0.5:12", "198.51.100.9, 198.51.100.4, 192.0.2.5", "198.51.100.4"},
		{"all trusted", "10.20.0.5:12", "192.0.2.5", ""},
		{"forwarded IPv6", "10.20.0.5:12", "2001:db8:1234:5678::f", "2001:db8:1234:5678::/64"},
		{"long chain", "10.20.0.5:12", strings.Repeat("198.51.100.4,", 33) + "198.51.100.4", ""},
	} {
		t.Run(tc.name, func(t *testing.T) {
			r := httptest.NewRequest("POST", "/v1/sessions", nil)
			r.RemoteAddr = tc.peer
			r.Header.Set("X-Forwarded-For", tc.forwarded)
			r.Header.Set("Fly-Client-IP", "198.51.100.99")
			r.Header.Set("CF-Connecting-IP", "198.51.100.99")
			r.Header.Set("X-Real-IP", "198.51.100.99")
			if got := clientNetworkKey(r, proxies...); got != tc.want {
				t.Fatalf("key=%q want=%q", got, tc.want)
			}
		})
	}
	r := httptest.NewRequest("POST", "/v1/sessions", nil)
	r.RemoteAddr = "10.20.0.5:12"
	r.Header.Add("X-Forwarded-For", "198.51.100.9")
	r.Header.Add("X-Forwarded-For", "198.51.100.4, 192.0.2.5")
	if got := clientNetworkKey(r, proxies...); got != "198.51.100.4" {
		t.Fatalf("multiple header lines key=%q", got)
	}
}
