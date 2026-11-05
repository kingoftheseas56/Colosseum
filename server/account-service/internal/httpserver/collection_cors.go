package httpserver

import (
	"net/http"
	"strings"
)

// collectionCORS wraps only the anonymous collection API. It never grants
// credentialed access; authenticated account and publication routes stay outside.
func collectionCORS(allowedOrigins []string, next http.Handler) http.Handler {
	allowed := make(map[string]bool, len(allowedOrigins))
	for _, origin := range allowedOrigins {
		if origin != "" && origin != "*" && origin != "null" {
			allowed[origin] = true
		}
	}
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Add("Vary", "Origin")
		w.Header().Set("Cache-Control", "no-store")
		origin := r.Header.Get("Origin")
		preflight := r.Method == http.MethodOptions
		if preflight {
			w.Header().Add("Vary", "Access-Control-Request-Method")
			w.Header().Add("Vary", "Access-Control-Request-Headers")
			method := r.Header.Get("Access-Control-Request-Method")
			valid := method == http.MethodGet || method == http.MethodPost
			if headers := r.Header.Get("Access-Control-Request-Headers"); headers != "" {
				for _, header := range strings.Split(headers, ",") {
					if !strings.EqualFold(strings.TrimSpace(header), "Content-Type") {
						valid = false
					}
				}
			}
			if !allowed[origin] || !valid {
				w.WriteHeader(http.StatusForbidden)
				return
			}
			w.Header().Set("Access-Control-Allow-Origin", origin)
			w.Header().Set("Access-Control-Allow-Methods", "GET, POST")
			w.Header().Set("Access-Control-Allow-Headers", "Content-Type")
			w.WriteHeader(http.StatusNoContent)
			return
		}
		if allowed[origin] {
			w.Header().Set("Access-Control-Allow-Origin", origin)
		}
		next.ServeHTTP(w, r)
	})
}
