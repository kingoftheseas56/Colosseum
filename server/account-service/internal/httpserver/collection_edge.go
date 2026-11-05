package httpserver

import (
	"crypto/sha256"
	"crypto/subtle"
	"net"
	"net/http"
	"net/netip"
	"strings"
)

// Anonymous collection opens are rate limited per visitor network. The
// hosting provider's ingress proxy cannot be verified, so when an edge secret
// is configured the only trusted hop is our own Cloudflare zone: Cloudflare
// overwrites CF-Connecting-IP with the address that connected to it, and a
// Transform Rule on that zone adds CollectionEdgeSecretHeader. A request that
// does not carry the exact secret did not come through our edge and is refused,
// so callers bypassing the edge can neither assert an address nor fall back to
// the provider proxy's shared address.
const (
	CollectionEdgeSecretHeader   = "X-Colosseum-Edge"
	collectionEdgeClientIPHeader = "CF-Connecting-IP"
)

// cloudflareNetworks are Cloudflare's published edge ranges
// (https://www.cloudflare.com/ips-v4, /ips-v6, fetched 2026-11-05). A client
// address inside them means a second Cloudflare hop replaced the visitor, which
// would put unrelated visitors in one bucket, so it fails closed instead.
var cloudflareNetworks = mustPrefixes(
	"173.245.48.0/20", "103.21.244.0/22", "103.22.200.0/22", "103.31.4.0/22",
	"141.101.64.0/18", "108.162.192.0/18", "190.93.240.0/20", "188.114.96.0/20",
	"197.234.240.0/22", "198.41.128.0/17", "162.158.0.0/15", "104.16.0.0/13",
	"104.24.0.0/14", "172.64.0.0/13", "131.0.72.0/22",
	"2400:cb00::/32", "2606:4700::/32", "2803:f800::/32", "2405:b500::/32",
	"2405:8100::/32", "2a06:98c0::/29", "2c0f:f248::/32",
)

func mustPrefixes(values ...string) []netip.Prefix {
	prefixes := make([]netip.Prefix, len(values))
	for i, value := range values {
		prefixes[i] = netip.MustParsePrefix(value)
	}
	return prefixes
}

func edgeSecretDigest(secret string) *[sha256.Size]byte {
	if secret == "" {
		return nil
	}
	digest := sha256.Sum256([]byte(secret))
	return &digest
}

// collectionSourceKey returns the visitor network key for anonymous collection
// opens, or "" when the source cannot be verified.
func (h *Handler) collectionSourceKey(r *http.Request) string {
	if h.edgeSecret == nil {
		return clientNetworkKey(r, h.trustedProxies...)
	}
	return edgeClientNetworkKey(r, h.edgeSecret)
}

func edgeClientNetworkKey(r *http.Request, secret *[sha256.Size]byte) string {
	presented := r.Header.Values(CollectionEdgeSecretHeader)
	if len(presented) != 1 {
		return ""
	}
	// Hash first so the comparison is constant time regardless of length.
	digest := sha256.Sum256([]byte(presented[0]))
	if subtle.ConstantTimeCompare(digest[:], secret[:]) != 1 {
		return ""
	}
	values := r.Header.Values(collectionEdgeClientIPHeader)
	if len(values) != 1 {
		return ""
	}
	ip, err := netip.ParseAddr(strings.TrimSpace(values[0]))
	if err != nil || ip.Zone() != "" {
		return ""
	}
	ip = ip.Unmap()
	if !ip.IsGlobalUnicast() || ip.IsPrivate() {
		return ""
	}
	for _, prefix := range cloudflareNetworks {
		if prefix.Contains(ip) {
			return ""
		}
	}
	return networkPrefix(net.IP(ip.AsSlice()))
}
