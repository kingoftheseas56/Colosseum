package httpserver

import (
	"context"
	"crypto/rand"
	"encoding/hex"
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

type SchemaChecker interface {
	CheckSchema(context.Context) error
}

type Handler struct {
	pinger           Pinger
	schemaChecker    SchemaChecker
	accounts         *account.Service
	readinessTimeout time.Duration
	logger           *slog.Logger
}

func New(
	pinger Pinger,
	accounts *account.Service,
	readinessTimeout time.Duration,
	logger *slog.Logger,
	schemaCheckers ...SchemaChecker,
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
	if len(schemaCheckers) > 0 {
		handler.schemaChecker = schemaCheckers[0]
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
	mux.HandleFunc("POST /v1/account/deletion/retry", handler.retryDeleteAccount)

	protected := http.NewServeMux()
	protected.HandleFunc("DELETE /v1/sessions/current", handler.logoutCurrent)
	protected.HandleFunc("POST /v1/sessions/logout-all", handler.logoutEverywhere)
	protected.HandleFunc("POST /v1/password/change", handler.changePassword)
	protected.HandleFunc("POST /v1/recovery-key/replace", handler.replaceRecoveryKey)
	protected.HandleFunc("GET /v1/profile", handler.getProfile)
	protected.HandleFunc("GET /v1/account/export", handler.exportAccount)
	protected.HandleFunc("DELETE /v1/account", handler.deleteAccount)
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
	return securityHeaders(handler.requestDiagnostics(mux))
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
		w.Header().Set("X-Colosseum-Error-Class", "readiness_unavailable")
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"status": "unavailable"})
		return
	}
	if h.schemaChecker != nil && h.schemaChecker.CheckSchema(ctx) != nil {
		w.Header().Set("X-Colosseum-Error-Class", "schema_unready")
		writeJSON(w, http.StatusServiceUnavailable, map[string]string{"status": "unavailable"})
		return
	}
	writeJSON(w, http.StatusOK, map[string]string{"status": "ready"})
}

type diagnosticsResponseWriter struct {
	http.ResponseWriter
	status int
}

func (w *diagnosticsResponseWriter) WriteHeader(status int) {
	if w.status != 0 {
		return
	}
	w.status = status
	w.ResponseWriter.WriteHeader(status)
}

func (w *diagnosticsResponseWriter) Write(body []byte) (int, error) {
	if w.status == 0 {
		w.WriteHeader(http.StatusOK)
	}
	return w.ResponseWriter.Write(body)
}

func (h *Handler) requestDiagnostics(next http.Handler) http.Handler {
	return http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		requestID := newRequestID()
		w.Header().Set("X-Request-ID", requestID)
		started := time.Now()
		response := &diagnosticsResponseWriter{ResponseWriter: w}
		next.ServeHTTP(response, r)

		status := response.status
		if status == 0 {
			status = http.StatusOK
		}
		errorClass := response.Header().Get("X-Colosseum-Error-Class")
		if errorClass == "" {
			switch {
			case status >= 500:
				errorClass = "server_error"
			case status >= 400:
				errorClass = "client_error"
			}
		}

		attrs := []any{
			"request_id", requestID,
			"operation", requestOperation(r),
			"status", status,
			"latency_ms", time.Since(started).Milliseconds(),
		}
		if errorClass != "" {
			attrs = append(attrs, "error_class", errorClass)
		}
		if status >= 500 {
			h.logger.Error("request completed", attrs...)
		} else if status >= 400 {
			h.logger.Warn("request completed", attrs...)
		} else {
			h.logger.Info("request completed", attrs...)
		}
	})
}

func newRequestID() string {
	var bytes [16]byte
	if _, err := rand.Read(bytes[:]); err == nil {
		return hex.EncodeToString(bytes[:])
	}
	return "unavailable"
}

func requestOperation(r *http.Request) string {
	path := r.URL.Path
	switch {
	case r.Method == http.MethodGet && path == "/healthz":
		return "health"
	case r.Method == http.MethodGet && path == "/readyz":
		return "readiness"
	case r.Method == http.MethodPost && path == "/v1/accounts":
		return "account_create"
	case r.Method == http.MethodGet && path == "/v1/account/export":
		return "account_export"
	case r.Method == http.MethodDelete && path == "/v1/account":
		return "account_delete"
	case r.Method == http.MethodPost && path == "/v1/account/deletion/retry":
		return "account_deletion_retry"
	case r.Method == http.MethodPost && path == "/v1/sessions":
		return "session_sign_in"
	case r.Method == http.MethodPost && path == "/v1/sessions/refresh":
		return "session_refresh"
	case r.Method == http.MethodPost && path == "/v1/sessions/revoke-refresh":
		return "session_revoke_refresh"
	case r.Method == http.MethodPost && path == "/v1/sync/push":
		return "sync_push"
	case r.Method == http.MethodGet && path == "/v1/sync/pull":
		return "sync_pull"
	case r.Method == http.MethodGet && path == "/v1/profile":
		return "profile_read"
	case r.Method == http.MethodPatch && path == "/v1/profile/username":
		return "profile_rename"
	case r.Method == http.MethodPut && path == "/v1/profile/avatar/builtin":
		return "avatar_builtin"
	case r.Method == http.MethodPost && path == "/v1/profile/avatar/upload":
		return "avatar_upload"
	case r.Method == http.MethodGet && path == "/v1/devices":
		return "devices_list"
	case r.Method == http.MethodDelete && strings.HasPrefix(path, "/v1/devices/"):
		return "device_revoke"
	case r.Method == http.MethodPut && path == "/v1/security/new-device-protection":
		return "device_protection"
	case r.Method == http.MethodGet && path == "/v1/approvals":
		return "approvals_list"
	case r.Method == http.MethodPost && strings.HasPrefix(path, "/v1/approvals/"):
		return "approval_decide"
	case r.Method == http.MethodDelete && path == "/v1/sessions/current":
		return "session_logout"
	case r.Method == http.MethodPost && path == "/v1/sessions/logout-all":
		return "session_logout_all"
	case r.Method == http.MethodPost && path == "/v1/password/recover":
		return "password_recover"
	case r.Method == http.MethodPost && path == "/v1/password/trusted-recovery":
		return "trusted_recovery_start"
	case r.Method == http.MethodPost && path == "/v1/password/trusted-recovery/poll":
		return "trusted_recovery_poll"
	case r.Method == http.MethodPost && path == "/v1/challenges/device/poll":
		return "device_challenge_poll"
	case r.Method == http.MethodPost && path == "/v1/challenges/device/recovery-key":
		return "device_challenge_recover"
	case r.Method == http.MethodPost && path == "/v1/password/change":
		return "password_change"
	case r.Method == http.MethodPost && path == "/v1/recovery-key/replace":
		return "recovery_key_replace"
	default:
		return "other"
	}
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
