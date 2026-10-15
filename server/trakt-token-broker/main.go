package main

import (
	"bytes"
	"context"
	"encoding/json"
	"errors"
	"io"
	"log"
	"mime"
	"net"
	"net/http"
	"os"
	"strconv"
	"strings"
	"sync"
	"time"
)

const (
	defaultDeviceTokenURL = "https://auth.trakt.tv/oauth/device/token"
	defaultTokenURL       = "https://auth.trakt.tv/oauth/token"
	defaultRevokeURL      = "https://auth.trakt.tv/oauth/revoke"
	maxRequestBytes       = 32 << 10
	maxResponseBytes      = 2 << 20
)

type config struct {
	ClientID           string
	ClientSecret       string
	RedirectURI        string
	ListenAddr         string
	TrustProxy         bool
	RateLimitPerMinute int
	DeviceTokenURL     string
	TokenURL           string
	RevokeURL          string
	HTTPClient         *http.Client
}

type windowCounter struct {
	minute int64
	count  int
}

type server struct {
	cfg      config
	mu       sync.Mutex
	counters map[string]windowCounter
}

func envBool(key string, fallback bool) bool {
	raw := strings.TrimSpace(os.Getenv(key))
	if raw == "" {
		return fallback
	}
	value, err := strconv.ParseBool(raw)
	if err != nil {
		return fallback
	}
	return value
}

func envInt(key string, fallback int) int {
	raw := strings.TrimSpace(os.Getenv(key))
	if raw == "" {
		return fallback
	}
	value, err := strconv.Atoi(raw)
	if err != nil || value <= 0 {
		return fallback
	}
	return value
}

func loadConfig() (config, error) {
	cfg := config{
		ClientID:           strings.TrimSpace(os.Getenv("TRAKT_CLIENT_ID")),
		ClientSecret:       strings.TrimSpace(os.Getenv("TRAKT_CLIENT_SECRET")),
		RedirectURI:        strings.TrimSpace(os.Getenv("TRAKT_REDIRECT_URI")),
		ListenAddr:         strings.TrimSpace(os.Getenv("LISTEN_ADDR")),
		TrustProxy:         envBool("TRUST_PROXY", false),
		RateLimitPerMinute: envInt("RATE_LIMIT_PER_MINUTE", 60),
		DeviceTokenURL:     defaultDeviceTokenURL,
		TokenURL:           defaultTokenURL,
		RevokeURL:          defaultRevokeURL,
		HTTPClient:         &http.Client{Timeout: 20 * time.Second},
	}
	if cfg.ListenAddr == "" {
		cfg.ListenAddr = ":8080"
	}
	if cfg.ClientID == "" || cfg.ClientSecret == "" {
		return config{}, errors.New("TRAKT_CLIENT_ID and TRAKT_CLIENT_SECRET are required")
	}
	if cfg.RedirectURI == "" {
		return config{}, errors.New("TRAKT_REDIRECT_URI is required for refresh exchange")
	}
	return cfg, nil
}

func main() {
	cfg, err := loadConfig()
	if err != nil {
		log.Fatal(err)
	}
	s := newServer(cfg)
	httpServer := &http.Server{
		Addr:              cfg.ListenAddr,
		Handler:           s.routes(),
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       15 * time.Second,
		WriteTimeout:      25 * time.Second,
		IdleTimeout:       60 * time.Second,
	}
	log.Printf("trakt token broker listening on %s", cfg.ListenAddr)
	if err := httpServer.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatal(err)
	}
}

func newServer(cfg config) *server {
	if cfg.HTTPClient == nil {
		cfg.HTTPClient = &http.Client{Timeout: 20 * time.Second}
	}
	client := *cfg.HTTPClient
	client.CheckRedirect = func(*http.Request, []*http.Request) error { return http.ErrUseLastResponse }
	if client.Timeout <= 0 {
		client.Timeout = 20 * time.Second
	}
	cfg.HTTPClient = &client
	if cfg.RateLimitPerMinute <= 0 {
		cfg.RateLimitPerMinute = 60
	}
	if cfg.DeviceTokenURL == "" {
		cfg.DeviceTokenURL = defaultDeviceTokenURL
	}
	if cfg.TokenURL == "" {
		cfg.TokenURL = defaultTokenURL
	}
	if cfg.RevokeURL == "" {
		cfg.RevokeURL = defaultRevokeURL
	}
	return &server{cfg: cfg, counters: make(map[string]windowCounter)}
}

func (s *server) routes() http.Handler {
	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", func(w http.ResponseWriter, r *http.Request) {
		writeJSON(w, http.StatusOK, map[string]any{"ok": true})
	})
	mux.HandleFunc("POST /v1/trakt/device-token", s.deviceToken)
	mux.HandleFunc("POST /v1/trakt/refresh", s.refresh)
	mux.HandleFunc("POST /v1/trakt/revoke", s.revoke)
	return s.rateLimit(mux)
}

func (s *server) clientKey(r *http.Request) string {
	if s.cfg.TrustProxy {
		if forwarded := strings.TrimSpace(strings.Split(r.Header.Get("X-Forwarded-For"), ",")[0]); forwarded != "" {
			return forwarded
		}
	}
	host, _, err := net.SplitHostPort(r.RemoteAddr)
	if err == nil && host != "" {
		return host
	}
	return r.RemoteAddr
}

func (s *server) rateLimit(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		if r.URL.Path == "/healthz" {
			next.ServeHTTP(w, r)
			return
		}
		if s.cfg.ClientID == "" || s.cfg.ClientSecret == "" || s.cfg.RedirectURI == "" {
			writeJSON(w, http.StatusServiceUnavailable, map[string]any{"error": "not_configured"})
			return
		}
		key := s.clientKey(r)
		minute := time.Now().Unix() / 60
		s.mu.Lock()
		if len(s.counters) >= 4096 {
			for address, value := range s.counters {
				if value.minute != minute {
					delete(s.counters, address)
				}
			}
			if _, exists := s.counters[key]; !exists && len(s.counters) >= 4096 {
				s.mu.Unlock()
				writeJSON(w, http.StatusTooManyRequests, map[string]any{"error": "rate_limited"})
				return
			}
		}
		counter := s.counters[key]
		if counter.minute != minute {
			counter = windowCounter{minute: minute}
		}
		counter.count++
		s.counters[key] = counter
		allowed := counter.count <= s.cfg.RateLimitPerMinute
		s.mu.Unlock()
		if !allowed {
			w.Header().Set("Retry-After", "60")
			writeJSON(w, http.StatusTooManyRequests, map[string]any{"error": "rate_limited"})
			return
		}
		next.ServeHTTP(w, r)
	})
}

func decodeStrict(w http.ResponseWriter, r *http.Request, target any) bool {
	contentType, _, err := mime.ParseMediaType(r.Header.Get("Content-Type"))
	if err != nil || contentType != "application/json" {
		writeJSON(w, http.StatusUnsupportedMediaType, map[string]any{"error": "json_required"})
		return false
	}
	r.Body = http.MaxBytesReader(w, r.Body, maxRequestBytes)
	decoder := json.NewDecoder(r.Body)
	decoder.DisallowUnknownFields()
	if err := decoder.Decode(target); err != nil {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": "invalid_request"})
		return false
	}
	var extra any
	if err := decoder.Decode(&extra); err != io.EOF {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": "invalid_request"})
		return false
	}
	return true
}

func validOpaque(value string, maximum int) bool {
	if value == "" || value != strings.TrimSpace(value) || len(value) > maximum {
		return false
	}
	for _, character := range value {
		if character < 0x21 || character == 0x7f {
			return false
		}
	}
	return true
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json")
	w.Header().Set("Cache-Control", "no-store")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(value)
}

func (s *server) proxyJSON(ctx context.Context, url string, payload map[string]any) (int, []byte, error) {
	body, err := json.Marshal(payload)
	if err != nil {
		return 0, nil, err
	}
	request, err := http.NewRequestWithContext(ctx, http.MethodPost, url, bytes.NewReader(body))
	if err != nil {
		return 0, nil, err
	}
	request.Header.Set("Content-Type", "application/json")
	request.Header.Set("Accept", "application/json")
	request.Header.Set("trakt-api-version", "2")
	request.Header.Set("trakt-api-key", s.cfg.ClientID)
	response, err := s.cfg.HTTPClient.Do(request)
	if err != nil {
		return 0, nil, err
	}
	defer response.Body.Close()
	limited := io.LimitReader(response.Body, maxResponseBytes+1)
	responseBody, err := io.ReadAll(limited)
	if err != nil {
		return 0, nil, err
	}
	if len(responseBody) > maxResponseBytes {
		return 0, nil, errors.New("upstream response too large")
	}
	return response.StatusCode, responseBody, nil
}

// Provider bodies can contain reflected request credentials. Only the token
// response schema crosses this boundary; failures never echo upstream content.
func (s *server) relayUpstream(w http.ResponseWriter, status int, body []byte, revoke bool) {
	if status < 200 || status >= 300 {
		if status < 400 || status > 599 {
			status = http.StatusBadGateway
		}
		writeJSON(w, status, map[string]any{"error": "upstream_rejected"})
		return
	}
	if revoke {
		writeJSON(w, status, map[string]any{})
		return
	}
	var token struct {
		AccessToken  string `json:"access_token"`
		RefreshToken string `json:"refresh_token"`
		ExpiresIn    int64  `json:"expires_in"`
		CreatedAt    int64  `json:"created_at"`
		TokenType    string `json:"token_type"`
	}
	if err := json.Unmarshal(body, &token); err != nil || !validOpaque(token.AccessToken, 16384) ||
		!validOpaque(token.RefreshToken, 16384) || token.ExpiresIn <= 0 || token.ExpiresIn > 31536000 ||
		token.CreatedAt <= 0 || token.CreatedAt > 253402300799 ||
		strings.Contains(token.AccessToken, s.cfg.ClientSecret) || strings.Contains(token.RefreshToken, s.cfg.ClientSecret) ||
		(token.TokenType != "" && token.TokenType != "bearer") {
		writeJSON(w, http.StatusBadGateway, map[string]any{"error": "invalid_upstream_response"})
		return
	}
	writeJSON(w, status, map[string]any{"access_token": token.AccessToken, "refresh_token": token.RefreshToken,
		"expires_in": token.ExpiresIn, "created_at": token.CreatedAt, "token_type": "bearer"})
}

type deviceTokenRequest struct {
	Code string `json:"code"`
}
type refreshRequest struct {
	RefreshToken string `json:"refresh_token"`
}
type revokeRequest struct {
	Token string `json:"token"`
}

func (s *server) deviceToken(w http.ResponseWriter, r *http.Request) {
	var input deviceTokenRequest
	if !decodeStrict(w, r, &input) {
		return
	}
	if !validOpaque(input.Code, 512) {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": "invalid_code"})
		return
	}
	status, body, err := s.proxyJSON(r.Context(), s.cfg.DeviceTokenURL, map[string]any{
		"code":          input.Code,
		"client_id":     s.cfg.ClientID,
		"client_secret": s.cfg.ClientSecret,
	})
	if err != nil {
		writeJSON(w, http.StatusBadGateway, map[string]any{"error": "upstream_unavailable"})
		return
	}
	s.relayUpstream(w, status, body, false)
}

func (s *server) refresh(w http.ResponseWriter, r *http.Request) {
	var input refreshRequest
	if !decodeStrict(w, r, &input) {
		return
	}
	if !validOpaque(input.RefreshToken, 16384) {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": "invalid_refresh_token"})
		return
	}
	status, body, err := s.proxyJSON(r.Context(), s.cfg.TokenURL, map[string]any{
		"refresh_token": input.RefreshToken,
		"client_id":     s.cfg.ClientID,
		"client_secret": s.cfg.ClientSecret,
		"redirect_uri":  s.cfg.RedirectURI,
		"grant_type":    "refresh_token",
	})
	if err != nil {
		writeJSON(w, http.StatusBadGateway, map[string]any{"error": "upstream_unavailable"})
		return
	}
	s.relayUpstream(w, status, body, false)
}

func (s *server) revoke(w http.ResponseWriter, r *http.Request) {
	var input revokeRequest
	if !decodeStrict(w, r, &input) {
		return
	}
	if !validOpaque(input.Token, 16384) {
		writeJSON(w, http.StatusBadRequest, map[string]any{"error": "invalid_token"})
		return
	}
	status, body, err := s.proxyJSON(r.Context(), s.cfg.RevokeURL, map[string]any{
		"token":         input.Token,
		"client_id":     s.cfg.ClientID,
		"client_secret": s.cfg.ClientSecret,
	})
	if err != nil {
		writeJSON(w, http.StatusBadGateway, map[string]any{"error": "upstream_unavailable"})
		return
	}
	s.relayUpstream(w, status, body, true)
}
