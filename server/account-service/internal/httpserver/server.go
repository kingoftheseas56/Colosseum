package httpserver

import (
	"context"
	"encoding/json"
	"io"
	"log/slog"
	"net/http"
	"strconv"
	"strings"
	"time"

	"github.com/kingoftheseas56/Colosseum-Account-Service/internal/account"
)

type Pinger interface {
	Ping(context.Context) error
}

type Handler struct {
	pinger           Pinger
	accounts         *account.Service
	readinessTimeout time.Duration
	logger           *slog.Logger
}

func New(
	pinger Pinger,
	accounts *account.Service,
	readinessTimeout time.Duration,
	logger *slog.Logger,
) http.Handler {
	if logger == nil {
		logger = slog.New(slog.NewTextHandler(io.Discard, nil))
	}
	handler := &Handler{
		pinger:           pinger,
		accounts:         accounts,
		readinessTimeout: readinessTimeout,
		logger:           logger,
	}

	mux := http.NewServeMux()
	mux.HandleFunc("GET /healthz", handler.health)
	mux.HandleFunc("GET /readyz", handler.ready)

	mux.HandleFunc("POST /v1/accounts", handler.createAccount)
	mux.HandleFunc("POST /v1/sessions", handler.signIn)
	mux.HandleFunc("POST /v1/sessions/refresh", handler.refreshSession)
	mux.HandleFunc("POST /v1/sessions/revoke-refresh", handler.revokeRefresh)
	mux.HandleFunc("POST /v1/password/recover", handler.recoverPassword)
	mux.HandleFunc("POST /v1/password/trusted-recovery", handler.startTrustedRecovery)
	mux.HandleFunc("POST /v1/password/trusted-recovery/poll", handler.pollTrustedRecovery)
	mux.HandleFunc("POST /v1/challenges/device/poll", handler.pollDeviceChallenge)
	mux.HandleFunc("POST /v1/challenges/device/recovery-key", handler.recoverDeviceChallengeWithKey)

	protected := http.NewServeMux()
	protected.HandleFunc("DELETE /v1/sessions/current", handler.logoutCurrent)
	protected.HandleFunc("POST /v1/sessions/logout-all", handler.logoutEverywhere)
	protected.HandleFunc("POST /v1/password/change", handler.changePassword)
	protected.HandleFunc("POST /v1/recovery-key/replace", handler.replaceRecoveryKey)
	protected.HandleFunc("GET /v1/profile", handler.getProfile)
	protected.HandleFunc("PATCH /v1/profile/username", handler.renameUsername)
	protected.HandleFunc("PUT /v1/profile/avatar/builtin", handler.setBuiltinAvatar)
	protected.HandleFunc("POST /v1/profile/avatar/upload", handler.uploadAvatar)
	protected.HandleFunc("GET /v1/devices", handler.listDevices)
	protected.HandleFunc("DELETE /v1/devices/{deviceID}", handler.revokeDevice)
	protected.HandleFunc("PUT /v1/security/new-device-protection", handler.setNewDeviceProtection)
	protected.HandleFunc("GET /v1/approvals", handler.listApprovals)
	protected.HandleFunc("POST /v1/approvals/{kind}/{challengeID}", handler.decideApproval)
	protected.HandleFunc("POST /v1/sync/push", handler.pushSync)
	protected.HandleFunc("GET /v1/sync/pull", handler.pullSync)
	protected.HandleFunc("GET /v1/sync/snapshot", handler.pullSyncSnapshot)
	protected.HandleFunc("POST /v1/profile/attachments", handler.beginProfileAttachment)
	protected.HandleFunc("GET /v1/profile/attachments/{attachmentID}", handler.getProfileAttachment)
	protected.HandleFunc("POST /v1/profile/attachments/{attachmentID}/commit", handler.commitProfileAttachment)

	mux.Handle("/v1/", handler.requireAuthFallback(protected))
	return securityHeaders(mux)
}

func (h *Handler) requireAuthFallback(protected http.Handler) http.Handler {
	authenticatedHandler := h.requireAuth(protected)
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		authenticatedHandler.ServeHTTP(w, r)
	})
}

func (h *Handler) health(w http.ResponseWriter, r *http.Request) {
	writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
}

func (h *Handler) ready(w http.ResponseWriter, r *http.Request) {
	ctx, cancel := context.WithTimeout(r.Context(), h.readinessTimeout)
	defer cancel()

	if h.pinger == nil || h.pinger.Ping(ctx) != nil {
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"status": "unavailable"})
		return
	}
	writeJSON(w, http.StatusOK, map[string]string{"status": "ready"})
}

func securityHeaders(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		w.Header().Set("Cache-Control", "no-store")
		w.Header().Set("X-Content-Type-Options", "nosniff")
		w.Header().Set("Referrer-Policy", "no-referrer")
		next.ServeHTTP(w, r)
	})
}

func writeJSON(w http.ResponseWriter, status int, body any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	_ = json.NewEncoder(w).Encode(body)
}

func waitSeconds(r *http.Request) int {
	value, err := strconv.Atoi(strings.TrimSpace(r.URL.Query().Get("wait_seconds")))
	if err != nil || value < 0 {
		return 0
	}
	if value > 25 {
		return 25
	}
	return value
}
