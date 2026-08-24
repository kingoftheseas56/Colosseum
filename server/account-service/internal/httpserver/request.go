package httpserver

import (
	"encoding/json"
	"errors"
	"io"
	"net"
	"net/http"
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

func clientNetworkKey(r *http.Request) string {
	if raw := strings.TrimSpace(r.Header.Get("Fly-Client-IP")); raw != "" {
		if ip := net.ParseIP(raw); ip != nil {
			return networkPrefix(ip)
		}
	}

	host, _, err := net.SplitHostPort(strings.TrimSpace(r.RemoteAddr))
	if err == nil {
		if ip := net.ParseIP(host); ip != nil {
			return networkPrefix(ip)
		}
	}
	if ip := net.ParseIP(strings.TrimSpace(r.RemoteAddr)); ip != nil {
		return networkPrefix(ip)
	}
	return "unknown"
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
