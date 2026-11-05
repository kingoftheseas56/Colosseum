// Package collectionviewer serves the anonymous Collections receiving surface.
package collectionviewer

import (
	"embed"
	"net/http"
	"regexp"
)

//go:embed index.html viewer.css viewer.js
var assets embed.FS

var sharePath = regexp.MustCompile(`^/collections/s/([A-Za-z0-9_-]{43})/?$`)

// Handler exposes only the three reviewed assets. When the API requires the
// edge, legacy share links go to the public site so it can open them there.
func Handler(redirectShareLinks bool) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("Referrer-Policy", "no-referrer")
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("X-Frame-Options", "DENY")
		w.Header().Set("Permissions-Policy", "camera=(), microphone=(), geolocation=()")
		w.Header().Set("Content-Security-Policy", "default-src 'none'; script-src 'self'; style-src 'self'; img-src data:; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'; form-action 'self'")
		if r.Method != http.MethodGet && r.Method != http.MethodHead {
			w.Header().Set("Allow", "GET, HEAD")
			http.Error(w, "Method not allowed", http.StatusMethodNotAllowed)
			return
		}
		var filename, contentType string
		share := sharePath.FindStringSubmatch(r.URL.Path)
		switch {
		case share != nil:
			if redirectShareLinks {
				http.Redirect(w, r, "https://getcolosseum.world/c/"+share[1], http.StatusFound)
				return
			}
			filename, contentType = "index.html", "text/html; charset=utf-8"
		case r.URL.Path == "/collections/assets/viewer.css":
			filename, contentType = "viewer.css", "text/css; charset=utf-8"
		case r.URL.Path == "/collections/assets/viewer.js":
			filename, contentType = "viewer.js", "text/javascript; charset=utf-8"
		default:
			http.NotFound(w, r)
			return
		}
		body, err := assets.ReadFile(filename)
		if err != nil {
			http.Error(w, "Viewer unavailable", http.StatusServiceUnavailable)
			return
		}
		w.Header().Set("Content-Type", contentType)
		if r.Method == http.MethodHead {
			w.WriteHeader(http.StatusOK)
			return
		}
		_, _ = w.Write(body)
	})
}
