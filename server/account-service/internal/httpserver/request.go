package httpserver

import (
	"encoding/json"
	"errors"
	"io"
	"net"
	"net/http"
	"net/netip"
	"strings"
)

const maxJSONBodyBytes int64 = 64 * 1024

func decodeJSON(w http.ResponseWriter, r *http.Request, destination any) error {
	r.Body = http.MaxBytesReader(w, r.Body, maxJSONBodyBytes)
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()

	if err := decoder.Decode(destination); err != nil {
		return err
	}
	var extra any
	if err := decoder.Decode(&extra); !errors.Is(err, io.EOF) {
		if err == nil {
			return errors.New("request body contains multiple JSON values")
		}
		return err
	}
	return nil
}

func clientNetworkKey(r *http.Request, trustedProxies ...netip.Prefix) string {
	rawPeer := strings.TrimSpace(r.RemoteAddr)
	peer, err := netip.ParseAddr(rawPeer)
	if err != nil {
		addrPort, parseErr := netip.ParseAddrPort(rawPeer)
		if parseErr != nil {
			return ""
		}
		peer = addrPort.Addr()
	}
	if peer.Zone() != "" || !peer.IsGlobalUnicast() && !peer.IsLoopback() {
		return ""
	}
	peer = peer.Unmap()
	trusted := func(ip netip.Addr) bool {
		for _, prefix := range trustedProxies {
			if prefix.Contains(ip) {
				return true
			}
		}
		return false
	}
	if !trusted(peer) {
		if len(trustedProxies) > 0 && (peer.IsPrivate() || peer.IsLoopback()) {
			// A newly introduced internal hop must be verified, not silently
			// used as a shared visitor bucket after a topology change.
			return ""
		}
		// Public/direct and untrusted private paths cannot assert their own
		// identity through a forwarded header, including a Fly header.
		return networkPrefix(net.IP(peer.AsSlice()))
	}

	// Parse every header line, preserving chain order. Only explicitly trusted
	// hops are removed from the right; a caller-controlled leftmost IP is never
	// preferred over the first untrusted hop. Bound parsing of hostile input.
	raw := strings.Join(r.Header.Values("X-Forwarded-For"), ",")
	if raw == "" || len(raw) > 4096 {
		return ""
	}
	parts := strings.Split(raw, ",")
	if len(parts) > 32 {
		return ""
	}
	chain := make([]netip.Addr, len(parts))
	for i, part := range parts {
		ip, err := netip.ParseAddr(strings.TrimSpace(part))
		if err != nil || ip.Zone() != "" {
			return ""
		}
		chain[i] = ip.Unmap()
	}
	for i := len(chain) - 1; i >= 0; i-- {
		ip := chain[i]
		if trusted(ip) {
			continue
		}
		// A private/loopback/link-local address from an untrusted forwarded hop
		// is ambiguous. Fail closed rather than collapsing clients onto it.
		if !ip.IsGlobalUnicast() || ip.IsPrivate() {
			return ""
		}
		return networkPrefix(net.IP(ip.AsSlice()))
	}
	return ""
}

func networkPrefix(ip net.IP) string {
	if ipv4 := ip.To4(); ipv4 != nil {
		return ipv4.String()
	}
	ipv6 := ip.To16()
	if ipv6 == nil {
		return "unknown"
	}
	masked := ipv6.Mask(net.CIDRMask(64, 128))
	return masked.String() + "/64"
}
